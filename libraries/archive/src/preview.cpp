/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/preview.h"
#include "mm/preview_rasterizer.h"
#include "zb_capture.h"
#include "preview_background_mesh.h"
#include "preview_depth_range.h"
#include "diagnostics.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include "soc_core.h"
#include "preview_actor.h"
#include "preview_actor_tree.h"
#include "preview_background.h"
#include "preview_presentation.h"
#undef size
ASSERTNAME

namespace MM
{
namespace
{
bool runtimeOwned = false;
bool Fail(Error &error, ErrorCode code, const char *message)
{
    error.code = code;
    error.message = message;
    if (code == ErrorCode::decode_failed && Internal::AssertionFailed())
        error.message += std::string(" ") + Internal::DiagnosticMessage();
    return false;
}
struct PickWorld
{
    br_actor *world = nullptr;
    ~PickWorld()
    {
        if (world) BrActorFree(world);
    }
};
std::pair<std::uint32_t, std::uint32_t> FittedSize(std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                                                std::uint32_t width, std::uint32_t height)
{
    if (std::uint64_t(width) * sourceHeight <= std::uint64_t(height) * sourceWidth)
        return {width, std::max(1u, std::uint32_t(std::uint64_t(sourceHeight) * width / sourceWidth))};
    return {std::max(1u, std::uint32_t(std::uint64_t(sourceWidth) * height / sourceHeight)), height};
}
struct PickState
{
    const std::map<br_actor *, int> &owners;
    float nearClip, closest;
    int candidate = -1, object = -1;
    float distanceScale = 1, distanceOrigin = 0;
};
int BR_CALLBACK PickFace(br_model *, br_material *material, br_vector3 *, br_vector3 *, br_scalar distance, int, int,
                         int, br_vector3 *, br_vector2 *map, void *context)
{
    auto &pick = *static_cast<PickState *>(context);
    const float depth = BrScalarToFloat(distance) * pick.distanceScale + pick.distanceOrigin;
    if (depth < pick.nearClip || depth > pick.closest)
        return 0;
    // Indexed texture zero is transparent unless the material makes it opaque.
    if (auto texture = material->colour_map)
    {
        if (!(material->flags & BR_MATF_MAP_OPAQUE) && texture->type == BR_PMT_INDEX_8 && texture->pixels &&
            texture->width > 0 && texture->height > 0)
        {
            br_vector2 uv;
            BrMatrix23ApplyP(&uv, map, &material->map_transform);
            const auto wrap = [](float value, int extent) {
                const int coordinate = static_cast<int>(std::floor(value * extent));
                return (coordinate % extent + extent) % extent;
            };
            const int x = wrap(BrScalarToFloat(uv.v[0]), texture->width);
            const int y = wrap(BrScalarToFloat(uv.v[1]), texture->height);
            const auto texel = static_cast<const byte *>(texture->pixels) + (y + texture->base_y) * texture->row_bytes +
                               x + texture->base_x;
            if (!*texel)
                return 0;
        }
    }
    pick.closest = depth;
    pick.object = pick.candidate;
    return 0;
}
int BR_CALLBACK PickModel(br_actor *actor, br_model *model, br_material *material, br_vector3 *position,
                          br_vector3 *direction, br_scalar nearClip, br_scalar farClip, void *context)
{
    auto &pick = *static_cast<PickState *>(context);
    const auto owner = pick.owners.find(actor);
    pick.candidate = owner == pick.owners.end() ? -1 : owner->second;
    // Original arrays may have been released. Pick the prepared, pivot-adjusted
    // faces/vertices that BRender actually draws, without changing shared models.
    auto prepared = *model;
    prepared.faces = model->prepared_faces;
    prepared.vertices = model->prepared_vertices;
    prepared.nfaces = model->nprepared_faces;
    prepared.nvertices = model->nprepared_vertices;
    // Preparation can move vertices by the model pivot after computing face
    // planes. Rebuild plane offsets in those same coordinates for ray tests;
    // otherwise front faces (notably 3D letters) can reject visible hits.
    // Keep the shared render geometry untouched.
    std::vector<br_face> faces(prepared.faces, prepared.faces + prepared.nfaces);
    for (auto &face : faces)
    {
        br_vector3 normal;
        for (int axis = 0; axis < 3; ++axis)
            normal.v[axis] = BrFractionToScalar(face.n.v[axis]);
        face.d = BrVector3Dot(&normal, &prepared.vertices[face.vertices[0]].p);
    }
    prepared.faces = faces.data();
    BrModelPick2D(&prepared, material, position, direction, nearClip, farClip, PickFace, context);
    return 0;
}
bool WithinBudget(const Archive &archive, PChunkyFile file, Error &error)
{
    std::uint64_t total = 0;
    for (const auto &chunk : archive.Chunks())
    {
        std::uint32_t decoded = chunk.storedBytes;
        if (chunk.packed)
        {
            DataBlock block;
            byte header[8];
            if (chunk.storedBytes < sizeof(header) || !file->FFind(chunk.id.type, chunk.id.number, &block) ||
                !block.FReadRgb(header, sizeof(header), 0, fTrue))
                return Fail(error, ErrorCode::decode_failed, "Invalid compressed preview dependency.");
            decoded = (std::uint32_t(header[4]) << 24) | (std::uint32_t(header[5]) << 16) |
                      (std::uint32_t(header[6]) << 8) | header[7];
        }
        total += decoded;
        if (chunk.storedBytes > 64u * 1024 * 1024 || decoded > 64u * 1024 * 1024 || total > 512u * 1024 * 1024)
        {
            error.hasChunk = true;
            error.chunk = chunk.id;
            return Fail(error, ErrorCode::resource_limit,
                        "Preview archives are limited to 64 MiB per chunk and 512 MiB of decoded dependencies.");
        }
    }
    return true;
}
} // namespace
struct Preview::Impl
{
    struct RenderHost;
    struct Source
    {
        std::int32_t id = 0;
        Archive archive;
        PChunkyResourceFile cache = nullptr;
        ~Source()
        {
            ReleasePpo(&cache);
        }
    };
    Archive assets, resources;
    PChunkyResourceFile cache = nullptr;
    PTemplate tmpl = nullptr;
    PBody body = nullptr;
    PModel model = nullptr;
    PreviewInfo info;
    Color palette[256] = {};
    bool running = false, ready = false;
    MMPreviewOrbit orbit = {0, 0, -1, {0, 0, 0}, 1, {0, 0, 0}};
    std::vector<std::unique_ptr<Source>> sources;
    std::vector<std::unique_ptr<Internal::PreviewActor>> actors;
    std::uint64_t visibilityClock = 0;
    std::string resolveError;
    void BindTemplate(PTemplate value)
    {
        if (!value->FIsTdt())
            return;
        static_cast<PThreeDText>(value)->SetFontResolver(this, [](void *context, const TAG &tag) -> PThreeDFont {
            auto self = static_cast<Impl *>(context);
            TAGOnFile wire;
            wire.From(tag);
            auto source = self->Resolve(wire);
            return source ? static_cast<PThreeDFont>(source->PbacoFetch(tag.ctg, tag.cno, ThreeDFont::FReadTdf))
                          : nullptr;
        });
    }
    std::unique_ptr<Internal::PreviewBackground> background;
    struct CameraChange
    {
        std::int32_t frame;
        std::uint32_t camera;
    };
    std::vector<CameraChange> cameraChanges;
    std::uint32_t initialCamera = 0;
    bool LoadBackground(PChunkyResourceFile source, ChunkNumber number)
    {
        std::unique_ptr<Internal::PreviewBackground> loaded(new Internal::PreviewBackground);
        if (!loaded->Load(source, number, palette))
            return false;
        info.cameraCount = static_cast<std::uint32_t>(loaded->cameras.size());
        background.swap(loaded);
        return true;
    }
    PChunkyResourceFile Resolve(const TAGOnFile &tag)
    {
        if (tag.sid == 0 && cache->Pcfl()->FFind(tag.ctg, tag.cno))
            return cache;
        for (const auto &source : sources)
            if (source->id == tag.sid && source->cache->Pcfl()->FFind(tag.ctg, tag.cno))
                return source->cache;
        resolveError = "Missing source " + std::to_string(tag.sid) + ": " + Identity({tag.ctg, tag.cno}) +
                       ". Add the content archive for this source.";
        return nullptr;
    }
    std::vector<PBody> Bodies() const
    {
        std::vector<PBody> bodies;
        if (body)
            bodies.push_back(body);
        for (const auto &actor : actors)
            if (actor->Visible())
                bodies.push_back(actor->body);
        return bodies;
    }
    bool AddModels(br_actor *world, std::map<br_actor *, int> *owners = nullptr) const
    {
        const auto append = [&](PBody value, int owner) {
            return Internal::CopyBodyActorTree(world, value->PbactRoot(), owners, owner);
        };
        if (body && !append(body, -1)) return false;
        std::vector<std::size_t> visible;
        for (std::size_t i = 0; i < actors.size(); ++i)
            if (actors[i]->Visible()) visible.push_back(i);
        std::stable_sort(visible.begin(), visible.end(), [&](std::size_t a, std::size_t b) {
            return actors[a]->DrawOrder() < actors[b]->DrawOrder();
        });
        for (auto i : visible)
            if (!append(actors[i]->body, static_cast<int>(i))) return false;
        if (model)
        {
            auto actor = BrActorAllocate(BR_ACTOR_MODEL, nullptr);
            if (!actor) return false;
            BrActorAdd(world, actor);
            actor->model = model->Pbmdl();
            if (owners) owners->emplace(actor, -1);
        }
        return true;
    }
    void RefreshStats()
    {
        info.parts = info.triangles = 0;
        const auto count = [&](PBody value) {
            for (LegacyLong i = 0; i < value->Cbact() - 2; ++i)
                if (auto model = value->PbactPart(i)->model)
                {
                    ++info.parts;
                    info.triangles += model->nprepared_faces;
                }
        };
        if (body)
            count(body);
        for (const auto &actor : actors)
            if (actor->Visible())
                count(actor->body);
        if (model)
        {
            info.parts = 1;
            info.triangles = model->Pbmdl()->nprepared_faces;
        }
    }
    void ClearAsset(bool preserveFraming = false)
    {
        actors.clear();
        visibilityClock = 0;
        background.reset();
        cameraChanges.clear();
        if (!preserveFraming)
            orbit.framed = false;
        initialCamera = 0;
        resolveError.clear();
        ReleasePpo(&body);
        ReleasePpo(&tmpl);
        ReleasePpo(&model);
        info = {};
        ready = false;
    }
    ~Impl()
    {
        ClearAsset();
        sources.clear();
        ReleasePpo(&cache);
        if (running)
        {
            Body::ReleaseSharedMaterial();
            Material_MTRL::ReleaseShadeTable();
            BrZbEnd();
            BrEnd();
            runtimeOwned = false;
        }
    }
};
Preview::Preview() : _impl(new Impl)
{
}
Preview::~Preview() = default;
void Preview::Close()
{
    _impl.reset(new Impl);
}
const PreviewInfo &Preview::Info() const
{
    return _impl->info;
}
void Preview::ResetOrbitFraming()
{
    _impl->orbit.framed = false;
}
bool Preview::MatchSceneCamera(float outputAspect, PreviewCamera &camera, Error &error)
{
    error = {};
    camera = {};
    if (!_impl->ready || !_impl->info.scene || !std::isfinite(outputAspect) || outputAspect <= 0)
        return Fail(error, ErrorCode::invalid_argument, "Select a scene before matching its camera.");
    float right[3] = {1, 0, 0}, backward[3] = {0, 0, 1};
    camera.z = 10;
    if (_impl->background)
    {
        const auto &source = *_impl->background;
        camera.x = BrScalarToFloat(source.camera.bmat34Cam.m[3][0]);
        camera.y = BrScalarToFloat(source.camera.bmat34Cam.m[3][1]);
        camera.z = BrScalarToFloat(source.camera.bmat34Cam.m[3][2]);
        for (int axis = 0; axis < 3; ++axis)
        {
            right[axis] = BrScalarToFloat(source.camera.bmat34Cam.m[0][axis]);
            backward[axis] = BrScalarToFloat(source.camera.bmat34Cam.m[2][axis]);
        }
        const float fov = float(source.camera.aFov) * (360.f / 65536.f);
        // Preserve the shot's framing inside its letterboxed image when the
        // inspection renderer uses a different output aspect ratio.
        const float scale = std::max(1.f, (float(source.width) / source.height) / outputAspect);
        camera.verticalFov = 2 * std::atan(std::tan(fov * .00872664626f) * scale) * 57.2957795f;
    }
    camera.yaw = std::atan2(backward[0], backward[2]) * 57.2957795f;
    camera.pitch = std::asin(std::max(-1.f, std::min(1.f, backward[1]))) * 57.2957795f;
    const float yaw = camera.yaw * .01745329252f, pitch = camera.pitch * .01745329252f;
    const float baseRight[3] = {std::cos(yaw), 0, -std::sin(yaw)};
    const float baseUp[3] = {-std::sin(yaw) * std::sin(pitch), std::cos(pitch), -std::cos(yaw) * std::sin(pitch)};
    float cosine = 0, sine = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        cosine += right[axis] * baseRight[axis];
        sine += right[axis] * baseUp[axis];
    }
    camera.roll = std::atan2(sine, cosine) * 57.2957795f;
    float focus = 10, low[3] = {}, high[3] = {};
    bool first = true;
    for (const auto body : _impl->Bodies())
    {
        BCB bounds;
        body->GetBcbBounds(&bounds, fTrue);
        const float min[3] = {BrScalarToFloat(bounds.xrMin), BrScalarToFloat(bounds.yrMin), BrScalarToFloat(bounds.zrMin)};
        const float max[3] = {BrScalarToFloat(bounds.xrMax), BrScalarToFloat(bounds.yrMax), BrScalarToFloat(bounds.zrMax)};
        for (int axis = 0; axis < 3; ++axis)
        {
            low[axis] = first ? min[axis] : std::min(low[axis], min[axis]);
            high[axis] = first ? max[axis] : std::max(high[axis], max[axis]);
        }
        first = false;
    }
    if (!first)
    {
        const float position[3] = {camera.x, camera.y, camera.z};
        focus = 0;
        for (int axis = 0; axis < 3; ++axis)
            focus += (position[axis] - (low[axis] + high[axis]) * .5f) * backward[axis];
        focus = std::max(1.f, std::min(10000.f, focus));
    }
    const float position[3] = {camera.x, camera.y, camera.z};
    for (int axis = 0; axis < 3; ++axis)
        _impl->orbit.center[axis] = position[axis] - backward[axis] * focus;
    _impl->orbit.radius = focus * std::min(1.f, outputAspect) / 2.8f;
    _impl->orbit.framed = _impl->orbit.includes_camera = true;
    _impl->orbit.object = -1;
    camera.moveSpeed = std::max(.1f, std::min(1000.f, focus * .25f));
    return true;
}
bool Preview::PanOrbit(const PreviewView &view, float right, float up, Error &error)
{
    error = {};
    if (!_impl->ready || !_impl->orbit.framed || view.width < 64 || view.height < 64 ||
        view.width > PreviewMaxDimension || view.height > PreviewMaxDimension ||
        !std::isfinite(right) || !std::isfinite(up) || !std::isfinite(view.yaw) ||
        !std::isfinite(view.pitch) || !std::isfinite(view.roll) || !std::isfinite(view.verticalFov) ||
        view.verticalFov <= 0 || view.verticalFov >= 179 || !std::isfinite(view.zoom) || view.pitch < -90 || view.pitch > 90 ||
        view.zoom < .1f || view.zoom > 10)
        return Fail(error, ErrorCode::invalid_argument,
                    "Render an orbit view with valid camera values before panning.");
    const float aspect = float(view.width) / view.height;
    const float distance = _impl->orbit.radius * 2.8f / std::min(1.f, aspect) / view.zoom;
    const float height = view.orthographic ? 2 * _impl->orbit.radius * 1.15f / view.zoom :
                         2 * distance * std::tan(view.verticalFov * .00872664626f);
    const float yaw = std::fmod(view.yaw, 360.f) * .01745329252f, pitch = view.pitch * .01745329252f;
    const float sy = std::sin(yaw), cy = std::cos(yaw), sp = std::sin(pitch), cp = std::cos(pitch);
    const float backward[3] = {sy * cp, sp, cy * cp};
    const float cr = std::cos(view.roll * .01745329252f), sr = std::sin(view.roll * .01745329252f);
    const float cameraRight[3] = {cy * cr - sy * sp * sr, cp * sr, -sy * cr - cy * sp * sr};
    const float cameraUp[3] = {-sy * sp * cr - cy * sr, cp * cr, -cy * sp * cr + sy * sr};
    float delta[3];
    for (int axis = 0; axis < 3; ++axis)
        delta[axis] = height * (right * aspect * cameraRight[axis] + up * cameraUp[axis]);
    float fraction = 1;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(delta[axis]))
            return Fail(error, ErrorCode::invalid_argument, "Orbit pan exceeds the camera range.");
        const float position = _impl->orbit.center[axis] + backward[axis] * distance;
        if (delta[axis] > 0)
            fraction = std::min(fraction, (32700 - position) / delta[axis]);
        else if (delta[axis] < 0)
            fraction = std::min(fraction, (-32700 - position) / delta[axis]);
    }
    for (int axis = 0; axis < 3; ++axis)
        _impl->orbit.center[axis] += delta[axis] * std::max(0.f, fraction);
    return true;
}
std::vector<PreviewObject> Preview::Objects() const
{
    std::vector<PreviewObject> result;
    if (_impl->ready)
        for (const auto &actor : _impl->actors)
            result.push_back(actor->Snapshot());
    return result;
}
bool Preview::ReadAnimationTimeline(int object, PreviewAnimationTimeline &timeline, Error &error)
try
{
    timeline = {};
    error = {};
    if (!_impl->ready || !_impl->info.scene || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size())
        return Fail(error, ErrorCode::invalid_argument, "Select a scene object before reading animation events.");
    if (!_impl->actors[object]->ReadTimeline(_impl->info.firstFrame, _impl->info.lastFrame, timeline))
    {
        timeline = {};
        return Fail(error, ErrorCode::decode_failed,
                    "Cannot evaluate this actor's animation events within the preview budget.");
    }
    return true;
}
catch (const std::bad_alloc &)
{
    timeline = {};
    return Fail(error, ErrorCode::out_of_memory, "Cannot allocate animation events.");
}
bool Preview::ObjectBounds(int object, PreviewBounds &bounds, Error &error)
{
    bounds = {};
    error = {};
    if (!_impl->ready || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size())
        return Fail(error, ErrorCode::invalid_argument, "Select a scene object before reading its bounds.");
    const auto &actor = *_impl->actors[object];
    if (!actor.Visible() || !actor.body)
        return true;
    BCB box;
    actor.body->GetBcbBounds(&box);
    const BRS low[3] = {box.xrMin, box.yrMin, box.zrMin}, high[3] = {box.xrMax, box.yrMax, box.zrMax};
    for (int axis = 0; axis < 3; ++axis)
        if (low[axis] > high[axis])
            return true;
    const auto &transform = actor.body->PbactRoot()->t.t.mat;
    for (int corner = 0; corner < 8; ++corner)
        for (int axis = 0; axis < 3; ++axis)
        {
            double position = BrScalarToFloat(transform.m[3][axis]);
            for (int local = 0; local < 3; ++local)
                position += BrScalarToFloat((corner & (1 << local)) ? high[local] : low[local]) *
                            double(BrScalarToFloat(transform.m[local][axis]));
            bounds.corners[corner * 3 + axis] = static_cast<float>(position);
        }
    // Palette entry 108 is kiclrNormalHilite in the original actor UI.
    const auto &colour = (_impl->background ? _impl->background->palette : _impl->palette)[108];
    bounds.colour = (std::uint32_t(colour.bRed) << 16) | (std::uint32_t(colour.bGreen) << 8) | colour.bBlue;
    bounds.valid = true;
    return true;
}
bool Preview::Pick(const PreviewProjection &projection, bool authoredCamera, std::uint32_t width, std::uint32_t height,
                   int x, int y, int &object, float &distance, Error &error, bool renderAtOutputResolution)
try
{
    error = {};
    object = -1;
    distance = std::numeric_limits<float>::infinity();
    Internal::ClearDiagnostic();
    if (!_impl->ready || !_impl->info.scene || !projection.valid || width < 64 || height < 64 ||
        width > PreviewMaxDimension || height > PreviewMaxDimension ||
        !std::isfinite(projection.verticalFov) || projection.verticalFov <= 0 ||
        projection.verticalFov >= 179 || !std::isfinite(projection.aspect) || projection.aspect <= 0 ||
        !std::isfinite(projection.nearClip) || !std::isfinite(projection.farClip) || projection.nearClip <= 0 ||
        projection.farClip <= projection.nearClip || projection.farClip > 32767)
        return Fail(error, ErrorCode::invalid_argument, "Pick requires a rendered scene projection.");
    for (const auto value : projection.transform)
        if (!std::isfinite(value) || std::abs(value) > 32767)
            return Fail(error, ErrorCode::invalid_argument, "Pick camera exceeds the renderer range.");
    if (x < 0 || y < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height))
        return true;
    const auto bg = authoredCamera ? _impl->background.get() : nullptr;
    const auto rasterSize = bg && renderAtOutputResolution ? FittedSize(bg->width, bg->height, width, height)
        : std::make_pair(bg ? bg->width : width, bg ? bg->height : height);
    const auto renderWidth = rasterSize.first, renderHeight = rasterSize.second;
    const float scale = std::min(float(width) / renderWidth, float(height) / renderHeight);
    const int fittedWidth = static_cast<int>(renderWidth * scale),
              fittedHeight = static_cast<int>(renderHeight * scale);
    x -= (int(width) - fittedWidth) / 2;
    y -= (int(height) - fittedHeight) / 2;
    if (x < 0 || y < 0 || x >= fittedWidth || y >= fittedHeight)
        return true;
    x = std::min(int(renderWidth) - 1, static_cast<int>(x / scale));
    y = std::min(int(renderHeight) - 1, static_cast<int>(y / scale));

    PickWorld frame;
    frame.world = BrActorAllocate(BR_ACTOR_NONE, nullptr);
    br_camera camera = {};
    camera.type = BR_CAMERA_PERSPECTIVE_FOV;
    if (projection.orthographic)
    {
        if (!std::isfinite(projection.orthoHeight) || projection.orthoHeight <= 0 || projection.orthoHeight > 60000 ||
            !std::isfinite(projection.orthoHeight * projection.aspect) || projection.orthoHeight * projection.aspect > 60000)
            return Fail(error, ErrorCode::invalid_argument, "Invalid orthographic pick span.");
        camera.type = BR_CAMERA_PARALLEL;
        camera.height = BrFloatToScalar(projection.orthoHeight * .5f);
        camera.width = BrFloatToScalar(projection.orthoHeight * projection.aspect * .5f);
    }
    camera.field_of_view = BR_ANGLE_DEG(projection.verticalFov);
    camera.aspect = BrFloatToScalar(projection.aspect);
    camera.hither_z = BrFloatToScalar(projection.nearClip);
    camera.yon_z = BrFloatToScalar(projection.farClip);
    auto cam = frame.world ? BrActorAllocate(BR_ACTOR_CAMERA, &camera) : nullptr;
    if (!cam)
        return Fail(error, ErrorCode::out_of_memory, "Cannot allocate pick camera.");
    BrActorAdd(frame.world, cam);
    cam->t.type = BR_TRANSFORM_MATRIX34;
    for (int row = 0; row < 4; ++row)
        for (int axis = 0; axis < 3; ++axis)
            cam->t.t.mat.m[row][axis] = BrFloatToScalar(projection.transform[row * 3 + axis]);
    // Avoid passing a singular camera matrix to the legacy fixed-point picker.
    br_matrix34 inverse;
    if (!BrMatrix34Inverse(&inverse, &cam->t.t.mat))
        return Fail(error, ErrorCode::invalid_argument, "Pick camera is singular.");
    std::map<br_actor *, int> owners;
    if (!_impl->AddModels(frame.world, &owners) ||
        (!bg && _impl->background && !_impl->background->Mesh().AddToScene(frame.world)))
        return Fail(error, ErrorCode::out_of_memory, "Cannot prepare scene picking.");
    PickState pick{owners, projection.nearClip, projection.farClip};
    if (projection.orthographic)
    {
        pick.distanceScale = projection.farClip - projection.nearClip;
        pick.distanceOrigin = projection.nearClip;
    }
    if (bg)
    {
        const auto sourceX = std::uint32_t(std::uint64_t(x) * bg->width / renderWidth);
        const auto sourceY = std::uint32_t(std::uint64_t(y) * bg->height / renderHeight);
        const auto depth = bg->depths[std::size_t(sourceY) * bg->width + sourceX];
        if (depth != 0xffff)
        {
            br_matrix4 matrix;
            BrMatrix4Perspective(&matrix, camera.field_of_view, camera.aspect, -camera.hither_z, -camera.yon_z);
            const double denominator = BrScalarToFloat(matrix.m[2][2]) - static_cast<std::int16_t>(depth) / 32768.0;
            const double distance = BrScalarToFloat(matrix.m[3][2]) / denominator;
            if (std::isfinite(distance) && distance > 0)
                pick.closest = std::min(pick.closest, static_cast<float>(distance));
        }
    }
    br_pixelmap viewport = {};
    viewport.width = static_cast<br_uint_16>(renderWidth);
    viewport.height = static_cast<br_uint_16>(renderHeight);
    BrScenePick2D(frame.world, cam, &viewport, x - int(renderWidth) / 2, y - int(renderHeight) / 2, PickModel, &pick);
    if (Internal::AssertionFailed())
        return Fail(error, ErrorCode::decode_failed, "The renderer rejected scene picking.");
    object = pick.object;
    if (object >= 0)
        distance = pick.closest;
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Cannot allocate scene picking.");
}
bool Preview::ReadPresentation(PreviewPresentation &presentation, Error &error)
{
    return ReadPresentation(_impl->info.asset, presentation, error);
}
bool Preview::ReadPresentation(Id scene, PreviewPresentation &presentation, Error &error)
try
{
    error = {};
    presentation = {};
    if (!_impl->ready || scene.type != kctgScen ||
        !Internal::ReadPresentation(_impl->cache->Pcfl(), scene.number, presentation))
        return Fail(error, ErrorCode::decode_failed, "Cannot read the movie's captions, sound cues or pauses.");
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory for movie presentation.");
}
std::vector<PreviewSoundCue> Preview::FrameSounds() const
{
    std::vector<PreviewSoundCue> result;
    for (const auto &actor : _impl->actors)
        result.insert(result.end(), actor->sounds.begin(), actor->sounds.end());
    return result;
}
bool Preview::ReadSoundCues(std::vector<PreviewSoundCue> &cues, Error &error)
{
    return ReadSoundCues(_impl->info.asset, cues, error);
}
bool Preview::ReadTimingPresentation(Id scene, PreviewPresentation &presentation, Error &error)
try
{
    error = {};
    presentation = {};
    if (!_impl->ready || scene.type != kctgScen ||
        !Internal::ReadPresentation(_impl->cache->Pcfl(), scene.number, presentation, true, true))
        return Fail(error, ErrorCode::decode_failed, "Cannot read scene timing.");
    for (auto &cue : presentation.sounds)
        cue.scene = scene;
    std::stable_sort(presentation.sounds.begin(), presentation.sounds.end(),
                     [](const PreviewSoundCue &a, const PreviewSoundCue &b) {
                         return a.frame != b.frame ? a.frame < b.frame : a.actorSound < b.actorSound;
                     });
    return true;
}
catch (const std::bad_alloc &)
{
    presentation = {};
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory for scene timing.");
}
bool Preview::ReadSoundCues(Id scene, std::vector<PreviewSoundCue> &cues, Error &error)
try
{
    error = {};
    cues.clear();
    PreviewPresentation presentation;
    if (!_impl->ready || scene.type != kctgScen ||
        !Internal::ReadPresentation(_impl->cache->Pcfl(), scene.number, presentation, true))
        return Fail(error, ErrorCode::decode_failed, "Cannot read the scene's sound events.");
    cues = std::move(presentation.sounds);
    for (auto &cue : cues)
        cue.scene = scene;
    std::stable_sort(cues.begin(), cues.end(), [](const PreviewSoundCue &a, const PreviewSoundCue &b) {
        return a.frame != b.frame ? a.frame < b.frame : a.actorSound < b.actorSound;
    });
    return true;
}
catch (const std::bad_alloc &)
{
    cues.clear();
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory for movie sound cues.");
}
std::uint32_t Preview::PresentationColour(std::uint32_t colour) const
{
    if ((colour >> 24) == 0xfe)
    {
        const auto &value = (_impl->background ? _impl->background->palette : _impl->palette)[colour & 255];
        return 0x01000000 | (std::uint32_t(value.bRed) << 16) | (std::uint32_t(value.bGreen) << 8) | value.bBlue;
    }
    return colour;
}
bool Preview::ReadSound(const PreviewSoundCue &cue, PreviewAudio &audio, Error &error)
try
{
    error = {};
    audio = {};
    if (cue.silent)
    {
        audio.silent = true;
        return true;
    }
    if (!_impl->ready)
        return Fail(error, ErrorCode::invalid_argument, "Select a scene before reading its sounds.");
    TAGOnFile tag = {cue.source, 0, cue.sound.type, cue.sound.number};
    if (cue.source == 0)
    {
        auto file = _impl->cache->Pcfl();
        ChildChunkIdentification child;
        const auto scene = cue.scene.type == kctgScen ? cue.scene.number : _impl->info.asset.number;
        if (file->FGetKidChidCtg(kctgScen, scene, cue.child, kctgMsnd, &child))
            tag.cno = child.cki.cno;
        else
            for (const auto &chunk : _impl->assets.Chunks())
                if (chunk.id.type == kctgMvie &&
                    file->FGetKidChidCtg(kctgMvie, chunk.id.number, cue.child, kctgMsnd, &child))
                {
                    tag.cno = child.cki.cno;
                    break;
                }
    }
    auto source = _impl->Resolve(tag);
    if (!source)
        return Fail(error, ErrorCode::decode_failed, _impl->resolveError.c_str());
    if (!Internal::ReadAudio(source->Pcfl(), tag.ctg, tag.cno, audio))
        return Fail(error, ErrorCode::decode_failed,
                    ("Cannot decode sound " + Identity({tag.ctg, tag.cno}) + ".").c_str());
    audio.source = cue.source;
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to decode movie audio.");
}
bool Preview::Open(const std::string &archivePath, const std::string &resourcesPath, Error &error)
{
    Archive archive;
    if (!archive.Open(archivePath, error))
        return false;
    return Open(std::move(archive), resourcesPath, error);
}
bool Preview::Open(Archive &&archive, const std::string &resourcesPath, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    // Validate both paths before touching the live renderer or session.
    if (!archive.NativeFile())
        return Fail(error, ErrorCode::invalid_argument, "Open the movie archive before transferring it to preview.");
    std::unique_ptr<Impl> opened(new Impl);
    opened->assets._impl.swap(archive._impl);
    if (!opened->resources.Open(resourcesPath, error))
        return false;
    if (!WithinBudget(opened->assets, static_cast<PChunkyFile>(opened->assets.NativeFile()), error) ||
        !WithinBudget(opened->resources, static_cast<PChunkyFile>(opened->resources.NativeFile()), error))
        return false;
    DataBlock block;
    auto file = static_cast<PChunkyFile>(opened->resources.NativeFile());
    if (!file->FFind(kctgColorTable, 0, &block))
        return Fail(error, ErrorCode::unsupported_format,
                    "Resources must contain the GLCR/0 palette from 3dmovie.chk.");
    short order;
    PDynamicArray colours = DynamicArray::PglRead(&block, &order);
    const bool paletteOk = colours && colours->CbEntry() == sizeof(Color) && colours->IvMac() == 256;
    if (paletteOk)
    {
        if (order == kboOther)
            SwapBytesRglw(colours->QvGet(0), colours->IvMac());
        std::memcpy(opened->palette, colours->QvGet(0), sizeof(opened->palette));
    }
    ReleasePpo(&colours);
    if (paletteOk)
        Internal::ResolveDisplayPalette(opened->palette);
    if (!paletteOk || !file->FFind(kctgTmap, 0))
        return Fail(error, ErrorCode::unsupported_format,
                    "Resources require a 256-colour GLCR/0 palette and TMAP/0 shade table.");
    if (runtimeOwned && !_impl->running)
        return Fail(error, ErrorCode::invalid_argument, "Another preview owns the BRender runtime. Close it first.");
    if (!_impl->running)
    {
        BrBegin();
        BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
        opened->running = runtimeOwned = true;
    }
    opened->cache = ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(opened->assets.NativeFile()), 32L << 20);
    if (!opened->cache || Internal::AssertionFailed())
        return Fail(error, ErrorCode::decode_failed, "Cannot initialize the asset resource reader.");
    if (!Material_MTRL::FSetShadeTable(file, kctgTmap, 0))
        return Fail(error, ErrorCode::decode_failed, "Cannot load the resource shade table.");
    // Transfer the runtime only after replacement resources are ready. Old
    // models/materials are destroyed immediately, without rendering them again.
    _impl->running = false;
    opened->running = true;
    _impl.swap(opened);
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to open preview resources.");
}
bool Preview::ReadSceneThumbnail(Id scene, PreviewImage &image, Error &error)
try
{
    image = {};
    error = {};
    auto file = static_cast<PChunkyFile>(_impl->assets.NativeFile());
    if (!file || scene.type != kctgScen)
        return Fail(error, ErrorCode::invalid_argument, "Open a movie before reading scene thumbnails.");
    ChildChunkIdentification kid;
    if (!file->FGetKidChidCtg(scene.type, scene.number, 0, kctgThumbMbmp, &kid))
        return true;
    DataBlock block;
    if (!file->FFind(kid.cki.ctg, kid.cki.cno, &block) || block.Cb() > 1024 * 1024)
        return Fail(error, ErrorCode::decode_failed, "Cannot read the saved scene thumbnail.");
    auto bitmap = MaskedBitmapMBMP::PmbmpRead(&block);
    if (!bitmap)
        return Fail(error, ErrorCode::decode_failed, "Cannot decode the saved scene thumbnail.");
    const auto releaseBitmap = [](MaskedBitmapMBMP *value) { ReleasePpo(&value); };
    std::unique_ptr<MaskedBitmapMBMP, decltype(releaseBitmap)> bitmapOwner(bitmap, releaseBitmap);
    RC bounds;
    bitmap->GetRc(&bounds);
    const auto width = bounds.Dxp(), height = bounds.Dyp();
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024)
    {
        return Fail(error, ErrorCode::decode_failed, "Invalid saved scene thumbnail dimensions.");
    }
    std::vector<byte> indices(static_cast<std::size_t>(width) * height, 0);
    image.rgba.resize(indices.size() * 4);
    bitmap->Draw(indices.data(), width, height, -bounds.xpLeft, -bounds.ypTop);
    Color palette[256];
    std::memcpy(palette, _impl->palette, sizeof(palette));
    auto resources = static_cast<PChunkyFile>(_impl->resources.NativeFile());
    if (resources->FFind(kctgColorTable, 0x20001, &block))
    {
        short order;
        auto colours = DynamicArray::PglRead(&block, &order);
        if (colours && colours->CbEntry() == sizeof(Color) && colours->IvMac() == 256)
        {
            if (order == kboOther)
                SwapBytesRglw(colours->QvGet(0), colours->IvMac());
            std::memcpy(palette, colours->QvGet(0), sizeof(palette));
        }
        ReleasePpo(&colours);
    }
    image.width = width;
    image.height = height;
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        const auto &colour = palette[indices[i]];
        image.rgba[i * 4] = colour.bRed;
        image.rgba[i * 4 + 1] = colour.bGreen;
        image.rgba[i * 4 + 2] = colour.bBlue;
        image.rgba[i * 4 + 3] = 255;
    }
    return true;
}
catch (const std::bad_alloc &)
{
    image = {};
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory for the saved scene thumbnail.");
}
bool Preview::AddContent(std::int32_t sourceId, const std::string &path, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->running || sourceId <= 0)
        return Fail(error, ErrorCode::invalid_argument,
                    "Open preview resources and specify a positive content source ID.");
    for (const auto &source : _impl->sources)
        if (source->id == sourceId && source->archive.Path() == path)
            return true;
    std::unique_ptr<Impl::Source> source(new Impl::Source);
    source->id = sourceId;
    if (!source->archive.Open(path, error))
        return false;
    if (!WithinBudget(source->archive, static_cast<PChunkyFile>(source->archive.NativeFile()), error))
        return false;
    source->cache = ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(source->archive.NativeFile()), 32L << 20);
    if (!source->cache || Internal::AssertionFailed())
        return Fail(error, ErrorCode::decode_failed, "Cannot initialize the content resource reader.");
    _impl->sources.push_back(std::move(source));
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to load content archive.");
}
bool Preview::Select(Id asset, Error &error, bool preserveFraming)
try
{
    error = {};
    Internal::ClearDiagnostic();
    _impl->ClearAsset(preserveFraming);
    if (!_impl->cache)
        return Fail(error, ErrorCode::invalid_argument, "Open preview resources first.");
    bool ok = false;
    if (asset.type == kctgTmpl)
    {
        _impl->tmpl = static_cast<PTemplate>(_impl->cache->PbacoFetch(asset.type, asset.number, Template::FReadTmpl));
        if (_impl->tmpl)
        {
            _impl->BindTemplate(_impl->tmpl);
            _impl->body = _impl->tmpl->PbodyCreate();
            auto &info = _impl->info;
            info.prop = _impl->tmpl->FIsProp();
            for (LegacyLong i = 0; i < _impl->tmpl->Cactn(); ++i)
            {
                LegacyLong count = 0;
                if (!_impl->tmpl->FGetCcelActn(i, &count) || count <= 0)
                    break;
                PreviewAction action;
                action.name = "Action " + std::to_string(i);
                ChildChunkIdentification child;
                if (_impl->cache->Pcfl()->FGetKidChidCtg(asset.type, asset.number, i, kctgActn, &child))
                    for (const auto &chunk : _impl->assets.Chunks())
                        if (chunk.id == Id{child.cki.ctg, child.cki.cno} && !chunk.name.empty())
                        {
                            action.name = std::to_string(i) + ": " + chunk.name;
                            break;
                        }
                action.celCount = static_cast<std::uint32_t>(count);
                info.actions.push_back(action);
            }
            ok = _impl->body && !info.actions.empty() && _impl->tmpl->FSetActnCel(_impl->body, 0, 0);
            if (ok)
            {
                BRA x, y, z;
                _impl->tmpl->GetRestOrien(&x, &y, &z);
                BMAT34 rest;
                BrMatrix34RotateX(&rest, x);
                BrMatrix34PostRotateY(&rest, y);
                BrMatrix34PostRotateZ(&rest, z);
                _impl->body->LocateOrient(rZero, rZero, rZero, &rest);
            }
        }
    }
    else if (asset.type == kctgBmdl)
    {
        _impl->model = static_cast<PModel>(_impl->cache->PbacoFetch(asset.type, asset.number, Model::FReadModl));
        ok = _impl->model != nullptr;
    }
    else if (asset.type == kctgBkgd)
        ok = _impl->LoadBackground(_impl->cache, asset.number);
    else if (asset.type == kctgScen || asset.type == kctgActr)
    {
        auto file = _impl->cache->Pcfl();
        std::vector<ChunkNumber> actorChunks;
        if (asset.type == kctgScen)
        {
            SceneData scene;
            if (!scene.FRead(file, asset.number))
                return Fail(error, ErrorCode::decode_failed, "Cannot read the scene header.");
            _impl->info.scene = true;
            _impl->info.firstFrame = scene.firstFrame;
            _impl->info.lastFrame = scene.lastFrame;
            ChildChunkIdentification child;
            DataBlock block;
            if (!file->FGetKidChidCtg(asset.type, asset.number, 1, kctgStartGg, &child) ||
                !file->FFind(child.cki.ctg, child.cki.cno, &block))
                return Fail(error, ErrorCode::decode_failed, "Scene start events are missing.");
            short order;
            auto starts = SceneData::PggReadEvents(&block, &order, true);
            if (!starts)
                return Fail(error, ErrorCode::decode_failed, "Cannot read scene start events.");
            TAGOnFile backgroundTag = {};
            bool hasBackground = false;
            for (LegacyLong i = 0; i < starts->IvMac(); ++i)
            {
                SceneEvent event;
                starts->GetFixed(i, &event);
                if (order == kboOther)
                    SwapBytesBom(&event, kbomSev);
                if (event.sevt == sevtAddActr)
                {
                    std::uint32_t slot;
                    starts->Get(i, &slot);
                    if (order == kboOther)
                        SwapBytesRglw(&slot, 1);
                    if (!file->FGetKidChidCtg(asset.type, asset.number, slot, kctgActr, &child))
                    {
                        ReleasePpo(&starts);
                        error.hasChunk = true;
                        error.chunk = asset;
                        return Fail(error, ErrorCode::decode_failed,
                                    ("Scene references missing actor child " + std::to_string(slot) + ".").c_str());
                    }
                    actorChunks.push_back(child.cki.cno);
                }
                else if (event.sevt == sevtSetBkgd)
                {
                    starts->Get(i, &backgroundTag);
                    if (order == kboOther)
                        SwapBytesRglw(&backgroundTag, sizeof(backgroundTag) / 4);
                    hasBackground = true;
                }
                else if (event.sevt == sevtChngCamera)
                {
                    starts->Get(i, &_impl->initialCamera);
                    if (order == kboOther)
                        SwapBytesRglw(&_impl->initialCamera, 1);
                }
            }
            ReleasePpo(&starts);
            if (hasBackground)
            {
                auto source = _impl->Resolve(backgroundTag);
                if (!source || !_impl->LoadBackground(source, backgroundTag.cno))
                    return Fail(error, ErrorCode::decode_failed,
                                source ? "Cannot load scene background." : _impl->resolveError.c_str());
            }
            if (!file->FGetKidChidCtg(asset.type, asset.number, 0, kctgFrmGg, &child) ||
                !file->FFind(child.cki.ctg, child.cki.cno, &block))
                return Fail(error, ErrorCode::decode_failed, "Scene frame events are missing.");
            auto frames = SceneData::PggReadEvents(&block, &order, false);
            if (!frames)
                return Fail(error, ErrorCode::decode_failed, "Cannot read scene frame events.");
            for (LegacyLong i = 0; i < frames->IvMac(); ++i)
            {
                SceneEvent event;
                frames->GetFixed(i, &event);
                if (order == kboOther)
                    SwapBytesBom(&event, kbomSev);
                if (event.sevt == sevtChngCamera)
                {
                    std::uint32_t camera;
                    frames->Get(i, &camera);
                    if (order == kboOther)
                        SwapBytesRglw(&camera, 1);
                    _impl->cameraChanges.push_back({event.nfrm, camera});
                }
            }
            ReleasePpo(&frames);
        }
        else
        {
            DataBlock block;
            ActorChunkOnFile header;
            if (!file->FFind(asset.type, asset.number, &block) || !ActorData::FReadHeader(&block, &header))
                return Fail(error, ErrorCode::decode_failed, "Cannot read the actor header.");
            if (header.nfrmFirst == knfrmInvalid)
                return Fail(error, ErrorCode::unsupported_format,
                            "This actor has no on-stage frames. Select its template to inspect its geometry.");
            _impl->info.firstFrame = header.nfrmFirst;
            _impl->info.lastFrame = header.nfrmLast;
            actorChunks.push_back(asset.number);
        }
        for (const auto number : actorChunks)
        {
            std::unique_ptr<Internal::PreviewActor> actor(new Internal::PreviewActor);
            if (!actor->Load(
                    _impl->cache, number, [&](const TAGOnFile &tag) { return _impl->Resolve(tag); },
                    [&](PTemplate value) { _impl->BindTemplate(value); }, _impl->visibilityClock))
            {
                const auto message = _impl->resolveError.empty() ? "Cannot load the actor route, events or template."
                                                                 : _impl->resolveError;
                _impl->ClearAsset();
                return Fail(error, ErrorCode::decode_failed, message.c_str());
            }
            _impl->actors.push_back(std::move(actor));
        }
        if (asset.type == kctgActr)
        {
            _impl->info.firstFrame = _impl->actors[0]->FirstFrame();
            _impl->info.lastFrame = _impl->actors[0]->LastFrame();
        }
        _impl->info.timeline = true;
        _impl->info.actors = static_cast<std::uint32_t>(_impl->actors.size());
        _impl->ready = true;
        ok = SetFrame(_impl->info.firstFrame, error);
    }
    else
        return Fail(error, ErrorCode::unsupported_format, "This chunk does not yet have a 3D preview.");
    if (!ok || Internal::AssertionFailed())
    {
        _impl->ClearAsset();
        error.hasChunk = true;
        error.chunk = asset;
        if (error)
            return false;
        return Fail(error, ErrorCode::decode_failed, "The engine could not load this asset and its pose dependencies.");
    }
    _impl->info.asset = asset;
    _impl->RefreshStats();
    _impl->ready = true;
    return true;
}
catch (const std::bad_alloc &)
{
    _impl->ClearAsset();
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to load preview asset.");
}
bool Preview::SetPose(std::uint32_t action, std::uint32_t cel, Error &error)
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->ready || !_impl->tmpl || action >= _impl->info.actions.size() ||
        cel >= _impl->info.actions[action].celCount)
        return Fail(error, ErrorCode::invalid_argument, "Action or cel is out of range.");
    if (!_impl->tmpl->FSetActnCel(_impl->body, action, cel) || Internal::AssertionFailed())
    {
        _impl->ready = false;
        return Fail(error, ErrorCode::decode_failed, "Cannot apply this action pose.");
    }
    _impl->RefreshStats();
    return true;
}
bool Preview::SetFrame(std::int32_t frame, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->ready || !_impl->info.timeline || frame < _impl->info.firstFrame || frame > _impl->info.lastFrame)
        return Fail(error, ErrorCode::invalid_argument, "Scene frame is out of range.");
    for (const auto &actor : _impl->actors)
        if (!actor->Seek(frame) || Internal::AssertionFailed())
        {
            _impl->ready = false;
            return Fail(error, ErrorCode::decode_failed,
                        _impl->resolveError.empty() ? "Cannot evaluate this scene frame."
                                                    : _impl->resolveError.c_str());
        }
    if (_impl->background)
    {
        auto camera = _impl->initialCamera;
        for (const auto &change : _impl->cameraChanges)
            if (change.frame <= frame)
                camera = change.camera;
        if (!SetCamera(camera, error))
            return false;
    }
    _impl->info.currentFrame = frame;
    _impl->RefreshStats();
    return true;
}
catch (const std::bad_alloc &)
{
    _impl->ready = false;
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to evaluate scene frame.");
}
bool Preview::CameraProjection(std::uint32_t camera, PreviewProjection &projection, Error &error)
try
{
    projection = {};
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->ready || !_impl->background || camera >= _impl->info.cameraCount)
        return Fail(error, ErrorCode::invalid_argument, "Background camera is out of range.");
    if (!_impl->background->CameraProjection(camera, projection))
        return Fail(error, ErrorCode::decode_failed, "Cannot read the background camera projection.");
    return true;
}
catch (const std::bad_alloc &)
{
    projection = {};
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to read camera guides.");
}
bool Preview::SetCamera(std::uint32_t camera, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->ready || !_impl->background || camera >= _impl->info.cameraCount)
        return Fail(error, ErrorCode::invalid_argument, "Background camera is out of range.");
    if (camera == _impl->info.currentCamera)
        return true;
    if (!_impl->background->SetCamera(camera) || Internal::AssertionFailed())
    {
        _impl->ready = false;
        return Fail(error, ErrorCode::decode_failed, "Cannot load the background camera and depth image.");
    }
    _impl->info.currentCamera = camera;
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to load the background camera.");
}
#include "preview_render_host.h"
bool Preview::Render(const PreviewView &view, PreviewImage &image, Error &error)
try
{
    image = {};
    error = {};
    Internal::ClearDiagnostic();
    Impl::RenderHost host = {this, _impl.get(), &view, &error};
    auto services = host.Services();
    MMPreviewView v = {};
    v.width = view.width;
    v.height = view.height;
    v.yaw = view.yaw;
    v.pitch = view.pitch;
    v.roll = view.roll;
    v.zoom = view.zoom;
    v.vertical_fov = view.verticalFov;
    v.near_clip = view.nearClip;
    v.position[0] = view.cameraX;
    v.position[1] = view.cameraY;
    v.position[2] = view.cameraZ;
    v.authored = view.authoredCamera;
    v.output_resolution = view.renderAtOutputResolution;
    v.free_camera = view.freeCamera;
    v.frame_camera = view.frameSceneCamera;
    v.stable_orbit = view.stableOrbit;
    v.object = view.orbitObject;
    v.depth = view.includeDepth;
    v.fit_depth = view.fitSceneDepth;
    v.display = view.displayMode;
    v.orthographic = view.orthographic;
    v.axis = view.axisView;
    v.rasterizer = view.rasterizer != nullptr;
    MMPreviewImage rendered = {};
    const auto result = mm_preview_render(&v, &_impl->orbit, &rendered, &services);
    if (host.exception)
        std::rethrow_exception(host.exception);
    switch (result)
    {
    case MM_PREVIEW_OK:
        break;
    case MM_PREVIEW_NOT_READY:
        return Fail(error, ErrorCode::invalid_argument, "Select an asset to render.");
    case MM_PREVIEW_INVALID:
        return Fail(error, ErrorCode::invalid_argument, "Preview dimensions or camera values are out of range.");
    case MM_PREVIEW_BUFFERS:
        return Fail(error, ErrorCode::out_of_memory, "Cannot allocate preview buffers.");
    case MM_PREVIEW_ASSET_RANGE:
        return Fail(error, ErrorCode::unsupported_format, "Asset exceeds the fixed-point preview camera range.");
    case MM_PREVIEW_CAMERA_RANGE:
        return Fail(error, ErrorCode::unsupported_format, "Camera position exceeds the fixed-point renderer range.");
    case MM_PREVIEW_CAMERA:
        return Fail(error, ErrorCode::out_of_memory, "Cannot allocate preview camera.");
    case MM_PREVIEW_LIGHTS:
        return Fail(error, ErrorCode::out_of_memory,
                    _impl->background ? "Cannot allocate scene lighting." : "Cannot allocate preview lighting.");
    case MM_PREVIEW_MODELS:
        return Fail(error, ErrorCode::out_of_memory, "Cannot allocate preview model instance.");
    case MM_PREVIEW_MESH:
        return Fail(error, ErrorCode::out_of_memory, "Cannot prepare background scenery models.");
    case MM_PREVIEW_RENDERER:
        return Fail(error, ErrorCode::decode_failed, "The renderer rejected this asset.");
    case MM_PREVIEW_HOST:
        return false;
    default:
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory for the preview image.");
    }
    auto &out = host.image;
    out.width = rendered.width;
    out.height = rendered.height;
    out.coveredPixels = rendered.covered;
    out.camera.x = rendered.position[0];
    out.camera.y = rendered.position[1];
    out.camera.z = rendered.position[2];
    out.camera.yaw = rendered.yaw;
    out.camera.pitch = rendered.pitch;
    out.camera.roll = rendered.roll;
    out.camera.moveSpeed = rendered.move_speed;
    out.camera.verticalFov = rendered.vertical_fov;
    out.projection = Impl::RenderHost::Projection(rendered.projection);
    out.sceneCamera = Impl::RenderHost::Projection(rendered.scene_camera);
    image = std::move(out);
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory for the preview image.");
}
} // namespace MM

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
#include "mm/scene_runtime.h"
#include "mm/preview_pick.h"
#include "mm/preview_camera.h"
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
    struct PickHost;
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
    std::size_t originalActors = 0;
    bool virtualScene = false;
    std::vector<PreviewSceneDefinition> sceneDefinitions;
    std::vector<PreviewPropDefinition> propDefinitions;
    bool IsVirtualScene(Id scene) const
    {
        return scene.type == kctgScen && std::any_of(sceneDefinitions.begin(), sceneDefinitions.end(),
            [&](const auto &value) { return value.number == scene.number; });
    }
    bool SyncActors(Error &error)
    {
        std::vector<const PreviewPropDefinition *> desired;
        for (const auto &prop : propDefinitions) if (prop.scene == info.asset.number) desired.push_back(&prop);
        std::size_t kept = 0;
        while (kept < desired.size() && originalActors + kept < actors.size() &&
               actors[originalActors + kept]->number == desired[kept]->number) ++kept;
        std::vector<std::unique_ptr<Internal::PreviewActor>> added;
        for (std::size_t i = kept; i < desired.size(); ++i)
        {
            auto actor = std::make_unique<Internal::PreviewActor>();
            if (!actor->Create(*desired[i], [&](const TAGOnFile &tag) { return Resolve(tag); },
                              [&](PTemplate value) { BindTemplate(value); }, visibilityClock) || !actor->Seek(info.currentFrame))
                return Fail(error, ErrorCode::decode_failed, "Cannot create the selected prop and its pose dependencies.");
            added.push_back(std::move(actor));
        }
        actors.reserve(originalActors + desired.size());
        actors.resize(originalActors + kept);
        for (auto &actor : added) actors.push_back(std::move(actor));
        info.actors = actors.size();
        return true;
    }
    // Explicit placement edits move the prop through the existing camera.
    // Advance the tracked anchor by the edit only, retaining animation follow.
    void PreserveOrbitForPlacement(int object, const PreviewObject &before)
    {
        if (!mm_preview_orbit_follows(&orbit, object, 1)) return;
        const auto after = actors[object]->Snapshot();
        const float oldPosition[] = {before.x, before.y, before.z}, newPosition[] = {after.x, after.y, after.z};
        mm_preview_orbit_move(&orbit, object, oldPosition, newPosition);
    }
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
    std::vector<CameraChange> cameraChanges, originalCameraChanges;
    std::uint32_t initialCamera = 0;
    std::int32_t originalFirstFrame = 0, originalLastFrame = 0;
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
        if (tag.sid == 0 && cache && cache->Pcfl()->FFind(tag.ctg, tag.cno))
            return cache;
        for (const auto &source : sources)
            if (source->id == tag.sid && source->cache->Pcfl()->FFind(tag.ctg, tag.cno))
                return source->cache;
        resolveError = "Missing source " + std::to_string(tag.sid) + ": " + Identity({tag.ctg, tag.cno}) +
                       ". Add the content archive for this source.";
        return nullptr;
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
        originalActors = 0;
        virtualScene = false;
        originalCameraChanges.clear();
        visibilityClock = 0;
        background.reset();
        cameraChanges.clear();
        if (!preserveFraming)
            mm_preview_orbit_reset(&orbit);
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
#include "preview_render_host.h"
#include "preview_pick_host.h"
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
    mm_preview_orbit_reset(&_impl->orbit);
}
bool Preview::MatchSceneCamera(float outputAspect, PreviewCamera &camera, Error &error)
{
    error = {};
    Impl::RenderHost host = {this, _impl.get(), nullptr, &error};
    auto services = host.Services();
    MMPreviewCameraPose pose;
    auto result = mm_preview_camera_match(outputAspect, &_impl->orbit, &pose, &services,
                                          [](float v) { return std::atan(v); });
    camera = {};
    camera.x = pose.position[0]; camera.y = pose.position[1]; camera.z = pose.position[2];
    camera.yaw = pose.yaw; camera.pitch = pose.pitch; camera.roll = pose.roll;
    camera.moveSpeed = pose.move_speed; camera.verticalFov = pose.vertical_fov;
    if (result != MM_PREVIEW_CAMERA_OK)
        return Fail(error, ErrorCode::invalid_argument, "Select a scene before matching its camera.");
    return true;
}
bool Preview::PanOrbit(const PreviewView &view, float right, float up, Error &error)
{
    error = {};
    Impl::RenderHost host = {this, _impl.get(), &view, &error};
    const auto services = host.Services();
    MMPreviewView v = {};
    v.width = view.width; v.height = view.height; v.yaw = view.yaw; v.pitch = view.pitch;
    v.roll = view.roll; v.vertical_fov = view.verticalFov; v.zoom = view.zoom; v.orthographic = view.orthographic;
    const auto result = mm_preview_orbit_pan(&v, right, up, &_impl->orbit, _impl->ready, &services.math);
    if (result == MM_PREVIEW_CAMERA_PAN_RANGE)
        return Fail(error, ErrorCode::invalid_argument, "Orbit pan exceeds the camera range.");
    if (result != MM_PREVIEW_CAMERA_OK)
        return Fail(error, ErrorCode::invalid_argument,
                    "Render an orbit view with valid camera values before panning.");
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
bool Preview::TranslateObject(int object, const float delta[3], Error &error)
{
    error = {};
    if (!_impl->ready || !_impl->info.scene || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size() ||
        !delta)
        return Fail(error, ErrorCode::invalid_argument, "Select a visible object and a position within +/-32700 world units.");
    const auto before = _impl->actors[object]->Snapshot();
    if (!_impl->actors[object]->Translate(delta))
        return Fail(error, ErrorCode::invalid_argument, "Select a visible object and a position within +/-32700 world units.");
    _impl->PreserveOrbitForPlacement(object, before);
    return true;
}
bool Preview::SetObjectOffset(int object, const float offset[3], Error &error)
{
    error = {};
    if (!_impl->ready || !_impl->info.scene || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size() ||
        !offset)
        return Fail(error, ErrorCode::invalid_argument, "The object placement is out of range.");
    const auto before = _impl->actors[object]->Snapshot();
    if (!_impl->actors[object]->SetOffset(offset))
        return Fail(error, ErrorCode::invalid_argument, "The object placement is out of range.");
    _impl->PreserveOrbitForPlacement(object, before);
    return true;
}
bool Preview::ObjectShape(int object, PreviewObjectShape &shape, float left[9], float right[9], Error &error) const
{
    error = {};
    if (!_impl->ready || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size() ||
        !_impl->actors[object]->body) return Fail(error, ErrorCode::invalid_argument, "Select a scene object first.");
    _impl->actors[object]->Shape(shape, left, right);
    return true;
}
bool Preview::SetObjectShape(int object, const PreviewObjectShape &shape, Error &error)
{
    error = {};
    if (!_impl->ready || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size() ||
        !_impl->actors[object]->body) return Fail(error, ErrorCode::invalid_argument, "Select a scene object first.");
    for (float n : shape.scale)
        if (!std::isfinite(n) || n < .01f || n > 10) return Fail(error, ErrorCode::invalid_argument, "Size must be between 1% and 1000%.");
    for (float n : shape.rotation)
        if (!std::isfinite(n) || std::abs(n) > 1.01f) return Fail(error, ErrorCode::invalid_argument, "Invalid object rotation.");
    const auto r=shape.rotation;
    const float determinant=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
    if (determinant<.99f) return Fail(error, ErrorCode::invalid_argument, "Object rotation cannot mirror or collapse an axis.");
    for (int a = 0; a < 3; ++a) for (int b = a; b < 3; ++b)
    {
        float dot = 0;
        for (int c = 0; c < 3; ++c) dot += shape.rotation[a*3+c] * shape.rotation[b*3+c];
        if (std::abs(dot - (a == b ? 1.f : 0.f)) > .003f) return Fail(error, ErrorCode::invalid_argument, "Rotation must be orthonormal.");
    }
    BCB before, after;
    auto &actor = *_impl->actors[object];
    const bool hold = mm_preview_orbit_follows(&_impl->orbit, object, actor.Visible()) != 0;
    if (hold) actor.body->GetBcbBounds(&before, fTrue);
    if (!actor.SetShape(shape)) return Fail(error, ErrorCode::decode_failed, "Cannot transform the selected object.");
    if (hold)
    {
        actor.body->GetBcbBounds(&after, fTrue);
        const MMI32 a[6] = {before.xrMin,before.yrMin,before.zrMin,before.xrMax,before.yrMax,before.zrMax};
        const MMI32 b[6] = {after.xrMin,after.yrMin,after.zrMin,after.xrMax,after.yrMax,after.zrMax};
        Impl::RenderHost host = {this, _impl.get(), nullptr, &error};
        const auto services = host.Services();
        mm_preview_orbit_resize(&_impl->orbit, a, b, &services.math);
    }
    return true;
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
    const MMI32 low[3] = {box.xrMin, box.yrMin, box.zrMin}, high[3] = {box.xrMax, box.yrMax, box.zrMax};
    const auto &transform = actor.body->PbactRoot()->t.t.mat;
    MMI32 matrix[12];
    static_assert(sizeof(matrix) == sizeof(transform), "Object bounds matrix layout");
    std::memcpy(matrix, &transform, sizeof(matrix));
    Impl::RenderHost host = {this, _impl.get(), nullptr, &error};
    const auto services = host.Services();
    if (!mm_preview_camera_bounds(low, high, matrix, bounds.corners, &services.math)) return true;
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
    Impl::RenderHost host = {this, _impl.get(), nullptr, &error};
    host.readBodyBounds = false;
    auto render = host.Services();
    auto services = Impl::PickHost::Services(render);
    MMPreviewProjection p = {};
    p.valid = projection.valid;
    p.orthographic = projection.orthographic;
    std::copy(projection.transform, projection.transform + 12, p.transform);
    p.vertical_fov = projection.verticalFov;
    p.aspect = projection.aspect;
    p.near_clip = projection.nearClip;
    p.far_clip = projection.farClip;
    p.ortho_height = projection.orthoHeight;
    MMI32 selected;
    const auto result = mm_preview_pick(&p, authoredCamera, renderAtOutputResolution, width, height,
                                        x, y, &selected, &distance, &services);
    if (host.exception) std::rethrow_exception(host.exception);
    switch (result)
    {
    case MM_PREVIEW_PICK_OK: object = selected; break;
    case MM_PREVIEW_PICK_INVALID:
        return Fail(error, ErrorCode::invalid_argument, "Pick requires a rendered scene projection.");
    case MM_PREVIEW_PICK_RANGE:
        return Fail(error, ErrorCode::invalid_argument, "Pick camera exceeds the renderer range.");
    case MM_PREVIEW_PICK_SPAN:
        return Fail(error, ErrorCode::invalid_argument, "Invalid orthographic pick span.");
    case MM_PREVIEW_PICK_CAMERA:
        return Fail(error, ErrorCode::out_of_memory, "Cannot allocate pick camera.");
    case MM_PREVIEW_PICK_SINGULAR:
        return Fail(error, ErrorCode::invalid_argument, "Pick camera is singular.");
    case MM_PREVIEW_PICK_MODELS:
        return Fail(error, ErrorCode::out_of_memory, "Cannot prepare scene picking.");
    case MM_PREVIEW_PICK_RENDERER:
        return Fail(error, ErrorCode::decode_failed, "The renderer rejected scene picking.");
    default: return Fail(error, ErrorCode::out_of_memory, "Cannot allocate scene picking.");
    }
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
    if (_impl->IsVirtualScene(scene)) return true;
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
    if (_impl->IsVirtualScene(scene)) return true;
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
    if (_impl->IsVirtualScene(scene)) return true;
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
bool Preview::Open(Archive &&archive, const std::string &resourcesPath, Error &error, bool preserveFraming)
{
    if (!archive.NativeFile()) return Fail(error, ErrorCode::invalid_argument, "Open the movie archive before transferring it to preview.");
    return OpenCore(std::move(archive), resourcesPath, error, preserveFraming);
}
bool Preview::OpenEmpty(const std::string &resourcesPath, Error &error)
{
    Archive empty;
    return OpenCore(std::move(empty), resourcesPath, error, false);
}
void Preview::ClearScene() { _impl->ClearAsset(); }
bool Preview::OpenCore(Archive &&archive, const std::string &resourcesPath, Error &error, bool preserveFraming)
try
{
    error = {};
    Internal::ClearDiagnostic();
    // Validate both paths before touching the live renderer or session.
    std::unique_ptr<Impl> opened(new Impl);
    opened->assets._impl.swap(archive._impl);
    if (!opened->resources.Open(resourcesPath, error))
        return false;
    if ((opened->assets.NativeFile() && !WithinBudget(opened->assets, static_cast<PChunkyFile>(opened->assets.NativeFile()), error)) ||
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
    if (opened->assets.NativeFile())
        opened->cache = ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(opened->assets.NativeFile()), 32L << 20);
    if ((opened->assets.NativeFile() && !opened->cache) || Internal::AssertionFailed())
        return Fail(error, ErrorCode::decode_failed, "Cannot initialize the asset resource reader.");
    if (!Material_MTRL::FSetShadeTable(file, kctgTmap, 0))
        return Fail(error, ErrorCode::decode_failed, "Cannot load the resource shade table.");
    // Transfer the runtime only after replacement resources are ready. Old
    // models/materials are destroyed immediately, without rendering them again.
    _impl->running = false;
    opened->running = true;
    if (preserveFraming) opened->orbit = _impl->orbit;
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
    if (_impl->IsVirtualScene(scene)) return true;
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
    _impl->info.asset = asset;
    if (_impl->IsVirtualScene(asset))
    {
        const auto definition = std::find_if(_impl->sceneDefinitions.begin(), _impl->sceneDefinitions.end(),
            [&](const auto &value) { return value.number == asset.number; });
        TAGOnFile tag = {definition->source, 0, kctgBkgd, definition->background};
        auto source = _impl->Resolve(tag);
        if (!source || !_impl->LoadBackground(source, tag.cno)) return Fail(error, ErrorCode::decode_failed, "Cannot load the new scene background.");
        _impl->info.scene = _impl->info.timeline = true;
        _impl->info.firstFrame = _impl->info.lastFrame = _impl->info.currentFrame = 1;
        _impl->originalFirstFrame = _impl->originalLastFrame = 1;
        _impl->virtualScene = _impl->ready = true;
        if (!_impl->SyncActors(error) || !SetFrame(1, error)) return false;
        _impl->RefreshStats();
        return true;
    }
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
            _impl->originalFirstFrame = scene.firstFrame;
            _impl->originalLastFrame = scene.lastFrame;
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
        _impl->originalActors = _impl->actors.size();
        _impl->originalCameraChanges = _impl->cameraChanges;
        if (asset.type == kctgScen && !_impl->SyncActors(error)) return false;
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
bool Preview::SetDefinitions(const std::vector<PreviewSceneDefinition> &scenes,
                             const std::vector<PreviewPropDefinition> &props, Error &error)
{
    error = {};
    const auto oldScenes = _impl->sceneDefinitions;
    const auto oldProps = _impl->propDefinitions;
    _impl->sceneDefinitions = scenes;
    _impl->propDefinitions = props;
    if (_impl->virtualScene && !_impl->IsVirtualScene(_impl->info.asset)) { _impl->ClearAsset(); return true; }
    if (_impl->ready && _impl->info.scene && !_impl->SyncActors(error))
    { _impl->sceneDefinitions = oldScenes; _impl->propDefinitions = oldProps; return false; }
    return true;
}
bool Preview::SetCameraEdits(const std::vector<PreviewCameraEdit> &edits, Error &error)
{
    error = {};
    if (!_impl->ready || !_impl->info.scene) return Fail(error, ErrorCode::invalid_argument, "Select a scene before editing its cameras.");
    for (const auto &edit : edits)
        if (edit.camera >= _impl->info.cameraCount) return Fail(error, ErrorCode::invalid_argument, "The camera index is out of range.");
    auto first = _impl->info.firstFrame;
    auto last = _impl->info.lastFrame;
    _impl->cameraChanges = _impl->originalCameraChanges;
    _impl->info.firstFrame = _impl->originalFirstFrame;
    _impl->info.lastFrame = _impl->originalLastFrame;
    if (!SetSceneRange(first, last, error)) return false;
    for (const auto &edit : edits)
    {
        const auto frame = edit.frame == std::numeric_limits<std::int32_t>::min() ? first : edit.frame;
        auto &changes = _impl->cameraChanges;
        changes.erase(std::remove_if(changes.begin(), changes.end(), [&](const auto &value) { return value.frame == frame; }), changes.end());
        changes.push_back({frame, edit.camera});
    }
    std::stable_sort(_impl->cameraChanges.begin(), _impl->cameraChanges.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
    return SetFrame(std::max(first, std::min(last, _impl->info.currentFrame)), error);
}
bool Preview::SetSceneRange(std::int32_t first, std::int32_t last, Error &error)
{
    error = {};
    if (!_impl->ready || !_impl->info.scene || first == std::numeric_limits<std::int32_t>::min() ||
        first > _impl->originalFirstFrame || last < _impl->originalLastFrame)
        return Fail(error, ErrorCode::invalid_argument, "The scene range must retain the original frames.");
    if (first == _impl->info.firstFrame && last == _impl->info.lastFrame) return true;
    SceneData scene;
    scene.firstFrame = _impl->info.firstFrame;
    MMSceneRuntimeServices services = {};
    services.context = _impl.get();
    services.event_count = [](void *p) -> MMI32 { return static_cast<Impl *>(p)->cameraChanges.size(); };
    services.event = [](void *p, MMI32 index, MMI32 *frame, int *kind) {
        *frame = static_cast<Impl *>(p)->cameraChanges[index].frame;
        *kind = MM_SCENE_CHANGE_CAMERA;
    };
    services.move_camera = [](void *p, MMI32 index, MMI32 frame) {
        auto &changes = static_cast<Impl *>(p)->cameraChanges;
        changes[index].frame = frame;
        std::rotate(changes.begin(), changes.begin() + index, changes.begin() + index + 1);
    };
    if (first != _impl->info.firstFrame) mm_scene_move_first(&scene, first, &services);
    _impl->info.firstFrame = first;
    _impl->info.lastFrame = last;
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
bool Preview::PlacementPoint(float point[3]) const
{
    MMI32 place[3] = {};
    if (_impl->background)
    {
        const auto &p = _impl->background->camera.apos;
        place[0] = p.xrPlace; place[1] = p.yrPlace; place[2] = p.zrPlace;
    }
    MMPreviewMath math = {};
    math.scalar_float = [](MMI32 v) { return BrScalarToFloat(v); };
    return mm_preview_camera_placement(_impl->ready, _impl->background ? place : nullptr, point, &math) != 0;
}
bool Preview::CameraProjection(std::uint32_t camera, PreviewProjection &projection, Error &error)
try
{
    projection = {};
    error = {};
    Internal::ClearDiagnostic();
    if (!mm_preview_camera_admit(_impl->ready, bool(_impl->background), _impl->info.cameraCount, camera))
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
    struct Host { Impl *impl; std::exception_ptr exception; } host = {_impl.get(), {}};
    int ready = _impl->ready;
    MMU32 current = _impl->info.currentCamera;
    const auto result = mm_preview_camera_select(&ready, &current, bool(_impl->background), _impl->info.cameraCount,
        camera, &host, [](void *v, MMU32 index) -> int {
            auto &h = *static_cast<Host *>(v);
            try { return h.impl->background->SetCamera(index) && !Internal::AssertionFailed(); }
            catch (...) { h.exception = std::current_exception(); return -1; }
        });
    _impl->ready = ready != 0;
    _impl->info.currentCamera = current;
    if (host.exception) std::rethrow_exception(host.exception);
    if (result == MM_PREVIEW_CAMERA_INVALID)
        return Fail(error, ErrorCode::invalid_argument, "Background camera is out of range.");
    if (result != MM_PREVIEW_CAMERA_OK)
        return Fail(error, ErrorCode::decode_failed, "Cannot load the background camera and depth image.");
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to load the background camera.");
}
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

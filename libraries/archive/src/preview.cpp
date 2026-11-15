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
#include "mm/preview_live.h"
#include "mm/preview_admission.h"
#include "mm/preview_session.h"
#include "mm/preview_media.h"
#include "mm/scene_data.h"
#include "preview_actor.h"
#include "preview_actor_tree.h"
#include "preview_background.h"
#include "preview_presentation.h"
#undef size
#include "preview_cue_view.h"
ASSERTNAME

namespace MM
{
namespace
{
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
    struct Host { const Archive *archive; PChunkyFile file; } host = {&archive, file};
    MMPreviewBudgetServices services = {};
    services.context = &host;
    services.count = static_cast<MMU32>(archive.Chunks().size());
    services.chunk = [](void *v, MMU32 i, MMPreviewChunk *out) {
        const auto &c = static_cast<Host *>(v)->archive->Chunks()[i];
        *out = {c.id.type, c.id.number, c.storedBytes, c.packed, !c.name.empty()};
    };
    services.header = [](void *v, const MMPreviewChunk *c, unsigned char *bytes) -> int {
        DataBlock block;
        return static_cast<Host *>(v)->file->FFind(c->type, c->number, &block) &&
               block.FReadRgb(bytes, 8, 0, fTrue);
    };
    MMU32 failed = 0;
    const int result = mm_preview_budget(&services, &failed);
    if (result == MM_PREVIEW_BUDGET_HEADER)
        return Fail(error, ErrorCode::decode_failed, "Invalid compressed preview dependency.");
    if (result == MM_PREVIEW_BUDGET_LIMIT)
    {
        error.hasChunk = true;
        error.chunk = archive.Chunks()[failed].id;
        return Fail(error, ErrorCode::resource_limit,
                    "Preview archives are limited to 64 MiB per chunk and 512 MiB of decoded dependencies.");
    }
    return true;
}
} // namespace
struct Preview::Impl
{
    struct RenderHost;
    struct PickHost;
    struct LiveHost;
    struct AdmissionHost;
    struct SessionHost;
    struct SoundHost;
    struct Source
    {
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
    bool ready = false;
    MMPreviewSession session = {};
    MMPreviewOrbit &orbit = session.orbit;
    MMPreviewLive live = {};
    struct ActorView
    {
        MMPreviewLive *live;
        std::size_t size() const { return live->actors.count; }
        Internal::PreviewActor *operator[](std::size_t i) const
        { return static_cast<Internal::PreviewActor *>(mm_preview_live_actor(live, static_cast<MMU32>(i))); }
        struct Iterator
        {
            const ActorView *view;
            std::size_t index;
            Internal::PreviewActor *operator*() const { return (*view)[index]; }
            Iterator &operator++() { ++index; return *this; }
            bool operator!=(const Iterator &other) const { return index != other.index; }
        };
        Iterator begin() const { return {this, 0}; }
        Iterator end() const { return {this, size()}; }
    } actors = {&live};
    Impl() { if (!mm_preview_live_init(&live, nullptr) || !mm_preview_session_init(&session, nullptr)) throw std::bad_alloc(); }
    bool IsVirtualScene(Id scene) const
    { return scene.type == kctgScen && mm_preview_live_scene(&live, scene.number); }
    bool SyncActors(Error &error);
    void CaptureLive()
    {
        auto &s = live.status;
        s.ready = ready; s.scene = info.scene; s.timeline = info.timeline; s.background = bool(background);
        s.scene_number = info.asset.number; s.camera_count = info.cameraCount; s.reported_actors = info.actors;
        s.first = info.firstFrame; s.last = info.lastFrame; s.frame = info.currentFrame;
    }
    void PublishLive()
    {
        const auto &s = live.status;
        ready = s.ready != 0; info.scene = s.scene != 0; info.timeline = s.timeline != 0;
        info.firstFrame = s.first; info.lastFrame = s.last; info.currentFrame = s.frame; info.actors = s.reported_actors;
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
    bool LoadBackground(PChunkyResourceFile source, ChunkNumber number)
    {
        std::unique_ptr<Internal::PreviewBackground> loaded(new Internal::PreviewBackground);
        if (!loaded->Load(source, number, palette))
            return false;
        info.cameraCount = static_cast<std::uint32_t>(loaded->cameras.size());
        background.swap(loaded);
        return true;
    }
    PChunkyResourceFile Resolve(const TAGOnFile &tag);
    void RefreshStats()
    {
        MMPreviewStatsServices s = {};
        s.context = this; s.body = body; s.model = model ? model->Pbmdl() : nullptr;
        s.actors = static_cast<MMU32>(actors.size());
        s.visible = [](void *v, MMU32 i) -> int { return static_cast<Impl *>(v)->actors[i]->Visible(); };
        s.actor_body = [](void *v, MMU32 i) -> void * { return static_cast<Impl *>(v)->actors[i]->body; };
        s.body_parts = [](void *, void *b) -> MMI32 { return static_cast<PBody>(b)->Cbact(); };
        s.part_model = [](void *, void *b, MMI32 i) -> void * { return static_cast<PBody>(b)->PbactPart(i)->model; };
        s.faces = [](void *, void *m) -> MMU32 { return static_cast<br_model *>(m)->nprepared_faces; };
        mm_preview_stats(&s, &info.parts, &info.triangles);
    }
    void ClearAsset(bool preserveFraming = false)
    {
        MMPreviewLiveServices services = {};
        services.actor_release = [](void *, void *a) { delete static_cast<Internal::PreviewActor *>(a); };
        mm_preview_live_clear(&live, &services);
        visibilityClock = 0;
        background.reset();
        if (!preserveFraming)
            mm_preview_orbit_reset(&orbit);
        resolveError.clear();
        ReleasePpo(&body);
        ReleasePpo(&tmpl);
        ReleasePpo(&model);
        info = {};
        ready = false;
    }
    ~Impl();
};
#include "preview_render_host.h"
#include "preview_pick_host.h"
#include "preview_live_host.h"
#include "preview_admission_host.h"
#include "preview_session_host.h"
#include "preview_sound_host.h"
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
    Internal::OrderPreviewCues(presentation.sounds, scene);
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
    Internal::OrderPreviewCues(cues, scene);
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
    Impl::SoundHost host = {_impl.get(), &audio};
    auto services = host.Services();
    const auto input = Internal::PreviewCueCore(cue);
    MMU32 number;
    const int result = mm_preview_sound(&input, _impl->ready, _impl->info.asset.number, &number, &services);
    if (host.exception) std::rethrow_exception(host.exception);
    if (result > 0) return true;
    if (result == MM_PREVIEW_SOUND_NOT_READY)
        return Fail(error, ErrorCode::invalid_argument, "Select a scene before reading its sounds.");
    if (result == MM_PREVIEW_SOUND_MISSING)
        return Fail(error, ErrorCode::decode_failed, _impl->resolveError.c_str());
    return Fail(error, ErrorCode::decode_failed,
                ("Cannot decode sound " + Identity({cue.sound.type, number}) + ".").c_str());
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
    return OpenCore(std::move(archive), resourcesPath, error, preserveFraming, true);
}
bool Preview::OpenEmpty(const std::string &resourcesPath, Error &error)
{
    Archive empty;
    return OpenCore(std::move(empty), resourcesPath, error, false, false);
}
void Preview::ClearScene() { _impl->ClearAsset(); }
bool Preview::OpenCore(Archive &&archive, const std::string &resourcesPath, Error &error, bool preserveFraming, bool requireAsset)
try
{
    Impl::SessionHost host = {this, _impl.get(), &archive, &error};
    auto services = host.Services();
    return host.Result(mm_preview_session_open(&_impl->session, &resourcesPath, requireAsset, preserveFraming, &services));
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
    Impl::SessionHost host = {this, _impl.get(), nullptr, &error};
    auto services = host.Services();
    return host.Result(mm_preview_session_add(&_impl->session, sourceId, &path, &services), true);
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to load content archive.");
}
bool Preview::Select(Id asset, Error &error, bool preserveFraming)
try
{
    error = {};
    Impl::AdmissionHost host = {this, _impl.get(), &error};
    auto state = host.State(asset);
    auto services = host.Services();
    return host.Result(state, mm_preview_admit(&state, preserveFraming, &services));
}
catch (const std::bad_alloc &)
{
    _impl->ClearAsset();
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to load preview asset.");
}
bool Preview::SetPose(std::uint32_t action, std::uint32_t cel, Error &error)
{
    error = {};
    Impl::AdmissionHost host = {this, _impl.get(), &error};
    auto state = host.State(_impl->info.asset);
    auto services = host.Services();
    const int result = mm_preview_admission_pose(&state, action, cel, &services);
    if (host.exception) std::rethrow_exception(host.exception);
    return host.Result(state, result);
}
bool Preview::SetDefinitions(const std::vector<PreviewSceneDefinition> &scenes,
                             const std::vector<PreviewPropDefinition> &props, Error &error)
{
    error = {};
    struct Definitions
    {
        const std::vector<PreviewSceneDefinition> &scenes;
        const std::vector<PreviewPropDefinition> &props;
    } definitions = {scenes, props};
    MMPreviewLiveDefinitions input = {};
    input.context = &definitions;
    input.scene_count = static_cast<MMU32>(scenes.size()); input.prop_count = static_cast<MMU32>(props.size());
    input.scene = [](void *v, MMU32 i, MMPreviewLiveScene *out) {
        const auto &d = static_cast<Definitions *>(v)->scenes[i];
        out->number = d.number; out->background = d.background; out->source = d.source;
    };
    input.prop = [](void *v, MMU32 i, MMPreviewLiveProp *out) {
        const auto &d = static_cast<Definitions *>(v)->props[i];
        out->scene = d.scene; out->number = d.number; out->template_number = d.templateNumber;
        out->source = d.source; out->actor_id = d.actorId; out->first = d.first; out->last = d.last;
        std::copy(d.point, d.point + 3, out->point);
    };
    _impl->CaptureLive();
    Impl::LiveHost host = {this, _impl.get(), &error};
    auto services = host.Services();
    const auto result = mm_preview_live_definitions(&_impl->live, &input, &services);
    _impl->PublishLive();
    host.Rethrow(result);
    return result == MM_PREVIEW_LIVE_OK ||
        Fail(error, ErrorCode::decode_failed, "Cannot create the selected prop and its pose dependencies.");
}
bool Preview::SetCameraEdits(const std::vector<PreviewCameraEdit> &edits, Error &error)
{
    error = {};
    _impl->CaptureLive();
    Impl::LiveHost host = {this, _impl.get(), &error};
    auto services = host.Services();
    MMPreviewLiveCameraEdits input = {};
    input.context = const_cast<std::vector<PreviewCameraEdit> *>(&edits);
    input.count = static_cast<MMU32>(edits.size());
    input.get = [](void *v, MMU32 i, MMPreviewLiveCamera *out) {
        const auto &edit = (*static_cast<const std::vector<PreviewCameraEdit> *>(v))[i];
        out->frame = edit.frame; out->camera = edit.camera;
    };
    const auto result = mm_preview_live_cameras(&_impl->live, &input, &services);
    _impl->PublishLive();
    if (result != MM_PREVIEW_LIVE_SEEK_MEMORY) host.Rethrow(result);
    if (result == MM_PREVIEW_LIVE_SCENE)
        return Fail(error, ErrorCode::invalid_argument, "Select a scene before editing its cameras.");
    if (result == MM_PREVIEW_LIVE_CAMERA_INDEX)
        return Fail(error, ErrorCode::invalid_argument, "The camera index is out of range.");
    if (result == MM_PREVIEW_LIVE_RANGE)
        return Fail(error, ErrorCode::invalid_argument, "The scene range must retain the original frames.");
    return host.FrameResult(result);
}
bool Preview::SetSceneRange(std::int32_t first, std::int32_t last, Error &error)
{
    error = {};
    _impl->CaptureLive();
    const auto result = mm_preview_live_range(&_impl->live, first, last);
    _impl->PublishLive();
    return result == MM_PREVIEW_LIVE_OK ||
        Fail(error, ErrorCode::invalid_argument, "The scene range must retain the original frames.");
}
bool Preview::SetFrame(std::int32_t frame, Error &error)
try
{
    error = {};
    _impl->CaptureLive();
    Impl::LiveHost host = {this, _impl.get(), &error};
    auto services = host.Services();
    const auto result = mm_preview_live_seek(&_impl->live, frame, &services);
    _impl->PublishLive();
    return host.FrameResult(result);
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

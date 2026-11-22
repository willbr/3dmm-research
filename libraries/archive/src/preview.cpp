/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/preview.h"
#include "mm/preview_rasterizer.h"
#include "scene_renderer.h"
#include "scene_pick.h"
#include "zb_capture.h"
#include "preview_buffer_view.h"
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
#include "engine_movie_graph.h"
#include "mm/archive_reader.h"
#include "mm/scene_runtime.h"
#include "mm/preview_pick.h"
#include "mm/preview_camera.h"
#include "mm/preview_live.h"
#include "mm/preview_admission.h"
#include "mm/preview_session.h"
#include "mm/preview_media.h"
#include "mm/scene_data.h"
#include "mm/scene_thumbnail.h"
#include "mm/chunk_copy.h"
#include "preview_actor.h"
#include "preview_background.h"
#include "preview_draft_host.h"
#include "preview_presentation.h"
#include "preview_audio_output.h"
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
} // namespace
struct Preview::Impl
{
    struct RenderHost;
    struct IsolatedActorHost;
    struct LiveHost;
    struct AdmissionHost;
    struct SessionHost;
    struct Source
    {
        Archive archive;
        MMResourceCache *cache = nullptr;
        ~Source()
        {
            if (cache)
                mm_resource_cache_release(cache);
        }
    };
    Archive assets, resources;
    MMResourceCache *cache = nullptr;
    PTemplate tmpl = nullptr;
    PBody body = nullptr;
    PModel model = nullptr;
    PreviewInfo info;
    Color palette[256] = {};
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
    bool StoredPresentation(ChunkNumber scene, PreviewPresentation &, bool soundSheet = false,
                            bool includeCaptions = false) const;
    Id Asset() const
    {
        return {live.status.asset_type, live.status.asset_number};
    }
    void PublishInfo()
    {
        const auto &s = live.status;
        info.asset = Asset();
        info.prop = s.prop != 0;
        info.scene = s.scene != 0;
        info.timeline = s.timeline != 0;
        info.firstFrame = s.first;
        info.lastFrame = s.last;
        info.currentFrame = s.frame;
        info.actors = s.reported_actors;
        info.cameraCount = s.camera_count;
        info.currentCamera = s.current_camera;
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
        if (!mm_template_owner_is_text(value))
            return;
        *mm_template_owner_font(value) = {
            this, [](void *context, const MMTag *font) -> MMFontOwner * {
                auto self = static_cast<Impl *>(context);
                auto source = self->Resolve({font->sid, 0, font->type, font->number});
                return source ? mm_brender_font_fetch(source, font->type, font->number) : nullptr;
            }
        };
    }
    std::unique_ptr<Internal::PreviewBackground> background;
    bool LoadBackground(const MMMovieSessionGraph &source, ChunkNumber number)
    {
        std::unique_ptr<Internal::PreviewBackground> loaded(new Internal::PreviewBackground);
        if (!loaded->Load(source, number, palette))
            return false;
        const auto count = static_cast<MMU32>(loaded->cameras.size());
        background.swap(loaded);
        mm_preview_live_background_loaded(&live, count, 0);
        PublishInfo();
        return true;
    }
    MMResourceCache *Resolve(const TAGOnFile &tag);
    PMaterial_MTRL FetchTextMaterial(int object, bool preset, MMI32 source, MMU32 number)
    {
        if (preset)
        {
            auto file = Resolve({source, 0, kctgMtrl, number});
            return file ? mm_brender_material_fetch(file, kctgMtrl, number) : nullptr;
        }
        if (object < 0 || std::size_t(object) >= actors.size())
            return mm_brender_material_new(-1, -1);
        auto material = actors[object]->TextMaterial();
        if (material)
            mm_material_owner_retain(material);
        return material;
    }
    bool ResolveGraph(const TAGOnFile &tag, MMMovieSessionGraph &graph);
    void DescribeMissingResource(const TAGOnFile &tag);
    void RefreshStats()
    {
        MMPreviewStatsServices s = {};
        s.context = this; s.body = body; s.model = model ? mm_model_owner_resource(model)->handle : nullptr;
        s.actors = static_cast<MMU32>(actors.size());
        s.visible = [](void *v, MMU32 i) -> int { return static_cast<Impl *>(v)->actors[i]->Visible(); };
        s.actor_body = [](void *v, MMU32 i) -> void * { return static_cast<Impl *>(v)->actors[i]->Pbody(); };
        s.body_parts = [](void *, void *b) -> MMI32 { return mm_body_owner_state(static_cast<PBody>(b))->shape.part_count + 2; };
        s.part_model = [](void *, void *b, MMI32 i) -> void * { return mm_body_owner_state(static_cast<PBody>(b))->shape.bindings[i + 2].model_handle; };
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
        mm_body_owner_release(body);
        body = nullptr;
        mm_template_owner_release(tmpl);
        tmpl = nullptr;
        mm_model_owner_release(model);
        model = nullptr;
        info = {};
    }
    ~Impl();
};
#include "preview_render_host.h"
#include "preview_live_host.h"
#include "preview_admission_host.h"
#include "preview_session_host.h"
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
    const auto result = mm_preview_orbit_pan(&v, right, up, &_impl->orbit, _impl->live.status.ready, &services.math);
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
    if (_impl->live.status.ready)
        for (const auto &actor : _impl->actors)
            result.push_back(actor->Snapshot());
    return result;
}
bool Preview::ObjectText(int object, PreviewObjectText &text, Error &error) const
{
    error = {};
    return (_impl->live.status.ready && object >= 0 && std::size_t(object) < _impl->actors.size() && _impl->actors[object]->Text(text)) ||
        Fail(error, ErrorCode::invalid_argument, "Select a 3D text object first.");
}
bool Preview::SoundName(const PreviewSoundCue &cue, std::string &out) const
{
    out.clear();
    TAGOnFile tag = {cue.source, 0, kctgMsnd, cue.sound.number};
    auto cache = _impl->Resolve(tag);
    if (!cache) return false;
    MMU32 number = cue.sound.number;
    if (!cue.source)
    {
        const auto graph = mm_resource_cache_chunks(cache);
        const auto scene = cue.scene.type ? cue.scene.number : _impl->live.status.asset_number;
        mm_chunk_link_number(&graph, kctgScen, scene, cue.child, kctgMsnd, &number);
    }
    String name;
    if (!ReadResourceName(cache, kctgMsnd, number, &name)) return false;
    out.assign(name.Psz(), name.Cch()); return true;
}
bool Preview::ObjectCostumes(int object, std::vector<PreviewCostume> &out, Error &error) const
{
    error = {}; out.clear();
    return (_impl->live.status.ready && object >= 0 && std::size_t(object) < _impl->actors.size() && _impl->actors[object]->Costumes(out)) ||
        Fail(error, ErrorCode::invalid_argument, "Select an actor or prop with costumes.");
}
bool Preview::ObjectActions(int object, std::vector<PreviewAction> &out, Error &error) const
{
    error = {}; out.clear();
    return (_impl->live.status.ready && object >= 0 && std::size_t(object) < _impl->actors.size() &&
            _impl->actors[object]->Actions(out)) ||
        Fail(error, ErrorCode::invalid_argument, "Select an actor, prop or 3D text object with actions.");
}
bool Preview::WriteObjectAnimation(int object, const PreviewAnimationEdit &edit, const std::string &path,
                                   int &lastActorFrame, int &lastEditedFrame, Error &error)
{
    error = {};
    const auto resolveSound = [this](const TAGOnFile &tag, MMMovieSessionGraph &out) {
        return _impl->ResolveGraph(tag, out);
    };
    return (_impl->live.status.ready && object >= 0 && std::size_t(object) < _impl->actors.size() &&
            _impl->actors[object]->WriteAnimation(edit, path, lastActorFrame, lastEditedFrame, resolveSound)) ||
        Fail(error, ErrorCode::decode_failed, "Cannot apply the animation edit. Check the action and path bounds.");
}
bool Preview::WriteObjectCostumes(int object, const std::vector<PreviewCostume> &values, const std::string &path, Error &error)
{
    error = {};
    return (_impl->live.status.ready && object >= 0 && std::size_t(object) < _impl->actors.size() && _impl->actors[object]->WriteCostumes(values, path)) ||
        Fail(error, ErrorCode::decode_failed, "Cannot write this costume change.");
}
bool Preview::SetObjectEventTrack(int object, std::int32_t source, Error &error)
{
    error = {};
    return (_impl->live.status.ready && object >= 0 && std::size_t(object) < _impl->actors.size() && _impl->actors[object]->SetEventTrack(source)) ||
        Fail(error, ErrorCode::decode_failed, "Cannot apply this costume change.");
}
struct Preview::Impl::IsolatedActorHost
{
    Preview &preview;
    Internal::PreviewActor &actor;
    int object;
    const std::vector<PreviewCostume> *values;
    const PreviewView &view;
    PreviewImage &image;
    Error &error;
    ScopedBodyCostume original, selected;
    MMActorBodyPose pose = {};
    std::vector<PreviewCostume> variants;
    std::exception_ptr failure;

    template<class Operation> int Guard(Operation operation)
    {
        try { return operation(); }
        catch (...)
        {
            failure = std::current_exception();
            return 0;
        }
    }
    int ReadBounds(float *corners)
    {
        return Guard([&] {
            PreviewBounds bounds;
            if (!preview.ObjectBounds(object, bounds, error) || !bounds.valid)
                return false;
            std::copy_n(bounds.corners, 24, corners);
            return true;
        });
    }
    int Render(const float *corners, bool crop)
    {
        return Guard([&] {
            PreviewBounds bounds;
            bounds.valid = true;
            std::copy_n(corners, 24, bounds.corners);
            return preview.RenderView(view, image, error, object, &bounds, crop);
        });
    }
    MMPreviewActionView ActionView()
    {
        return {this,
            [](void *p, float *corners) -> int { return static_cast<IsolatedActorHost *>(p)->ReadBounds(corners); },
            [](void *p, const float *corners) -> int { return static_cast<IsolatedActorHost *>(p)->Render(corners, false); }};
    }
    MMPreviewCostumeServices CostumeServices()
    {
        return {this,
            [](void *p, int selected) -> int {
                auto &host = *static_cast<IsolatedActorHost *>(p);
                auto body = host.actor.Pbody();
                return host.Guard([&] {
                    if (selected)
                        return host.selected.FGet(body);
                    if (!host.original.FGet(body))
                        return false;
                    host.pose.orientation = mm_body_owner_state(body)->shape.bindings[0].matrix;
                    return true;
                });
            },
            [](void *p, int selected) {
                auto &host = *static_cast<IsolatedActorHost *>(p);
                auto body = host.actor.Pbody();
                if (selected)
                    host.selected.Set(body);
                else
                {
                    host.original.Set(body);
                    mm_body_locate(body, &host.pose, &mm_kauai_body_renderer()->edit.runtime);
                }
            },
            [](void *p) -> int {
                auto &host = *static_cast<IsolatedActorHost *>(p);
                return host.Guard([&] { return host.actor.RestPose(); });
            },
            [](void *p) -> int {
                auto &host = *static_cast<IsolatedActorHost *>(p);
                return host.Guard([&] { return host.actor.TryCostumes(*host.values); });
            },
            [](void *p, float *corners) -> int { return static_cast<IsolatedActorHost *>(p)->ReadBounds(corners); },
            [](void *p, std::size_t *count) -> int {
                auto &host = *static_cast<IsolatedActorHost *>(p);
                return host.Guard([&] {
                    const auto ok = host.actor.Costumes(host.variants);
                    *count = host.variants.size();
                    return ok;
                });
            },
            [](void *p, std::size_t i) -> MMI32 { return static_cast<IsolatedActorHost *>(p)->variants[i].bodySet; },
            [](void *p, std::size_t i) -> int {
                auto &host = *static_cast<IsolatedActorHost *>(p);
                return host.Guard([&] { return host.actor.TryCostumes({host.variants[i]}); });
            },
            [](void *p, float *range) {
                MMBodyBounds bounds;
                mm_brender_body_bounds(mm_kauai_body_renderer(), static_cast<IsolatedActorHost *>(p)->actor.Pbody(), 1, &bounds);
                range[0] = BrScalarToFloat(bounds.min[1]);
                range[1] = BrScalarToFloat(bounds.max[1]);
            },
            [](void *p, MMI32 set, float *low, float *high) -> int {
                MMBodyBounds bounds;
                if (!mm_brender_body_set_bounds(static_cast<IsolatedActorHost *>(p)->actor.Pbody(), set, &bounds))
                    return 0;
                for (int axis = 0; axis < 3; ++axis)
                {
                    low[axis] = BrScalarToFloat(bounds.min[axis]);
                    high[axis] = BrScalarToFloat(bounds.max[axis]);
                }
                return 1;
            },
            [](void *p, const float *corners, int crop) -> int {
                return static_cast<IsolatedActorHost *>(p)->Render(corners, crop != 0);
            }};
    }
};

bool Preview::RenderObjectCostumes(int object, const std::vector<PreviewCostume> &values,
    const PreviewView &view, PreviewImage &image, Error &error, int bodySet)
{
    error = {}; image = {};
    if (!_impl->live.status.ready || object < 0 || std::size_t(object) >= _impl->actors.size() || !_impl->actors[object]->Visible())
        return Fail(error, ErrorCode::invalid_argument, "Select a visible actor or prop.");
    Impl::IsolatedActorHost host = {*this, *_impl->actors[object], object, &values, view, image, error};
    const auto services = host.CostumeServices();
    const auto result = mm_preview_costume_render(bodySet, &services);
    if (host.failure)
        std::rethrow_exception(host.failure);
    if (result == MM_PREVIEW_COSTUME_PREPARE)
        return Fail(error, ErrorCode::decode_failed, "Cannot prepare costume preview.");
    if (result == MM_PREVIEW_COSTUME_APPLY)
        return Fail(error, ErrorCode::decode_failed, "Cannot preview this costume.");
    if (result == MM_PREVIEW_COSTUME_NO_SET)
        return Fail(error, ErrorCode::invalid_argument, "This object has no such costume body part.");
    return result == MM_PREVIEW_COSTUME_OK;
}
bool Preview::RenderObjectAction(int object, int action, int cel, const PreviewView &view,
                                 PreviewImage &image, Error &error, PreviewBounds *clipBounds)
{
    image = {}; error = {};
    if (!_impl->live.status.ready || object < 0 || std::size_t(object) >= _impl->actors.size() ||
        !_impl->actors[object]->Visible())
        return Fail(error, ErrorCode::invalid_argument, "Select an on-stage object.");
    Impl::IsolatedActorHost host = {*this, *_impl->actors[object], object, nullptr, view, image, error};
    int clipValid = clipBounds && clipBounds->valid;
    const int result = host.actor.RenderAction(action, cel, clipBounds ? clipBounds->corners : nullptr, &clipValid,
                                               host.ActionView());
    if (clipBounds)
        clipBounds->valid = clipValid != 0;
    if (host.failure)
        std::rethrow_exception(host.failure);
    if (result == MM_PREVIEW_ACTION_RANGE)
        return Fail(error, ErrorCode::invalid_argument, "Choose an available action and cel.");
    if (result == MM_PREVIEW_ACTION_MEMORY)
        return Fail(error, ErrorCode::out_of_memory, "Cannot allocate the action preview model.");
    if (result == MM_PREVIEW_ACTION_POSE)
        return Fail(error, ErrorCode::decode_failed, "Cannot display this action cel.");
    if (result == MM_PREVIEW_ACTION_FRAME)
        return Fail(error, ErrorCode::decode_failed, "Cannot frame this action preview.");
    return result == MM_PREVIEW_ACTION_OK;
}
bool Preview::SetObjectTextTemplate(int object, std::int32_t source, std::uint32_t number, Error &error)
{
    error = {};
    return (_impl->live.status.ready && object >= 0 && std::size_t(object) < _impl->actors.size() &&
        _impl->actors[object]->SetTextTemplate(source, number, [this](PTemplate value) { _impl->BindTemplate(value); })) ||
        Fail(error, ErrorCode::decode_failed, "Cannot change this 3D text template.");
}
bool Preview::TextMaterial(int object, bool preset, std::int32_t source, std::uint32_t number, PreviewTextMaterial &out, Error &error)
{
    error = {};
    auto material = _impl->FetchTextMaterial(object, preset, source, number);
    if (!material) return Fail(error, ErrorCode::decode_failed, "Cannot read the text material.");
    MMMaterialAppearance appearance;
    mm_brender_material_read_view(material, &appearance);
    out.textured = appearance.textured != 0;
    out.colour = appearance.colour;
    out.indexBase = appearance.index_base;
    out.indexRange = appearance.index_range;
    out.ambient = appearance.ambient;
    out.diffuse = appearance.diffuse;
    out.specular = appearance.specular;
    out.shininess = appearance.shininess;
    std::copy_n(appearance.mapping, 6, out.mapping);
    mm_material_owner_release(material); return true;
}
bool Preview::WriteTextMaterial(int object, bool preset, std::int32_t source, std::uint32_t number,
    const PreviewTextMaterial &value, const std::string &path, Error &error)
{
    error = {};
    auto material = _impl->FetchTextMaterial(object, preset, source, number);
    if (!material) return Fail(error, ErrorCode::decode_failed, "Cannot copy the text material.");
    std::unique_ptr<MMMaterialOwner, decltype(&mm_material_owner_release)> owner(material, mm_material_owner_release);
    String nativePath;
    if (!PreviewDraftPath(path, nativePath))
        return Fail(error, ErrorCode::invalid_argument, "Invalid text material path.");
    const auto file = OpenPreviewDraft(nativePath);
    if (!file)
        return Fail(error, ErrorCode::decode_failed, "Cannot write the text material.");
    const auto writer = mm_private_archive_writer(file.get());
    MMMaterialAppearance appearance = {value.colour, value.indexBase, value.indexRange, value.ambient, value.diffuse,
                                      value.specular, value.shininess, {}, value.textured};
    std::copy_n(value.mapping, 6, appearance.mapping);
    ChunkNumber chunk = 0;
    if (!mm_brender_material_write_view(material, &writer, kctgMtrl, &chunk, &appearance) || chunk != 0)
        return Fail(error, ErrorCode::decode_failed, "Cannot write the text material.");
    return mm_private_archive_save(file.get(), kctgSoc) || Fail(error, ErrorCode::decode_failed, "Cannot save the text material.");
}
bool Preview::SetObjectTextMaterial(int object, std::int32_t source, std::uint32_t number, Error &error)
{
    return (object >= 0 && std::size_t(object) < _impl->actors.size() && _impl->actors[object]->SetTextMaterial(source, number)) ||
        Fail(error, ErrorCode::decode_failed, "Cannot apply the text material.");
}
bool Preview::PrepareTextPreview(const std::string &path, std::int32_t fontSource, std::uint32_t font, Error &error)
{
    const TAGOnFile tag = {fontSource, 0, kctgTdf, font}; auto source = _impl->Resolve(tag);
    String nativePath;
    if (!source || !PreviewDraftPath(path, nativePath))
        return Fail(error, ErrorCode::decode_failed, "Cannot copy the preview font.");
    const auto file = OpenPreviewDraft(nativePath);
    if (!file)
        return Fail(error, ErrorCode::decode_failed, "Cannot prepare the preview font.");
    MMResourceChunkCopy sourceBinding;
    const auto sourceChunks = mm_resource_cache_copy_source(source, &sourceBinding);
    const int status = mm_private_archive_bind_font(file.get(), &sourceChunks, 0, font);
    if (status == MM_CHUNK_NOT_FOUND)
        Bug("chunk not found");
    if (status != MM_CHUNK_OK)
        return Fail(error, ErrorCode::decode_failed, "Cannot prepare the preview font.");
    return mm_private_archive_save(file.get(), kctgSoc) ||
        Fail(error, ErrorCode::decode_failed, "Cannot link the preview font.");
}
bool Preview::UseTextPreviewMaterial(Error &error)
{
    if (!_impl->body || !_impl->tmpl || !EngineTemplate::FIsTdt(_impl->tmpl)) return false;
    auto material = mm_brender_material_fetch(_impl->cache, kctgMtrl, 0);
    if (!material) return Fail(error, ErrorCode::decode_failed, "Cannot load the preview material.");
    mm_body_set_generic(_impl->body, 0, material, &mm_kauai_body_renderer()->edit.runtime); mm_material_owner_release(material); return true;
}
bool Preview::ReadAnimationTimeline(int object, PreviewAnimationTimeline &timeline, Error &error)
try
{
    timeline = {};
    error = {};
    if (!_impl->live.status.ready || !_impl->live.status.scene || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size())
        return Fail(error, ErrorCode::invalid_argument, "Select a scene object before reading animation events.");
    if (!_impl->actors[object]->ReadTimeline(_impl->live.status.first, _impl->live.status.last, timeline))
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
    if (!_impl->live.status.ready || !_impl->live.status.scene || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size() ||
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
    if (!_impl->live.status.ready || !_impl->live.status.scene || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size() ||
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
    if (!_impl->live.status.ready || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size() ||
        !_impl->actors[object]->Pbody()) return Fail(error, ErrorCode::invalid_argument, "Select a scene object first.");
    _impl->actors[object]->Shape(shape, left, right);
    return true;
}
bool Preview::ObjectTrackPose(int object, PreviewTrackPose &pose) const
{
    return object >= 0 && static_cast<std::size_t>(object) < _impl->actors.size() &&
        _impl->actors[object]->TrackPose(pose);
}
bool Preview::SetObjectShape(int object, const PreviewObjectShape &shape, Error &error)
{
    error = {};
    if (!_impl->live.status.ready || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size() ||
        !_impl->actors[object]->Pbody()) return Fail(error, ErrorCode::invalid_argument, "Select a scene object first.");
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
    MMBodyBounds before, after;
    auto &actor = *_impl->actors[object];
    const bool hold = mm_preview_orbit_follows(&_impl->orbit, object, actor.Visible()) != 0;
    if (hold) mm_brender_body_bounds(mm_kauai_body_renderer(), actor.Pbody(), 1, &before);
    if (!actor.SetShape(shape)) return Fail(error, ErrorCode::decode_failed, "Cannot transform the selected object.");
    if (hold)
    {
        mm_brender_body_bounds(mm_kauai_body_renderer(), actor.Pbody(), 1, &after);
        const MMI32 a[6] = {before.min[0], before.min[1], before.min[2], before.max[0], before.max[1], before.max[2]};
        const MMI32 b[6] = {after.min[0], after.min[1], after.min[2], after.max[0], after.max[1], after.max[2]};
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
    if (!_impl->live.status.ready || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size())
        return Fail(error, ErrorCode::invalid_argument, "Select a scene object before reading its bounds.");
    const auto &actor = *_impl->actors[object];
    if (!actor.Visible() || !actor.Pbody())
        return true;
    MMBodyBounds box;
    mm_brender_body_bounds(mm_kauai_body_renderer(), actor.Pbody(), 0, &box);
    const auto &transform = mm_body_owner_state(actor.Pbody())->shape.bindings[0].matrix;
    MMI32 matrix[12];
    static_assert(sizeof(matrix) == sizeof(transform), "Object bounds matrix layout");
    std::memcpy(matrix, &transform, sizeof(matrix));
    Impl::RenderHost host = {this, _impl.get(), nullptr, &error};
    const auto services = host.Services();
    if (!mm_preview_camera_bounds(box.min, box.max, matrix, bounds.corners, &services.math)) return true;
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
    MMPreviewPickServices services = {};
    services.render = &render;
    mm_brender_preview_pick_services(&services);
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
bool Preview::Impl::StoredPresentation(ChunkNumber scene, PreviewPresentation &out, bool soundSheet,
                                       bool includeCaptions) const
{
    auto reader = assets.Reader();
    if (!reader)
        return false;
    Internal::ClearDiagnostic();
    const auto graph = assets.PlaybackGraph();
    const auto stored = mm_archive_reader_wire_index(reader) ? mm_archive_reader_stored_payload : mm_movie_owner_stored;
    return Internal::ReadPresentation(graph, stored, scene, out, soundSheet, includeCaptions);
}
bool Preview::ReadPresentation(PreviewPresentation &presentation, Error &error)
{
    return ReadPresentation(_impl->Asset(), presentation, error);
}
bool Preview::ReadPresentation(Id scene, PreviewPresentation &presentation, Error &error)
try
{
    error = {};
    presentation = {};
    if (_impl->IsVirtualScene(scene)) return true;
    if (!_impl->live.status.ready || scene.type != kctgScen ||
        !_impl->StoredPresentation(scene.number, presentation))
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
    return ReadSoundCues(_impl->Asset(), cues, error);
}
bool Preview::ReadTimingPresentation(Id scene, PreviewPresentation &presentation, Error &error)
try
{
    error = {};
    presentation = {};
    if (_impl->IsVirtualScene(scene)) return true;
    if (!_impl->live.status.ready || scene.type != kctgScen ||
        !_impl->StoredPresentation(scene.number, presentation, true, true))
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
    if (!_impl->live.status.ready || scene.type != kctgScen ||
        !_impl->StoredPresentation(scene.number, presentation, true))
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
bool Preview::CopyPresentationPalette(unsigned char bgrx[1024]) const
{
    if (!bgrx || !_impl->live.status.ready)
        return false;
    const auto *palette = _impl->background ? _impl->background->palette : _impl->palette;
    static_assert(sizeof(Color) == 4, "Preview palette entries use BGRX layout");
    std::memcpy(bgrx, palette, 256 * sizeof(Color));
    return true;
}
void Preview::OverlayTransitionPalette(unsigned char bgrx[1024]) const
{
    if (bgrx && _impl->background)
        _impl->background->OverlayAuthoredPalette(bgrx);
}
bool Preview::ReadSound(const PreviewSoundCue &cue, PreviewAudio &audio, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    Internal::AudioOutput output{audio, {}};
    const auto audioServices = output.Services();
    struct SoundSource
    {
        Impl &impl;
        Internal::AudioOutput &audio;
    } source{*_impl, output};
    const MMPreviewSoundServices services{
        &source,
        [](void *p, MMI32 sid) { static_cast<SoundSource *>(p)->audio.output.source = sid; },
        [](void *p, MMI32 sid, MMU32 type, MMU32 number, MMMovieSessionGraph *out) {
            auto &host = *static_cast<SoundSource *>(p);
            return host.audio.Guard([&] {
                return host.impl.ResolveGraph({sid, 0, type, number}, *out);
            });
        }};
    const auto input = Internal::PreviewCueCore(cue);
    MMChunkAccess chunks;
    const bool hasMovie = _impl->assets.Reader() != nullptr;
    if (hasMovie)
    {
        const auto graph = _impl->assets.PlaybackGraph();
        graph.access(graph.context, &chunks);
    }
    MMU32 number;
    const int result = mm_preview_sound(&input, _impl->live.status.ready, _impl->live.status.asset_number,
                                        hasMovie ? &chunks : nullptr, &number, &services, &audioServices);
    if (output.exception) std::rethrow_exception(output.exception);
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
    if (!archive.OpenForPreview(archivePath, error))
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
    auto reader = _impl->assets.Reader();
    if (!reader || scene.type != kctgScen)
        return Fail(error, ErrorCode::invalid_argument, "Open a movie before reading scene thumbnails.");
    Internal::ClearDiagnostic();
    static_assert(sizeof(Color) == 4, "Thumbnail palette colour layout");
    const MMSceneThumbnailPreview preview = {
        reader, _impl->resources.Reader(),
        reinterpret_cast<const unsigned char *>(_impl->palette), &image,
        [](void *out, MMU32 width, MMU32 height) -> unsigned char * {
            auto &result = *static_cast<PreviewImage *>(out);
            try
            {
                result.rgba.resize(static_cast<std::size_t>(width) * height * 4);
            }
            catch (const std::bad_alloc &)
            {
                return nullptr;
            }
            result.width = width;
            result.height = height;
            return result.rgba.data();
        }};
    switch (mm_scene_thumbnail_preview(scene.number, &preview))
    {
    case MM_SCENE_THUMBNAIL_OK:
        return true;
    case MM_SCENE_THUMBNAIL_READ:
        return Fail(error, ErrorCode::decode_failed, "Cannot read the saved scene thumbnail.");
    case MM_SCENE_THUMBNAIL_DIMENSIONS:
        return Fail(error, ErrorCode::decode_failed, "Invalid saved scene thumbnail dimensions.");
    case MM_SCENE_THUMBNAIL_MEMORY:
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory for the saved scene thumbnail.");
    default:
        return Fail(error, ErrorCode::decode_failed, "Cannot decode the saved scene thumbnail.");
    }
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
    auto state = host.State(_impl->Asset());
    auto services = host.Services();
    const int result = mm_preview_admission_pose(&state, action, cel, &services);
    if (host.exception) std::rethrow_exception(host.exception);
    return host.Result(state, result);
}
bool Preview::SetDefinitions(const std::vector<PreviewSceneDefinition> &scenes,
                             const std::vector<PreviewPropDefinition> &props, Error &error)
{
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
    return SetLiveDefinitions(input, error);
}
bool Preview::SetLiveDefinitions(const MMPreviewLiveDefinitions &input, Error &error)
{
    error = {};
    Impl::LiveHost host = {this, _impl.get(), &error};
    auto services = host.Services();
    const auto result = mm_preview_live_definitions(&_impl->live, &input, &services);
    _impl->PublishInfo();
    host.Rethrow(result);
    return result == MM_PREVIEW_LIVE_OK ||
        Fail(error, ErrorCode::decode_failed, "Cannot create the selected object and its pose dependencies.");
}
bool Preview::SetBackground(std::int32_t source, std::uint32_t background, Error &error)
{
    error = {};
    Internal::ClearDiagnostic();
    Impl::LiveHost runtime = {this, _impl.get(), &error};
    struct Host
    {
        Impl::LiveHost &runtime;
        MMI32 source;
        MMU32 number;
        std::unique_ptr<Internal::PreviewBackground> candidate;
    } host = {runtime, source, background, nullptr};
    MMPreviewLiveBackgroundServices services = {};
    services.context = &host;
    services.load = [](void *p, MMU32 *count) -> int {
        auto &h = *static_cast<Host *>(p);
        try
        {
            TAGOnFile tag = {h.source, 0, kctgBkgd, h.number};
            auto impl = h.runtime.impl;
            MMMovieSessionGraph graph;
            if (!impl->ResolveGraph(tag, graph))
                return 0;
            h.candidate = std::make_unique<Internal::PreviewBackground>();
            if (!h.candidate->Load(graph, h.number, impl->palette))
                return 0;
            *count = static_cast<MMU32>(h.candidate->cameras.size());
            return 1;
        }
        catch (...)
        {
            return h.runtime.Exception();
        }
    };
    services.select = [](void *p, MMU32 camera) -> int {
        auto &h = *static_cast<Host *>(p);
        try { return h.candidate->SetCamera(camera); }
        catch (...) { return h.runtime.Exception(); }
    };
    services.commit = [](void *p) {
        auto &h = *static_cast<Host *>(p);
        h.runtime.impl->background.swap(h.candidate);
    };
    const int result = mm_preview_live_replace_background(&_impl->live, &services);
    runtime.Rethrow(result);
    if (result == MM_PREVIEW_LIVE_SCENE)
        return Fail(error, ErrorCode::invalid_argument, "Select a scene before replacing its background.");
    if (result == MM_PREVIEW_LIVE_BACKGROUND)
        return Fail(error, ErrorCode::decode_failed, "Cannot load the replacement scene background.");
    if (result == MM_PREVIEW_LIVE_CAMERA)
        return Fail(error, ErrorCode::decode_failed, "Cannot load the replacement camera image.");
    _impl->PublishInfo();
    return true;
}
bool Preview::RenderWithLights(const PreviewView &view, const std::vector<PreviewLight> &lights, PreviewImage &image, Error &error)
{
    if (!_impl->background || lights.size() > 16)
        return Fail(error, ErrorCode::invalid_argument, "A background and at most sixteen lights are required.");
    std::vector<MMBackgroundLight> native;
    for (const auto &l : lights)
    {
        if (!std::isfinite(l.x) || !std::isfinite(l.y) || !std::isfinite(l.z) || !std::isfinite(l.yaw) ||
            !std::isfinite(l.pitch) || !std::isfinite(l.intensity) || std::abs(l.x) > 32700 || std::abs(l.y) > 32700 ||
            std::abs(l.z) > 32700 || std::abs(l.yaw) > 360 || std::abs(l.pitch) > 89 || l.intensity < 0 || l.intensity > 1)
            return Fail(error, ErrorCode::invalid_argument, "Invalid preview light settings.");
        if (l.intensity == 0) continue;
        BMAT34 matrix;
        const auto angle = [](float d) { return static_cast<unsigned short>(static_cast<int>(d * (65536.f / 360))); };
        BrMatrix34RotateX(&matrix, angle(-l.pitch));
        BrMatrix34PostRotateY(&matrix, angle(l.yaw));
        matrix.m[3][0] = BrFloatToScalar(l.x); matrix.m[3][1] = BrFloatToScalar(l.y); matrix.m[3][2] = BrFloatToScalar(l.z);
        MMBackgroundLight light = {};
        std::memcpy(&light.matrix, &matrix, sizeof(matrix));
        light.intensity = BrFloatToScalar(std::min(32700.f, 1.f / l.intensity)); light.type = l.point ? BR_LIGHT_POINT : BR_LIGHT_DIRECT;
        native.push_back(light);
    }
    auto &background = *_impl->background;
    const auto previous = background.lights;
    const auto previousOrbit = _impl->orbit;
    struct Restore
    {
        Internal::PreviewBackground &b; decltype(background.lights) old;
        MMPreviewOrbit &orbit, savedOrbit;
        ~Restore() { b.lights = old; orbit = savedOrbit; }
    } restore{background, previous, _impl->orbit, previousOrbit};
    background.lights.Reset(native.data(), native.size());
    return Render(view, image, error);
}
bool Preview::SetCameraEdits(const std::vector<PreviewCameraEdit> &edits, Error &error)
{
    MMPreviewLiveCameraEdits input = {};
    input.context = const_cast<std::vector<PreviewCameraEdit> *>(&edits);
    input.count = static_cast<MMU32>(edits.size());
    input.get = [](void *v, MMU32 i, MMPreviewLiveCamera *out) {
        const auto &edit = (*static_cast<const std::vector<PreviewCameraEdit> *>(v))[i];
        out->frame = edit.frame; out->camera = edit.camera;
    };
    return SetLiveCameraEdits(input, error);
}
bool Preview::SetLiveCameraEdits(const MMPreviewLiveCameraEdits &input, Error &error)
{
    error = {};
    Error ignored;
    Impl::LiveHost host = {this, _impl.get(), &error};
    Impl::LiveHost recovery = {this, _impl.get(), &ignored};
    auto services = host.Services();
    auto recoveryServices = recovery.Services();
    const auto result = mm_preview_live_cameras(&_impl->live, &input, &services, &recoveryServices);
    _impl->PublishInfo();
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
    const auto result = mm_preview_live_range(&_impl->live, first, last);
    _impl->PublishInfo();
    return result == MM_PREVIEW_LIVE_OK ||
        Fail(error, ErrorCode::invalid_argument, "The scene range must retain the original frames.");
}
bool Preview::SetFrame(std::int32_t frame, Error &error)
try
{
    error = {};
    Impl::LiveHost host = {this, _impl.get(), &error};
    auto services = host.Services();
    const auto result = mm_preview_live_seek(&_impl->live, frame, &services);
    _impl->PublishInfo();
    return host.FrameResult(result);
}
catch (const std::bad_alloc &)
{
    _impl->live.status.ready = false;
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to evaluate scene frame.");
}
bool Preview::PlacementPoint(float point[3]) const
{
    MMI32 place[3] = {};
    if (_impl->background)
    {
        const auto &p = _impl->background->Camera().place;
        place[0] = p.dxr; place[1] = p.dyr; place[2] = p.dzr;
    }
    MMPreviewMath math = {};
    math.scalar_float = [](MMI32 v) { return BrScalarToFloat(v); };
    return mm_preview_camera_placement(_impl->live.status.ready, _impl->background ? place : nullptr, point, &math) != 0;
}
bool Preview::CameraProjection(std::uint32_t camera, PreviewProjection &projection, Error &error)
try
{
    projection = {};
    error = {};
    Internal::ClearDiagnostic();
    const auto &status = _impl->live.status;
    if (!mm_preview_camera_admit(status.ready, status.background, status.camera_count, camera))
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
bool Preview::NextPlacementPoint(float point[3])
{
    return _impl->live.status.ready && _impl->background && _impl->background->NextPlacementPoint(point);
}
void Preview::ReusePlacementPoint()
{
    if (_impl->background) _impl->background->ReusePlacementPoint();
}
bool Preview::SetCamera(std::uint32_t camera, Error &error, bool temporary)
try
{
    error = {};
    Internal::ClearDiagnostic();
    struct Host { Impl *impl; std::exception_ptr exception; bool temporary; } host = {_impl.get(), {}, temporary};
    auto &status = _impl->live.status;
    const auto result = mm_preview_camera_select(
        &status.ready, &status.current_camera, status.background, status.camera_count, camera, &host,
        [](void *v, MMU32 index) -> int {
            auto &h = *static_cast<Host *>(v);
            try { return h.impl->background->SetCamera(index, !h.temporary) && !Internal::AssertionFailed(); }
            catch (...) { h.exception = std::current_exception(); return -1; }
        });
    _impl->PublishInfo();
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
{
    return RenderView(view, image, error, -1, nullptr, false);
}

bool Preview::RenderView(const PreviewView &view, PreviewImage &image, Error &error, int object,
                         const PreviewBounds *bounds, bool crop)
try
{
    image = {};
    error = {};
    Internal::ClearDiagnostic();
    Impl::RenderHost host = {this, _impl.get(), &view, &error};
    if (bounds)
    {
        host.onlyObject = object;
        host.orbitBounds = bounds;
    }
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
    const auto result = bounds ? mm_preview_render_isolated(&v, &_impl->orbit, object, bounds->corners, crop,
                                                           host.image.contentRect, &rendered, &services) :
                                 mm_preview_render(&v, &_impl->orbit, &rendered, &services);
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
    case MM_PREVIEW_ISOLATED_INVALID:
        return Fail(error, ErrorCode::invalid_argument, "Invalid object preview size or zoom.");
    case MM_PREVIEW_ISOLATED_FIT:
        return Fail(error, ErrorCode::invalid_argument, "Cannot fit this object preview.");
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

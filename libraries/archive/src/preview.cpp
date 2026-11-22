/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/preview.h"
#include "mm/preview_rasterizer.h"
#include "scene_renderer.h"
#include "model_renderer.h"
#include "scene_pick.h"
#include "zb_capture.h"
#include "preview_depth_range.h"
#include "diagnostics.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include "soc_core.h"
#include "engine_movie_graph.h"
#include "mm/archive_reader.h"
#include "mm/actor_animation.h"
#include "mm/movie_placement_batch.h"
#include "mm/scene_runtime.h"
#include "mm/preview_pick.h"
#include "mm/preview_camera.h"
#include "mm/preview_live.h"
#include "mm/preview_objects.h"
#include "mm/preview_admission.h"
#include "mm/preview_session.h"
#include "mm/preview_media.h"
#include "mm/scene_data.h"
#include "mm/scene_thumbnail.h"
#include "mm/chunk_copy.h"
#include "preview_actor.h"
#include "preview_draft_host.h"
#include "mm/movie_scene_journal.h"
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
    PreviewInfo info;
    unsigned char palette[1024] = {};
    MMPreviewSession session = {};
    MMPreviewOrbit &orbit = session.orbit;
    MMPreviewLive live = {};
    struct ActorView
    {
        MMPreviewLive *live;
        std::size_t size() const { return live->actors.count; }
        Internal::PreviewActor *operator[](std::size_t i) const
        { return static_cast<Internal::PreviewActor *>(mm_preview_live_actor(live, static_cast<MMU32>(i))); }
    } actors = {&live};
    Impl()
    {
        if (!mm_preview_live_init(&live, nullptr) || !mm_preview_session_init(&session, nullptr))
            throw std::bad_alloc();
        live.actor_view = Internal::PreviewActor::View;
        live.model_faces = mm_brender_model_faces;
    }
    bool IsVirtualScene(Id scene) const
    { return scene.type == kctgScen && mm_preview_live_scene(&live, scene.number); }
    bool SyncActors(Error &error);
    bool ReadScenePresentation(Id, const PreviewPresentationOutput &, Error &);
    bool StoredPresentation(ChunkNumber scene, const PreviewPresentationOutput &, bool soundSheet = false,
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
    struct BackgroundDeleter
    {
        void operator()(MMBackgroundPreviewOwner *owner) const { mm_background_preview_close(owner); }
    };
    using Background = std::unique_ptr<MMBackgroundPreviewOwner, BackgroundDeleter>;
    const unsigned char *Palette() const
    {
        const auto *background = live.assets.background;
        return background ? mm_background_preview_palette(background) : palette;
    }
    Background OpenBackground(const MMMovieSessionGraph &source, ChunkNumber number) const
    {
        MMBackgroundPreviewServices renderer = {};
        mm_brender_preview_background_services(&renderer);
        MMBackgroundPreviewOwner *owner = nullptr;
        if (mm_background_preview_open(&owner, &source, number, palette, &renderer, nullptr) < 0)
            throw std::bad_alloc();
        return Background(owner);
    }
    bool LoadBackground(const MMMovieSessionGraph &source, ChunkNumber number)
    {
        auto loaded = OpenBackground(source, number);
        if (!loaded)
            return false;
        const auto count = mm_background_preview_state(loaded.get())->camera_count;
        loaded.reset(mm_preview_live_exchange_background(&live, loaded.release()));
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
    void PublishGeometry()
    {
        info.parts = live.status.parts;
        info.triangles = live.status.triangles;
    }
    void ClearAsset(bool preserveFraming = false)
    {
        MMPreviewLiveServices services = {};
        services.actor_release = [](void *, void *a) { delete static_cast<Internal::PreviewActor *>(a); };
        mm_preview_live_clear(&live, &services);
        if (!preserveFraming)
            mm_preview_orbit_reset(&orbit);
        resolveError.clear();
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
    Impl::RenderHost host = {_impl.get(), nullptr, &error};
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
    Impl::RenderHost host = {_impl.get(), &view, &error};
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
namespace
{
PreviewObject PublicObject(const MMPreviewActorInfo &info)
{
    PreviewObject object;
    object.asset = {kctgActr, info.number};
    object.templateAsset = {info.template_type, info.template_number};
    object.actorId = info.actor;
    object.sourceId = info.source;
    object.hasLifetime = info.has_lifetime;
    object.firstFrame = info.first;
    object.lastFrame = info.last;
    object.prop = info.prop;
    object.text = info.text;
    object.visible = info.visible;
    object.action = info.action;
    object.cel = info.cel;
    float position[3];
    mm_preview_object_position(&info, position);
    object.x = position[0];
    object.y = position[1];
    object.z = position[2];
    return object;
}
} // namespace
std::vector<PreviewObject> Preview::Objects() const
{
    const auto count = mm_preview_object_count(&_impl->live);
    std::vector<PreviewObject> objects;
    objects.reserve(count);
    for (MMU32 index = 0; index < count; ++index)
    {
        MMPreviewActorInfo info;
        if (!ReadObjectState(static_cast<MMI32>(index), info))
            return {};
        objects.push_back(PublicObject(info));
    }
    return objects;
}
bool Preview::ObjectAt(int index, PreviewObject &out) const
{
    MMPreviewActorInfo info;
    if (!ReadObjectState(index, info))
        return false;
    out = PublicObject(info);
    return true;
}
bool Preview::ReadObjectState(int index, MMPreviewActorInfo &out) const
{
    return mm_preview_object_at(&_impl->live, index, &out) != 0;
}
int Preview::CaptureObjectStates(MMPreviewObjectRows &rows) const
{
    return mm_preview_object_rows_capture(&rows, &_impl->live);
}
int Preview::CollectWorkerPlacements(MMMovieWorkerPlacements &batch, const MMPreviewObjectRows &rows,
                                     const MMMoviePlacementDrafts &drafts, const MMMoviePoseJournal &poses,
                                     std::uint32_t scene) const
{
    return mm_movie_worker_placements_collect(&batch, &rows, &_impl->live, &drafts, &poses, scene);
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
    static_assert(int(PreviewAnimationEdit::Kind::action) == MM_ACTOR_ANIMATION_ACTION &&
                  int(PreviewAnimationEdit::Kind::record) == MM_ACTOR_ANIMATION_RECORD &&
                  int(PreviewAnimationEdit::Kind::tweak) == MM_ACTOR_ANIMATION_TWEAK, "animation edit kinds");
    const MMActorAnimation request{int(edit.kind), edit.freeze, edit.replace, edit.action, edit.cel,
                                   edit.points.data(), edit.points.size(),
                                   [](const void *points, std::size_t index, MMActorAnimationPoint *out) {
                                       const auto &point = static_cast<const PreviewPathPoint *>(points)[index];
                                       *out = {point.x, point.y, point.z};
                                   }};
    return WriteObjectAnimationRequest(object, request, path, lastActorFrame, lastEditedFrame, error);
}
bool Preview::WriteObjectAnimationRequest(int object, const MMActorAnimation &edit, const std::string &path,
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
    int object;
    const PreviewView &view;
    PreviewImage &image;
    Error &error;
    std::exception_ptr failure;

    MMPreviewIsolatedView Services()
    {
        return {this, &mm_kauai_body_renderer()->geometry, mm_brender_body_model_bounds,
            [](void *p, const float *corners, int crop) -> int {
                auto &host = *static_cast<IsolatedActorHost *>(p);
                try
                {
                    return host.preview.RenderView(host.view, host.image, host.error, host.object, corners, crop != 0);
                }
                catch (...)
                {
                    host.failure = std::current_exception();
                    return 0;
                }
            }};
    }
};

bool Preview::RenderObjectCostumes(int object, const std::vector<PreviewCostume> &values,
    const PreviewView &view, PreviewImage &image, Error &error, int bodySet)
{
    error = {}; image = {};
    if (!_impl->live.status.ready || object < 0 || std::size_t(object) >= _impl->actors.size() || !_impl->actors[object]->Visible())
        return Fail(error, ErrorCode::invalid_argument, "Select a visible actor or prop.");
    Impl::IsolatedActorHost host = {*this, object, view, image, error};
    const auto result = _impl->actors[object]->RenderCostumes(values, bodySet, host.Services());
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
    Impl::IsolatedActorHost host = {*this, object, view, image, error};
    int clipValid = clipBounds && clipBounds->valid;
    const int result = _impl->actors[object]->RenderAction(action, cel, clipBounds ? clipBounds->corners : nullptr,
                                                          &clipValid, host.Services());
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
    const auto &assets = _impl->live.assets;
    if (!assets.body || !assets.tmpl || !EngineTemplate::FIsTdt(assets.tmpl)) return false;
    auto material = mm_brender_material_fetch(_impl->cache, kctgMtrl, 0);
    if (!material) return Fail(error, ErrorCode::decode_failed, "Cannot load the preview material.");
    mm_body_set_generic(assets.body, 0, material, &mm_kauai_body_renderer()->edit.runtime);
    mm_material_owner_release(material);
    return true;
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
    if (!_impl->actors[object]->Translate(delta, _impl->orbit, object))
        return Fail(error, ErrorCode::invalid_argument, "Select a visible object and a position within +/-32700 world units.");
    return true;
}
bool Preview::SetObjectOffset(int object, const float offset[3], Error &error)
{
    error = {};
    if (!_impl->live.status.ready || !_impl->live.status.scene || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size() ||
        !offset)
        return Fail(error, ErrorCode::invalid_argument, "The object placement is out of range.");
    if (!_impl->actors[object]->SetOffset(offset, _impl->orbit, object))
        return Fail(error, ErrorCode::invalid_argument, "The object placement is out of range.");
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
    switch (_impl->actors[object]->SetShape(shape, _impl->orbit, object))
    {
    case MM_PREVIEW_SHAPE_OK: return true;
    case MM_PREVIEW_SHAPE_SCALE:
        return Fail(error, ErrorCode::invalid_argument, "Size must be between 1% and 1000%.");
    case MM_PREVIEW_SHAPE_ROTATION:
        return Fail(error, ErrorCode::invalid_argument, "Invalid object rotation.");
    case MM_PREVIEW_SHAPE_REFLECTION:
        return Fail(error, ErrorCode::invalid_argument, "Object rotation cannot mirror or collapse an axis.");
    case MM_PREVIEW_SHAPE_AXES:
        return Fail(error, ErrorCode::invalid_argument, "Rotation must be orthonormal.");
    default:
        return Fail(error, ErrorCode::decode_failed, "Cannot transform the selected object.");
    }
}
namespace
{
struct ActorReplayHost
{
    Preview &preview;
    Error &error;
    void *nameContext;
    PreviewActorNamePublish publishName;
    std::exception_ptr failure;

    template <class Effect> static int Run(void *context, Effect effect)
    {
        auto &host = *static_cast<ActorReplayHost *>(context);
        try { return effect(host); }
        catch (...) { host.failure = std::current_exception(); return 0; }
    }

    static int Name(void *context, MMI32 actor, const char *name)
    {
        return Run(context, [&](ActorReplayHost &h) { return h.publishName(h.nameContext, actor, name); });
    }

    MMMovieActorReplayServices Services()
    {
        return {this,
            [](void *p, MMI32 object, MMI32 source) {
                return Run(p, [&](ActorReplayHost &h) { return h.preview.SetObjectEventTrack(object, source, h.error); });
            },
            [](void *p, MMI32 object, MMI32 source) {
                return Run(p, [&](ActorReplayHost &h) { return h.preview.SetObjectTextTemplate(object, source, 0, h.error); });
            },
            Name,
            [](void *p, MMI32 object, MMI32 source) {
                return Run(p, [&](ActorReplayHost &h) { return h.preview.SetObjectTextMaterial(object, source, 0, h.error); });
            },
            [](void *p, MMI32 object, const float value[3]) {
                return Run(p, [&](ActorReplayHost &h) { return h.preview.SetObjectOffset(object, value, h.error); });
            },
            [](void *p, MMI32 object, const MMPreviewActorShape *shape) {
                return Run(p, [&](ActorReplayHost &h) {
                    PreviewObjectShape value;
                    std::copy_n(shape->rotation, 9, value.rotation);
                    std::copy_n(shape->scale, 3, value.scale);
                    return h.preview.SetObjectShape(object, value, h.error);
                });
            }};
    }

    bool Result(int status)
    {
        if (failure)
            std::rethrow_exception(failure);
        if (status == MM_MOVIE_REPLAY_BAD_PLACEMENTS)
            return Fail(error, ErrorCode::invalid_argument, "Invalid ASM object placement.");
        return status == MM_MOVIE_REPLAY_OK;
    }
};
}

bool Preview::ReplayActorEdits(const MMMovieActorEdits &edits, void *context, PreviewActorNamePublish name, Error &error)
{
    ActorReplayHost host{*this, error, context, name, {}};
    const auto services = host.Services();
    return host.Result(mm_movie_actor_replay(&_impl->live, &edits, &services));
}
bool Preview::ReplayActorNames(const MMMovieTextJournal &texts, void *context, PreviewActorNamePublish name, Error &error)
{
    ActorReplayHost host{*this, error, context, name, {}};
    return host.Result(mm_movie_actor_replay_names(&_impl->live, &texts, &host, ActorReplayHost::Name));
}
bool Preview::ReplayActorPlacements(const MMMovieActorPlacement *placements, std::size_t count, Error &error)
{
    ActorReplayHost host{*this, error, nullptr, nullptr, {}};
    const auto services = host.Services();
    return host.Result(mm_movie_actor_replay_placements(&_impl->live, placements, count, &services));
}

bool Preview::ObjectBounds(int object, PreviewBounds &bounds, Error &error)
{
    bounds = {};
    error = {};
    if (!_impl->live.status.ready || object < 0 || static_cast<std::size_t>(object) >= _impl->actors.size())
        return Fail(error, ErrorCode::invalid_argument, "Select a scene object before reading its bounds.");
    const auto actor = mm_preview_live_actor_view(&_impl->live, object);
    bounds.valid = mm_preview_actor_bounds(actor, bounds.corners, &mm_kauai_body_renderer()->geometry,
                                           [](MMI32 x) { return BrScalarToFloat(x); }) != 0;
    if (bounds.valid)
        // Palette entry 108 is kiclrNormalHilite in the original actor UI.
        bounds.colour = mm_pixel_rgb(_impl->Palette() + 108 * 4);
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
    Impl::RenderHost host = {_impl.get(), nullptr, &error};
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
bool Preview::Impl::StoredPresentation(ChunkNumber scene, const PreviewPresentationOutput &out, bool soundSheet,
                                       bool includeCaptions) const
{
    auto reader = assets.Reader();
    if (!reader)
    {
        out.reset(out.context);
        return false;
    }
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
{
    return _impl->ReadScenePresentation(scene, Internal::PresentationCollector(presentation), error);
}
bool Preview::ReadPresentation(const PreviewPresentationOutput &output, Error &error)
{
    return _impl->ReadScenePresentation(_impl->Asset(), output, error);
}
bool Preview::Impl::ReadScenePresentation(Id scene, const PreviewPresentationOutput &output, Error &error)
try
{
    error = {};
    if (IsVirtualScene(scene))
    {
        output.reset(output.context);
        return true;
    }
    if (live.status.ready && scene.type == kctgScen)
    {
        if (StoredPresentation(scene.number, output))
            return true;
    }
    else
        output.reset(output.context);
    return Fail(error, ErrorCode::decode_failed, "Cannot read the movie's captions, sound cues or pauses.");
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory for movie presentation.");
}
std::vector<PreviewSoundCue> Preview::FrameSounds() const
{
    std::vector<PreviewSoundCue> result;
    VisitFrameSounds(&result, [](void *p, const MMPreviewCue *cue) {
        static_cast<std::vector<PreviewSoundCue> *>(p)->push_back(Internal::PreviewCue(*cue));
        return 1;
    });
    return result;
}
bool Preview::VisitFrameSounds(void *context, int (*visit)(void *, const MMPreviewCue *)) const
{
    struct Visitor
    {
        void *context;
        MMPreviewFrameCueVisitor *visit;
        std::exception_ptr failure;
    } visitor{context, visit, {}};
    const int result = mm_preview_live_visit_cues(&_impl->live, &visitor, [](void *p, const MMPreviewCue *cue) -> int {
        auto &visitor = *static_cast<Visitor *>(p);
        try { return visitor.visit(visitor.context, cue); }
        catch (...) { visitor.failure = std::current_exception(); return 0; }
    });
    if (visitor.failure)
        std::rethrow_exception(visitor.failure);
    return result != 0;
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
        !_impl->StoredPresentation(scene.number, Internal::PresentationCollector(presentation), true, true))
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
        !_impl->StoredPresentation(scene.number, Internal::PresentationCollector(presentation), true))
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
        return 0x01000000 | mm_pixel_rgb(_impl->Palette() + (colour & 255) * 4);
    }
    return colour;
}
bool Preview::CopyPresentationPalette(unsigned char bgrx[1024]) const
{
    if (!bgrx || !_impl->live.status.ready)
        return false;
    std::memcpy(bgrx, _impl->Palette(), sizeof(_impl->palette));
    return true;
}
void Preview::OverlayTransitionPalette(unsigned char bgrx[1024]) const
{
    if (bgrx && _impl->live.assets.background)
        mm_background_preview_overlay_palette(_impl->live.assets.background, bgrx);
}
MMPreviewCue PreviewSoundCue::Native() const
{
    return Internal::PreviewCueCore(*this);
}
bool Preview::ReadSound(const PreviewSoundCue &cue, PreviewAudio &audio, Error &error)
{
    return ReadSoundCue(cue.Native(), audio, error);
}
bool Preview::ReadSoundCue(const MMPreviewCue &cue, PreviewAudio &audio, Error &error)
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
    MMChunkAccess chunks;
    const bool hasMovie = _impl->assets.Reader() != nullptr;
    if (hasMovie)
    {
        const auto graph = _impl->assets.PlaybackGraph();
        graph.access(graph.context, &chunks);
    }
    MMU32 number;
    const int result = mm_preview_sound(&cue, _impl->live.status.ready, _impl->live.status.asset_number,
                                        hasMovie ? &chunks : nullptr, &number, &services, &audioServices);
    if (output.exception) std::rethrow_exception(output.exception);
    if (result > 0) return true;
    if (result == MM_PREVIEW_SOUND_NOT_READY)
        return Fail(error, ErrorCode::invalid_argument, "Select a scene before reading its sounds.");
    if (result == MM_PREVIEW_SOUND_MISSING)
        return Fail(error, ErrorCode::decode_failed, _impl->resolveError.c_str());
    return Fail(error, ErrorCode::decode_failed,
                ("Cannot decode sound " + Identity({cue.type, number}) + ".").c_str());
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
    const MMSceneThumbnailPreview preview = {
        reader, _impl->resources.Reader(),
        _impl->palette, &image,
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
        Impl::Background candidate;
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
            h.candidate = impl->OpenBackground(graph, h.number);
            if (!h.candidate)
                return 0;
            *count = mm_background_preview_state(h.candidate.get())->camera_count;
            return 1;
        }
        catch (...)
        {
            return h.runtime.Exception();
        }
    };
    services.select = [](void *p, MMU32 camera) -> int {
        auto &h = *static_cast<Host *>(p);
        return mm_background_preview_set_camera(h.candidate.get(), camera, 1);
    };
    services.commit = [](void *p) {
        auto &h = *static_cast<Host *>(p);
        h.candidate.reset(mm_preview_live_exchange_background(&h.runtime.impl->live, h.candidate.release()));
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
    MMPreviewLightValues values[MM_BACKGROUND_LIGHT_RIG_LIMIT];
    const auto copied = std::min(lights.size(), std::size_t(MM_BACKGROUND_LIGHT_RIG_LIMIT));
    for (std::size_t i = 0; i < copied; ++i)
    {
        const auto &light = lights[i];
        values[i] = {light.x, light.y, light.z, light.yaw, light.pitch, light.intensity, light.point};
    }
    return RenderWithLightValues(view, values, lights.size(), image, error);
}
bool Preview::RenderWithLightValues(const PreviewView &view, const MMPreviewLightValues *values, std::size_t count,
                                    PreviewImage &image, Error &error)
{
    const LightInput input = {values, count};
    return RenderView(view, image, error, -1, nullptr, false, &input);
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
    const auto *background = _impl->live.assets.background;
    if (background)
    {
        const auto &p = mm_background_preview_state(background)->camera.data.place;
        place[0] = p.dxr; place[1] = p.dyr; place[2] = p.dzr;
    }
    MMPreviewMath math = {};
    math.scalar_float = [](MMI32 v) { return BrScalarToFloat(v); };
    return mm_preview_camera_placement(_impl->live.status.ready, background ? place : nullptr, point, &math) != 0;
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
    MMPreviewProjection native;
    const int result = mm_background_preview_projection(_impl->live.assets.background, camera, &native);
    if (result == MM_PREVIEW_CAMERA_MEMORY)
        throw std::bad_alloc();
    projection = Impl::RenderHost::Projection(native);
    if (result != MM_PREVIEW_CAMERA_OK)
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
    auto *background = _impl->live.assets.background;
    return _impl->live.status.ready && background && mm_background_preview_next_place(background, point);
}
void Preview::ReusePlacementPoint()
{
    if (_impl->live.assets.background)
        mm_background_preview_reuse_placement(_impl->live.assets.background);
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
            try
            {
                const int result = mm_background_preview_set_camera(h.impl->live.assets.background, index, !h.temporary);
                return result <= 0 ? result : !Internal::AssertionFailed();
            }
            catch (...) { h.exception = std::current_exception(); return -1; }
        });
    _impl->PublishInfo();
    if (host.exception) std::rethrow_exception(host.exception);
    if (result == MM_PREVIEW_CAMERA_INVALID)
        return Fail(error, ErrorCode::invalid_argument, "Background camera is out of range.");
    if (result == MM_PREVIEW_CAMERA_MEMORY)
        throw std::bad_alloc();
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
                         const float bounds[24], bool crop, const LightInput *lights)
try
{
    Impl::RenderHost host = {_impl.get(), &view, &error};
    auto services = host.Services();
    MMBackgroundLightRig rig;
    if (lights)
    {
        const auto result = mm_preview_prepare_lights(lights->values, lights->count, _impl->live.assets.background != nullptr,
                                                      &rig, &services);
        if (result == MM_PREVIEW_LIGHT_RIG_INVALID)
            return Fail(error, ErrorCode::invalid_argument, "A background and at most sixteen lights are required.");
        if (result != MM_PREVIEW_OK)
            return Fail(error, ErrorCode::invalid_argument, "Invalid preview light settings.");
        services.light_override = &rig;
    }
    image = {};
    error = {};
    Internal::ClearDiagnostic();
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
    const auto result = bounds ? mm_preview_render_isolated(&v, &_impl->orbit, object, bounds, crop,
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
    case MM_PREVIEW_OBJECT_INDEX:
        return Fail(error, ErrorCode::invalid_argument, "Select a scene object before reading its bounds.");
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
                    _impl->live.assets.background ? "Cannot allocate scene lighting." : "Cannot allocate preview lighting.");
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

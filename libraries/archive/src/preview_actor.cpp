/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "archive_input.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include "preview_actor.h"
#include "engine_actor_timeline.h"
#include "handle_services.h"
#include "mm/preview_live.h"
#include "preview_actor_renderer.h"
#include "preview_draft_host.h"
#undef size
ASSERTNAME
using namespace ActorEvent;

namespace MM
{
namespace Internal
{
// Adapt document-specific resource resolution and diagnostics to the C binding.
struct PreviewActor::Host
{
    PreviewActor *actor;
    const MMMovieSessionGraph *graph = nullptr;
    std::exception_ptr exception;
    int failure = 0;
    MMStorageAllocator trackAllocator = CollectionAllocator();
    MMBRenderPreviewActor binding = {};

    template <class F> int Guard(F f)
    {
        try
        {
            return f();
        }
        catch (const std::bad_alloc &)
        {
            exception = std::current_exception();
            return failure = -1;
        }
        catch (...)
        {
            exception = std::current_exception();
            return failure = -2;
        }
    }
    bool Result(int result)
    {
        if (exception)
            std::rethrow_exception(exception);
        if (result == -1)
            throw std::bad_alloc();
        return result > 0;
    }
    MMBRenderPreviewActor *Binding()
    {
        binding.actor = &actor->core;
        binding.graph = graph;
        binding.track_allocator = &trackAllocator;
        binding.trash_unused = DebugShip(1, 0);
        binding.context = this;
        binding.resolve = [](void *p, MMI32 source, MMU32 type, MMU32 number) -> MMResourceCache * {
            auto &h = *static_cast<Host *>(p);
            MMResourceCache *cache = nullptr;
            h.Guard([&] {
                const auto &resources = h.actor->resources;
                cache = resources.resolve(resources.context, source, type, number);
                return cache != nullptr;
            });
            return cache;
        };
        if (actor->resources.fonts.fetch)
            binding.fonts = &actor->resources.fonts;
        binding.fault = [](void *p) { return static_cast<Host *>(p)->failure; };
        binding.diagnostic = EngineActorTimeline::Diagnostic;
        return &binding;
    }
    MMPreviewActorServices Services()
    {
        MMPreviewActorServices services;
        mm_brender_preview_actor_services(Binding(), &services);
        return services;
    }
};

#include "preview_timeline_host.h"
PreviewActor::~PreviewActor()
{
    Host host = {this};
    auto services = host.Services();
    mm_preview_actor_dispose(&core, &services);
}
bool PreviewActor::Load(const MMMovieSessionGraph &graph, ChunkNumber number,
                        const Resources &provider, MMWide &drawClock)
{
    resources = provider;
    Host host = {this, &graph};
    return host.Result(mm_brender_preview_actor_load(&core, number, &drawClock, host.Binding()));
}
bool PreviewActor::Create(const MMPreviewLiveProp &definition, const Resources &provider, MMWide &drawClock)
{
    resources = provider;
    Host host = {this};
    return host.Result(mm_brender_preview_prop_create(&core, &definition, kboCur, koskCur, &drawClock, host.Binding()));
}

bool PreviewActor::Text(PreviewObjectText &out) const
{
    MMPreviewActorText descriptor;
    if (!mm_preview_actor_text(&core, &descriptor)) return false;
    if (!ArchiveTextUtf8(descriptor.name, out.text)) return false;
    out.shape = descriptor.shape;
    out.fontSource = descriptor.font_source;
    out.font = descriptor.font_number;
    return true;
}
bool PreviewActor::SetTextMaterial(std::int32_t source, std::uint32_t number)
{
    Host host = {this};
    const auto services = host.Services();
    return host.Result(mm_preview_actor_set_material(&core, source, number, &services));
}
bool PreviewActor::SetTextTemplate(std::int32_t source, std::uint32_t number)
{
    Host host = {this};
    const auto services = host.Services();
    return host.Result(mm_preview_actor_replace_template(&core, source, number, &services));
}

static MMPreviewCostumeEdits CostumeEdits(const std::vector<PreviewCostume> &values)
{
    return {&values, values.size(), [](const void *context, std::size_t i, MMPreviewCostumeChoice *out) {
        const auto &value = (*static_cast<const std::vector<PreviewCostume> *>(context))[i];
        *out = {value.bodySet, value.costume};
    }};
}

bool PreviewActor::Costumes(std::vector<PreviewCostume> &out) const
{
    struct Host
    {
        std::vector<PreviewCostume> &out;
        std::exception_ptr exception;
    } host = {out, {}};
    out.clear();
    const auto catalog = mm_brender_preview_actor_costumes();
    const int result = mm_preview_actor_costumes(&core, &catalog, &host,
        [](void *context, const MMPreviewCostumeEntry *entry) -> int {
            auto &h = *static_cast<Host *>(context);
            try
            {
                h.out.push_back({entry->choice.set, entry->choice.id, entry->selected != 0, entry->accessory != 0});
                return 1;
            }
            catch (...)
            {
                h.exception = std::current_exception();
                return 0;
            }
        });
    if (host.exception)
        std::rethrow_exception(host.exception);
    return result != 0;
}
std::string PreviewActor::ActionName(LegacyLong action) const
{
    STZ name;
    const MMStringValue counted = {name, sizeof(achar)};
    std::string text;
    if (action >= 0 && action < mm_preview_actor_action_count(&core) &&
        mm_brender_template_action_name(Ptmpl(), action, &counted) && ArchiveTextUtf8(counted, text) && !text.empty())
        return text;
    return "Action " + std::to_string(action);
}
bool PreviewActor::Actions(std::vector<PreviewAction> &out) const
{
    out.clear();
    if (!Ptmpl()) return false;
    for (LegacyLong i = 0; i < mm_preview_actor_action_count(&core); ++i)
        out.push_back({ActionName(i), static_cast<std::uint32_t>(ActionCelCount(i))});
    return true;
}
LegacyLong PreviewActor::ActionCelCount(LegacyLong action) const
{
    return mm_preview_actor_action_cels(&core, action, mm_brender_template_cel_count);
}
int PreviewActor::RenderAction(LegacyLong action, LegacyLong cel, float *clip, int *clipValid,
                               const MMPreviewIsolatedView &view)
{
    Host host = {this};
    const auto services = host.Services();
    const int result = mm_preview_actor_action_render(&core, action, cel, ActionCelCount(action), clip, clipValid,
                                                      &services, &view);
    if (host.exception)
        std::rethrow_exception(host.exception);
    return result;
}
int PreviewActor::RenderCostumes(const std::vector<PreviewCostume> &values, MMI32 set,
                                 const MMPreviewIsolatedView &view)
{
    Host host = {this};
    const auto services = host.Services();
    const auto catalog = mm_brender_preview_actor_costumes();
    const auto edits = CostumeEdits(values);
    const int result = mm_preview_actor_costume_render(&core, set, &edits, &catalog, &services, &view);
    if (host.exception)
        std::rethrow_exception(host.exception);
    return result;
}
struct PreviewActor::DraftHost : PreviewActor::Host
{
    const std::string &path;
    bool readSound;

    DraftHost(PreviewActor *actor, const std::string &path, bool readSound = false)
        : Host{actor}, path(path), readSound(readSound) {}

    MMPreviewActorEditServices EditServices(const MMPreviewActorServices &playback)
    {
        MMPreviewActorEditServices s = {};
        s.context = this;
        s.playback = &playback;
        s.write = [](void *p, const MMActorTrack *track, int withRoute) -> int {
            auto &h = *static_cast<DraftHost *>(p);
            return h.Guard([&] {
                String nativePath;
                if (!PreviewDraftPath(h.path, nativePath))
                    return false;
                const auto profile = KauaiPrivateArchiveProfile();
                return mm_preview_store_actor_tracks(reinterpret_cast<const MMNativePathChar *>(nativePath.Psz()),
                                                     &profile, kctgSoc, track, withRoute) != 0;
            });
        };
        if (readSound)
            s.sound_graph = [](void *p, const MMTag *tag, MMMovieSessionGraph *out) -> int {
                auto &h = *static_cast<DraftHost *>(p);
                return h.Guard([&] {
                    const auto &resources = h.actor->resources;
                    return resources.graph(resources.context, *tag, *out);
                });
            };
        mm_brender_preview_actor_edit_services(&s);
        return s;
    }
};

bool PreviewActor::WriteCostumes(const std::vector<PreviewCostume> &values, const std::string &path)
{
    DraftHost host(this, path);
    const auto playback = host.Services();
    const auto services = host.EditServices(playback);
    const auto catalog = mm_brender_preview_actor_costumes();
    const auto edits = CostumeEdits(values);
    return host.Result(mm_preview_actor_write_costumes(&core, &edits, &catalog, &services));
}

bool PreviewActor::WriteAnimation(const MMActorAnimation &edit, const std::string &path,
                                  int &lastActorFrame, int &lastEditedFrame)
{
    DraftHost host(this, path, true);
    const auto playback = host.Services();
    const auto services = host.EditServices(playback);
    MMActorAnimationRange range;
    if (!host.Result(mm_preview_actor_write_animation(&core, &edit, &range, &services)))
        return false;
    lastActorFrame = range.actor_last;
    lastEditedFrame = range.edited_last;
    return true;
}
bool PreviewActor::SetEventTrack(std::int32_t source)
{
    Host host = {this};
    const auto services = host.Services();
    return host.Result(mm_brender_preview_actor_replace_events(&core, source, KauaiDecodeBlockHandle, &services));
}
bool PreviewActor::ReadTimeline(LegacyLong first, LegacyLong last, PreviewAnimationTimeline &timeline) const
{
    TimelineHost host = {this, &timeline};
    auto services = host.Services();
    const auto track = Track();
    const int result = mm_preview_actor_timeline(&core, &track, first, last, &services);
    if (host.exception) std::rethrow_exception(host.exception);
    return result > 0;
}
bool PreviewActor::Translate(const float delta[3], MMPreviewOrbit &orbit, MMI32 object)
{
    Host host = {this};
    auto services = host.Services();
    return host.Result(mm_preview_actor_translate(&core, delta, &orbit, object, &services));
}
bool PreviewActor::SetOffset(const float offset[3], MMPreviewOrbit &orbit, MMI32 object)
{
    Host host = {this};
    auto services = host.Services();
    return host.Result(mm_preview_actor_offset(&core, offset, &orbit, object, &services));
}
int PreviewActor::SetShape(const PreviewObjectShape &value, MMPreviewOrbit &orbit, MMI32 object)
{
    MMPreviewActorShape shape;
    static_assert(sizeof(shape) == sizeof(value), "Preview shape layout");
    std::memcpy(&shape, &value, sizeof(shape));
    Host host = {this};
    auto services = host.Services();
    const auto result = mm_preview_actor_set_shape(&core, &shape, &orbit, object,
                                                   &mm_kauai_body_renderer()->geometry, &services);
    return host.Result(result) ? result : MM_PREVIEW_SHAPE_FAILED;
}
void PreviewActor::Shape(PreviewObjectShape &value, float left[9], float right[9]) const
{
    MMPreviewActorShape shape;
    Host host = {const_cast<PreviewActor *>(this)};
    auto services = host.Services();
    mm_preview_actor_shape(&core, &shape, left, right, &services);
    std::memcpy(&value, &shape, sizeof(shape));
}
bool PreviewActor::TrackPose(PreviewTrackPose &pose) const
{
    MMPreviewTrackPose nativePose;
    static_assert(sizeof(nativePose) == sizeof(pose), "Preview track pose layout");
    std::memcpy(&nativePose, &pose, sizeof(nativePose));
    Host host = {const_cast<PreviewActor *>(this)};
    const auto services = host.Services();
    const auto result = mm_preview_actor_track_pose(&core, &nativePose, &services);
    std::memcpy(&pose, &nativePose, sizeof(pose));
    return host.Result(result);
}
bool PreviewActor::Seek(LegacyLong frame)
{
    Host host = {this};
    auto services = host.Services();
    return host.Result(mm_preview_actor_seek(&core, frame, &services));
}

} // namespace Internal
} // namespace MM

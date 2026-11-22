/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "platform_text.h"
#include <codecvt>
#include <algorithm>
#include <cmath>
#include <locale>
#include <map>
#include <sstream>
#include "preview_actor.h"
#include "engine_actor_motion.h"
#include "engine_actor_timeline.h"
#include "handle_services.h"
#include "mm/preview_actor_edit.h"
#include "mm/preview_metadata.h"
#include "preview_draft_host.h"
#undef size
ASSERTNAME
using namespace ActorEvent;

namespace MM
{
namespace Internal
{
#include "preview_actor_host.h"
#include "preview_timeline_host.h"
PreviewActor::~PreviewActor()
{
    Host host = {this};
    auto services = host.Services();
    mm_preview_actor_dispose(&core, &services);
}
bool PreviewActor::Load(const MMMovieSessionGraph &graph, ChunkNumber number,
                        std::function<MMResourceCache *(const TAGOnFile &)> resolver,
                        std::function<void(PTemplate)> bindTemplate, MMWide &drawClock)
{
    core.draw_clock = &drawClock;
    resolve = resolver;
    Host host = {this, &graph, &bindTemplate};
    auto services = host.Services();
    MMChunkAccess chunks;
    graph.access(graph.context, &chunks);
    return host.Result(mm_preview_actor_load(&core, number, &chunks, 0, &services));
}
bool PreviewActor::Create(const PreviewPropDefinition &definition,
                          std::function<MMResourceCache *(const TAGOnFile &)> resolver,
                          std::function<void(PTemplate)> bindTemplate, MMWide &drawClock)
{
    resolve = std::move(resolver);
    core.draw_clock = &drawClock;
    MMPreviewActorDefinition d = {};
    d.number = definition.number;
    d.header.order = kboCur;
    d.header.system = koskCur;
    d.header.actor_id = definition.actorId;
    d.header.first_frame = definition.first;
    d.header.last_frame = definition.last;
    d.header.source_id = definition.source;
    d.header.template_type = kctgTmpl;
    d.header.template_number = definition.templateNumber;
    std::copy(definition.point, definition.point + 3, d.point);
    Host host = {this, nullptr, &bindTemplate};
    auto services = host.Services();
    return host.Result(mm_preview_actor_create(&core, &d, &services));
}

bool PreviewActor::Text(PreviewObjectText &out) const
{
    MMPreviewActorText descriptor;
    if (!mm_preview_actor_text(&core, &descriptor)) return false;
    String text;
    text.SetSt(static_cast<PPascalString>(descriptor.name.buffer));
    const auto count = KauaiText::ToUtf16(text.Psz(), text.Cch(), KauaiText::Encoding::WindowsAnsi, nullptr, 0);
    if (text.Cch() && !count) return false;
    std::vector<std::uint16_t> units(count);
    if (count && KauaiText::ToUtf16(text.Psz(), text.Cch(), KauaiText::Encoding::WindowsAnsi,
        units.data(), count) != count) return false;
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    out.text = convert.to_bytes(std::u16string(units.begin(), units.end()));
    out.shape = descriptor.shape;
    out.fontSource = descriptor.font_source;
    out.font = descriptor.font_number;
    return true;
}
PMaterial_MTRL PreviewActor::TextMaterial() const
{
    if (!Ptmpl() || !EngineTemplate::FIsTdt(Ptmpl()) || !Pbody()) return nullptr;
    int custom;
    auto material = mm_brender_body_material(mm_kauai_body_renderer(), Pbody(), 0, &custom);
    return custom ? nullptr : material;
}
bool PreviewActor::SetTextMaterial(std::int32_t source, std::uint32_t number)
{
    Host host = {this};
    const auto services = host.Services();
    return host.Result(mm_preview_actor_set_material(&core, source, number, &services));
}
bool PreviewActor::SetTextTemplate(std::int32_t source, std::uint32_t number, std::function<void(PTemplate)> bindTemplate)
{
    Host host = {this, nullptr, &bindTemplate};
    const auto services = host.Services();
    return host.Result(mm_preview_actor_replace_template(&core, source, number, &services));
}

static MMPreviewCostumeCatalog CostumeCatalog()
{
    return {&mm_kauai_body_renderer()->edit.runtime, mm_brender_template_costume_count,
            mm_brender_template_costume_id, mm_brender_template_costume, mm_brender_template_is_accessory};
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
    const auto catalog = CostumeCatalog();
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
    String name;
    const MMStringValue counted = {name.Pst(), sizeof(achar)};
    if (action >= 0 && action < mm_preview_actor_action_count(&core) &&
        mm_brender_template_action_name(Ptmpl(), action, &counted) && name.Cch())
    {
        const auto encoding = sizeof(achar) == 1 ? KauaiText::Encoding::WindowsAnsi : KauaiText::Encoding::Utf16LE;
        const auto count = KauaiText::ToUtf16(name.Psz(), name.Cch() * sizeof(achar), encoding, nullptr, 0);
        std::u16string text(count, 0);
        if (count && KauaiText::ToUtf16(name.Psz(), name.Cch() * sizeof(achar), encoding,
                                        reinterpret_cast<std::uint16_t *>(&text[0]), count))
            return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().to_bytes(text);
    }
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
    const auto catalog = CostumeCatalog();
    const auto edits = CostumeEdits(values);
    const int result = mm_preview_actor_costume_render(&core, set, &edits, &catalog, &services, &view);
    if (host.exception)
        std::rethrow_exception(host.exception);
    return result;
}
struct PreviewActor::DraftHost : PreviewActor::Host
{
    const std::string &path;
    const SoundGraphResolver *resolveSound;

    DraftHost(PreviewActor *actor, const std::string &path, const SoundGraphResolver *resolveSound = nullptr)
        : Host{actor}, path(path), resolveSound(resolveSound) {}

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
        if (resolveSound)
            s.sound_graph = [](void *p, const MMTag *tag, MMMovieSessionGraph *out) -> int {
                auto &h = *static_cast<DraftHost *>(p);
                return h.Guard([&] {
                    return (*h.resolveSound)({tag->sid, 0, tag->type, tag->number}, *out);
                });
            };
        s.template_sound = mm_brender_template_sound;
        s.distance = [](const MMActorPoint *point) -> MMI32 { return BR_LENGTH3(point->dxr, point->dyr, point->dzr); };
        return s;
    }
};

bool PreviewActor::WriteCostumes(const std::vector<PreviewCostume> &values, const std::string &path)
{
    DraftHost host(this, path);
    const auto playback = host.Services();
    const auto services = host.EditServices(playback);
    const auto catalog = CostumeCatalog();
    const auto edits = CostumeEdits(values);
    return host.Result(mm_preview_actor_write_costumes(&core, &edits, &catalog, &services));
}

bool PreviewActor::WriteAnimation(const MMActorAnimation &edit, const std::string &path,
                                  int &lastActorFrame, int &lastEditedFrame, const SoundGraphResolver &resolveSound)
{
    DraftHost host(this, path, &resolveSound);
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
    const MMPreviewEventReplacement edit = {
        &host,
        [](void *p, MMI32 source, MMU32 type, MMU32 number, MMResourceCache **out) -> int {
            auto &input = *static_cast<Host *>(p);
            return input.Guard([&] {
                const TAGOnFile tag = {source, 0, type, number};
                auto cache = input.actor->resolve(tag);
                if (!cache)
                    return false;
                *out = cache;
                return true;
            });
        },
        nullptr,
        KauaiDecodeBlockHandle};
    return host.Result(mm_preview_actor_replace_track(&core, source, &edit, &services));
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

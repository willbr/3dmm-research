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
#include "engine_actor_effect.h"
#include "mm/actor_costume_edit.h"
#include "mm/actor_animation.h"
#include "mm/sound_resource.h"
#include "preview_draft_host.h"
#undef size
#include "preview_cue_view.h"
ASSERTNAME
using namespace ActorEvent;

namespace MM
{
namespace Internal
{
#include "preview_actor_host.h"
#include "preview_costume_host.h"
#include "preview_animation_host.h"
#include "preview_timeline_host.h"
PreviewActor::~PreviewActor()
{
    Host host = {this};
    auto services = host.Services();
    mm_preview_actor_dispose(&core, &services);
    mm_actor_sounds_dispose(&motionSounds);
}
bool PreviewActor::Load(const MMMovieSessionGraph &graph, ChunkNumber number,
                        std::function<MMResourceCache *(const TAGOnFile &)> resolver,
                        std::function<void(PTemplate)> bindTemplate, MMWide &drawClock)
{
    core.draw_clock = &drawClock;
    this->number = number;
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
    number = definition.number;
    MMPreviewActorDefinition d = {};
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

PreviewObject PreviewActor::Snapshot() const
{
    PreviewObject object;
    object.asset = {kctgActr, number};
    object.templateAsset = {core.header.template_type, core.header.template_number};
    object.actorId = core.header.actor_id;
    object.sourceId = core.header.source_id;
    object.hasLifetime = !core.inactive;
    object.firstFrame = core.inactive ? 0 : core.header.first_frame;
    object.lastFrame = core.inactive ? 0 : core.header.last_frame;
    object.prop = Ptmpl() && EngineTemplate::FIsProp(Ptmpl());
    object.text = Ptmpl() && EngineTemplate::FIsTdt(Ptmpl());
    object.visible = !core.inactive && core.state.onStage;
    object.action = core.state.action;
    object.cel = core.state.cel;
    object.x = BrScalarToFloat(BrsAdd(core.state.routeTranslation.dxr, core.state.position.dxr));
    object.y = BrScalarToFloat(BrsAdd(core.state.routeTranslation.dyr, core.state.position.dyr));
    object.z = BrScalarToFloat(BrsAdd(core.state.routeTranslation.dzr, core.state.position.dzr));
    return object;
}
bool PreviewActor::Text(PreviewObjectText &out) const
{
    if (!Ptmpl() || !EngineTemplate::FIsTdt(Ptmpl())) return false;
    const auto &descriptor = *mm_template_owner_descriptor(Ptmpl());
    String text;
    text.SetSt(static_cast<PPascalString>(mm_template_owner_name(Ptmpl()).buffer));
    const auto count = KauaiText::ToUtf16(text.Psz(), text.Cch(), KauaiText::Encoding::WindowsAnsi, nullptr, 0);
    if (text.Cch() && !count) return false;
    std::vector<std::uint16_t> units(count);
    if (count && KauaiText::ToUtf16(text.Psz(), text.Cch(), KauaiText::Encoding::WindowsAnsi,
        units.data(), count) != count) return false;
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    out.text = convert.to_bytes(std::u16string(units.begin(), units.end()));
    out.shape = descriptor.shape;
    out.fontSource = descriptor.font.sid;
    out.font = descriptor.font.number;
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

bool PreviewActor::Costumes(std::vector<PreviewCostume> &out) const
{
    out.clear();
    if (!Ptmpl() || !Pbody() || EngineTemplate::FIsTdt(Ptmpl())) return false;
    for (int set = 0; set < mm_body_owner_state(Pbody())->shape.set_count; ++set)
    {
        int custom;
        const auto current = mm_brender_body_material(mm_kauai_body_renderer(), Pbody(), set, &custom);
        for (int i = 0; i < mm_brender_template_costume_count(Ptmpl(), set); ++i)
        {
            const auto id = mm_brender_template_costume_id(Ptmpl(), set, i);
            auto costume = mm_brender_template_costume(Ptmpl(), id);
            if (!costume) return false;
            const bool selected = custom && current == costume;
            mm_material_owner_release(costume);
            out.push_back({set, int(id), selected, mm_brender_template_is_accessory(Ptmpl(), set) != 0});
        }
    }
    return true;
}
std::string PreviewActor::ActionName(LegacyLong action) const
{
    String name;
    const MMStringValue counted = {name.Pst(), sizeof(achar)};
    if (action >= 0 && action < EngineTemplate::Cactn(Ptmpl()) &&
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
    for (LegacyLong i = 0; i < EngineTemplate::Cactn(Ptmpl()); ++i)
        out.push_back({ActionName(i), static_cast<std::uint32_t>(ActionCelCount(i))});
    return true;
}
LegacyLong PreviewActor::ActionCelCount(LegacyLong action) const
{
    LegacyLong count = 0;
    if (Ptmpl() && action >= 0 && action < EngineTemplate::Cactn(Ptmpl()) && mm_brender_template_cel_count(Ptmpl(), action, &count))
        return std::max<LegacyLong>(0, count);
    return 0;
}
int PreviewActor::RenderAction(LegacyLong action, LegacyLong cel, float *clip, int *clipValid,
                               const MMPreviewActionView &view)
{
    Host host = {this};
    const auto services = host.Services();
    const int result = mm_preview_actor_action_render(&core, action, cel, ActionCelCount(action), clip, clipValid,
                                                      &services, &view);
    if (host.exception)
        std::rethrow_exception(host.exception);
    return result;
}
bool PreviewActor::RestPose()
{
    Host host = {this};
    const auto services = host.Services();
    return host.Result(mm_preview_actor_rest_pose(&core, &services));
}
bool PreviewActor::TryCostumes(const std::vector<PreviewCostume> &values)
{
    std::vector<PreviewCostume> available;
    if (!Costumes(available)) return false;
    for (const auto &value : values)
    {
        if (std::none_of(available.begin(), available.end(), [&](const PreviewCostume &c) {
            return c.bodySet == value.bodySet && c.costume == value.costume; })) return false;
        auto costume = mm_brender_template_costume(Ptmpl(), value.costume);
        if (!costume) return false;
        mm_body_set_custom(Pbody(), costume, &mm_kauai_body_renderer()->edit.runtime);
        mm_material_owner_release(costume);
    }
    return true;
}
struct PreviewActor::TrackDraft
{
    PreviewActor &actor;
    MMPreviewActorDraft draft = {};
    std::vector<PreviewSoundCue> cues;
    explicit TrackDraft(PreviewActor &a) : actor(a) {}
    bool Begin()
    {
        Host host = {&actor};
        const auto services = host.Services();
        if (!host.Result(mm_preview_actor_begin_draft(&actor.core, &actor.motionSounds, &draft, &services)))
            return false;
        actor.sounds.swap(cues);
        return true;
    }
    ~TrackDraft()
    {
        if (!draft.active)
            return;
        Host host = {&actor};
        const auto services = host.Services();
        mm_preview_actor_end_draft(&actor.core, &actor.motionSounds, &draft, &services);
        actor.sounds.swap(cues);
    }
    bool Write(const std::string &path, bool withRoute) const
    {
        String nativePath;
        if (!PreviewDraftPath(path, nativePath))
            return false;
        const auto profile = KauaiPrivateArchiveProfile();
        const auto track = actor.Track();
        return mm_preview_store_actor_tracks(reinterpret_cast<const MMNativePathChar *>(nativePath.Psz()),
                                             &profile, kctgSoc, &track, withRoute) != 0;
    }
};
bool PreviewActor::WriteCostumes(const std::vector<PreviewCostume> &values, const std::string &path)
{
    if (!Visible() || !Ptmpl() || EngineTemplate::FIsTdt(Ptmpl())) return false;
    TrackDraft draft(*this);
    if (!draft.Begin()) return false;
    std::vector<PreviewCostume> available;
    if (!Costumes(available)) return false;
    CostumeHost host(this); const auto services = host.Services();
    const auto motion = EngineActorMotion::Services(); const MMTag tag = {};
    for (const auto &value : values)
    {
        auto found = std::find_if(available.begin(), available.end(), [&](const PreviewCostume &c) {
            return c.bodySet == value.bodySet && c.costume == value.costume; });
        if (found == available.end()) return false;
        if (!host.Result(mm_actor_edit_set_costume(&core.state, &motionSounds, Track().events, core.header.actor_id,
            value.bodySet, &tag, value.costume, 1, &motion, &services))) return false;
    }
    return draft.Write(path, false);
}
bool PreviewActor::WriteAnimation(const PreviewAnimationEdit &edit, const std::string &path,
                                  int &lastActorFrame, int &lastEditedFrame, const SoundGraphResolver &resolveSound)
{
    if (!Visible() || !Ptmpl() || edit.points.size() > MM_ACTOR_ANIMATION_MAX_POINTS) return false;
    static_assert(int(PreviewAnimationEdit::Kind::action) == MM_ACTOR_ANIMATION_ACTION &&
                  int(PreviewAnimationEdit::Kind::record) == MM_ACTOR_ANIMATION_RECORD &&
                  int(PreviewAnimationEdit::Kind::tweak) == MM_ACTOR_ANIMATION_TWEAK, "animation edit kinds");
    std::vector<MMActorAnimationPoint> points;
    points.reserve(edit.points.size());
    for (const auto &point : edit.points)
        points.push_back({point.x, point.y, point.z});
    const MMActorAnimation request{int(edit.kind), edit.freeze, edit.replace, edit.action, edit.cel,
                                   points.data(), points.size()};
    TrackDraft draft(*this);
    if (!draft.Begin()) return false;
    AnimationHost host(this, resolveSound);
    const auto playback = host.Host::Services();
    const auto services = host.Record(playback);
    const auto actionCount = edit.kind == PreviewAnimationEdit::Kind::action ? EngineTemplate::Cactn(Ptmpl()) : 0;
    MMActorAnimationRange range;
    if (!host.Result(mm_actor_animation_edit(&core, &motionSounds, &host.record, &request, actionCount,
                                             &services, &range)) || !draft.Write(path, true))
        return false;
    lastActorFrame = range.actor_last;
    lastEditedFrame = range.edited_last;
    return true;
}
bool PreviewActor::SetEventTrack(std::int32_t source)
{
    struct EventSource
    {
        Host host;
        ScopedBodyCostume costume;
    } sourceTrack = {{this}};
    const auto services = sourceTrack.host.Services();
    const MMPreviewEventReplacement edit = {
        &sourceTrack,
        [](void *p, MMI32 source, MMU32 type, MMU32 number, MMResourceCache **out) -> int {
            auto &input = *static_cast<EventSource *>(p);
            return input.host.Guard([&] {
                const TAGOnFile tag = {source, 0, type, number};
                auto cache = input.host.actor->resolve(tag);
                if (!cache)
                    return false;
                *out = cache;
                return true;
            });
        },
        nullptr,
        KauaiDecodeBlockHandle,
        [](void *p) -> int {
            auto &input = *static_cast<EventSource *>(p);
            return input.host.Guard([&] { return input.costume.FGet(input.host.actor->Pbody()); });
        },
        [](void *p) {
            auto &input = *static_cast<EventSource *>(p);
            input.host.Guard([&] {
                input.costume.Set(input.host.actor->Pbody());
                return true;
            });
        }};
    return sourceTrack.host.Result(mm_preview_actor_replace_track(&core, source, &edit, &services));
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
bool PreviewActor::ApplyEvent(MMActorState &value, const MMActorTrack &events, LegacyLong index, bool metadataOnly,
                              MMActorSounds *motion, std::vector<PreviewSoundCue> *cues) const
{
    struct Host
    {
        const PreviewActor *actor;
        MMPreviewActor *live;
        MMActorEventTable *events;
        MMActorSounds *motion;
        std::vector<PreviewSoundCue> *cues;
        std::exception_ptr failure;
    } host = {this, metadataOnly ? nullptr : const_cast<MMPreviewActor *>(&core), events.events, motion, cues};
    EngineActorEffectHost body = {Ptmpl(), Pbody(), motion, &host};
    auto effects = body.Services();
    effects.action_count = [](void *p) -> MMI32 { return EngineTemplate::Cactn(EngineActorEffectHost::From(p).tmpl); };
    effects.visibility = [](void *p, int shown) {
        auto *live = static_cast<Host *>(EngineActorEffectHost::From(p).owner)->live;
        if (shown && live)
            mm_preview_actor_shown(live);
    };
    effects.fetch_material = [](void *p, const MMTag *tag) -> void * {
        const auto actor = static_cast<Host *>(EngineActorEffectHost::From(p).owner)->actor;
        const auto source = actor->resolve({tag->sid, 0, tag->type, tag->number});
        return source ? mm_brender_material_fetch(source, tag->type, tag->number) : nullptr;
    };
    effects.enqueue_sound = [](void *p, MMI32 index) -> int {
        auto &host = *static_cast<Host *>(EngineActorEffectHost::From(p).owner);
        MMActorSoundServices services = {};
        services.context = &host;
        services.cues_only = 1;
        services.cue = [](void *context, const MMActorSound *value) -> int {
            auto &host = *static_cast<Host *>(context);
            try
            {
                if (host.actor->core.sound_display)
                    host.cues->push_back(host.actor->SoundCue(*value));
                return 1;
            }
            catch (...)
            {
                host.failure = std::current_exception();
                return 0;
            }
        };
        return mm_actor_sound_event(host.motion, host.events, index, &services);
    };
    const MMActorPoint origin = {MMI32(core.header.translation[0]), MMI32(core.header.translation[1]),
                                 MMI32(core.header.translation[2])};
    const auto tables = EngineActorMotion::Services();
    const bool ok = mm_actor_effect_apply(&value, events.route, events.events, index, &origin, body.body != nullptr,
                                          metadataOnly, &tables, &effects) != 0;
    if (host.failure)
        std::rethrow_exception(host.failure);
    return ok;
}
PreviewSoundCue PreviewActor::SoundCue(const MMActorSound &sound) const
{
    MMPreviewCue cue;
    mm_preview_actor_cue(&cue, &sound, core.state.frame, core.header.actor_id);
    return PreviewCue(cue);
}
bool PreviewActor::Translate(const float delta[3])
{
    Host host = {this};
    auto services = host.Services();
    return host.Result(mm_preview_actor_translate(&core, delta, &services));
}
bool PreviewActor::SetOffset(const float offset[3])
{
    Host host = {this};
    auto services = host.Services();
    return host.Result(mm_preview_actor_offset(&core, offset, &services));
}
bool PreviewActor::SetShape(const PreviewObjectShape &value)
{
    MMPreviewActorShape shape;
    static_assert(sizeof(shape) == sizeof(value), "Preview shape layout");
    std::memcpy(&shape, &value, sizeof(shape));
    Host host = {this};
    auto services = host.Services();
    return host.Result(mm_preview_actor_set_shape(&core, &shape, &services));
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
    if (!Visible() || !Pbody() || !Ptmpl()) return false;
    const auto &state = core.state;
    pose.node = state.location.irpt;
    pose.offset = state.location.dwrOffset;
    pose.routeFrame = state.location.dnfrm;
    const auto &position = state.position;
    const auto &translation = state.routeTranslation;
    pose.position[0] = position.dxr; pose.position[1] = position.dyr; pose.position[2] = position.dzr;
    pose.translation[0] = translation.dxr; pose.translation[1] = translation.dyr; pose.translation[2] = translation.dzr;
    BRA angles[3];
    mm_brender_template_rest(Ptmpl(), angles, angles + 1, angles + 2);
    MMActorMatrix rest;
    mm_matrix_identity(&rest);
    for (int axis = 0; axis < 3; ++axis) mm_matrix_rotate(&rest, axis, angles[axis], 0);
    const BRS stretch[] = {state.transform.aevpull.rScaleX, state.transform.aevpull.rScaleY, state.transform.aevpull.rScaleZ};
    const auto &root = mm_body_owner_state(Pbody())->shape.bindings[0].matrix;
    // Recover the world rotation after removing rest orientation and authored size.
    for (int row = 0; row < 3; ++row)
    {
        const auto scaled = mm_fixed_mul(mm_fixed_mul(stretch[row], BrFloatToScalar(core.shape.scale[row])),
                                        state.transform.rScaleStep);
        if (!scaled) return false;
        for (int col = 0; col < 3; ++col)
        {
            double value = 0;
            for (int i = 0; i < 3; ++i)
                value += BrScalarToFloat(rest.m[i][row]) * double(BrScalarToFloat(root.m[i][col]));
            pose.rotation[row * 3 + col] = static_cast<float>(value / BrScalarToFloat(scaled));
        }
    }
    return true;
}
bool PreviewActor::Seek(LegacyLong frame)
{
    Host host = {this};
    auto services = host.Services();
    return host.Result(mm_preview_actor_seek(&core, frame, &services));
}

} // namespace Internal
} // namespace MM

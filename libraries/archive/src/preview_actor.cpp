/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "platform_text.h"
#include <codecvt>
#include <algorithm>
#include <cmath>
#include <locale>
#include <map>
#include <sstream>
#include <memory>
#include "preview_actor.h"
#include "engine_actor_motion.h"
#include "engine_actor_timeline.h"
#include "engine_movie_graph.h"
#include "mm/actor_effect.h"
#include "mm/actor_costume_edit.h"
#include "mm/actor_animation.h"
#include "mm/sound_resource.h"
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
    ReleasePpo(&textMaterial);
}
bool PreviewActor::Load(PChunkyResourceFile file, ChunkNumber number,
                        std::function<PChunkyResourceFile(const TAGOnFile &)> resolver,
                        std::function<void(PTemplate)> bindTemplate, std::uint64_t &visibilityClock)
{
    this->visibilityClock = &visibilityClock;
    this->number = number;
    resolve = resolver;
    Host host = {this, file, &bindTemplate};
    auto services = host.Services();
    const auto chunks = file->Pcfl()->ReadChunkAccess();
    return host.Result(mm_preview_actor_load(&core, number, &chunks, 0, &services));
}
bool PreviewActor::Create(const PreviewPropDefinition &definition,
                          std::function<PChunkyResourceFile(const TAGOnFile &)> resolver,
                          std::function<void(PTemplate)> bindTemplate, std::uint64_t &clock)
{
    resolve = std::move(resolver);
    visibilityClock = &clock;
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
    object.prop = tmpl && tmpl->FIsProp();
    object.text = tmpl && tmpl->FIsTdt();
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
    if (!tmpl || !tmpl->FIsTdt()) return false;
    String text; LegacyLong shape; TAG font;
    static_cast<PThreeDText>(tmpl)->GetInfo(&text, &shape, &font);
    const auto count = KauaiText::ToUtf16(text.Psz(), text.Cch(), KauaiText::Encoding::WindowsAnsi, nullptr, 0);
    if (text.Cch() && !count) return false;
    std::vector<std::uint16_t> units(count);
    if (count && KauaiText::ToUtf16(text.Psz(), text.Cch(), KauaiText::Encoding::WindowsAnsi,
        units.data(), count) != count) return false;
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    out.text = convert.to_bytes(std::u16string(units.begin(), units.end())); out.shape = shape; out.fontSource = font.sid; out.font = font.cno;
    return true;
}
PMaterial_MTRL PreviewActor::TextMaterial() const
{
    if (!tmpl || !tmpl->FIsTdt() || !body) return nullptr;
    bool plain; PMaterial_MTRL material = nullptr; PCustomMaterial_CMTL custom = nullptr;
    body->GetPartSetMaterial(0, &plain, &material, &custom);
    return plain ? material : nullptr;
}
bool PreviewActor::SetTextMaterial(std::int32_t source, std::uint32_t number)
{
    if (!tmpl || !tmpl->FIsTdt() || !body) return false;
    const TAGOnFile tag = {source, 0, kctgMtrl, number};
    auto file = resolve(tag);
    auto material = file ? static_cast<PMaterial_MTRL>(file->PbacoFetch(kctgMtrl, number, Material_MTRL::FReadMtrl)) : nullptr;
    if (!material) return false;
    ReleasePpo(&textMaterial); textMaterial = material;
    body->SetPartSetMtrl(0, material); return true;
}
bool PreviewActor::SetTextTemplate(std::int32_t source, std::uint32_t number, std::function<void(PTemplate)> bindTemplate)
{
    if (!tmpl || !tmpl->FIsTdt() || !body) return false;
    if (core.header.source_id == source && core.header.template_number == number) return true;
    const TAGOnFile tag = {source, 0, kctgTmpl, number};
    auto file = resolve(tag);
    if (!file) return false;
    const auto release = [](auto *p) { if (p) p->Release(); };
    std::unique_ptr<Template, decltype(release)> next(static_cast<PTemplate>(file->PbacoFetch(kctgTmpl, number, Template::FReadTmpl)), release);
    if (!next || !next->FIsTdt()) return false;
    bindTemplate(next.get());
    // Match retail's FChangeTagTmpl: adjust a body with its current materials,
    // then reselect its action/cel. Work on a duplicate for atomic failure.
    std::unique_ptr<Body, decltype(release)> nextBody(body->PbodyDup(), release);
    if (!nextBody || !static_cast<PThreeDText>(next.get())->FAdjustBody(nextBody.get())) return false;
    const auto saved = core;
    auto oldTemplate = tmpl; auto oldBody = body;
    tmpl = next.get(); body = nextBody.get(); core.tmpl = tmpl; core.body = body;
    core.header.source_id = source; core.header.template_number = number;
    try
    {
        Host host = {this}; auto services = host.Services();
        if (!host.Result(mm_preview_actor_position(&core, 1, &services)))
        { core = saved; tmpl = oldTemplate; body = oldBody; return false; }
    }
    catch (...) { core = saved; tmpl = oldTemplate; body = oldBody; throw; }
    next.release(); nextBody.release();
    oldBody->Release(); oldTemplate->Release();
    return true;
}
bool PreviewActor::Costumes(std::vector<PreviewCostume> &out) const
{
    out.clear();
    if (!tmpl || !body || tmpl->FIsTdt()) return false;
    for (int set = 0; set < body->Cbset(); ++set)
    {
        bool plain; PMaterial_MTRL material; PCustomMaterial_CMTL current;
        body->GetPartSetMaterial(set, &plain, &material, &current);
        for (int i = 0; i < tmpl->CcmidOfBset(set); ++i)
        {
            const auto id = tmpl->CmidOfBset(set, i);
            auto costume = tmpl->PcmtlFetch(id);
            if (!costume) return false;
            const bool selected = !plain && current == costume;
            costume->Release();
            out.push_back({set, int(id), selected, tmpl->FBsetIsAccessory(set)});
        }
    }
    return true;
}
std::string PreviewActor::ActionName(LegacyLong action) const
{
    String name;
    if (action >= 0 && action < tmpl->Cactn() && tmpl->FGetActnName(action, &name) && name.Cch())
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
    if (!tmpl) return false;
    for (LegacyLong i = 0; i < tmpl->Cactn(); ++i)
        out.push_back({ActionName(i), static_cast<std::uint32_t>(ActionCelCount(i))});
    return true;
}
LegacyLong PreviewActor::ActionCelCount(LegacyLong action) const
{
    LegacyLong count = 0;
    if (tmpl && action >= 0 && action < tmpl->Cactn() && tmpl->FGetCcelActn(action, &count))
        return std::max<LegacyLong>(0, count);
    return 0;
}
bool PreviewActor::TryActionCel(LegacyLong action, LegacyLong cel)
{
    return tmpl->FSetActnCel(body, action, cel);
}
void PreviewActor::RestOrientation(BMAT34 &matrix) const
{
    BRA x, y, z;
    tmpl->GetRestOrien(&x, &y, &z);
    BrMatrix34RotateX(&matrix, x);
    BrMatrix34PostRotateY(&matrix, y);
    BrMatrix34PostRotateZ(&matrix, z);
}
bool PreviewActor::CostumeBounds(int bodySet, PreviewBounds &out) const
{
    out = {};
    if (!body || bodySet < 0 || bodySet >= body->Cbset()) return false;
    float low[3] = {}, high[3] = {};
    for (int i = 0; i < body->Cbact() - 2; ++i)
    {
        auto part = body->PbactPart(i); LegacyLong set = -1;
        Body::PbodyFromBact(part, &set);
        if (set != bodySet || part->type != BR_ACTOR_MODEL || !part->model) continue;
        MMActorMatrix matrix;
        BMAT34 world;
        body->PartWorldMatrix(i, matrix);
        std::memcpy(&world, &matrix, sizeof(world));
        for (int corner = 0; corner < 8; ++corner)
        {
            BVEC3 point, transformed;
            for (int a = 0; a < 3; ++a) point.v[a] = (corner & (1 << a)) ? part->model->bounds.max.v[a] : part->model->bounds.min.v[a];
            BrMatrix34ApplyP(&transformed, &point, &world);
            for (int a = 0; a < 3; ++a)
            {
                const auto value = BrScalarToFloat(transformed.v[a]);
                low[a] = out.valid ? std::min(low[a], value) : value;
                high[a] = out.valid ? std::max(high[a], value) : value;
            }
            out.valid = true;
        }
    }
    if (out.valid) for (int c = 0; c < 8; ++c) for (int a = 0; a < 3; ++a)
        out.corners[c * 3 + a] = (c & (1 << a)) ? high[a] : low[a];
    return out.valid;
}
bool PreviewActor::TryCostumes(const std::vector<PreviewCostume> &values)
{
    std::vector<PreviewCostume> available;
    if (!Costumes(available)) return false;
    for (const auto &value : values)
    {
        if (std::none_of(available.begin(), available.end(), [&](const PreviewCostume &c) {
            return c.bodySet == value.bodySet && c.costume == value.costume; })) return false;
        auto costume = tmpl->PcmtlFetch(value.costume);
        if (!costume) return false;
        body->SetPartSetCmtl(costume); costume->Release();
    }
    return true;
}
struct PreviewActor::TrackDraft
{
    PreviewActor &actor;
    MMPreviewActor original;
    PBody originalBody;
    std::uint64_t shown, clock;
    ActorTrack saved;
    ActorSoundList motion;
    std::vector<PreviewSoundCue> cues;
    bool active = false;
    explicit TrackDraft(PreviewActor &a)
        : actor(a), original(a.core), originalBody(a.body), shown(a.lastShown), clock(*a.visibilityClock) {}
    bool Begin()
    {
        if (!saved.FCloneFrom(actor.track) || !motion.FCloneFrom(actor.motionSounds)) return false;
        auto duplicate = originalBody->PbodyDup();
        if (!duplicate) return false;
        actor.track.Swap(saved);
        actor.motionSounds.Swap(motion);
        actor.sounds.swap(cues);
        actor.body = duplicate;
        actor.core.body = duplicate;
        actor.core.route = actor.track.Route();
        actor.core.events = actor.track.Events();
        return active = true;
    }
    ~TrackDraft()
    {
        if (!active) return;
        actor.body->Release();
        actor.track.Swap(saved);
        actor.motionSounds.Swap(motion);
        actor.sounds.swap(cues);
        actor.body = originalBody;
        actor.core = original;
        actor.lastShown = shown;
        *actor.visibilityClock = clock;
    }
    bool Write(const std::string &path, bool withRoute) const
    {
        auto release = [](auto *p) { if (p) p->Release(); };
        std::unique_ptr<MMActorEventTable, decltype(&mm_actor_events_release)> wire(
            ActorData::PeventsEncode(actor.track.Events()), mm_actor_events_release);
        String text; Filename filename; text.SetSzs(const_cast<char *>(path.c_str()));
        if (!wire || !filename.FBuildFromPath(&text)) return false;
        std::unique_ptr<ChunkyFile, decltype(release)> file(ChunkyFile::PcflCreate(&filename, fcflWriteEnable), release);
        ChunkNumber number; DataBlock block;
        if (!file || !file->FAdd(mm_actor_events_wire_size(wire.get()), kctgGgae, &number, &block) || number != 0 || !ActorData::FWriteEvents(wire.get(), &block))
            return false;
        auto route = actor.track.Route();
        if (withRoute && (!file->FAdd(mm_actor_path_wire_size(route), kctgPath, &number, &block) || number != 0 || !ActorData::FWritePath(route, &block)))
            return false;
        return file->FSave(kctgSoc);
    }
};
bool PreviewActor::WriteCostumes(const std::vector<PreviewCostume> &values, const std::string &path)
{
    if (!Visible() || !tmpl || tmpl->FIsTdt()) return false;
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
        if (!host.Result(mm_actor_edit_set_costume(&core.state, motionSounds.Core(), track.Events(), core.header.actor_id,
            value.bodySet, &tag, value.costume, 1, &motion, &services))) return false;
    }
    return draft.Write(path, false);
}
bool PreviewActor::WriteAnimation(const PreviewAnimationEdit &edit, const std::string &path,
                                  int &lastActorFrame, int &lastEditedFrame)
{
    if (!Visible() || !tmpl || edit.points.size() > MM_ACTOR_ANIMATION_MAX_POINTS) return false;
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
    AnimationHost host(this);
    const auto services = host.Record();
    const auto actionCount = edit.kind == PreviewAnimationEdit::Kind::action ? tmpl->Cactn() : 0;
    MMActorAnimationRange range;
    if (!host.Result(mm_actor_animation_edit(&core, motionSounds.Core(), &host.record, &request, actionCount,
                                             &services, &range)) || !draft.Write(path, true))
        return false;
    lastActorFrame = range.actor_last;
    lastEditedFrame = range.edited_last;
    return true;
}
bool PreviewActor::SetEventTrack(std::int32_t source)
{
    if (source == eventSource) return true;
    if (!body || !tmpl || tmpl->FIsTdt()) return false;
    const TAGOnFile tag = {source, 0, kctgGgae, 0};
    auto file = resolve(tag); DataBlock block;
    if (!file || !file->Pcfl()->FFind(kctgGgae, 0, &block)) return false;
    ActorTrack saved;
    if (!saved.FCloneFrom(track)) return false;
    auto events = ActorData::PeventsRead(&block);
    if (!events) return false;
    saved.AdoptEvents(events);
    const auto original = core;
    BodyCostume costume;
    if (!costume.FGet(body)) return false;
    track.Swap(saved); core.route = track.Route(); core.events = track.Events();
    try
    {
        Host host = {this}; const auto services = host.Services();
        if (host.Result(mm_preview_actor_replace_events(&core, track.Events(), original.state.frame, &services)))
        { eventSource = source; return true; }
    }
    catch (...) { track.Swap(saved); core = original; costume.Set(body); throw; }
    track.Swap(saved); core = original; costume.Set(body); return false;
}
bool PreviewActor::ReadTimeline(LegacyLong first, LegacyLong last, PreviewAnimationTimeline &timeline) const
{
    TimelineHost host = {this, &timeline};
    auto services = host.Services();
    const int result = mm_preview_actor_timeline(&core, &track.Core(), first, last, &services);
    if (host.exception) std::rethrow_exception(host.exception);
    return result > 0;
}
bool PreviewActor::ApplyEvent(MMActorState &value, const MMActorTrack &events, LegacyLong index, bool metadataOnly,
                              ActorSoundList *motion, std::vector<PreviewSoundCue> *cues) const
{
    struct Host
    {
        const PreviewActor *actor;
        const MMActorTrack *track;
        ActorSoundList *motion;
        std::vector<PreviewSoundCue> *cues;
        BodyCostume costume;
    } host;
    host.actor = this;
    host.track = &events;
    host.motion = motion;
    host.cues = cues;
    MMActorEffectServices effects = {};
    effects.context = &host;
    effects.write_event = [](void *group, MMI32 row, const MMActorEvent *value) {
        Base event;
        std::memcpy(&event, value, sizeof(event));
        mm_actor_events_set_fixed(static_cast<MMActorEventTable *>(group), row, &event);
    };
    effects.run = [](void *context, const MMActorEffectCommand *command, MMActorEffectReply *reply) {
        auto &host = *static_cast<Host *>(context);
        auto actor = host.actor;
        switch (command->operation)
        {
        case MM_EFFECT_CLEAR_SOUNDS:
            host.motion->Clear();
            break;
        case MM_EFFECT_SET_ACTION_CEL:
            reply->success = command->first >= 0 && command->first < actor->tmpl->Cactn() &&
                             actor->tmpl->FSetActnCel(actor->body, command->first, command->second);
            break;
        case MM_EFFECT_CEL_COUNT:
        {
            LegacyLong count = reply->value;
            reply->success = command->first >= 0 && command->first < actor->tmpl->Cactn() &&
                             actor->tmpl->FGetCcelActn(command->first, &count);
            reply->value = count;
            break;
        }
        case MM_EFFECT_ACTION_FLAGS:
        {
            LegacyUlong flags = 0;
            reply->success = command->first >= 0 && command->first < actor->tmpl->Cactn() &&
                             actor->tmpl->FGetGrfactn(command->first, &flags);
            reply->flags = flags;
            break;
        }
        case MM_EFFECT_SAVE_COSTUME:
            reply->success = host.costume.FGet(actor->body);
            break;
        case MM_EFFECT_DEFAULT_COSTUME:
            reply->success = actor->tmpl->FSetDefaultCost(actor->body);
            break;
        case MM_EFFECT_RESTORE_COSTUME:
            host.costume.Set(actor->body);
            break;
        case MM_EFFECT_SHOW:
            // Native Body::Show attaches at the head of World's actor list.
            // Keep attachment history even though preview bodies have no World.
            actor->lastShown = ++*actor->visibilityClock;
            break;
        case MM_EFFECT_HIDE:
            // Preview presentation uses the shared onStage value; Body has no World.
            break;
        case MM_EFFECT_FETCH_CUSTOM:
            reply->resource = actor->tmpl->PcmtlFetch(command->first);
            break;
        case MM_EFFECT_FETCH_MATERIAL:
        {
            TAG native;
            std::memcpy(&native, command->tag, sizeof(native));
            TAGOnFile tag;
            tag.From(native);
            auto source = actor->resolve(tag);
            if (source)
                reply->resource = source->PbacoFetch(tag.ctg, tag.cno, Material_MTRL::FReadMtrl);
            break;
        }
        case MM_EFFECT_APPLY_CUSTOM:
            actor->body->SetPartSetCmtl(static_cast<PCustomMaterial_CMTL>(command->resource));
            break;
        case MM_EFFECT_APPLY_MATERIAL:
            actor->body->SetPartSetMtrl(command->first, static_cast<PMaterial_MTRL>(command->resource));
            break;
        case MM_EFFECT_RELEASE_CUSTOM:
        {
            auto resource = static_cast<PCustomMaterial_CMTL>(command->resource);
            ReleasePpo(&resource);
            break;
        }
        case MM_EFFECT_RELEASE_MATERIAL:
        {
            auto resource = static_cast<PMaterial_MTRL>(command->resource);
            ReleasePpo(&resource);
            break;
        }
        case MM_EFFECT_ENQUEUE_SOUND:
        {
            MMActorEvent event;
            MMActorSound sound;
            mm_actor_events_get_fixed(host.track->events, command->first, &event);
            mm_actor_events_get_payload(host.track->events, command->first, &sound);
            MMActorSoundServices services = {};
            services.context = &host;
            services.cues_only = 1;
            services.cue = [](void *context, const MMActorSound *value) {
                auto &host = *static_cast<Host *>(context);
                Sound sound;
                std::memcpy(&sound, value, sizeof(sound));
                if (host.actor->core.sound_display)
                    host.cues->push_back(host.actor->SoundCue(sound));
                return 1;
            };
            reply->success = mm_actor_sound_event(host.motion->Core(), &event, &sound, command->first, &services);
            break;
        }
        }
    };
    const MMActorPoint origin = {MMI32(core.header.translation[0]), MMI32(core.header.translation[1]),
                                 MMI32(core.header.translation[2])};
    const auto tables = EngineActorMotion::Services();
    bool success = mm_actor_effect_apply(&value, events.route, events.events, index, &origin, body != nullptr,
                                         metadataOnly, &tables, &effects) != 0;
    return success;
}
PreviewSoundCue PreviewActor::SoundCue(const Sound &sound) const
{
    MMActorSound value;
    static_assert(sizeof(value) == sizeof(sound), "Preview actor sound layout");
    std::memcpy(&value, &sound, sizeof(value));
    MMPreviewCue cue;
    mm_preview_actor_cue(&cue, &value, core.state.frame, core.header.actor_id);
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
    if (!Visible() || !body || !tmpl) return false;
    const auto &state = core.state;
    pose.node = state.location.irpt;
    pose.offset = state.location.dwrOffset;
    pose.routeFrame = state.location.dnfrm;
    const auto &position = state.position;
    const auto &translation = state.routeTranslation;
    pose.position[0] = position.dxr; pose.position[1] = position.dyr; pose.position[2] = position.dzr;
    pose.translation[0] = translation.dxr; pose.translation[1] = translation.dyr; pose.translation[2] = translation.dzr;
    BRA angles[3];
    tmpl->GetRestOrien(angles, angles + 1, angles + 2);
    MMActorMatrix rest;
    mm_matrix_identity(&rest);
    for (int axis = 0; axis < 3; ++axis) mm_matrix_rotate(&rest, axis, angles[axis], 0);
    const BRS stretch[] = {state.transform.aevpull.rScaleX, state.transform.aevpull.rScaleY, state.transform.aevpull.rScaleZ};
    const auto &root = body->RootMatrix();
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
    const bool ok = host.Result(mm_preview_actor_seek(&core, frame, &services));
    if (ok && textMaterial && body) body->SetPartSetMtrl(0, textMaterial);
    return ok;
}

} // namespace Internal
} // namespace MM

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
#include "mm/actor_effect.h"
#undef size
ASSERTNAME
using namespace ActorEvent;

namespace MM
{
namespace Internal
{
#include "preview_actor_host.h"
PreviewActor::~PreviewActor()
{
    Host host = {this};
    auto services = host.Services();
    mm_preview_actor_dispose(&core, &services);
}
ActorStepSource PreviewActor::Steps() const
{
    ActorStepSource steps;
    steps.context = tmpl;
    steps.resolve = [](void *value, LegacyLong action, LegacyLong cel, BRS *step) {
        return static_cast<PTemplate>(value)->FGetDwrActnCel(action, cel, step);
    };
    return steps;
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
    return host.Result(mm_preview_actor_load(&core, number, &services));
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
bool PreviewActor::ReadTimeline(LegacyLong first, LegacyLong last, PreviewAnimationTimeline &timeline) const
{
    timeline = {};
    if (core.inactive || core.header.first_frame > last)
        return true;
    if (int64_t(last) - core.header.first_frame > 100000)
        return false;
    ActorTrack copy;
    if (!copy.FCloneFrom(track))
        return false;
    ActorFrameState cursor;
    cursor.RewindRoute(copy);
    std::map<LegacyLong, std::string> names;
    const auto actionName = [&](LegacyLong action) {
        auto found = names.find(action);
        if (found != names.end())
            return found->second;
        std::string result = "Action " + std::to_string(action);
        String name;
        if (action >= 0 && action < tmpl->Cactn() && tmpl->FGetActnName(action, &name) && name.Cch())
        {
            const auto encoding = sizeof(achar) == 1 ? KauaiText::Encoding::WindowsAnsi : KauaiText::Encoding::Utf16LE;
            const auto count = KauaiText::ToUtf16(name.Psz(), name.Cch() * sizeof(achar), encoding, nullptr, 0);
            std::u16string text(count, 0);
            if (count && KauaiText::ToUtf16(name.Psz(), name.Cch() * sizeof(achar), encoding,
                                            reinterpret_cast<std::uint16_t *>(&text[0]), count))
                result = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().to_bytes(text);
        }
        names.emplace(action, result);
        return result;
    };
    const auto describe = [&](LegacyLong index, const Base &event) {
        PreviewAnimationEvent marker;
        marker.frame = cursor.frame;
        std::ostringstream detail;
        detail.precision(4);
        switch (event.aet)
        {
        case aetAdd:
            marker.kind = PreviewAnimationKind::Entrance;
            marker.name = "Enter stage";
            break;
        case aetRem:
            marker.kind = PreviewAnimationKind::Exit;
            marker.name = "Leave stage";
            break;
        case aetActn: {
            Action action;
            copy.Events()->Get(index, &action);
            marker.kind = PreviewAnimationKind::Action;
            marker.name = actionName(action.anid);
            detail << "Action " << action.anid << ", starting cel " << action.celn;
            break;
        }
        case aetCost: {
            Costume costume;
            copy.Events()->Get(index, &costume);
            marker.kind = PreviewAnimationKind::Costume;
            marker.name = "Costume";
            detail << "Part set " << costume.ibset;
            if (costume.fCmtl)
                detail << ", costume " << costume.cmid;
            else
                detail << ", material " << costume.tag.cno << ", source " << costume.tag.sid;
            break;
        }
        case aetRotF:
        case aetRotH:
            marker.kind = PreviewAnimationKind::Rotation;
            marker.name = event.aet == aetRotH ? "Rotate this frame" : "Rotate";
            break;
        case aetSize:
            marker.kind = PreviewAnimationKind::Scale;
            marker.name = "Size";
            detail << BrScalarToFloat(cursor.transform.rScaleStep) << "x";
            break;
        case aetPull:
            marker.kind = PreviewAnimationKind::Stretch;
            marker.name = "Stretch";
            detail << "X " << BrScalarToFloat(cursor.transform.aevpull.rScaleX) << ", Y "
                   << BrScalarToFloat(cursor.transform.aevpull.rScaleY) << ", Z "
                   << BrScalarToFloat(cursor.transform.aevpull.rScaleZ);
            break;
        case aetMove:
        case aetTweak: {
            RoutePoint value;
            copy.Events()->Get(index, &value);
            marker.kind = event.aet == aetMove ? PreviewAnimationKind::Move : PreviewAnimationKind::Path;
            marker.name = event.aet == aetMove ? "Move path" : "Adjust path";
            detail << "X " << BrScalarToFloat(value.dxr) << ", Y " << BrScalarToFloat(value.dyr) << ", Z "
                   << BrScalarToFloat(value.dzr);
            break;
        }
        case aetStep:
            marker.kind = PreviewAnimationKind::Step;
            marker.name = cursor.step == rZero     ? "Stop movement"
                          : cursor.step == kdwrNil ? "Action speed"
                                                   : "Movement speed";
            if (cursor.step != kdwrNil)
                detail << BrScalarToFloat(cursor.step) << " units per frame (before size scaling)";
            break;
        case aetFreeze:
            marker.kind = PreviewAnimationKind::Freeze;
            marker.name = cursor.frozen ? "Freeze animation" : "Unfreeze animation";
            break;
        default:
            return; // Actor sound events already have their own dope sheet.
        }
        marker.detail = detail.str();
        timeline.events.push_back(std::move(marker));
    };
    // Use the same sequencer and value-event operations as live playback. Body,
    // costume and sound effects are deliberately absent from this private host.
    const auto apply = [&](LegacyLong index) {
        Base event;
        copy.Events()->GetFixed(index, &event);
        auto value = EngineActorMotion::Core(cursor);
        const bool ok = ApplyEvent(value, copy, index, true);
        EngineActorMotion::Publish(cursor, value);
        if (!ok) return false;
        event.nfrm = cursor.frame;
        if (cursor.frame >= first)
        {
            if (timeline.events.size() >= 100000)
                return false;
            describe(index, event);
        }
        return true;
    };
    LegacyLong previousAction = ivNil, previousEntrance = ivNil;
    for (int64_t frame = core.header.first_frame; frame <= last; ++frame)
    {
        cursor.frame = static_cast<LegacyLong>(frame);
        cursor.firstEventInFrame = cursor.nextEvent;
        ActorFrameInputs inputs;
        inputs.firstFrame = core.header.first_frame;
        inputs.displayFrame = true;
        inputs.suppressSounds = true;
        inputs.stepSucceeded = Steps().FPlaybackStep(cursor, &inputs.step);
        ActorFrameEvaluator evaluator(cursor, copy, inputs, true);
        for (;;)
        {
            const auto command = evaluator.Next();
            if (command.kind == ActorFrameCommandKind::Finished)
                break;
            if (command.kind == ActorFrameCommandKind::CheckEntrance)
            {
                Base event;
                copy.Events()->GetFixed(command.eventIndex, &event);
                evaluator.Complete(event.nfrm <= cursor.frame);
            }
            else
                evaluator.Complete(apply(command.eventIndex));
        }
        if (!evaluator.Result().success)
            return false;
        if (frame >= first && cursor.onStage)
        {
            if (timeline.spans.empty() || timeline.spans.back().endFrame != frame ||
                previousAction != cursor.actionEvent || previousEntrance != cursor.addEvent ||
                timeline.spans.back().frozen != cursor.frozen)
                timeline.spans.push_back({frame, frame + 1, cursor.action, cursor.frozen, actionName(cursor.action)});
            else
                timeline.spans.back().endFrame = frame + 1;
        }
        previousAction = cursor.actionEvent;
        previousEntrance = cursor.addEvent;
    }
    return true;
}
bool PreviewActor::ApplyEvent(MMActorState &value, const ActorTrack &events, LegacyLong index, bool metadataOnly,
                              ActorSoundList *motion, std::vector<PreviewSoundCue> *cues) const
{
    struct Host
    {
        const PreviewActor *actor;
        const ActorTrack *track;
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
        static_cast<PGeneralGroup>(group)->PutFixed(row, &event);
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
            host.track->Events()->GetFixed(command->first, &event);
            host.track->Events()->Get(command->first, &sound);
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
    bool success = mm_actor_effect_apply(&value, events.Route(), events.Events(), index, &origin, body != nullptr,
                                         metadataOnly, &tables, &effects) != 0;
    return success;
}
PreviewSoundCue PreviewActor::SoundCue(const Sound &sound) const
{
    PreviewSoundCue cue;
    cue.frame = core.state.frame;
    cue.actor = core.header.actor_id;
    cue.actorSound = true;
    cue.sound = {sound.tag.ctg, sound.tag.cno};
    cue.source = sound.tag.sid;
    cue.child = sound.chid;
    cue.style = sound.sty;
    cue.volume = sound.vlm;
    cue.loop = sound.fLoop != tNo;
    cue.queue = sound.fQueue != tNo;
    cue.silent = sound.fNoSound == tYes;
    cue.motion = sound.celn != ivNil;
    cue.motionCel = sound.celn;
    return cue;
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
bool PreviewActor::Seek(LegacyLong frame)
{
    Host host = {this};
    auto services = host.Services();
    return host.Result(mm_preview_actor_seek(&core, frame, &services));
}

} // namespace Internal
} // namespace MM

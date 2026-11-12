/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "platform_text.h"
#include <codecvt>
#include <locale>
#include <map>
#include <sstream>
#include "preview_actor.h"
#include "engine_actor_motion.h"
#include "three_mm/actor_effect.h"
#undef size
ASSERTNAME
using namespace ActorEvent;

namespace ThreeMM
{
namespace Internal
{
PreviewActor::~PreviewActor()
{
    ReleasePpo(&body);
    ReleasePpo(&tmpl);
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
                        std::function<void(PTemplate)> bindTemplate)
{
    this->number = number;
    resolve = resolver;
    auto chunky = file->Pcfl();
    DataBlock block;
    ChildChunkIdentification child;
    if (!chunky->FFind(kctgActr, number, &block) || !ActorData::FReadHeader(&block, &header) ||
        !chunky->FGetKidChidCtg(kctgActr, number, 0, kctgPath, &child) ||
        !chunky->FFind(child.cki.ctg, child.cki.cno, &block) || !track.AdoptRoute(ActorData::PglReadRoute(&block)) ||
        !chunky->FGetKidChidCtg(kctgActr, number, 0, kctgGgae, &child) ||
        !chunky->FFind(child.cki.ctg, child.cki.cno, &block) || !track.AdoptEvents(ActorData::PggReadEvents(&block)))
        return false;
    if (header.nfrmFirst == knfrmInvalid)
    {
        inactive = true; // Roll-call actors with no entrance are valid and invisible.
        return true;
    }
    if (header.tagTmpl.sid == ksidUseCrf && chunky->FGetKidChidCtg(kctgActr, number, 0, kctgTmpl, &child))
        header.tagTmpl.cno = child.cki.cno;
    auto source = resolve(header.tagTmpl);
    if (!source)
        return false;
    tmpl = static_cast<PTemplate>(source->PbacoFetch(header.tagTmpl.ctg, header.tagTmpl.cno, Template::FReadTmpl));
    if (!tmpl)
        return false;
    bindTemplate(tmpl);
    if (!(body = tmpl->PbodyCreate()) || (track.Events()->IvMac() && !track.Route()->IvMac()))
        return false;
    // Route/event readers validate row widths. Validate node references before
    // evaluation; reject impossible timelines rather than running unbounded.
    for (LegacyLong i = 0; i < track.Events()->IvMac(); ++i)
    {
        Base event;
        track.Events()->GetFixed(i, &event);
        if (event.rtel.irpt < 0 || event.rtel.irpt >= track.Route()->IvMac())
            return false;
    }
    // Old actor headers omit nfrmLast. Recompute through Studio's evaluator,
    // with an explicit host work budget rather than rejecting the old format.
    if (header.nfrmLast == knfrmInvalid)
    {
        LegacyLong last;
        if (!ActorTimeline::FComputeLifetime(track, header.nfrmFirst, false, Steps(), &last, 100000))
            return false;
        header.nfrmLast = last;
    }
    if (header.nfrmLast < header.nfrmFirst || int64_t(header.nfrmLast) - header.nfrmFirst > 100000)
        return false;
    state.RewindRoute(track);
    return true;
}
PreviewObject PreviewActor::Snapshot() const
{
    PreviewObject object;
    object.asset = {kctgActr, number};
    object.templateAsset = {header.tagTmpl.ctg, header.tagTmpl.cno};
    object.actorId = header.arid;
    object.sourceId = header.tagTmpl.sid;
    object.hasLifetime = !inactive;
    object.firstFrame = inactive ? 0 : header.nfrmFirst;
    object.lastFrame = inactive ? 0 : header.nfrmLast;
    object.prop = tmpl && tmpl->FIsProp();
    object.text = tmpl && tmpl->FIsTdt();
    object.visible = !inactive && state.onStage;
    object.action = state.action;
    object.cel = state.cel;
    object.x = BrScalarToFloat(BrsAdd(state.routeTranslation.dxr, state.position.dxr));
    object.y = BrScalarToFloat(BrsAdd(state.routeTranslation.dyr, state.position.dyr));
    object.z = BrScalarToFloat(BrsAdd(state.routeTranslation.dzr, state.position.dzr));
    return object;
}
bool PreviewActor::ReadTimeline(LegacyLong first, LegacyLong last, PreviewAnimationTimeline &timeline) const
{
    timeline = {};
    if (inactive || header.nfrmFirst > last)
        return true;
    if (int64_t(last) - header.nfrmFirst > 100000)
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
        if (!ApplyEvent(cursor, copy, index, true))
            return false;
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
    for (int64_t frame = header.nfrmFirst; frame <= last; ++frame)
    {
        cursor.frame = static_cast<LegacyLong>(frame);
        cursor.firstEventInFrame = cursor.nextEvent;
        ActorFrameInputs inputs;
        inputs.firstFrame = header.nfrmFirst;
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
bool PreviewActor::Apply(LegacyLong index)
{
    return ApplyEvent(state, track, index, false, &motionSounds, &sounds);
}
bool PreviewActor::ApplyEvent(ActorFrameState &value, const ActorTrack &events, LegacyLong index, bool metadataOnly,
                              ActorSoundList *motion, std::vector<PreviewSoundCue> *cues) const
{
    struct Host
    {
        const PreviewActor *actor;
        const ActorTrack *track;
        ActorFrameState *view;
        ActorSoundList *motion;
        std::vector<PreviewSoundCue> *cues;
        BodyCostume costume;
    } host;
    host.actor = this;
    host.track = &events;
    host.view = &value;
    host.motion = motion;
    host.cues = cues;
    ThreeMMActorEffectServices effects = {};
    effects.context = &host;
    effects.write_event = [](void *group, ThreeMMI32 row, const ThreeMMActorEvent *value) {
        Base event;
        std::memcpy(&event, value, sizeof(event));
        static_cast<PGeneralGroup>(group)->PutFixed(row, &event);
    };
    effects.publish = [](void *context, const ThreeMMActorState *state) {
        EngineActorMotion::Publish(*static_cast<Host *>(context)->view, *state);
    };
    effects.run = [](void *context, const ThreeMMActorEffectCommand *command, ThreeMMActorEffectReply *reply) {
        auto &host = *static_cast<Host *>(context);
        auto actor = host.actor;
        switch (command->operation)
        {
        case THREE_MM_EFFECT_CLEAR_SOUNDS:
            host.motion->Clear();
            break;
        case THREE_MM_EFFECT_SET_ACTION_CEL:
            reply->success = command->first >= 0 && command->first < actor->tmpl->Cactn() &&
                             actor->tmpl->FSetActnCel(actor->body, command->first, command->second);
            break;
        case THREE_MM_EFFECT_CEL_COUNT:
        {
            LegacyLong count = reply->value;
            reply->success = command->first >= 0 && command->first < actor->tmpl->Cactn() &&
                             actor->tmpl->FGetCcelActn(command->first, &count);
            reply->value = count;
            break;
        }
        case THREE_MM_EFFECT_ACTION_FLAGS:
        {
            LegacyUlong flags = 0;
            reply->success = command->first >= 0 && command->first < actor->tmpl->Cactn() &&
                             actor->tmpl->FGetGrfactn(command->first, &flags);
            reply->flags = flags;
            break;
        }
        case THREE_MM_EFFECT_SAVE_COSTUME:
            reply->success = host.costume.FGet(actor->body);
            break;
        case THREE_MM_EFFECT_DEFAULT_COSTUME:
            reply->success = actor->tmpl->FSetDefaultCost(actor->body);
            break;
        case THREE_MM_EFFECT_RESTORE_COSTUME:
            host.costume.Set(actor->body);
            break;
        case THREE_MM_EFFECT_SHOW:
        case THREE_MM_EFFECT_HIDE:
            // Preview presentation uses the shared onStage value; Body has no World.
            break;
        case THREE_MM_EFFECT_FETCH_CUSTOM:
            reply->resource = actor->tmpl->PcmtlFetch(command->first);
            break;
        case THREE_MM_EFFECT_FETCH_MATERIAL:
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
        case THREE_MM_EFFECT_APPLY_CUSTOM:
            actor->body->SetPartSetCmtl(static_cast<PCustomMaterial_CMTL>(command->resource));
            break;
        case THREE_MM_EFFECT_APPLY_MATERIAL:
            actor->body->SetPartSetMtrl(command->first, static_cast<PMaterial_MTRL>(command->resource));
            break;
        case THREE_MM_EFFECT_RELEASE_CUSTOM:
        {
            auto resource = static_cast<PCustomMaterial_CMTL>(command->resource);
            ReleasePpo(&resource);
            break;
        }
        case THREE_MM_EFFECT_RELEASE_MATERIAL:
        {
            auto resource = static_cast<PMaterial_MTRL>(command->resource);
            ReleasePpo(&resource);
            break;
        }
        case THREE_MM_EFFECT_ENQUEUE_SOUND:
        {
            ThreeMMActorEvent event;
            ThreeMMActorSound sound;
            host.track->Events()->GetFixed(command->first, &event);
            host.track->Events()->Get(command->first, &sound);
            ThreeMMActorSoundServices services = {};
            services.context = &host;
            services.cues_only = 1;
            services.cue = [](void *context, const ThreeMMActorSound *value) {
                auto &host = *static_cast<Host *>(context);
                Sound sound;
                std::memcpy(&sound, value, sizeof(sound));
                if (host.actor->soundDisplay)
                    host.cues->push_back(host.actor->SoundCue(sound));
                return 1;
            };
            reply->success = three_mm_actor_sound_event(host.motion->Core(), &event, &sound, command->first, &services);
            break;
        }
        }
    };
    auto core = EngineActorMotion::Core(value);
    const auto origin = EngineActorMotion::Point(header.dxyzFullRte);
    const auto tables = EngineActorMotion::Services();
    bool success = three_mm_actor_effect_apply(&core, events.Route(), events.Events(), index, &origin,
                                               body != nullptr, metadataOnly, &tables, &effects) != 0;
    EngineActorMotion::Publish(value, core);
    return success;
}
PreviewSoundCue PreviewActor::SoundCue(const Sound &sound) const
{
    PreviewSoundCue cue;
    cue.frame = state.frame;
    cue.actor = header.arid;
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
bool PreviewActor::Position(bool selectCel)
{
    ActorPoseInputs pose;
    tmpl->GetRestOrien(&pose.restX, &pose.restY, &pose.restZ);
    pose.fixedPathOrientation = tmpl->FIsTdt();
    BMAT34 matrix;
    ActorPoseEvaluator::Update(state, track, pose, &state.position, &matrix);
    body->LocateOrient(BrsAdd(state.routeTranslation.dxr, state.position.dxr),
                       BrsAdd(state.routeTranslation.dyr, state.position.dyr),
                       BrsAdd(state.routeTranslation.dzr, state.position.dzr), &matrix);
    return !selectCel || !state.onStage || tmpl->FSetActnCel(body, state.action, state.cel);
}
bool PreviewActor::Evaluate(bool display)
{
    sounds.clear();
    soundDisplay = display;
    ActorFrameInputs inputs;
    inputs.firstFrame = header.nfrmFirst;
    inputs.displayFrame = display;
    inputs.suppressSounds = false;
    inputs.stepSucceeded = Steps().FPlaybackStep(state, &inputs.step);
    ActorFrameEvaluator evaluator(state, track, inputs, true);
    for (;;)
    {
        auto command = evaluator.Next();
        if (command.kind == ActorFrameCommandKind::Finished)
            break;
        if (command.kind == ActorFrameCommandKind::CheckEntrance)
        {
            Base event;
            track.Events()->GetFixed(command.eventIndex, &event);
            evaluator.Complete(event.nfrm <= state.frame);
        }
        else
            evaluator.Complete(Apply(command.eventIndex));
    }
    const auto &result = evaluator.Result();
    if (!result.success)
        return false;
    if (result.considerMotionSounds && state.frame <= header.nfrmLast && state.celCount > 0)
    {
        ThreeMMActorSoundServices services = {};
        services.context = this;
        services.cues_only = 1;
        services.cel_count = [](void *context, ThreeMMI32, ThreeMMI32 *out) {
            *out = static_cast<PreviewActor *>(context)->state.celCount;
            return 1;
        };
        services.cue = [](void *context, const ThreeMMActorSound *value) {
            auto actor = static_cast<PreviewActor *>(context);
            Sound sound;
            std::memcpy(&sound, value, sizeof(sound));
            actor->sounds.push_back(actor->SoundCue(sound));
            return 1;
        };
        if (!three_mm_actor_sound_motion(motionSounds.Core(), state.action, state.cel, &services))
            return false;
    }
    return (!result.positionBody && !result.updatePose) || Position(result.selectCel);
}
bool PreviewActor::Seek(LegacyLong frame)
{
    if (frame != state.frame)
        sounds.clear();
    if (inactive)
        return true;
    if (int64_t(frame) - header.nfrmFirst > 100000)
        return false;
    ActorSeekEvaluator seek(state, track, header.nfrmFirst, header.nfrmLast, frame, Steps());
    for (;;)
    {
        auto command = seek.Next();
        switch (command.kind)
        {
        case ActorSeekCommandKind::Hide:
            state.onStage = false;
            seek.Complete(true);
            break;
        case ActorSeekCommandKind::ResetCostume:
            motionSounds.Clear();
            seek.Complete(tmpl->FSetDefaultCost(body));
            break;
        case ActorSeekCommandKind::ApplyQuickEffects:
            seek.Complete(Position(true));
            break;
        case ActorSeekCommandKind::EvaluateFrame:
            seek.Complete(Evaluate(command.displayFrame));
            break;
        case ActorSeekCommandKind::Finished:
            return seek.Success();
        }
    }
}
} // namespace Internal
} // namespace ThreeMM

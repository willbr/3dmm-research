/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "preview_actor.h"
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
        !chunky->FFind(child.cki.ctg, child.cki.cno, &block) || !(track.route = ActorData::PglReadRoute(&block)) ||
        !chunky->FGetKidChidCtg(kctgActr, number, 0, kctgGgae, &child) ||
        !chunky->FFind(child.cki.ctg, child.cki.cno, &block) || !(track.events = ActorData::PggReadEvents(&block)))
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
    if (!(body = tmpl->PbodyCreate()) || (track.events->IvMac() && !track.route->IvMac()))
        return false;
    // Route/event readers validate row widths. Validate node references before
    // evaluation; reject impossible timelines rather than running unbounded.
    for (LegacyLong i = 0; i < track.events->IvMac(); ++i)
    {
        Base event;
        track.events->GetFixed(i, &event);
        if (event.rtel.irpt < 0 || event.rtel.irpt >= track.route->IvMac())
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
bool PreviewActor::Apply(LegacyLong index)
{
    Base event;
    track.events->GetFixed(index, &event);
    event.nfrm = state.frame;
    track.events->PutFixed(index, &event);
    auto applied = state.ApplyValueEvent(track, index, header.dxyzFullRte);
    if (applied != ActorValueEventResult::NeedsRuntime)
        return applied == ActorValueEventResult::Applied;
    switch (event.aet)
    {
    case aetAdd: {
        Add add;
        track.events->Get(index, &add);
        if (!tmpl->FSetDefaultCost(body) || !tmpl->FSetActnCel(body, state.action, state.cel))
            return false;
        state.subrouteTranslation = {add.dxr, add.dyr, add.dzr};
        state.UpdateOrigin(header.dxyzFullRte);
        state.InitTransform();
        BrMatrix34PostRotateX(&state.transform.bmat34Cur, add.xa);
        BrMatrix34PostRotateY(&state.transform.bmat34Cur, add.ya);
        BrMatrix34PostRotateZ(&state.transform.bmat34Cur, add.za);
        state.transform.xaPath = add.xa;
        state.transform.yaPath = add.ya;
        state.transform.zaPath = add.za;
        state.firstEventInFrame = state.addEvent = index;
        state.location = event.rtel;
        ActorRouteEvaluator::Position(track, &state.location, &state.position);
        state.onStage = true;
        motionSounds.clear();
        return true;
    }
    case aetActn: {
        Action action;
        track.events->Get(index, &action);
        if (action.anid < 0 || action.anid >= tmpl->Cactn() || !tmpl->FSetActnCel(body, action.anid, action.celn))
            return false;
        if (state.action != action.anid)
            motionSounds.clear();
        state.action = action.anid;
        state.cel = action.celn;
        state.actionEvent = index;
        state.location = event.rtel;
        ActorRouteEvaluator::Position(track, &state.location, &state.position);
        return tmpl->FGetCcelActn(state.action, &state.celCount) && tmpl->FGetGrfactn(state.action, &state.actionFlags);
    }
    case aetCost: {
        Costume costume;
        track.events->Get(index, &costume);
        if (costume.fCmtl)
        {
            auto material = tmpl->PcmtlFetch(costume.cmid);
            if (!material)
                return false;
            body->SetPartSetCmtl(material);
            ReleasePpo(&material);
        }
        else
        {
            TAGOnFile tag;
            tag.From(costume.tag);
            auto source = resolve(tag);
            if (!source)
                return false;
            auto material = static_cast<PMaterial_MTRL>(source->PbacoFetch(tag.ctg, tag.cno, Material_MTRL::FReadMtrl));
            if (!material)
                return false;
            body->SetPartSetMtrl(costume.ibset, material);
            ReleasePpo(&material);
        }
        return true;
    }
    case aetRem:
        state.onStage = false;
        return true;
    case aetSnd: {
        Sound sound;
        track.events->Get(index, &sound);
        if (sound.celn != ivNil)
        {
            for (auto &existing : motionSounds)
                if (existing.celn == sound.celn && existing.sty == sound.sty)
                {
                    existing = sound;
                    return true;
                }
            motionSounds.push_back(sound);
        }
        else if (soundDisplay)
            sounds.push_back(SoundCue(sound));
        return true;
    }
    default:
        return false;
    }
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
            track.events->GetFixed(command.eventIndex, &event);
            evaluator.Complete(event.nfrm <= state.frame);
        }
        else
            evaluator.Complete(Apply(command.eventIndex));
    }
    const auto &result = evaluator.Result();
    if (!result.success)
        return false;
    if (result.considerMotionSounds && state.frame <= header.nfrmLast && state.celCount > 0)
        for (const auto &sound : motionSounds)
            if (sound.celn == state.cel % state.celCount)
                sounds.push_back(SoundCue(sound));
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
            motionSounds.clear();
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

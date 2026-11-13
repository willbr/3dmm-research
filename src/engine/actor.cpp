/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Actor Engine

    Primary Author : *****
    Review Status: Reviewed

    Actors contain:
    1. A path, pglrpt, which is a gl of route points, the nodes of the path.
    2. An event list, pggaev, which is a list of variable size events
        occurring on or between nodes on the path.
    3. A motion match list of sounds applicable during the current action.
    4. State variables

    See Movie.doc for a detailed description of the functionality.

    Routes consists of one or more subroutes which can be separately
    translated, deleted, etc.

    This is a *where* based model of editing.  Events occur at specified
    locations along the path.  The path is linear between nodes.  Each
    node represents the point at which the actor was located in the original
    path recording session.  It is not redefined on resizing or motion fill.
    Instead, the event list is edited.  The mouse input is however, smoothed
    in movie before actor is called.

    The one exception to events being executed based on location is the
    aetAdd event, which is executed at its specified time.
    Due to motion fill inheriting the static or non-static path property,
    note that it is (no longer) possible for motion fill to cause an
    actor to not reach the end of a subroute.

    Misc comments:

    Actors begin each subroute with the	default costume, reimposed
    automatically by the code at every new subroute.	Any
    initialization events are then CHANGE events, post applied via
    events in the event stream.	When a new subroute is created, the
    new initialization events are copied from the previous subroute,
    if one exists, and from the following subroute if no previous subroute
    exists.

    Subroutes can be pushed in time by record, rerecord, & sooner/later.
    An actor is represented at at most one location at any given time.

    All events located <= _rtelCur on the path are executed at the end
    of any given frame. (See FGotoFrame()).

    Each subroute has an aetAdd event as its first event in the event list.

    Rotation transformations are absolute, not cumulative. The transformation
    effective at the current frame is stored in _xfrm.  There are now two
    fundamentally different types of rotation.  The aetRotF represent forward
    rotation and have path orientation applied concurrently.  The aetRotH
    apply to static segments only and do not have path orientation post
    applied.  _xfrm represents both, and _fUseBmat34 flags (for each frame)
    which transformation is to be applied when the body is positioned.
    The aetRotH events take precedence over aetRotF events if both exist in the
    same frame. This is necessary to allow the aetRotF events to control future
    path rotation while still allowing single frame orientation.  The aetRotF
    events nuke the aetRotH events, however, in order to allow the user to
    visibly see the forward rotation currently being applied.

    Costume changes are per body part set.

    Freeze inhibits cel advancement.
    Step size events control floating as well as cel advancement in place.
    Each subpath is terminated with a step=0 and a freeze event.

    Each actor is visible (not hidden, though possibly out of view) between
    aetAdd and aetRem events.

    The stretch events, aetPull, do not alter step size.  Uniform stretching,
    aetSize events, do.

    Move events are used to translate the path forward of the event by the
    delta-xyz value in the event.  These are therefore cumulative, so that
    (unlike rotations), a move at frame 50 followed by a move at frame 20
    will result in frame 50 -to- end-of-subroute being translated by the sum
    of the translations at frames 20 and 50.  Tweak events do not translate
    the path, but instead change the current display location only.

    This code carefully maintains wysiwyg.  There are two parts to this.
    One part is the maintenance of exact location, so that if a recorded
    actor follows a specific path (eg, through rather than into doorways)
    that path is retained exactly.  A second part is that by <calculating>
    exact locations (unaltered by numerical roundoff), a carefully edited
    and synchronized actor will not end up one frame off in time on replay.
    Both were considered high priority be design.

    Actors currently proceed to the end of their path, taking a potentially
    partial step at the end.  As a result, to maintain wysiwyg, actors
    display at the location of aetActn and aetStep events.
    NOTE: This UI decision probably added more complexity to the
    code than it gained in UI functionality.  Had actors always
    displayed only at full step increments, these scenarios would not
    include wysiwyg issues.

    Frame numbers are stored with the events, but are not guaranteed to be
    correct for frame numbers larger than current frame _nfrmCur.
    FComputeLifetime() guarantees these for all frames, but FComputeLifetime()
    can fail -> the code should not rely on future nfrm values.

    The route is translated by _dxyzFullRte in class Actor.
    Each subroute is additionally translated by _dxyzSubRte.
    _dxyzRte combines the overall actor translation for efficiency only.

***************************************************************************/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "frame.h"
#include "soc.h"

#include "body_world.h"
#include "engine_actor_motion.h"
#include "engine_actor_timeline.h"
#include "three_mm/actor_effect.h"
#include "three_mm/actor_placement.h"

ASSERTNAME

using namespace ActorEvent;

RTCLASS(Actor)

/***************************************************************************

    Constructor for Actor - private.

***************************************************************************/
Actor::Actor(void)
{
    ThreeMMActorRuntime runtime; three_mm_actor_runtime_init(&runtime); _PublishRuntime(runtime);
}

/***************************************************************************

    Destructor for an Actor

***************************************************************************/
Actor::~Actor(void)
{
    AssertBaseThis(0);
    const auto services=_OwnerServices(); three_mm_actor_owner_dispose(this,&services);
}

/***************************************************************************

    Initialize the actor
    The actor will not yet be grounded to any initial scene frame.

***************************************************************************/
bool Actor::_FInit(TAG *ptagTmpl)
{
    AssertBaseThis(0);
    AssertVarMem(ptagTmpl);

    _ptmpl = (PTemplate)vptagm->PbacoFetch(ptagTmpl, Template::FReadTmpl);
    if (pvNil == _ptmpl)
        return fFalse;

    AssertPo(_ptmpl, 0);
    _tagTmpl = *ptagTmpl;
    TagManager::DupTag(&_tagTmpl);

    if (!_FCreateGroups())
        return fFalse;

    _SetStateRewound();
    if (!_ptmpl->FGetGrfactn(0, &_state.actionFlags))
        return fFalse;

    return fTrue;
}

/***************************************************************************

    Initialize the transformation matrix & factors
    (Sets the absolute scale = 1, and use the rest orientation)

***************************************************************************/
void Actor::_InitXfrm(void)
{
    AssertThis(0);
    _state.InitTransform();
}

/***************************************************************************

    Initialize the rotation part of the transformation
    The rest orientation is now applied post user-rotations so that
    user rotations can be with respect to the actor's coordinate system

***************************************************************************/
void Actor::_InitXfrmRot(BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    BrMatrix34Identity(pbmat34);
}

/***************************************************************************

    Allocate a new actor
    The actor is not attached to a scene until SetPscen is called.
    The actor will not have an identifiable ID until SetArid is called.
    Note that an actor which is not yet added to a scene is neither
    grounded in time (_nfrmFirst) nor space (_dxyzFullRte).

***************************************************************************/
PActor Actor::PactrNew(TAG *ptagTmpl)
{
    AssertVarMem(ptagTmpl);

    PActor pactr;

    if ((pactr = NewObj Actor()) == pvNil)
        return pvNil;

    if (!pactr->_FInit(ptagTmpl))
    {
        ReleasePpo(&pactr);
        return pvNil;
    }

    return pactr;
}

/***************************************************************************

    Create the groups _track.Events(), _track.Route() and motion-sound storage

***************************************************************************/
bool Actor::_FCreateGroups(void)
{
    AssertBaseThis(0);

    if (!_track.FCreate())
        return fFalse;

    if (!_motionSounds.FCreate())
        return fFalse;

    return fTrue;
}

/***************************************************************************

    Set the owning scene and the brender world for the body.

***************************************************************************/
void Actor::SetPscen(Scene *pscen)
{
    AssertBaseThis(0);

    AssertVarMem(pscen);
    Assert(_pscen == pvNil || _pscen == pscen, "SetPscen logic error");

    if (_pscen != pvNil)
    {
        Assert(_pbody != pvNil, "Bad body pointer");
        return;
    }

    Assert(_pbody == pvNil, "Bad body pointer");
    _pscen = pscen;

    // Create the body parts
    if (pvNil == (_pbody = _ptmpl->PbodyCreate()))
        return;

    BodyWorld::Bind(_pbody, pscen->Pmvie()->Pbwld());
}

/***************************************************************************

    Hide the Actor and initialize the Actor State Variables

***************************************************************************/
void Actor::_InitState(void)
{
    AssertBaseThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    three_mm_actor_route_edit_reset(&state,_track.Route(),&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Set the actor state variables to the rewound position

***************************************************************************/
void Actor::_SetStateRewound(void)
{
    AssertBaseThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    three_mm_actor_route_edit_rewind(&state,_track.Route(),&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Prepare the actor for display in frame nfrm.

    nfrm can be any frame number in the movie - it is actor independent.
    NOTE: The state of this actor is expected to be current for frame _nfrmCur
    at the time this routine is called.
    FGotoFrame exits with _nfrmCur == nfrm.
    (A scene level interrogation of all actors should find
    that each actor's _nfrmCur is the same).

    Note: The scope of fPosition dirty spans multiple calls to _FDoFrm

***************************************************************************/
bool Actor::FGotoFrame(long nfrm, bool *pfSoundInFrame)
{
    AssertThis(0);
    bool positionDirty = false;
    ActorSeekEvaluator evaluator(_state, _track, _nfrmFirst, _nfrmLast, nfrm, _StepSource());
    for (;;)
    {
        ActorSeekCommand command = evaluator.Next();
        switch (command.kind)
        {
        case ActorSeekCommandKind::Hide:
            _Hide();
            evaluator.Complete(true);
            break;
        case ActorSeekCommandKind::ResetCostume:
            _fModeRecord = false;
            _ptmpl->FSetDefaultCost(_pbody); // Preserve best-effort reset.
            evaluator.Complete(true);
            break;
        case ActorSeekCommandKind::ApplyQuickEffects:
            evaluator.Complete(_FApplyQuickSeekEffects());
            break;
        case ActorSeekCommandKind::EvaluateFrame:
            evaluator.Complete(_FDoFrm(command.displayFrame, &positionDirty, pfSoundInFrame));
            break;
        case ActorSeekCommandKind::Finished:
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
            if (!evaluator.Success())
            {
                const auto batch = std::getenv("THREEDMM_BATCH_TEST");
                if (batch && std::strcmp(batch, "1") == 0)
                    std::fprintf(stderr, "BATCH_ACTOR_FRAME_FAILED actor=%ld frame=%ld action=%ld cel=%ld event=%ld "
                                         "template_sid=%ld template_cno=%08lx\n",
                                 _arid, nfrm, static_cast<long>(_state.action), static_cast<long>(_state.cel),
                                 static_cast<long>(_state.nextEvent), static_cast<long>(_tagTmpl.sid),
                                 static_cast<unsigned long>(_tagTmpl.cno));
            }
#endif
            return evaluator.Success();
        }
    }
}

/***************************************************************************

    Apply runtime sound/body/cel effects after core completes a quick seek.

***************************************************************************/
bool Actor::_FApplyQuickSeekEffects()
{
    AssertThis(0);
    if (!(_pscen->GrfScen() & fscenSounds) && _state.frame <= _nfrmLast && _state.onStage)
        _FEnqueueSmmInMsq();
    _PositionBody(&_state.position);
    return !_state.onStage || _ptmpl->FSetActnCel(_pbody, _state.action, _state.cel, pvNil);
}

/***************************************************************************

    Update the internal actor state variables to frame _nfrmCur.
    Ready the actor to display in frame _nfrmCur if fPositionBody is fTrue.
    Sets *pfPositionDirty if a change is encountered.
    Clears *pfPositionDirty after updating Brender.

    Assumes previous frame correctly rendered.
    Note: PositionBody is delayed until after Events are processed - Tweak,
    Actn, Step and Transform events all can alter the location or orientation.
    Also, new Action Events precede Cel Positioning

    Note: The scope of *pfPositionDirty spans multiple _FDoFrm calls

***************************************************************************/
bool Actor::_FDoFrm(bool fPositionBody, bool *pfPositionDirty, bool *pfSoundInFrame)
{
    AssertThis(0);
    AssertVarMem(pfPositionDirty);

    ActorFrameInputs inputs;
    inputs.firstFrame = _nfrmFirst;
    inputs.recording = _fModeRecord;
    inputs.rejoining = _fRejoin;
    inputs.displayFrame = fPositionBody;
    inputs.suppressSounds = FPure(_pscen->GrfScen() & fscenSounds);
    inputs.stepSucceeded = _FGetDwrPlay(&inputs.step);
    ActorFrameEvaluator evaluator(_state, _track, inputs, *pfPositionDirty);
    for (;;)
    {
        ActorFrameCommand command = evaluator.Next();
        if (command.kind == ActorFrameCommandKind::Finished)
            break;
        if (command.kind == ActorFrameCommandKind::CheckEntrance)
            evaluator.Complete(_FIsAddNow(command.eventIndex));
        else
            evaluator.Complete(_FDoAevCore(command.eventIndex));
    }
    const ActorFrameResult &result = evaluator.Result();
    *pfPositionDirty = result.positionDirty;
    if (pfSoundInFrame != pvNil && result.soundInFrame)
        *pfSoundInFrame = fTrue;
    if (result.aborted)
        return fFalse;

    bool success = result.success;
    if (result.considerMotionSounds && _state.frame <= _nfrmLast)
        _FEnqueueSmmInMsq(); // Preserve best-effort motion-match dispatch.
    if (result.positionBody)
    {
        _PositionBody(&_state.position);
        *pfPositionDirty = fFalse;
    }
    if (result.selectCel && !_ptmpl->FSetActnCel(_pbody, _state.action, _state.cel, pvNil))
        success = false;
    if (result.updatePose)
    {
        BMAT34 matrix;
        _MatrixRotUpdate(&_state.position, &matrix);
    }
    return success;
}

/***************************************************************************

    FReplayFrame : Replays the sound for the current frame
        -> Re-enqueues sounds for the current frame
    grfscen - Things that are supposed to be played right now.

***************************************************************************/
bool Actor::FReplayFrame(long grfscen)
{
    AssertThis(0);

    Base aev;
    long iaev;

    // Check if there is anything to do
    if (!(grfscen & fscenSounds) || !_state.onStage)
        return fTrue;

    for (iaev = _state.firstEventInFrame; iaev < _state.nextEvent; iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.aet != aetSnd)
            continue;

        if (!_FDoAevCore(iaev))
            return fFalse;
    }

    // Also send any motion match sounds to msq to play
    return _FEnqueueSmmInMsq();
}

/***************************************************************************

    _FGetStatic : Returns true/false on success/failure
    Returns the bool value in *pfStatic

***************************************************************************/
bool Actor::_FGetStatic(long anid, bool *pfStatic)
{
    AssertThis(0);
    AssertIn(anid, 0, klwMax);
    AssertVarMem(pfStatic);

    LegacyUlong grfactn;

    if (!_ptmpl->FGetGrfactn(anid, &grfactn))
        return fFalse;

    if (ivNil == _state.actionEvent)
    {
        *pfStatic = fTrue;
        return fTrue;
    }

    *pfStatic = FPure(grfactn & factnStatic);
    return fTrue;
}

/***************************************************************************

    Is the actor in the last active frame of the subroute?

***************************************************************************/
bool Actor::_FIsDoneAevSub(long iaev, RouteLocation rtel)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _track.Events()->IvMac() + 1);
    return ActorFrameEvaluator::FSubrouteEventsDone(_track, iaev, rtel);
}

/***************************************************************************

    Is the actor at an AddOnStage event which can be applied now?

***************************************************************************/
bool Actor::_FIsAddNow(long iaev)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _track.Events()->IvMac());

    Base *paev;

    if (_fLifeDirty)
    {
        // Update the nfrm values
        if (!_FComputeLifetime())
            return fFalse;
    }

    Assert(_track.Events()->Cb(iaev) == size(Add), "Corrupt event list");
    paev = (Base *)_track.Events()->QvFixedGet(iaev);

    if (paev->nfrm <= _state.frame)
        return fTrue;

    return fFalse;
}

/***************************************************************************

    Return functional (sized) step size when playing
    Zero is a valid return value

***************************************************************************/
ActorStepSource Actor::_StepSource() const
{
    ActorStepSource source;
    source.context = _ptmpl;
    source.resolve = [](void *context, LegacyLong action, LegacyLong cel, BRS *step) {
        return ((Template *)context)->FGetDwrActnCel(action, cel, step);
    };
    return source;
}

bool Actor::_FGetDwrPlay(BRS *pdwr)
{
    AssertBaseThis(0);
    return _StepSource().FPlaybackStep(_state, pdwr);
}

/***************************************************************************

    Return functional (sized) step size when recording.
    The returned size is expected to be authored > 0

***************************************************************************/
bool Actor::_FGetDwrRecord(BRS *pdwr)
{
    AssertBaseThis(0); AssertVarMem(pdwr);
    const auto state=EngineActorMotion::Core(_state);
    const auto services=_SubrouteEditServices();
    ThreeMMI32 distance=0;
    bool ok=three_mm_actor_record_step(&state,&distance,&services.timeline)!=0;
    *pdwr=distance; return ok;
}

/***************************************************************************

    Execute the current event.
    State Variables are updated.

***************************************************************************/
bool Actor::_FDoAevCur(void)
{
    AssertBaseThis(0);

    if (!_FDoAevCore(_state.nextEvent))
        return fFalse;

    _state.nextEvent++;
    Assert(_state.nextEvent <= _track.Events()->IvMac(), "_state.nextEvent bug");

    return fTrue;
}

/***************************************************************************

    Execute the event iaev.

***************************************************************************/
bool Actor::_FDoAevCore(long iaev)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _track.Events()->IvMac());
    struct Host
    {
        Actor *actor;
        BodyCostume costume;
    } host;
    host.actor = this;
    ThreeMMActorEffectServices effects = {};
    effects.context = &host;
    effects.write_event = [](void *events, ThreeMMI32 index, const ThreeMMActorEvent *value) {
        Base event;
        std::memcpy(&event, value, sizeof(event));
        static_cast<PGeneralGroup>(events)->PutFixed(index, &event);
    };
    effects.publish = [](void *context, const ThreeMMActorState *value) {
        EngineActorMotion::Publish(static_cast<Host *>(context)->actor->_state, *value);
    };
    effects.run = [](void *context, const ThreeMMActorEffectCommand *command, ThreeMMActorEffectReply *reply) {
        auto &host = *static_cast<Host *>(context);
        auto actor = host.actor;
        switch (command->operation)
        {
        case THREE_MM_EFFECT_CLEAR_SOUNDS:
            actor->_motionSounds.Clear();
            break;
        case THREE_MM_EFFECT_SET_ACTION_CEL:
            reply->success = actor->_ptmpl->FSetActnCel(actor->_pbody, command->first, command->second);
            break;
        case THREE_MM_EFFECT_CEL_COUNT:
        {
            LegacyLong count = reply->value;
            reply->success = actor->_ptmpl->FGetCcelActn(command->first, &count);
            reply->value = count;
            break;
        }
        case THREE_MM_EFFECT_ACTION_FLAGS:
        {
            LegacyUlong flags = 0;
            reply->success = actor->_ptmpl->FGetGrfactn(command->first, &flags);
            reply->flags = flags;
            break;
        }
        case THREE_MM_EFFECT_SAVE_COSTUME:
            reply->success = host.costume.FGet(actor->_pbody);
            break;
        case THREE_MM_EFFECT_DEFAULT_COSTUME:
            reply->success = actor->_ptmpl->FSetDefaultCost(actor->_pbody);
            break;
        case THREE_MM_EFFECT_RESTORE_COSTUME:
            host.costume.Set(actor->_pbody);
            break;
        case THREE_MM_EFFECT_SHOW:
            actor->_pbody->Show();
            break;
        case THREE_MM_EFFECT_HIDE:
            actor->_pbody->Hide();
            break;
        case THREE_MM_EFFECT_FETCH_CUSTOM:
            reply->resource = actor->_ptmpl->PcmtlFetch(command->first);
            break;
        case THREE_MM_EFFECT_FETCH_MATERIAL:
        {
            TAG tag;
            std::memcpy(&tag, command->tag, sizeof(tag));
            reply->resource = vptagm->PbacoFetch(&tag, Material_MTRL::FReadMtrl);
            break;
        }
        case THREE_MM_EFFECT_APPLY_CUSTOM:
            actor->_pbody->SetPartSetCmtl(static_cast<PCustomMaterial_CMTL>(command->resource));
            break;
        case THREE_MM_EFFECT_APPLY_MATERIAL:
            actor->_pbody->SetPartSetMtrl(command->first, static_cast<PMaterial_MTRL>(command->resource));
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
            reply->success = actor->_FEnqueueSnd(command->first);
            break;
        }
    };
    auto state = EngineActorMotion::Core(_state);
    const auto origin = EngineActorMotion::Point(_dxyzFullRte);
    const auto tables = EngineActorMotion::Services();
    bool success = three_mm_actor_effect_apply(&state, _track.Route(), _track.Events(), iaev, &origin,
                                               _pbody != pvNil, 0, &tables, &effects) != 0;
    EngineActorMotion::Publish(_state, state);
    return success;
}

/***************************************************************************

    Add the specified event to the event list at the current frame, and
    execute the event

    NOTE:  Overwrites events of the same type if appropriate
    NOTE:  If the new action event is the same as the most recent
    previous action, the new action is still inserted.	This avoids
    repositioning the actor based on a past Set Action.

***************************************************************************/
bool Actor::_FAddDoAev(long aetNew, long cbNew, void *pvVar)
{
    AssertBaseThis(0);
    AssertIn(aetNew, 0, aetLim); AssertIn(cbNew, 0, 100); AssertPvCb(pvVar, cbNew);
    Assert(_state.onStage || aetNew == aetAdd, "Error! Beginning subroute without Add");
#ifndef BUG1870
#error C89 actor editing requires the enabled retail BUG1870 policy
#endif
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto edits=_EventEditServices();
    bool ok=three_mm_actor_edit_add_apply(&state,_motionSounds.Core(),_track.Events(),_arid,aetNew,cbNew,pvVar,
                                          &tables,&edits)!=0;
    EngineActorMotion::Publish(_state,state);
    if (ok) AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************

    Merge event iaevNew among preceding events, beginning with
    event iaevFirst (which is usually the first event in the frame).
    Unless equal to pvNil, Return *piaevRtn to be the final index of the
    merged event
    NOTE:  Add events (only) are not merged.

***************************************************************************/
void Actor::_MergeAev(long iaevFirst, long iaevNew, long *piaevRtn)
{
    AssertBaseThis(0);
    AssertIn(iaevFirst, 0, _track.Events()->IvMac());
    AssertIn(iaevNew, iaevFirst, _track.Events()->IvMac());
    AssertNilOrVarMem(piaevRtn);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto edits=_EventEditServices();
    ThreeMMI32 result=0;
    three_mm_actor_edit_merge(&state,_motionSounds.Core(),_track.Events(),_arid,iaevFirst,iaevNew,
                              piaevRtn ? &result : nullptr,&tables,&edits);
    EngineActorMotion::Publish(_state,state);
    if (piaevRtn) *piaevRtn=result;
}

/***************************************************************************

    Add (or replace) an action
    Add the event to the event list

***************************************************************************/
ThreeMMActorActionEditServices Actor::_ActionEditServices()
{
    static_assert(factnStatic==8 && kdwrNil==-65536,"actor action policy ABI");
    ThreeMMActorActionEditServices s={}; s.context=this; s.sounds=_SoundEditServices();
    s.action_flags=[](void *p,ThreeMMI32 action,ThreeMMU32 *flags) {
        LegacyUlong value=0; bool ok=static_cast<Actor *>(p)->_ptmpl->FGetGrfactn(action,&value); *flags=value; return int(ok);
    };
    s.text_template=[](void *p) { return int(static_cast<Actor *>(p)->_ptmpl->FIsTdt()); };
    s.mark_life_dirty=[](void *p) { static_cast<Actor *>(p)->_fLifeDirty=true; };
    return s;
}
ThreeMMActorSubrouteEditServices Actor::_SubrouteEditServices()
{
    ThreeMMActorSubrouteEditServices s={}; s.route=_RouteEditServices(); s.action=_ActionEditServices();
    s.timeline.motion=EngineActorMotion::Services(); s.timeline.context=this;
    s.timeline.resolve=[](void *p,ThreeMMI32 action,ThreeMMI32 cel,ThreeMMI32 *out) {
        BRS value=*out; bool ok=static_cast<Actor *>(p)->_ptmpl->FGetDwrActnCel(action,cel,&value); *out=value; return int(ok);
    };
    s.timeline.put_event=s.route.write_event;
    s.timeline.publish_frame=[](void *p,ThreeMMI32 frame) { static_cast<Actor *>(p)->_nfrmLast=frame; };
    s.timeline.diagnostic=[](void *,int fault) { EngineActorTimeline::Diagnostic(nullptr,fault); };
    s.get_flags=[](void *p,ThreeMMActorSubrouteFlags *flags) {
        auto actor=static_cast<Actor *>(p); flags->dirty=actor->_fLifeDirty; flags->recording=actor->_fModeRecord; flags->rejoining=actor->_fRejoin;
    };
    s.set_dirty=[](void *p,int dirty) { static_cast<Actor *>(p)->_fLifeDirty=dirty!=0; };
    s.pose_inputs=[](void *p,ThreeMMActorPoseInputs *inputs) {
        auto actor=static_cast<Actor *>(p); actor->_ptmpl->GetRestOrien(&inputs->restX,&inputs->restY,&inputs->restZ);
        inputs->fixedPathOrientation=actor->_ptmpl->FIsTdt();
    };
    s.locate_body=[](void *p,const ThreeMMActorPoint *position,const ThreeMMActorMatrix *value) {
        BMAT34 matrix; std::memcpy(&matrix,value,sizeof(matrix));
        static_cast<Actor *>(p)->_pbody->LocateOrient(position->dxr,position->dyr,position->dzr,&matrix);
    };
    s.seek=[](void *p,ThreeMMActorState *state,ThreeMMI32 frame) {
        auto actor=static_cast<Actor *>(p); EngineActorMotion::Publish(actor->_state,*state);
        bool ok=actor->FGotoFrame(frame); *state=EngineActorMotion::Core(actor->_state); return int(ok);
    };
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case THREE_MM_SUBROUTE_BAD_LIFETIME: PushErc(ercSocBadFrameSlider); break;
        case THREE_MM_SUBROUTE_BAD_HOLD: Bug("Event list out of order (dnfrm error)"); break;
        case THREE_MM_SUBROUTE_OFFSTAGE_EVENT: Bug("Offstage case should be already handled"); break;
        }
    };
    return s;
}
bool Actor::FSetActionCore(long anid, long celn, bool fFreeze)
{
    AssertThis(0); AssertIn(anid,0,klwMax); AssertIn(celn,klwMin,klwMax);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_ActionEditServices();
    ChildChunkID external=0; TrashVar(&external);
    bool ok=three_mm_actor_edit_set_action(&state,_motionSounds.Core(),_track.Events(),_arid,anid,celn,fFreeze,
                                           external,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state);
    return ok;
}

/***************************************************************************

    Remove an actor from the stage
    NOTE: This is a low level API and has no effect on the event list

***************************************************************************/
void Actor::_Hide(void)
{
    AssertBaseThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto services=_RouteEditServices();
    three_mm_actor_route_edit_hide(&state,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Insert a node point *prpt in the route.
    Update *prpt.dwr, and the previous node's dwr.
    On input:
        *prpt.dwr == 0 iff the new node is a subpath terminating node
        *prpt.xyz has the new node coordinates
        dwrPrior is the distance from the previous node to the new one.

***************************************************************************/
ThreeMMActorRouteEditServices Actor::_RouteEditServices()
{
    ThreeMMActorRouteEditServices s={}; s.edits=_EventEditServices();
    s.insert_point=[](void *p,ThreeMMI32 i,const ThreeMMActorRoutePoint *v) { return int(static_cast<PDynamicArray>(p)->FInsert(i,const_cast<ThreeMMActorRoutePoint *>(v))); };
    s.write_point=[](void *p,ThreeMMI32 i,const ThreeMMActorRoutePoint *v) { static_cast<PDynamicArray>(p)->Put(i,const_cast<ThreeMMActorRoutePoint *>(v)); };
    s.delete_points=[](void *p,ThreeMMI32 i,ThreeMMI32 count) { static_cast<PDynamicArray>(p)->Delete(i,count); };
    s.clear_route=[](void *p) { static_cast<PDynamicArray>(p)->FSetIvMac(0); };
    s.write_event=[](void *p,ThreeMMI32 i,const ThreeMMActorEvent *v) { static_cast<PGeneralGroup>(p)->PutFixed(i,const_cast<ThreeMMActorEvent *>(v)); };
    s.swap_events=[](void *p,ThreeMMI32 a,ThreeMMI32 b) { static_cast<PGeneralGroup>(p)->Swap(a,b); };
    s.reserve_events=[](void *p,ThreeMMU32 count,ThreeMMU32 bytes) { return int(static_cast<Actor *>(p)->_track.Events()->FEnsureSpace(count,bytes,fgrpNil)); };
    s.life_dirty=[](void *p) { static_cast<Actor *>(p)->_fLifeDirty=true; };
    s.recording=[](void *p,int enabled) { static_cast<Actor *>(p)->_fModeRecord=enabled!=0; };
    s.hide_body=[](void *p) { auto actor=static_cast<Actor *>(p); if (actor->_pbody) actor->_pbody->Hide(); };
    s.default_costume=[](void *p) { auto actor=static_cast<Actor *>(p); if (actor->_ptmpl && actor->_pbody) actor->_ptmpl->FSetDefaultCost(actor->_pbody); };
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case THREE_MM_ROUTE_BAD_ALIVE: Bug("Invalid fAlive value"); break;
        case THREE_MM_ROUTE_FREEZE_FAILED: Assert(false,"Expected freeze event to succeed"); break;
        case THREE_MM_ROUTE_STEP_FAILED: Assert(false,"Expected set step event to succeed"); break;
        }
    };
    return s;
}
bool Actor::_FInsertGgRpt(long irpt, RouteDistancePoint *prpt, BRS dwrPrior)
{
    AssertBaseThis(0); AssertIn(irpt,0,_track.Route()->IvMac()+1); AssertVarMem(prpt);
    static_assert(rEps==1,"actor route epsilon ABI");
    ThreeMMActorRoutePoint point; std::memcpy(&point,prpt,sizeof(point));
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    bool ok=three_mm_actor_route_edit_insert(_track.Route(),irpt,&point,dwrPrior,&tables,&services)!=0;
    std::memcpy(prpt,&point,sizeof(point));
    return ok;
}

/***************************************************************************

    Set a Stepsize Event
    Add it to the event list

    dwrStep == 0 -> Stop the actor from forward motion.
    dwrStep == kdwrNil -> Use the scaled template step size
    Note that this is orthogonal and independent of freezing an actor

***************************************************************************/
bool Actor::FSetStep(BRS dwrStep)
{
    AssertThis(0); Assert(kdwrNil==dwrStep || rZero<=dwrStep, "Invalid dwrStep argument");
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_SoundEditServices();
    bool ok=three_mm_actor_edit_set_step(&state,_motionSounds.Core(),_track.Events(),_arid,dwrStep,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state);
    return ok;
}

/***************************************************************************

    Get the new origin for the to-be positioned actor.  Always place
    actors on the "floor" (Y = 0)...except 3-D Text actors, which are
    at Y = 1.0 meters.

***************************************************************************/
void Actor::_GetNewOrigin(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);

    static_assert(kzrDefault == -1638400, "Placement distance ABI");
    BRS x, y, z;
    _pscen->Pbkgd()->GetActorPlacePoint(&x, &y, &z);
    ThreeMMActorPoint point{x, y, z};
    ThreeMMActorPlacementServices services{};
    services.context = this;
    services.camera = [](void *context, ThreeMMActorMatrix *matrix, ThreeMMI32 *hither, ThreeMMI32 *yon) {
        auto *actor = static_cast<Actor *>(context);
        BMAT34 camera;
        BRS nearClip, farClip;
        actor->_pscen->Pmvie()->Pbwld()->GetCamera(&camera, &nearClip, &farClip);
        Assert(nearClip >= 0 && farClip >= 0, "Negative camera values");
        *hither = nearClip;
        *yon = farClip;
        std::memcpy(matrix, &camera, sizeof(camera));
    };
    three_mm_actor_placement(&point, _ptmpl->FIsTdt(), &services);
    *pxr = point.dxr;
    *pyr = point.dyr;
    *pzr = point.dzr;
}

/***************************************************************************

    Add actor on the stage - ie, create a new subroute.
    Add the Add event to the event list
    Nukes existing subroute if obsoleted by current Add.

    Add the initialization events for costumes, xforms, etc from the
    preceding or subsequent subroute.
***************************************************************************/
ThreeMMActorStageEditServices Actor::_StageEditServices()
{
    static_assert(knfrmInvalid==2147483647 && kcbVarAdd==20 && kcbVarActn==8 &&
                  kcbVarFreeze==4 && kcbVarStep==4 && kcbVarRot==48 && kcbVarSize==4 && kcbVarPull==12,
                  "Stage edit ABI");
    ThreeMMActorStageEditServices s{}; s.subroute=_SubrouteEditServices();
    s.reserve_points=[](void *route,ThreeMMU32 count) {
        return int(static_cast<PDynamicArray>(route)->FEnsureSpace(count,fgrpNil));
    };
    s.placement=[](void *p,ThreeMMActorPoint *point,ThreeMMActorPoint *origin,unsigned short *yaw) {
        auto actor=static_cast<Actor *>(p); BRS x,y,z;
        actor->_GetNewOrigin(&x,&y,&z); *point={x,y,z};
        std::memcpy(origin,&actor->_dxyzFullRte,sizeof(*origin));
        *yaw=actor->_pscen->Pbkgd()->BraRotYCamera();
    };
    s.diagnostic=[](void *,int fault) { Assert(fault==THREE_MM_STAGE_INSERT_FAILED,"Unknown stage edit fault"); Bug("Logic error"); };
    return s;
}
bool Actor::FAddOnStageCore(void)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_StageEditServices();
    ChildChunkID external=0; TrashVar(&external);
    bool ok=three_mm_actor_stage_add(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                      external,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state);
    return ok;
}

/***************************************************************************

    Collect events < iaevLim & insert them beginning at event number
    _iaevCur.
    NOTE:  The primary complication is to OMIT copying events which can
    never be reached due to static actions (stalling) in the previous
    subroute.  Due to initialization now spec'd to occur in every subroute,
    note that only the immediately preceding subroute is of concern.
    NOTE: This is optimized to add only the latest event of each given type.

***************************************************************************/
bool Actor::_FAddAevFromPrev(long iaevLim, ulong grfaet)
{
    AssertBaseThis(0); AssertIn(iaevLim,0,_track.Events()->IvMac()+1);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    bool ok=three_mm_actor_subroute_from_previous(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                                  iaevLim,grfaet,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state);
    return ok;
}

/***************************************************************************

    Gather the initialization events of the subsequent subroute and insert
    them at the	current event index, _iaevCur.

***************************************************************************/
bool Actor::_FAddAevFromLater(void)
{
    AssertBaseThis(0); Assert(_state.nextEvent==1,"_FAddAevFromLater logic error");
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    ChildChunkID external=0; TrashVar(&external);
    bool ok=three_mm_actor_subroute_from_later(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                               external,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state);
    return ok;
}

/***************************************************************************

    Locate the event of type aet with index closest to but smaller than iaevCur
    Return true if found, with its index in *piaevAdd

***************************************************************************/
bool Actor::_FFindPrevAevAet(long aet, long iaevCur, long *piaevAdd)
{
    AssertBaseThis(0); AssertIn(aet,0,aetLim); AssertVarMem(piaevAdd);
    const auto tables=EngineActorMotion::Services();
    *piaevAdd=three_mm_actor_route_edit_find(_track.Events(),aet,iaevCur,1,&tables);
    return *piaevAdd>=0;
}

/***************************************************************************

    Locate the event of type aet with index closest to but >= iaevCur.
    Return true if one is found, with its index in *piaevAdd

***************************************************************************/
bool Actor::_FFindNextAevAet(long aet, long iaevCur, long *piaevAdd)
{
    AssertBaseThis(0); AssertIn(aet,0,aetLim); AssertVarMem(piaevAdd);
    const auto tables=EngineActorMotion::Services();
    *piaevAdd=three_mm_actor_route_edit_find(_track.Events(),aet,iaevCur,0,&tables);
    return *piaevAdd>=0;
}

/***************************************************************************

    Locate the last reachable event in the specified subroute
    Note: This is complicated by static actions.
    Input:
        Begin the search at iaevAdd.
        The limit of the search is iaevLim, which, if == ivNil, is
        the extent of the ggaev.
    Return its index in *piaevAdd

***************************************************************************/
void Actor::_FindAevLastSub(long iaevAdd, long iaevLim, long *piaevLast)
{
    AssertBaseThis(0); AssertVarMem(piaevLast);
    const auto tables=EngineActorMotion::Services();
    *piaevLast=three_mm_actor_route_edit_last(_track.Events(),iaevAdd,iaevLim,&tables);
}

/***************************************************************************

    Remove the actor from the stage
    Add the event to the event list

***************************************************************************/
bool Actor::FRemFromStageCore(void)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_StageEditServices();
    bool ok=three_mm_actor_stage_remove(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                         &tables,&services)!=0;
    EngineActorMotion::Publish(_state,state);
    return ok;
}

/***************************************************************************

    Delete the current entire subroute & reposition actor accordingly.
    Based on current state variables

***************************************************************************/
bool Actor::_FDeleteEntireSubrte(void)
{
    AssertThis(0);
    Assert(static_cast<Base *>(_track.Events()->QvFixedGet(_state.addEvent))->nfrm==_state.frame,
           "Logic error: trying to delete whole route from the middle");
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    bool ok=three_mm_actor_stage_delete_subroute(&state,_track.Route(),_track.Events(),&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state);
    return ok;
}

/***************************************************************************

    Delete the events and point corresponding to a single point subroute

***************************************************************************/
void Actor::_DelAddFrame(long iaevAdd, long iaevLim)
{
    AssertThis(0); AssertIn(iaevAdd,0,iaevLim);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    three_mm_actor_route_edit_delete_add(&state,_track.Route(),_track.Events(),iaevAdd,iaevLim,&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Set the Costume for a body part
    Add the event to the event list

***************************************************************************/
bool Actor::FSetCostumeCore(long ibsetClicked, TAG *ptag, long cmid, tribool fCmtl)
{
    AssertThis(0); Assert(fCmtl || ibsetClicked>=0,"Invalid body set"); AssertVarMem(ptag);
    ThreeMMTag tag; std::memcpy(&tag,ptag,sizeof(tag));
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_CostumeEditServices();
    bool ok=three_mm_actor_edit_set_costume(&state,_motionSounds.Core(),_track.Events(),_arid,ibsetClicked,
                                            &tag,cmid,fCmtl,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state);
    return ok;
}

/***************************************************************************

    Freeze the actor (don't advance cels)
    Add the event to the event list

***************************************************************************/
bool Actor::_FFreeze(void)
{
    AssertThis(0);

    int32_t faevfrz = (int32_t)fTrue;
    return _FAddDoAev(aetFreeze, kcbVarFreeze, &faevfrz);
}

/***************************************************************************

    Unfreeze the actor
    Add the event to the event list

***************************************************************************/
bool Actor::_FUnfreeze(void)
{
    AssertThis(0);
    int32_t faevfrz = (int32_t)fFalse;
    return _FAddDoAev(aetFreeze, kcbVarFreeze, &faevfrz);
}

/***************************************************************************

    Modify the add event to re-orient the actor
    The add event retains the single point orientation.

    On Input: If pxyz == pvNil, no averaging is done in orientation
    grfbra are the flags indicating angles of rotation.  In directions
    in which no rotation has taken place, the wa are invalid.

    WARNING: Rotations are non-abelian.  Ordering is x then y then z.
    If ordering other than this is desired, separate calls must be made.

***************************************************************************/
void Actor::SetAddOrient(BRA xa, BRA ya, BRA za, ulong grfbra, RoutePoint *pdxyz)
{
    AssertThis(0); AssertIn(_state.addEvent,0,_track.Events()->IvMac());
    static_assert(kdwrFast==196608 && krOriWeightMin==6553,"actor orientation weight ABI");
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto edits=_EventEditServices();
    ThreeMMI32 distance=0;
    if (pdxyz) distance=BR_LENGTH3(pdxyz->dxr,pdxyz->dyr,pdxyz->dzr);
    three_mm_actor_edit_add_orientation(&state,_track.Events(),xa,ya,za,grfbra,pdxyz?&distance:nullptr,&tables,&edits);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Average two angles by (rw1 * a1 + (1-rw1)*a2)
    rw1 is the weighting for a1

***************************************************************************/
BRA Actor::_BraAvgAngle(BRA a1, BRA a2, BRS rw1)
{
    AssertBaseThis(0); AssertIn(rw1,rZero,rOne+rEps);
    return three_mm_actor_edit_average_angle(a1,a2,rw1);
}

/***************************************************************************

    Rotate the actor
    Add the event to the event list
    If !fFromHereFwd, the rotation is to apply to the current frame only

    WARNING: Rotations are non-abelian.  Ordering is x then y then z.
    If ordering other than this is desired, separate calls must be made.

***************************************************************************/
ThreeMMActorTransformEditServices Actor::_TransformEditServices()
{
    static_assert(fnormSize==THREE_MM_NORMAL_SIZE && fnormRotate==THREE_MM_NORMAL_ROTATE &&
                  fmafGround==THREE_MM_MOVE_GROUND && fmafOrient==THREE_MM_MOVE_ORIENT &&
                  fmafEntireScene==THREE_MM_MOVE_SCENE && fmafEntireSubrte==THREE_MM_MOVE_SUBROUTE &&
                  krScaleMinFactor==6553 && krScaleMin==16384 && krScaleMax==655360 && krAngleMax==26214,
                  "Actor transform edit ABI");
    Assert(krScaleMaxFactor==three_mm_fixed_div(65536,6553) && krPullMax==262144 &&
           krAngleMinRcp==three_mm_fixed_div(65536,6553),"Actor transform bounds ABI");
    ThreeMMActorTransformEditServices s{}; s.subroute=_SubrouteEditServices(); s.fill=_FillEditServices();
    s.scene_present=[](void *p) { return int(static_cast<Actor *>(p)->_pscen!=nullptr); };
    s.normalise=[](void *,ThreeMMActorMatrix *value) {
        BMAT34 matrix,normal; std::memcpy(&matrix,value,sizeof(matrix)); BrMatrix34LPNormalise(&normal,&matrix);
        std::memcpy(value,&normal,sizeof(normal));
    };
    s.distance=[](void *,const ThreeMMActorPoint *point) { return ThreeMMI32(BR_LENGTH3(point->dxr,point->dyr,point->dzr)); };
    s.publish_origin=[](void *p,const ThreeMMActorPoint *origin) { std::memcpy(&static_cast<Actor *>(p)->_dxyzFullRte,origin,sizeof(*origin)); };
    s.seek_scene=[](void *p,ThreeMMActorState *state,ThreeMMI32 frame) {
        auto actor=static_cast<Actor *>(p); bool ok=actor->_pscen->FGotoFrm(frame);
        *state=EngineActorMotion::Core(actor->_state); return int(ok);
    };
    s.scrollbars=[](void *p) { static_cast<Actor *>(p)->_pscen->Pmvie()->Pmcc()->UpdateScrollbars(); };
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case THREE_MM_TRANSFORM_BAD_LIFETIME: PushErc(ercSocBadFrameSlider); break;
        case THREE_MM_TRANSFORM_ADD_FAILED: Bug("EnsureSpace insufficient"); break;
        case THREE_MM_TRANSFORM_MOVE_ROTATE_FAILED: Bug("Should have ensured space"); break;
        }
    };
    return s;
}
bool Actor::FRotate(BRA xa, BRA ya, BRA za, bool fFromHereFwd)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services(); const auto services=_TransformEditServices();
    bool ok=three_mm_actor_transform_rotate(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                             xa,ya,za,fFromHereFwd,0,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); return ok;
}

/***************************************************************************

    Apply a world-axis rotation. FRotate treats (xa,ya,za) as rotations
    around the actor's LOCAL axes (after the rest orientation TS is applied
    at render time pbmat = TS * bmat34Cur). For multi-actor rigid group
    rotation we want every selected actor's body to tilt around the SAME
    world axis -- otherwise actors with different rest orientations tilt
    around different world axes and the group rotation looks chaotic.

    The fix is the similarity transform: applying R_world = TS * R_local * TS^-1
    to the world matrix is equivalent to applying R_local := TS^-1 * R_world * TS
    to the local-frame bmat34. We build R_world from the input Euler angles,
    conjugate it through the actor's TS, and PreRotate bmat34 with the
    resulting matrix using BrMatrix34Pre.

    For a fresh actor whose template has no rest orientation, TS == identity
    and this collapses to the same matrix FRotate would build via successive
    PreRotateX/Y/Z calls.

***************************************************************************/
bool Actor::FRotateAroundWorld(BRA xa, BRA ya, BRA za, bool fFromHereFwd)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services(); const auto services=_TransformEditServices();
    bool ok=three_mm_actor_transform_rotate(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                             xa,ya,za,fFromHereFwd,1,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); return ok;
}

/***************************************************************************

    "Normalize" the actor
    The revert tool
    Insert normalize transformation events into the event list

***************************************************************************/
bool Actor::FNormalizeCore(ulong grfnorm)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services(); const auto services=_TransformEditServices();
    bool ok=three_mm_actor_transform_normalize(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                                grfnorm,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); return ok;
}

/***************************************************************************

    Scale the actor.
    Add the event to the event list
    Bound the allowable scaling.

    Note that the stepsize and the actor lifetime is then subject to change,
    with the attendent specified tweak etc modifications.

***************************************************************************/
bool Actor::FScale(BRS rScale)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services(); const auto services=_TransformEditServices();
    bool ok=three_mm_actor_transform_scale(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                            rScale,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); return ok;
}

/***************************************************************************

    Drag the actor forwards or backwards in time
    NOTE** Drag this subroute only -> sliding other subroutes in time

***************************************************************************/
bool Actor::FSoonerLater(long dnfrm)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services(); const auto services=_TransformEditServices();
    bool ok=three_mm_actor_transform_time(&state,_track.Route(),_track.Events(),dnfrm,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); return ok;
}

/***************************************************************************

    Pull, stretch, squash the actor.
    Add the event to the event list

    Bound the allowable scaling.

***************************************************************************/
bool Actor::FPull(BRS rScaleX, BRS rScaleY, BRS rScaleZ)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services(); const auto services=_TransformEditServices();
    const ThreeMMActorStretch stretch{rScaleX,rScaleY,rScaleZ};
    bool ok=three_mm_actor_transform_pull(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                           &stretch,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); return ok;
}

/***************************************************************************

    Translate one point of the actor's path
    Add an event to the event list

    The (dxr,dyr,dzr) passed in is a delta distance from the previous
    location (set by GoToFrame, ie, determined by _rtelCur).

***************************************************************************/
bool Actor::FTweakRoute(BRS dxr, BRS dyr, BRS dzr, ulong grfmaf)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services(); const auto services=_TransformEditServices();
    const ThreeMMActorPoint delta{dxr,dyr,dzr};
    bool ok=three_mm_actor_transform_tweak(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                            &delta,grfmaf,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); return ok;
}

/***************************************************************************

    Update xyzRte from the overall origin & the subroute translation

***************************************************************************/
void Actor::_UpdateXyzRte(void)
{
    AssertBaseThis(0);
    _state.UpdateOrigin(_dxyzFullRte);
}

/***************************************************************************

    Move the current subroute.	(Compose tool)

    If fmafOrient is set, the actor is oriented and moved.

    If fmafEntireSubrte is set, this does not add an event to the aev list.
    It alters the data in the Add event & _dxyzSubRte.	Return fTrue.

    If fmafEntireScene is set, _dxyzFullRte is modified. Return fTrue.

    If !fmafEntireSubrte && !fmafEntireScene, a move event is added
    to the event list. This can therefore fail.

    Return success or failure
    Return *pfmoved indicating whether or not the path moved.

***************************************************************************/
bool Actor::FMoveRoute(BRS dxr, BRS dyr, BRS dzr, bool *pfMoved, ulong grfmaf)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services(); const auto services=_TransformEditServices();
    AssertNilOrVarMem(pfMoved);
    const ThreeMMActorPoint delta{dxr,dyr,dzr}; ThreeMMActorPoint origin;
    std::memcpy(&origin,&_dxyzFullRte,sizeof(origin));
    ThreeMMActorMoveResult result{}; result.context=pfMoved;
    result.publish=[](void *p,int moved) { if (p) *static_cast<bool *>(p)=moved!=0; };
    bool ok=three_mm_actor_transform_move(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                           &origin,&delta,grfmaf,&result,&tables,&services)!=0;
    std::memcpy(&_dxyzFullRte,&origin,sizeof(origin));
    EngineActorMotion::Publish(_state,state); return ok;
}

/***************************************************************************

    Load starting point orientation into state variable _xfrm

***************************************************************************/
void Actor::_LoadAddOrien(Add *paevadd, bool fNoReset)
{
    AssertThis(0);
    static_assert(sizeof(Add) == sizeof(ThreeMMActorEntrance) && offsetof(Add, xa) == 12 &&
                      offsetof(Add, ya) == 14 && offsetof(Add, za) == 16,
                  "actor entrance ABI");
    ThreeMMActorEntrance add;
    std::memcpy(&add, paevadd, sizeof(add));
    auto state = EngineActorMotion::Core(_state);
    three_mm_actor_add_orientation(&state, &add, fNoReset);
    EngineActorMotion::Publish(_state, state);
}

/***************************************************************************

    Remove redundant events of a targeted type.	 (Actor uses this for
    Xfrm editing)
    NOTE***: This is not valid for Actn or Cost fill.  Special case routines
    exist for those event types.

    This is called _before_ inserting the new event.

    Note: For transforms, this code will rarely have effect unless the client
    uses discretized increments of observable size, ie, it is statistically
    unlikely that a match of the same size will be found. This code was
    requested in anticipation of discretized increments.

    Input: aet == the event type
        *pvVar == the variable part of the new event being inserted
        cbVar == the size of the variable part
        iaevMin == the first possible event to delete
        iaevCmp == ivNil or the event being replaced
        grfaet == faetNil or types of events to nuke along the way

***************************************************************************/
void Actor::_PrepXfrmFill(long aet, void *pvVar, long cbVar, long iaevMin, long iaevCmp, ulong grfaet)
{
    AssertBaseThis(0);
    Assert(aet==aetSize || aet==aetRotF || aet==aetPull, "Illegal fill event");
    AssertPvCb(pvVar,cbVar); AssertIn(cbVar,0,100);
    AssertIn(iaevMin,0,_track.Events()->IvMac()+1); AssertIn(iaevCmp,-1,_track.Events()->IvMac()+1);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_FillEditServices();
    three_mm_actor_edit_transform_fill(&state,_track.Events(),aet,pvVar,cbVar,iaevMin,iaevCmp,grfaet,&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Insert Aev.  Update state variables
    Since _track.Events() is getting a copy of a tag, a call to DupTag is required

***************************************************************************/
ThreeMMActorFillServices Actor::_FillEditServices()
{
    ThreeMMActorFillServices services={};
    services.edits=_EventEditServices();
    services.lock_events=[](void *p) { static_cast<PGeneralGroup>(p)->Lock(); };
    services.unlock_events=[](void *p) { static_cast<PGeneralGroup>(p)->Unlock(); };
    services.payload_view=[](void *p, ThreeMMI32 index)->const void * { return static_cast<PGeneralGroup>(p)->QvGet(index); };
    return services;
}
bool Actor::_FInsertAev(long iaev, long cbNew, void *pvVar, void *paev, bool fUpdateState)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _track.Events()->IvMac()+1); AssertIn(cbNew, 0, 100);
    AssertPvCb(paev,sizeof(Base));
    ThreeMMActorEvent event; std::memcpy(&event,paev,sizeof(event));
    auto state=EngineActorMotion::Core(_state);
    const auto edits=_EventEditServices();
    bool ok=three_mm_actor_edit_insert(&state,iaev,cbNew,pvVar,&event,fUpdateState,&edits)!=0;
    EngineActorMotion::Publish(_state,state);
    return ok;
}

/***************************************************************************

    Remove Aev.  Update state variables

***************************************************************************/
ThreeMMActorEditServices Actor::_EventEditServices()
{
    ThreeMMActorEditServices services = {};
    services.context = this;
    services.close_event_tag = [](void *context, ThreeMMI32 index) {
        auto actor = static_cast<Actor *>(context);
        PTAG tag;
        actor->_track.Events()->Lock();
        if (actor->_FIsIaevTag(actor->_track.Events(), index, &tag)) TagManager::CloseTag(tag);
        actor->_track.Events()->Unlock();
    };
    services.delete_event = [](void *context, ThreeMMI32 index) {
        static_cast<Actor *>(context)->_track.Events()->Delete(index);
    };
    services.scene_frame = [](void *context, ThreeMMI32 *frame) {
        auto actor = static_cast<Actor *>(context);
        if (!actor->_pscen) return 0;
        *frame = actor->_pscen->Nfrm();
        return 1;
    };
    services.update_sound_frame = [](void *context) { static_cast<Actor *>(context)->_pscen->UpdateSndFrame(); };
    services.publish = [](void *context, const ThreeMMActorState *state) {
        EngineActorMotion::Publish(static_cast<Actor *>(context)->_state, *state);
    };
    services.insert_event = [](void *p, ThreeMMI32 index, ThreeMMU32 bytes, const void *payload, const ThreeMMActorEvent *value) {
        Base event; std::memcpy(&event,value,sizeof(event));
        return int(static_cast<Actor *>(p)->_track.Events()->FInsert(index,bytes,const_cast<void *>(payload),&event));
    };
    services.duplicate_event_tag = [](void *p, ThreeMMI32 index) {
        auto actor=static_cast<Actor *>(p); PTAG tag;
        actor->_track.Events()->Lock();
        if (actor->_FIsIaevTag(actor->_track.Events(),index,&tag)) TagManager::DupTag(tag);
        actor->_track.Events()->Unlock();
    };
    services.write_payload = [](void *p, ThreeMMI32 index, const void *value) {
        static_cast<Actor *>(p)->_track.Events()->Put(index,const_cast<void *>(value));
    };
    services.copy_payload = [](void *p, ThreeMMI32 destination, ThreeMMI32 source) {
        auto events=static_cast<Actor *>(p)->_track.Events();
        Assert(events->Cb(source)==events->Cb(destination), "Wrong Var size");
        events->Put(destination,events->QvGet(source));
    };
    services.apply_event = [](void *p, ThreeMMActorState *state, ThreeMMI32 index) {
        auto actor=static_cast<Actor *>(p);
        EngineActorMotion::Publish(actor->_state,*state);
        bool ok=actor->_FDoAevCore(index);
        *state=EngineActorMotion::Core(actor->_state);
        return int(ok);
    };
    services.dirty = [](void *p) { static_cast<Actor *>(p)->_pscen->MarkDirty(); };
    services.get_range = [](void *p, ThreeMMActorEditRange *range) {
        auto actor=static_cast<Actor *>(p); range->first=actor->_nfrmFirst; range->last=actor->_nfrmLast;
    };
    services.invalidate_range = [](void *p) { static_cast<Actor *>(p)->_pscen->InvalFrmRange(); };
    services.publish_range = [](void *p, const ThreeMMActorEditRange *range) {
        auto actor=static_cast<Actor *>(p); actor->_nfrmFirst=range->first; actor->_nfrmLast=range->last;
    };
    return services;
}
void Actor::_RemoveAev(long iaev, bool fUpdateState)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _track.Events()->IvMac());
    auto state = EngineActorMotion::Core(_state);
    const auto tables = EngineActorMotion::Services();
    const auto edits = _EventEditServices();
    three_mm_actor_edit_remove(&state, _track.Events(), iaev, fUpdateState, &tables, &edits);
    EngineActorMotion::Publish(_state, state);
}

/***************************************************************************

    Remove specified (eg, tweak, step, freeze) events for the current action
    If faetActn is set in the grfaet, also remove action events
    matching the replaced action followed by action events matching
    the new (current) action _anidCur.

    Note: This routine exists in addition to the more general PrepXfrmFill
    because actn events are	considered to match if the anid's match
    regardless of the value of the celn.

***************************************************************************/
void Actor::_PrepActnFill(long iaevMin, long anidPrev, long anidNew, ulong grfaet)
{
    AssertBaseThis(0); AssertIn(iaevMin,0,_track.Events()->IvMac()+1);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_EventEditServices();
    three_mm_actor_edit_action_fill(&state,_track.Events(),iaevMin,anidPrev,anidNew,grfaet,&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Remove redundant costume changes
    Note:  	Delete all costume events that match the costume being replaced
            Then delete all subsequent costume events that match the
            costume being inserted

***************************************************************************/
ThreeMMActorCostumeEditServices Actor::_CostumeEditServices()
{
    ThreeMMActorCostumeEditServices services={}; services.context=this; services.edits=_EventEditServices();
    services.reserve_events=[](void *p,ThreeMMU32 entries,ThreeMMU32 bytes) {
        return int(static_cast<Actor *>(p)->_track.Events()->FEnsureSpace(entries,bytes,fgrpNil));
    };
    services.current=[](void *p,ThreeMMI32 set,int *custom)->void * {
        bool ordinary; PMaterial_MTRL material; PCustomMaterial_CMTL costume;
        static_cast<Actor *>(p)->_pbody->GetPartSetMaterial(set,&ordinary,&material,&costume);
        *custom=!ordinary;
        return ordinary ? static_cast<void *>(material) : static_cast<void *>(costume);
    };
    services.fetch_material=[](void *,const ThreeMMTag *value)->void * {
        TAG tag; std::memcpy(&tag,value,sizeof(tag));
        return vptagm->PbacoFetch(&tag,Material_MTRL::FReadMtrl);
    };
    services.fetch_custom=[](void *p,ThreeMMI32 costume)->void * { return static_cast<Actor *>(p)->_ptmpl->PcmtlFetch(costume); };
    services.body_set=[](void *,void *resource)->ThreeMMI32 { return static_cast<PCustomMaterial_CMTL>(resource)->Ibset(); };
    services.release=[](void *,void *resource) { static_cast<PBaseCacheableObject>(resource)->Release(); };
    return services;
}
void Actor::_PrepCostFill(long iaevMin, Costume *paevcost)
{
    AssertBaseThis(0); AssertIn(iaevMin,0,_track.Events()->IvMac()+1); AssertVarMem(paevcost);
    ThreeMMActorCostume costume; std::memcpy(&costume,paevcost,sizeof(costume));
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_CostumeEditServices();
    three_mm_actor_edit_costume_fill(&state,_track.Events(),iaevMin,&costume,&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    AdjustAevForRte for an already inserted point at irptAdjust
    adjust the rtel's of the subsequent affected events

***************************************************************************/
void Actor::_AdjustAevForRteIns(long irptAdjust, long iaevMin)
{
    AssertBaseThis(0); AssertIn(irptAdjust,0,_track.Route()->IvMac()); AssertIn(iaevMin,0,_track.Events()->IvMac()+1);
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    three_mm_actor_route_edit_adjust_insert(_track.Route(),_track.Events(),irptAdjust,iaevMin,&tables,&services);
}
/***************************************************************************

    AdjustAevForRte for a to-be deleted point at irptAdjust
    adjust the rtel's of the affected events

***************************************************************************/
void Actor::_AdjustAevForRteDel(long irptAdjust, long iaevMin)
{
    AssertBaseThis(0); AssertIn(irptAdjust,0,_track.Route()->IvMac()); AssertIn(iaevMin,0,_track.Events()->IvMac()+1);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    three_mm_actor_route_edit_adjust_delete(&state,_track.Route(),_track.Events(),irptAdjust,iaevMin,&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Locate a point on the path dwrStep further along the route from *prtel
    Events which potentially modify this step (Action, Add) begin at iaevCur.
    *pfEndRoute is set only when an attempt is made to move past the last
    node of the subroute.

    NOTE: This routine is designed to be independent of all frame dependent
    state variables.
    Spec: The end of a subroute is <always> reached in a moving action.
***************************************************************************/
void Actor::_AdvanceRtel(BRS dwrStep, RouteLocation *prtel, long iaevCur, long nfrmCur, bool *pfEndRoute)
{
    AssertBaseThis(0);
    ActorRouteEvaluator::Advance(_track, dwrStep, prtel, iaevCur, nfrmCur, _nfrmFirst, !_fModeRecord && !_fRejoin,
                                 pfEndRoute);
}

/***************************************************************************

    Convert a route location (rtel) to an xyz point (in *pxyz)

***************************************************************************/
void Actor::_GetXyzFromRtel(RouteLocation *prtel, PRoutePoint pxyz)
{
    AssertBaseThis(0);
    ActorRouteEvaluator::Position(_track, prtel, pxyz);
}

/***************************************************************************

    Find a point rFract fractional distance between two points.
    Store in *pxyz

***************************************************************************/
void Actor::_GetXyzOnLine(PRoutePoint pxyzFirst, PRoutePoint pxyzSecond, BRS rFract, PRoutePoint pxyz)
{
    AssertBaseThis(0);
    ActorRouteEvaluator::Interpolate(pxyzFirst, pxyzSecond, rFract, pxyz);
}

/***************************************************************************

    Locate the actor (in Brender terms), first adjusting the location.
    Post-impose a rotation looking forward along the route

***************************************************************************/
void Actor::_PositionBody(RoutePoint *pxyz)
{
    AssertBaseThis(0); AssertVarMem(pxyz);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    const auto position=EngineActorMotion::Point(*pxyz);
    three_mm_actor_subroute_position_body(&state,_track.Route(),_track.Events(),&position,&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Update the orientation matrices
    _state.transform.bmat34Cur must be kept current

***************************************************************************/
void Actor::_MatrixRotUpdate(RoutePoint *pxyz, BMAT34 *pbmat34)
{
    AssertBaseThis(0);
    ActorPoseInputs inputs;
    _ptmpl->GetRestOrien(&inputs.restX, &inputs.restY, &inputs.restZ);
    inputs.fixedPathOrientation = _ptmpl->FIsTdt();
    ActorPoseEvaluator::Update(_state, _track, inputs, pxyz, pbmat34);
}

/***************************************************************************

    Compute the angles of rotation defined by the vector *pxyz
    Apply the rotation to matrix *pbmat34
    Return the angles of rotation in *pxa, *pya, *pza
    Return a mask of the axes rotated

    Note: Banking can be done by approximating the second derivative
    which can be done using additional arguments.

***************************************************************************/
void Actor::_ApplyRotFromVec(RoutePoint *pxyz, BMAT34 *pbmat34, BRA *pxa, BRA *pya, BRA *pza, LegacyUlong *pgrfbra)
{
    AssertBaseThis(0);
    ActorPoseEvaluator::ApplyVectorRotation(_state.actionFlags, pxyz, pbmat34, pxa, pya, pza, pgrfbra);
}

/***************************************************************************

    Get the actor's event lifetime
    Update nfrm values for all events
    Return false if the actor has not yet been placed in time
    pnfrmLast may be pvNil

***************************************************************************/
bool Actor::FGetLifetime(long *pnfrmFirst, long *pnfrmLast)
{
    AssertThis(0);
    AssertNilOrVarMem(pnfrmFirst);
    AssertNilOrVarMem(pnfrmLast);

    if (klwMax == _nfrmFirst)
        return fFalse;

    if (pvNil != pnfrmFirst)
        *pnfrmFirst = _nfrmFirst;

    if (pvNil == pnfrmLast)
        return fTrue;

    if (!_fLifeDirty)
    {
        *pnfrmLast = _nfrmLast;
        Assert(_nfrmLast >= _nfrmFirst, "fLifeDirty incorrect");
        return fTrue;
    }

    if (!_FComputeLifetime(pnfrmLast))
        PushErc(ercSocBadFrameSlider);

    return fTrue;
}

/***************************************************************************

    Compute the actor's event lifetime
    Update nfrm values for all events.	Recording or rerecording pushes
    out later subroutes, whereas recording off screen followed by an earlier
    add does not - note therefore that when roll calling invisible actors,
    the end of the existing subroute must first be truncated.

    On a false return, it is possible that the template could not be
    accessed, so the update is incomplete.

    pnfrmLast may be pvNil
    NOTE: Now that by spec, edits do not cross subroute boundaries,
    performance optimization can be achieved by storing the
    index if the first aev edited when _fLifeDirty is set.

    Note: There is some amount of redundant checking here now
    that subroutes are now step=0 terminated.  Zero termination implies that
    fEndSubRoute should be false, as actors should not be attempting to move
    past the end of their paths.  Note that FIsStalled() now flags the end
    of step=0 terminated subroutes; stalls mid path should now be impossible
    as by spec, motion fill now inherits the original movement along the path.

***************************************************************************/
bool Actor::_FComputeLifetime(long *pnfrmLast)
{
    AssertBaseThis(0); AssertNilOrVarMem(pnfrmLast);
    const auto services=_SubrouteEditServices();
    ThreeMMI32 last=0;
    bool ok=three_mm_actor_subroute_lifetime(_track.Route(),_track.Events(),pnfrmLast?&last:nullptr,&services)!=0;
    if (ok && pnfrmLast) *pnfrmLast=last;
    return ok;
}

/***************************************************************************

    An actor is said to be stalled if more route exists, the step size is
    ever zero with no subsequent nonzero step size to follow.

    Determine whether an actor is stalled on a subroute
    Input: IaevFirst is	the first event to check on the subroute.
        *prtel is the route point to test for stalling
    Return fFalse if not stalled.
    If stalled, return the last active event.
***************************************************************************/
bool Actor::_FIsStalled(long iaevFirst, RouteLocation *prtel, long *piaevLast)
{
    AssertBaseThis(0);
    LegacyLong lastEvent;
    const bool stalled = ActorTimeline::FIsStalled(_track, iaevFirst, prtel, piaevLast ? &lastEvent : pvNil);
    if (stalled && piaevLast)
        *piaevLast = lastEvent;
    return stalled;
}

/***************************************************************************

    Has the mouse been down long enough or moved far enough to qualify this
    as a valid recording session (vs merely motion filling)?

    NOTE:  ** If sufficient time has not elapsed, intended motion fill will
    inadvertently delete the remaining subroute	is record is selected
    instead of rerecord.

    NOTE:  ** This requires that SetTsInsert() have been called on mouse
    down to initialize the timing of the recording session

***************************************************************************/
ThreeMMActorRecording Actor::_RecordingState() const
{
    ThreeMMActorRecording record{};
    std::memcpy(&record.insertion,&_rtelInsert,sizeof(record.insertion));
    std::memcpy(&record.raw,&_dxyzRaw,sizeof(record.raw));
    record.timestamp=_tsInsert; record.recording=_fModeRecord; record.rejoining=_fRejoin;
    record.inserted=_fPathInserted; record.gap=_dnfrmGap;
    return record;
}
void Actor::_PublishRecordingState(const ThreeMMActorRecording &record)
{
    std::memcpy(&_rtelInsert,&record.insertion,sizeof(_rtelInsert));
    std::memcpy(&_dxyzRaw,&record.raw,sizeof(_dxyzRaw));
    _tsInsert=record.timestamp; _fModeRecord=record.recording!=0; _fRejoin=record.rejoining!=0;
    _fPathInserted=record.inserted!=0; _dnfrmGap=record.gap;
}
ThreeMMActorRecordServices Actor::_RecordServices()
{
#if !defined(BUG1961) || !defined(BUG1973)
#error C89 actor recording requires the enabled retail BUG1961/BUG1973 policies
#endif
    static_assert(kdtsThreshRte==750 && kdwrThreshRte==131072 && kdwrMax==2147418112 &&
                  fmafFreeze==THREE_MM_RECORD_FREEZE && fmafGround==THREE_MM_RECORD_GROUND && sizeof(ulong)==4,
                  "Actor recording ABI");
    ThreeMMActorRecordServices s{}; s.subroute=_SubrouteEditServices();
    s.reserve_points=[](void *route,ThreeMMU32 count) { return int(static_cast<PDynamicArray>(route)->FEnsureSpace(count,fgrpNil)); };
    s.distance=[](void *,const ThreeMMActorPoint *point) { return ThreeMMI32(BR_LENGTH3(point->dxr,point->dyr,point->dzr)); };
    s.publish_record=[](void *p,const ThreeMMActorRecording *record) { static_cast<Actor *>(p)->_PublishRecordingState(*record); };
    s.restore=[](void *p,void *undo,ThreeMMActorState *state,ThreeMMActorRecording *record) {
        auto actor=static_cast<Actor *>(p); actor->_RestoreFromUndo(static_cast<PActor>(undo));
        *state=EngineActorMotion::Core(actor->_state); *record=actor->_RecordingState();
    };
    s.seek_scene=[](void *p,ThreeMMActorState *state,ThreeMMActorRecording *record,ThreeMMI32 frame) {
        auto actor=static_cast<Actor *>(p); bool ok=actor->_pscen->FGotoFrm(frame);
        *state=EngineActorMotion::Core(actor->_state); *record=actor->_RecordingState(); return int(ok);
    };
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case THREE_MM_RECORD_BAD_LIFETIME: PushErc(ercSocBadFrameSlider); break;
        case THREE_MM_RECORD_UNFREEZE_FAILED: case THREE_MM_RECORD_FREEZE_FAILED: Bug("Ensurespace should have ensured space"); break;
        case THREE_MM_RECORD_STEP_FAILED: Bug("EnsureSpace insufficient"); break;
        }
    };
    return s;
}
bool Actor::FIsRecordValid(BRS dxr, BRS dyr, BRS dzr, ulong tsCurrent)
{
    AssertThis(0);
    const auto record=_RecordingState();
    const auto services=_RecordServices();
    const ThreeMMActorPoint delta{dxr,dyr,dzr};
    return three_mm_actor_record_valid(&record,&delta,tsCurrent,&services)!=0;
}

/***************************************************************************

    Begin a new route
    fReplace = ftrue if recording, fFalse if rerecording
    On failure, actor is restored from pactrRestore

***************************************************************************/
bool Actor::FBeginRecord(ulong tsCurrent, bool fReplace, PActor pactrRestore)
{
    AssertThis(0);
    (void)tsCurrent; // Native beginning deliberately preserves _tsInsert.
    auto state=EngineActorMotion::Core(_state); auto record=_RecordingState();
    const auto tables=EngineActorMotion::Services(); const auto services=_RecordServices();
    bool ok=three_mm_actor_record_begin(&state,&record,_track.Route(),_track.Events(),fReplace,pactrRestore,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); _PublishRecordingState(record); return ok;
}

/***************************************************************************

    Add to a new route

    On Input:
    grfmaf flags the Frozen/unfrozen state.
    *pfStepFrm returns whether the frame was advanced
    *pfStepRte returns whether the route was extended.

    Note: During recording, dwr = rZero marks the end of the new subroute
    NOTE: On failure, actor is restored from pactrRestore

***************************************************************************/
bool Actor::FRecordMove(BRS dxr, BRS dyr, BRS dzr, ulong grfmaf, ulong tsCurrent, bool *pfStepFrm, bool *pfStepRte,
                       PActor pactrRestore)
{
    AssertThis(0); AssertVarMem(pfStepFrm); AssertVarMem(pfStepRte);
    Assert(_state.onStage,"Recording an actor that wasn't selectable??");
    auto state=EngineActorMotion::Core(_state); auto record=_RecordingState();
    const auto tables=EngineActorMotion::Services(); const auto services=_RecordServices();
    const ThreeMMActorPoint delta{dxr,dyr,dzr}; ThreeMMActorRecordResult result{};
    struct Outputs { bool *frame,*route; } outputs{pfStepFrm,pfStepRte};
    result.context=&outputs;
    result.publish=[](void *p,int frame,int route,unsigned changed) {
        auto out=static_cast<Outputs *>(p);
        if ((changed&1) && out->frame) *out->frame=frame!=0;
        if ((changed&2) && out->route) *out->route=route!=0;
    };
    bool ok=three_mm_actor_record_move(&state,&record,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                       &delta,grfmaf,tsCurrent,&result,pactrRestore,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); _PublishRecordingState(record);
    return ok;
}

/***************************************************************************

    End recording a new route or route section	(mouse up)
    Insert a freeze event if at the end of the subroute.

    Input: fReplace is meaningful only if recording or rerecording (versus
    motion fill.  _fModeRecord is false on motion fill).
    fReplace = fTrue if recording, ie, if replacing the remaining subroute

    Path splicing rule:  Never rejoin across Add events
    Always reconnect to the existing subroute at the closest point which
    occurs later in the route than where the initial re-recording began.

    NOTE** Actn, Tweak, Freeze, Step & Rem events are deleted from the
    section of the route being replaced.  Other events are pushed up to
    the join point (if rejoining) or to the next dnfrm of the current point,
    if not rejoining.

    On Failure: The actor is restored from pactrRestore
***************************************************************************/
// REVIEW *****(*****): Ver 2.0 As motion fill is self sufficient,
// 		FEndRecord() should be able to exit immediately on !_fModeRecord.
//   	Also, pactrRestore should be state var _pactrRecordRestore.
bool Actor::FEndRecord(bool fReplace, PActor pactrRestore)
{
    AssertThis(0);
    auto state=EngineActorMotion::Core(_state); auto record=_RecordingState();
    const auto tables=EngineActorMotion::Services(); const auto services=_RecordServices();
    bool ok=three_mm_actor_record_end(&state,&record,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                      fReplace,pactrRestore,&tables,&services)!=0;
    EngineActorMotion::Publish(_state,state); _PublishRecordingState(record);
    if (ok) { AssertThis(fobjAssertFull); }
    return ok;
}

/***************************************************************************

    Delete the path and events beyond this frame - and -
    Insert a Freeze event to terminate the truncated event list

***************************************************************************/
void Actor::DeleteFwdCore(bool fDeleteAll, bool *pfAlive, long iaevCur)
{
    AssertThis(0);
#ifndef BUG1870
#error C89 route editing requires the enabled retail BUG1870 policy
#endif
#ifndef BUG1932
#error C89 route editing requires the enabled retail BUG1932 policy
#endif
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    int alive=0;
    three_mm_actor_route_edit_delete_forward(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,fDeleteAll,pfAlive?&alive:nullptr,iaevCur,&tables,&services);
    EngineActorMotion::Publish(_state,state);
    if (pfAlive) *pfAlive=alive!=0;
}

/***************************************************************************

    Delete the path and events beyond this frame
    **NOTE:  This does not send the actor offstage.	See FRemFromStageCore.
    NOTE: While _fLifeDirty flags needed lifetime recomputation, scene's
    InvalFrmRange() is _not_ called due to the special artifact of the
    frame slider being spec'd to show extensions but not reductions.

    On Input:
        fDeleteAll specifies full route vs subroute deletion
        If one frame was backed up before the delete, iaevCur is the
        current event before the backup (else ivNil).
    Returns *pfAlive = false if all events and route for this actor
    have been deleted

***************************************************************************/
void Actor::_DeleteFwdCore(bool fDeleteAll, bool *pfAlive, long iaevCur)
{
    AssertThis(0);
#ifndef BUG1870
#error C89 route editing requires the enabled retail BUG1870 policy
#endif
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    int alive=0;
    three_mm_actor_route_edit_delete_forward_core(&state,_track.Route(),_track.Events(),fDeleteAll,pfAlive?&alive:nullptr,iaevCur,&tables,&services);
    EngineActorMotion::Publish(_state,state);
    if (pfAlive) *pfAlive=alive!=0;
}

/***************************************************************************

    Save the path specific part of the current orientation

***************************************************************************/
void Actor::_SaveCurPathOrien(void)
{
    AssertBaseThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    three_mm_actor_subroute_save_orientation(&state,_track.Route(),_track.Events(),&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Delete the path and events prior to this frame

***************************************************************************/
void Actor::DeleteBackCore(bool *pfAlive)
{
    AssertThis(0); AssertNilOrVarMem(pfAlive);
#if !defined(BUG1870) || !defined(BUG1866)
#error C89 backward deletion requires enabled retail BUG1870/BUG1866 policies
#endif
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    int alive=0;
    three_mm_actor_subroute_delete_back(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
                                        pfAlive?&alive:nullptr,&tables,&services);
    EngineActorMotion::Publish(_state,state);
    if (pfAlive) *pfAlive=alive!=0;
}

/***************************************************************************

    Truncate the last linear section of the subroute.
    Make the current point a node.

    Adjust event entries from the current rtel through events located
    at irptDelLim.
    Note:  This truncates the route only, not the event list.

***************************************************************************/
void Actor::_TruncateSubRte(long irptDelLim)
{
    AssertBaseThis(0);
    auto state=EngineActorMotion::Core(_state);
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    three_mm_actor_route_edit_truncate(&state,_track.Route(),_track.Events(),irptDelLim,&tables,&services);
    EngineActorMotion::Publish(_state,state);
}

/***************************************************************************

    Is the mouse point within this actor.

***************************************************************************/
bool Actor::FPtIn(long xp, long yp, long *pibset)
{
    AssertThis(0);
    AssertVarMem(pibset);

    return BodyWorld::FPtInBody(_pbody, _pscen->Pmvie()->Pbwld(), xp, yp, pibset);
}

/***************************************************************************

    FMustRender.  Optimizaton: Is Rerendering necessary between _nfrmCur and
    nfrmLast, exclusive of the current frame?
    Returns fFalse only when rerendering *known* to be unnecessary.
    Otherwise returns fTrue.

***************************************************************************/
bool Actor::FMustRender(long nfrmRenderLast)
{
    AssertThis(0);
    Assert(nfrmRenderLast >= _state.frame, "Invalid argument to FMustRender");
    Assert(!_fLifeDirty, "FMustRender was called when nfrm values were invalid");

    Base *paev;
    long iaev;

    if (nfrmRenderLast == _state.frame)
        goto LStill;
    if (nfrmRenderLast < _nfrmFirst)
        goto LStill;
    if (_state.frame > _nfrmLast)
        goto LStill;
    if (_track.Route()->IvMac() == 0)
        goto LStill;

    // Intervening events?	Sounds don't affect rendering.
    if (_state.nextEvent < _track.Events()->IvMac())
    {
        paev = (Base *)_track.Events()->QvFixedGet(_state.nextEvent);
        if (paev->nfrm <= nfrmRenderLast)
        {
            for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
            {
                paev = (Base *)_track.Events()->QvFixedGet(iaev);
                if (paev->nfrm > nfrmRenderLast)
                    break;
                if (aetSnd == paev->aet)
                    continue;
                if (paev->nfrm < _nfrmLast)
                    goto LMoving;
                // Freeze and step events affect future frames
                if (aetFreeze != paev->aet && aetStep != paev->aet)
                    goto LMoving;
            }
        }
    }

    if (_state.step != rZero)
        goto LMoving; // moving along path

    // Not advancing, but moving in place?
    if (_state.frozen || _state.celCount == 1)
        goto LStill;
LMoving:
    return fTrue;

LStill:
    return fFalse;
}

/***************************************************************************

    Get actor name

***************************************************************************/
void Actor::GetName(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    if (!_pscen->Pmvie()->FGetName(_arid, pstn))
    {
        Ptmpl()->GetName(pstn);
    }
}

/***************************************************************************

    Get actor world coordinates

***************************************************************************/
void Actor::GetXyzWorld(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertNilOrVarMem(pxr);
    AssertNilOrVarMem(pyr);
    AssertNilOrVarMem(pzr);

    RoutePoint xyz;

    _GetXyzFromRtel(&_state.location, &xyz);

    if (pvNil != pxr)
        *pxr = BrsAdd(xyz.dxr, _state.routeTranslation.dxr);
    if (pvNil != pyr)
        *pyr = BrsAdd(xyz.dyr, _state.routeTranslation.dyr);
    if (pvNil != pzr)
        *pzr = BrsAdd(xyz.dzr, _state.routeTranslation.dyr);
}

/***************************************************************************

    Hilite the actor

***************************************************************************/
void Actor::Hilite(void)
{
    AssertThis(0);

    Body::SetHiliteColor(_fTimeFrozen ? kiclrTimeFreezeHilite : kiclrNormalHilite);
    _pbody->Hilite();
}

/***************************************************************************

    Change this actor's template.  This gets called if the actor is based
    on a 3-D Text template and the text gets edited.  In addition to
    changing the template itself, this function updates _pbody to conform
    to the new template, and removes all costume events on body part sets
    that no longer exist.  For instance, if a three-letter text actor was
    changed to two letters, all costume events on the third letter are
    deleted.

***************************************************************************/
bool Actor::FChangeTagTmpl(TAG *ptagTmplNew)
{
    AssertThis(0);
    AssertVarMem(ptagTmplNew);

    PTemplate ptmpl;
    long cbsetNew;
    long iaev;
    Base aev;
    Costume aevcost;

    ptmpl = (PTemplate)vptagm->PbacoFetch(ptagTmplNew, Template::FReadTmpl);
    if (pvNil == ptmpl)
        return fFalse;
    if (ptmpl->FIsTdt())
    {
        if (!((PThreeDText)ptmpl)->FAdjustBody(_pbody))
        {
            ReleasePpo(&ptmpl);
            return fFalse;
        }
    }
    if (!ptmpl->FSetActnCel(_pbody, _state.action, _state.cel))
        return fFalse;
    _PositionBody(&_state.position);
    ReleasePpo(&_ptmpl);
    _ptmpl = ptmpl;
    TagManager::CloseTag(&_tagTmpl);
    _tagTmpl = *ptagTmplNew;
    TagManager::DupTag(ptagTmplNew);

    // If the new Template is not a ThreeDText, we're done...although currently this
    // function should only be called with ptagTmplNew being a ThreeDText
    if (!ptmpl->FIsTdt())
        return fTrue;

    cbsetNew = _pbody->Cbset();
    // Need to remove any costume events acting on ibset >= cbsetNew
    // Loop backwards to prevent indexing problems since we are deleting
    // events in this GeneralGroup as we go
    for (iaev = _track.Events()->IvMac() - 1; iaev >= 0; iaev--)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.aet == aetCost)
        {
            _track.Events()->Get(iaev, &aevcost);
            if (aevcost.ibset >= cbsetNew)
            {
                _RemoveAev(iaev, fTrue);
            }
        }
    }
    return fTrue;
}

#ifdef DEBUG
/***************************************************************************

    Assert the validity of the Actor.

***************************************************************************/
void Actor::AssertValid(LegacyUlong grfobj)
{

    Actor_PAR::AssertValid(fobjAllocated);
    AssertNilOrPo(_pbody, 0);
    AssertNilOrPo(_ptmpl, 0);
    AssertPo(_track.Events(), 0);
    AssertPo(_track.Route(), 0);
    Assert(_motionSounds.Valid(), "invalid C motion sound storage");

    long iaevMac = _track.Events()->IvMac();
    long irptMac = _track.Route()->IvMac();
    bool fTracing = FPure(grfobj & fobjAssertFull);

    AssertIn(_state.nextEvent, -1, iaevMac + 1);
    if (irptMac > 0)
        AssertIn(_state.location.irpt, 0, irptMac);
    if (ivNil != _state.actionEvent)
        AssertIn(_state.actionEvent, 0, iaevMac);
    if (_state.addEvent != ivNil)
        AssertIn(_state.addEvent, 0, iaevMac);

    if (fTracing)
    {
        RouteDistancePoint rpt;
        long irpt;
        long iaev;
        Base aev;
        bool mpaetfSeen[aetLim];
        RouteLocation rtel;

        ClearPb(mpaetfSeen, size(mpaetfSeen));

        rtel.irpt = -1;
        rtel.dwrOffset = rZero;
        rtel.dnfrm = 0;

        // Supply a debug readable view of br_scalar path
        for (irpt = 0; irpt < irptMac; irpt++)
        {
            _track.Route()->Get(irpt, &rpt);
        }

        AssertIn(_state.location.irpt, 0, irptMac + 1); // Supply a debug readable view of the event stream
        _track.Events()->GetFixed(0, &aev);
        Assert(aetAdd == aev.aet, "BUG: No add event at front of list");

        for (iaev = 0; iaev < _track.Events()->IvMac(); iaev++)
        {
            _track.Events()->GetFixed(iaev, &aev);
            Assert(rtel <= aev.rtel, "Illegal ordering in event list");
            AssertIn(aev.aet, 0, aetLim);

            // Verify uniqueness of event types in a single frame
            bool fNewFrame = (aev.aet == aetAdd || rtel != aev.rtel);

            if (mpaetfSeen[aev.aet] == fTrue)
            {
                switch (aev.aet)
                {
                case aetCost:
                case aetAdd:
                case aetSnd:
                    break;

                default:
                    Assert(fNewFrame, "Duplicate events in a single frame");
                }
            }

            if (fNewFrame)
            {
                ClearPb(mpaetfSeen, size(mpaetfSeen));
            }

            mpaetfSeen[aev.aet] = fTrue;
            rtel = aev.rtel;
            _track.Route()->Get(aev.rtel.irpt, &rpt);
            Assert(aev.rtel.dwrOffset < rpt.dwr || rpt.dwr == rZero, "Invalid rtel.dwrOffset");

            // Variable portion of aev retrieved for debug viewing
            switch (aev.aet)
            {
            case aetAdd: {
                Add aevadd;
                Assert(_track.Events()->Cb(iaev) == kcbVarAdd, "Corrupt size in event list");
                _track.Events()->Get(iaev, &aevadd);
                break;
            }
            case aetActn: {
                Action aevactn;
                _track.Events()->Get(iaev, &aevactn);
                Assert(_track.Events()->Cb(iaev) == kcbVarActn, "Corrupt size in event list");
                break;
            }
            case aetCost:
                Assert(_track.Events()->Cb(iaev) == kcbVarCost, "Corrupt size in event list");
                break;
            case aetRotF:
            case aetRotH:
                Assert(_track.Events()->Cb(iaev) == kcbVarRot, "Corrupt size in event list");
                break;
            case aetSize:
                Assert(_track.Events()->Cb(iaev) == kcbVarSize, "Corrupt size in event list");
                break;
            case aetPull:
                Assert(_track.Events()->Cb(iaev) == kcbVarPull, "Corrupt size in event list");
                break;
            case aetSnd:
                break;
            case aetFreeze: {
                int32_t faevfrz;
                _track.Events()->Get(iaev, &faevfrz);
                Assert(_track.Events()->Cb(iaev) == kcbVarFreeze, "Corrupt size in event list");
                break;
            }
            case aetTweak:
                Assert(_track.Events()->Cb(iaev) == kcbVarTweak, "Corrupt size in event list");
                break;
            case aetStep: {
                BRS dwrAev;
                _track.Events()->Get(iaev, &dwrAev);
                Assert(_track.Events()->Cb(iaev) == kcbVarStep, "Corrupt size in event list");
                break;
            }
            case aetRem:
                break;
            case aetMove:
                Assert(_track.Events()->Cb(iaev) == kcbVarMove, "Corrupt size in event list");
                break;
            default:
                Assert(0, "Unknown event type");
            }
        }
    }
}

/***************************************************************************

    Mark memory used by the Actor

***************************************************************************/
void Actor::MarkMem(void)
{
    AssertThis(0);
    long iaev;
    Base *paev;
    Sound *paevsnd;

    Actor_PAR::MarkMem();
    for (iaev = 0; iaev < _track.Events()->IvMac(); iaev++)
    {
        paev = (Base *)_track.Events()->QvFixedGet(iaev);
        if (aetSnd == paev->aet)
        {
            paevsnd = (Sound *)_track.Events()->QvGet(iaev);
            if (paevsnd->tag.sid == ksidUseCrf)
                paevsnd->tag.MarkMem();
        }
    }
    MarkMemObj(_track.Events());
    MarkMemObj(_track.Route());
    MarkMemObj(_pbody);
    MarkMemObj(_ptmpl);
    _tagTmpl.MarkMem();
}

#endif // DEBUG

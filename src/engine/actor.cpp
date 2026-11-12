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

ASSERTNAME

using namespace ActorEvent;

RTCLASS(Actor)

/***************************************************************************

    Constructor for Actor - private.

***************************************************************************/
Actor::Actor(void)
{
    _arid = aridNil;
    _tagTmpl.sid = ksidInvalid;
    _tagSnd.sid = ksidInvalid;
    _state.frame = _nfrmFirst = knfrmInvalid;
    _nfrmLast = klwMin;
    _fLifeDirty = fFalse;
}

/***************************************************************************

    Destructor for an Actor

***************************************************************************/
Actor::~Actor(void)
{
    AssertBaseThis(0);

    _CloseTags();
    ReleasePpo(&_pbody);
    _track.Clear();
    ReleasePpo(&_pglsmm);
    ReleasePpo(&_ptmpl);
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

    Create the groups _track.Events(), _track.Route() and _pglsmm

***************************************************************************/
bool Actor::_FCreateGroups(void)
{
    AssertBaseThis(0);

    if (!_track.FCreate())
        return fFalse;

    if (pvNil == (_pglsmm = DynamicArray::PglNew(size(SoundMotionMatch), kcsmmGrow)))
        return fFalse;
    _pglsmm->SetMinGrow(kcsmmGrow);

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

    _Hide();
    _fLifeDirty = fTrue;
    if (_ptmpl != pvNil && _pbody != pvNil)
        _ptmpl->FSetDefaultCost(_pbody);
    _SetStateRewound();
}

/***************************************************************************

    Set the actor state variables to the rewound position

***************************************************************************/
void Actor::_SetStateRewound(void)
{
    AssertBaseThis(0);
    _fModeRecord = fFalse;
    _state.RewindRoute(_track);
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
    AssertBaseThis(0);
    AssertVarMem(pdwr);

    *pdwr = rZero;

    if (!_ptmpl->FGetDwrActnCel(_state.action, _state.cel, pdwr))
        return fFalse;

    *pdwr = BrsMul(*pdwr, _state.transform.rScaleStep);

    return fTrue;
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

    Base aev;
    BodyCostume cost;

    _track.Events()->GetFixed(iaev, &aev);
    if (aev.nfrm != _state.frame)
    {
        aev.nfrm = _state.frame;
        _track.Events()->PutFixed(iaev, &aev);
    }
    AssertIn(aev.aet, 0, aetLim);

    ActorValueEventResult result = _state.ApplyValueEvent(_track, iaev, _dxyzFullRte);
    if (result != ActorValueEventResult::NeedsRuntime)
        return result == ActorValueEventResult::Applied;

    switch (aev.aet)
    {
    case aetActn:
        Action aevactn;
        LegacyUlong grfactn;

        _track.Events()->Get(iaev, &aevactn);

        // Empty the motion match sound list	if the action <changed>
        if (aevactn.anid != _state.action)
            _pglsmm->FSetIvMac(0);

        if (!_ptmpl->FSetActnCel(_pbody, aevactn.anid, aevactn.celn))
        {
            return fFalse;
        }
        _state.cel = aevactn.celn;
        _state.action = aevactn.anid;
        if (!_ptmpl->FGetCcelActn(_state.action, &_state.celCount)) // Cache the cel count
            return fFalse;
        _state.actionEvent = iaev;

        // Force the location to the step event
        // Avoids incorrect event ordering on static segments
        _state.location = aev.rtel;
        _GetXyzFromRtel(&_state.location, &_state.position);
        if (!_ptmpl->FGetGrfactn(_state.action, &grfactn))
        {
            return fFalse;
        }
        _state.actionFlags = grfactn;
        break;

    case aetAdd:
        Add aevadd;
        RouteDistancePoint rpt;
        // Save old costume in case of error
        if (!cost.FGet(_pbody))
            return fFalse;
        // Invoke the default costume/orientation
        if (!_ptmpl->FSetDefaultCost(_pbody))
            return fFalse;
        // Put the actor on stage.	Set up models.
        if (!_ptmpl->FSetActnCel(_pbody, _state.action, _state.cel))
        {
            cost.Set(_pbody); // restore old costume
            return fFalse;
        }

        // Empty the motion match sound list
        _pglsmm->FSetIvMac(0);

        // Set the translation for the subroute
        _track.Events()->Get(iaev, &aevadd);
        _state.subrouteTranslation.dxr = aevadd.dxr;
        _state.subrouteTranslation.dyr = aevadd.dyr;
        _state.subrouteTranslation.dzr = aevadd.dzr;
        _UpdateXyzRte();

        // Load the initial orientation
        _InitXfrm();
        _LoadAddOrien(&aevadd);

        // Set state variables
        _state.firstEventInFrame = _state.addEvent = iaev;
        _state.location = aev.rtel;
        _GetXyzFromRtel(&_state.location, &_state.position);
        _track.Route()->Get(aev.rtel.irpt, &rpt);

        // Show the actor
        if (!_state.onStage)
            _pbody->Show();
        _state.onStage = fTrue;
        break;

    case aetRem:
        // Exit the actor from the stage
        _Hide();
        break;

    case aetCost:
        Costume aevcost;
        _track.Events()->Get(iaev, &aevcost);
        if (aevcost.fCmtl)
        {
            PCustomMaterial_CMTL pcmtl = _ptmpl->PcmtlFetch(aevcost.cmid);
            if (pvNil == pcmtl)
                return fFalse;
            _pbody->SetPartSetCmtl(pcmtl);
            ReleasePpo(&pcmtl);
        }
        else
        {
            PMaterial_MTRL pmtrl;
            pmtrl = (PMaterial_MTRL)vptagm->PbacoFetch(&aevcost.tag, Material_MTRL::FReadMtrl);
            if (pvNil == pmtrl)
                return fFalse;
            _pbody->SetPartSetMtrl(aevcost.ibset, pmtrl);
            ReleasePpo(&pmtrl);
        }
        break;

    case aetSnd:
        // Enqueue non-mm sounds
        if (!_FEnqueueSnd(iaev)) // Ignore failure
            return fFalse;
        break;

    default:
        Bug("Unimplemented actor event");
    }

    return fTrue;
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
    AssertIn(aetNew, 0, aetLim);
    AssertIn(cbNew, 0, 100); // approximate upper bound
    AssertPvCb(pvVar, cbNew);
    Assert(_state.onStage || aetNew == aetAdd, "Error!  Beginning subroute with no Add Onstage event");

    Base aev;
    long iaevNew;
    // Setup fixed part of the gg
    aev.aet = aetNew;
    aev.rtel = _state.location;
    aev.nfrm = _state.frame;

    if (!_FInsertAev(_state.nextEvent, cbNew, pvVar, &aev))
        return fFalse;
    _MergeAev(_state.firstEventInFrame, _state.nextEvent, &iaevNew);

#ifdef BUG1870
    // REVIEW *****: V2.0
    //		Though the only situation in which this arises is aetMove,
    //		it might be that the FDoAevCore() should be called before
    //		merging events (so that any cumulative changes get executed
    //		only once, without requiring this special casing of aetMove.
    if (aetMove == aetNew)
    {
        // Skip the "do" of the FAddDoAev().
        // The 'Do' part currently merges events before
        // executing them, which in this case would cause
        // any existing same frame translaton to be added
        // to the state	variables twice.
        // Instead, adjust state var translation here.
        _state.subrouteTranslation.dxr = BrsAdd(((RoutePoint *)pvVar)->dxr, _state.subrouteTranslation.dxr);
        _state.subrouteTranslation.dyr = BrsAdd(((RoutePoint *)pvVar)->dyr, _state.subrouteTranslation.dyr);
        _state.subrouteTranslation.dzr = BrsAdd(((RoutePoint *)pvVar)->dzr, _state.subrouteTranslation.dzr);
    }
    else if (!_FDoAevCore(iaevNew))
        return fFalse;
#else  //! BUG1870
    if (!_FDoAevCore(iaevNew))
        return fFalse;
#endif //! BUG1870

    if (_state.nextEvent == iaevNew)
        _state.nextEvent++;

    _pscen->MarkDirty();

    Assert(aetNew != aetActn || _state.actionEvent == iaevNew, "_state.actionEvent not up to date");
    AssertIn(_state.nextEvent, 0, _track.Events()->IvMac() + 1);
    Assert(!(_track.Events()->IvMac() == 1 && _nfrmFirst != _state.frame), "check case");

    if (_state.frame < _nfrmFirst)
    {
        _pscen->InvalFrmRange();
        _nfrmFirst = _state.frame;
    }

    if (_state.frame > _nfrmLast)
    {
        _pscen->InvalFrmRange();
        _nfrmLast = _state.frame;
    }

    AssertThis(fobjAssertFull);
    return fTrue;
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

    Base aev;
    Base aevNew;
    void *pvVar;
    LegacyLong cbVar;
    long iaev;
    BMAT34 bmat34;

    if ((_track.Events()->IvMac() < iaevFirst) || (iaevFirst == iaevNew))
    {
        if (pvNil != piaevRtn)
            *piaevRtn = iaevNew;
        return;
    }

    _track.Events()->GetFixed(iaevNew, &aevNew);

    //
    // Check if Aev is in the list already
    //
    for (iaev = iaevFirst; iaev < iaevNew; iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);

        if ((aev.aet != aevNew.aet) && (aev.aet != aetRem) && (aev.aet != aetRotH))
            continue;

        switch (aev.aet)
        {
        case aetRem:
            if (aevNew.aet == aetAdd && aevNew.nfrm != aev.nfrm)
            {
                _RemoveAev(iaev);
                iaevNew--;
                iaev--;
            }
            break;

        case aetAdd:
            // We cannot remove events associated with a same-frame Add event
            // or we will not retain costume etc information to propogate
            // forward (eg drag single pt actor offstage & roll call back)
            continue;
            break;

        case aetMove: // Move is accumulation of previous moves
            RoutePoint dxyz;
            RoutePoint dxyzNew;
            _track.Events()->Get(iaev, &dxyz);
            _track.Events()->Get(iaevNew, &dxyzNew);
            // Note: Merging moves should not alter state variables!
#ifndef BUG1870
            // Remove these lines of code
            _state.subrouteTranslation.dxr = BrsSub(_state.subrouteTranslation.dxr, dxyz.dxr);
            _state.subrouteTranslation.dyr = BrsSub(_state.subrouteTranslation.dyr, dxyz.dyr);
            _state.subrouteTranslation.dzr = BrsSub(_state.subrouteTranslation.dzr, dxyz.dzr);
#endif //! BUG1870
            dxyz.dxr = BrsAdd(dxyz.dxr, dxyzNew.dxr);
            dxyz.dyr = BrsAdd(dxyz.dyr, dxyzNew.dyr);
            dxyz.dzr = BrsAdd(dxyz.dzr, dxyzNew.dzr);
            _track.Events()->Put(iaev, &dxyz);
            goto LDeleteNew;
            break;

        case aetCost:
            Costume aevcost;
            Costume aevcostNew;

            // Check that the body parts match
            _track.Events()->Get(iaev, &aevcost);
            _track.Events()->Get(iaevNew, &aevcostNew);

            if (aevcost.ibset != aevcostNew.ibset)
                continue;

            if (aev.rtel != aevNew.rtel)
                goto LDeleteOld;
            _track.Events()->Put(iaev, &aevcostNew);
            goto LDeleteNew;
            break;

        case aetSnd:
            Sound aevsnd;
            Sound aevsndNew;
            long ismm;
            SoundMotionMatch *psmm;
            // Check that the sound types match
            _track.Events()->Get(iaev, &aevsnd);
            _track.Events()->Get(iaevNew, &aevsndNew);
            if (MovieSoundMSND::SqnActr(aevsnd.sty, _arid) != MovieSoundMSND::SqnActr(aevsndNew.sty, _arid))
                continue;
            // Queued sounds need to have multiple events reside in a single frame
            if (aevsndNew.fQueue)
                continue;
            // Non queued sounds need to replace queued sounds of the same type
            // First, the _pggsmm needs to be updated
            for (ismm = 0; ismm < _pglsmm->IvMac(); ismm++)
            {
                psmm = (SoundMotionMatch *)_pglsmm->QvGet(ismm);
                if (psmm->aevsnd.sty == aevsnd.sty && aevsnd.celn == psmm->aevsnd.celn)
                {
                    _pglsmm->Delete(ismm);
                    break;
                }
            }
            _RemoveAev(iaev);
            iaevNew--;
            iaev--;
            continue;
            break;

        case aetRotH:
            if (aevNew.aet == aetRotF && aevNew.nfrm == aev.nfrm)
            {
                // Forward rotations must get rid of tweak rotations in the current frame
                _RemoveAev(iaev);
                iaevNew--;
                iaev--;
            }
            if (aevNew.aet != aetRotH)
                continue;
            if (aev.rtel != aevNew.rtel)
                goto LDeleteOld;
            _track.Events()->Get(iaevNew, &bmat34);
            _track.Events()->Put(iaev, &bmat34);
            goto LDeleteNew;
            break;

        case aetRotF:
            // New == old == forward-rotate
            // Need to replace the old rotation, but continue on to remove tweak-rotations
            _RemoveAev(iaev);
            iaevNew--;
            iaev--;
            continue;
            break;

        default:
            if (aev.rtel != aevNew.rtel)
            {
                goto LDeleteOld;
            }
            pvVar = _track.Events()->QvGet(iaevNew, &cbVar);
            Assert(cbVar == _track.Events()->Cb(iaev), "Wrong Var size");
            _track.Events()->Put(iaev, pvVar);
            goto LDeleteNew;
        }
    }

    //
    // Leave it where it is.  No match found.
    //
    if (pvNil != piaevRtn)
        *piaevRtn = iaevNew;

    return;

LDeleteNew:
    if (pvNil != piaevRtn)
    {
        *piaevRtn = iaev;
        AssertIn(iaev, iaevFirst, iaevNew);
    }
    _RemoveAev(iaevNew);
    return;

LDeleteOld:
    _RemoveAev(iaev);
    iaevNew--;
    if (pvNil != piaevRtn)
        *piaevRtn = iaevNew;
}

/***************************************************************************

    Add (or replace) an action
    Add the event to the event list

***************************************************************************/
bool Actor::FSetActionCore(long anid, long celn, bool fFreeze)
{
    AssertThis(0);
    AssertIn(anid, 0, klwMax);
    AssertIn(celn, klwMin, klwMax);

    bool fStatic;
    bool fNewAction = fFalse;
    Action aevactn;
    long anidPrev = _state.action;
    long iaevMin;

    long cbVar = kcbVarActn + kcbVarFreeze + (kcbVarStep * 2);
    if (!_track.Events()->FEnsureSpace(4, cbVar, fgrpNil))
        return fFalse;

    if (!_FGetStatic(anid, &fStatic))
        return fFalse;

    // If the action is changing:
    // Remove all motion match sounds of the previous action
    // Query the template and insert new sound events
    if (_state.action != anid)
    {
        fNewAction = fTrue;
        if (!_FRemoveAevMm(anid))
            return fFalse;
    }

    // var part of gg
    aevactn.anid = anid;
    aevactn.celn = celn;

    // Add this action to the event list
    iaevMin = (_state.firstEventInFrame == _state.addEvent) ? _state.firstEventInFrame + 1 : _state.firstEventInFrame;
    _PrepActnFill(iaevMin, _state.action, anid, faetTweak | faetFreeze | faetActn);

    if (!_FAddDoAev(aetActn, kcbVarActn, &aevactn))
        return fFalse;

    // If the action is changing:
    // Query the template and insert new default motion match sound events
    if (fNewAction)
    {
        if (!_FAddAevDefMm(anid))
            return fFalse;
    }

    if (_state.frame != _nfrmLast)
    {
        _fLifeDirty = fTrue;
        _pscen->InvalFrmRange();
    }

    if (!fFreeze && !_FIsDoneAevSub(_state.nextEvent, _state.location))
    {
        if (!_FUnfreeze())
            return fFalse;
    }
    else
    {
        if (!_FFreeze())
            return fFalse;
    }

    // If the actor is in the middle of a static segment, the application of a
    // non-static action is supposed to make the actor start moving forward along
    // the remaining path.
    // The last frame in the subpath must retain its final step=0 event, however.
    if (!fStatic && !_ptmpl->FIsTdt() && !_FIsDoneAevSub(_state.nextEvent, _state.location))
    {
        if (!FSetStep(kdwrNil))
            return fFalse;
    }

    return fTrue;
}

/***************************************************************************

    Remove an actor from the stage
    NOTE: This is a low level API and has no effect on the event list

***************************************************************************/
void Actor::_Hide(void)
{
    AssertBaseThis(0);

    if (_state.onStage && _pbody != pvNil)
        _pbody->Hide();
    _state.onStage = fFalse;
    return;
}

/***************************************************************************

    Insert a node point *prpt in the route.
    Update *prpt.dwr, and the previous node's dwr.
    On input:
        *prpt.dwr == 0 iff the new node is a subpath terminating node
        *prpt.xyz has the new node coordinates
        dwrPrior is the distance from the previous node to the new one.

***************************************************************************/
bool Actor::_FInsertGgRpt(long irpt, RouteDistancePoint *prpt, BRS dwrPrior)
{
    AssertBaseThis(0);
    AssertIn(irpt, 0, _track.Route()->IvMac() + 1);
    AssertNilOrVarMem(prpt);

    RouteDistancePoint rpt;
    BRS dwrTotal = rZero; // Dist from prev node to node after the inserted node

    if (!_track.Route()->FInsert(irpt, prpt))
        return fFalse;

    if (0 < irpt)
    {
        _track.Route()->Get(irpt - 1, &rpt);
        dwrTotal = rpt.dwr;

        // Do not alter end of route dwr's
        if (rZero != rpt.dwr)
        {
            // Adjust the distance from the previous point here
            rpt.dwr = dwrPrior;
            if (rZero == rpt.dwr)
                rpt.dwr = rEps; // Epsilon.  Prevent pathological incorrect end-of-path
            _track.Route()->Put(irpt - 1, &rpt);
        }
        else
            Assert(dwrPrior == rZero, "Illegal distance");
    }

    if (irpt < _track.Route()->IvMac() - 1)
    {
        _track.Route()->Get(irpt + 1, &rpt);
        if (rZero != prpt->dwr) // If not at end of subroute
        {
            // Set the distance to the next point in this subroute
            Assert(rZero != dwrTotal, "Overwriting end of route");
            prpt->dwr = BrsSub(dwrTotal, dwrPrior);
            if (rZero >= prpt->dwr)
                prpt->dwr = rEps; // Epsilon.  Prevent pathological incorrect end-of-path
            _track.Route()->Put(irpt, prpt);
        }
    }
    else
        Assert(rZero == prpt->dwr, "Invalid end sub-node dwr");

    return fTrue;
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
    AssertThis(0);
    Assert(kdwrNil == dwrStep || rZero <= dwrStep, "Invalid dwrStep argument");

    if (!_track.Events()->FEnsureSpace(1, kcbVarStep, fgrpNil))
        return fFalse;

    return _FAddDoAev(aetStep, kcbVarStep, &dwrStep);
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

    BRS xrCam = rZero;
    BRS yrCam = rZero;
    BRS zrCam = kzrDefault;
    BRS zrYon, zrHither;
    BMAT34 bmat34Cam;

    _pscen->Pbkgd()->GetActorPlacePoint(pxr, pyr, pzr);
    // A (0, 0, 0) place point means that one hasn't been authored yet,
    // so use the old system.
    if (*pxr == rZero && *pyr == rZero && *pzr == rZero)
    {
        _pscen->Pmvie()->Pbwld()->GetCamera(&bmat34Cam, &zrHither, &zrYon);

        Assert(zrCam < 0, "Non-negative placement value");
        Assert(zrHither >= 0 && zrYon >= 0, "Negative camera values");
        if (BR_ABS(zrCam) > zrYon)
            zrCam = BR_CONST_DIV(BR_ADD(zrYon, zrHither), -2);

        *pxr =
            BR_MAC3(xrCam, bmat34Cam.m[0][0], yrCam, bmat34Cam.m[1][0], zrCam, bmat34Cam.m[2][0]) + bmat34Cam.m[3][0];
        *pyr = rZero;
        *pzr =
            BR_MAC3(xrCam, bmat34Cam.m[0][2], yrCam, bmat34Cam.m[1][2], zrCam, bmat34Cam.m[2][2]) + bmat34Cam.m[3][2];
    }
    if (_ptmpl->FIsTdt())
        *pyr += BR_SCALAR(10.0); // 1.0 meters
}

/***************************************************************************

    Add actor on the stage - ie, create a new subroute.
    Add the Add event to the event list
    Nukes existing subroute if obsoleted by current Add.

    Add the initialization events for costumes, xforms, etc from the
    preceding or subsequent subroute.
***************************************************************************/
bool Actor::FAddOnStageCore(void)
{
    AssertThis(0);

    BRS xr;
    BRS yr;
    BRS zr;
    LegacyLong cbVar;
    Add aevadd;
    bool fUpdateFrmRange = fFalse;

    cbVar = kcbVarAdd + kcbVarActn + kcbVarStep + kcbVarFreeze;
    if (_track.Events()->IvMac() == 0)
    {
        if (!_track.Events()->FEnsureSpace(4, cbVar, fgrpNil) || !_track.Route()->FEnsureSpace(1, fgrpNil))
            return fFalse;
    }
    else
    {
        cbVar += kcbVarCost + kcbVarRot + kcbVarSize + kcbVarPull;
        if (!_track.Events()->FEnsureSpace(8, cbVar, fgrpNil) || !_track.Route()->FEnsureSpace(1, fgrpNil))
        {
            return fFalse;
        }
    }

    _GetNewOrigin(&xr, &yr, &zr);
    aevadd.dxr = BrsSub(xr, _dxyzFullRte.dxr);
    aevadd.dyr = BrsSub(yr, _dxyzFullRte.dyr);
    aevadd.dzr = BrsSub(zr, _dxyzFullRte.dzr);
    aevadd.xa = aevadd.za = aZero;

    // Rotate the actor to be facing the camera
    // NOTE: 3D spletter code uses this also.
    aevadd.ya = _pscen->Pbkgd()->BraRotYCamera();

    if (_state.frame < _nfrmFirst)
    {
        fUpdateFrmRange = fTrue;
        _nfrmFirst = _state.frame;
    }
    if (_state.frame > _nfrmLast)
    {
        fUpdateFrmRange = fTrue;
        _nfrmLast = _state.frame;
    }

    RouteDistancePoint rptNil = {rZero, rZero, rZero, rZero};

    if (_state.onStage) // May have walked offstage
    {
        // Delete the remnant subroute (non-inclusive of current frame)
        _DeleteFwdCore(fFalse);
    }

    _state.location.dnfrm = 0;
    _state.location.dwrOffset = rZero;
    if (_state.addEvent < 0)
        _state.location.irpt = 0;
    else
        _state.location.irpt++;

    AssertDo(_FInsertGgRpt(_state.location.irpt, &rptNil), "Logic error");
    _AdjustAevForRteIns(_state.location.irpt, 0);

    _GetXyzFromRtel(&_state.location, &_state.position);

    if (!_FAddDoAev(aetAdd, kcbVarAdd, &aevadd))
        return fFalse;

    // Copy costume and transform events forward if this is
    // the earliest Add event : each subroute needs all
    // initialization events.
    // Note: _state.nextEvent is already incremented at this point
    if (1 == _state.nextEvent)
    {
        // The earliest Add.  Gather later events and insert them
        if (!_FAddAevFromLater())
            return fFalse;
    }
    else
    {
        // Not the earliest Add.  Gather earlier events	and insert them
        ulong grfaet = faetActn | faetCost | faetPull | faetSize | faetRotF;
        if (!_FAddAevFromPrev(_state.nextEvent - 1, grfaet))
            return fFalse;
    }

    _PositionBody(&_state.position);

    if (!FSetStep(rZero))
        return fFalse;
    if (!_FFreeze())
        return fFalse;

    if (fUpdateFrmRange)
    {
        _pscen->InvalFrmRange();
    }

    return fTrue;
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
    AssertBaseThis(0);
    AssertIn(iaevLim, 0, _track.Events()->IvMac() + 1);

    Base aev;
    Base *paev;
    Base aevCur;
    LegacyLong cb;
    long iaev;
    long iaevAdd;
    long iaevNew;
    long iaevLast;
    bool fPrunedPrevSubrte = fFalse;

    // Locate the next active (not stalled) region of the subroute
    // Note: Not finding a previous aev is not a failure
    _FFindPrevAevAet(aetAdd, iaevLim, &iaevAdd);
    _FindAevLastSub(iaevAdd, iaevLim, &iaevLast);

    _track.Events()->GetFixed(_state.nextEvent - 1, &aevCur);

    // It is more efficient to insert non-costume events backwards
    for (iaev = iaevLast; iaev > iaevAdd; iaev--)
    {
        if (0 == grfaet)
            break;

        Assert(iaev >= 0, "Logic error");
        _track.Events()->GetFixed(iaev, &aev);
        if (!(grfaet & (1 << aev.aet)) || aev.aet == aetCost)
            continue;

        // Non-costume events are added only once
        grfaet ^= (1 << aev.aet);

        // Allocate space
        cb = _track.Events()->Cb(iaev);
        aev.rtel = aevCur.rtel;
        // aev.nfrm is set by _FDoAevCore()

        if (!_FInsertAev(_state.nextEvent, cb, pvNil, &aev))
            return fFalse;

        // Insert event
        _track.Events()->Put(_state.nextEvent, _track.Events()->QvGet(iaev));

        // Merge events to avoid duplicates
        _MergeAev(_state.firstEventInFrame, _state.nextEvent, &iaevNew);

        if (!_FDoAevCore(iaevNew))
            return fFalse;

        if (iaevNew == _state.nextEvent)
            _state.nextEvent++;
    }

    if (!(grfaet & (1 << aetCost)))
        return fTrue;

    // Costumes needed to be gathered forward
    for (iaev = iaevAdd + 1; iaev <= iaevLast; iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.aet != aetCost)
            continue;

        aev.rtel = aevCur.rtel;
        // aev.nfrm is set by _FDoAevCore()

        // Allocate space
        cb = _track.Events()->Cb(iaev);
        if (!_FInsertAev(_state.nextEvent, cb, pvNil, &aev))
            return fFalse;

        // Insert event
        _track.Events()->Put(_state.nextEvent, _track.Events()->QvGet(iaev));

        // Merge events to avoid duplicates
        _MergeAev(_state.firstEventInFrame, _state.nextEvent, &iaevNew);

        if (!_FDoAevCore(iaevNew))
            return fFalse;

        if (iaevNew == _state.nextEvent)
            _state.nextEvent++;
    }

    // Remove redundant (same frame) events from previous Add
    if (iaevAdd < 0)
        return fTrue;

    // May need to delete entire previous subroute
    // if the prev Add occurred at the same frame
    paev = (Base *)_track.Events()->QvFixedGet(iaevAdd);
    if (_state.frame == paev->nfrm)
    {
        // Deleting the entire subroute
        _DelAddFrame(iaevAdd, _state.addEvent);
        return fTrue;
    }

    // May need to prune out same-frame events from prev subrte
    for (iaev = _state.addEvent - 1; iaev >= iaevAdd; iaev--)
    {
        paev = (Base *)_track.Events()->QvFixedGet(iaev);
        if (_state.frame == paev->nfrm)
        {
            fPrunedPrevSubrte = fTrue;
            _RemoveAev(iaev);
        }
    }

    if (fPrunedPrevSubrte)
    {
        // The previous path requires pruning
        // Terminating stop & freeze events need to be inserted
        long nfrm = _state.frame;
        if (!FGotoFrame(_state.frame - 1))
            return fFalse;
        DeleteFwdCore(fFalse, pvNil, _state.nextEvent);
        return FGotoFrame(nfrm);
    }

    return fTrue;
}

/***************************************************************************

    Gather the initialization events of the subsequent subroute and insert
    them at the	current event index, _iaevCur.

***************************************************************************/
bool Actor::_FAddAevFromLater(void)
{
    AssertBaseThis(0);

    Base aev;
    long iaev;
    long iaevStart = -1;
    RouteLocation rtelAdd;
    bool fPositionBody = fFalse;

    Assert(1 == _state.nextEvent, "_FAddAevFromLater logic error");
    // Find the next Add event
    for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.aet == aetAdd)
        {
            iaevStart = iaev;
            rtelAdd = aev.rtel;
            break;
        }
    }

    if (iaevStart < 0)
        goto LEnd;

    // Stuff in the events from this next Add event
    for (iaev = iaevStart; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);

        if (aev.rtel != rtelAdd)
        {
            break;
        }

        if (aetCost == aev.aet || aetPull == aev.aet || aetSize == aev.aet || aetRotF == aev.aet)
        {
            aev.rtel = _state.location;
            // aev.nfrm is set by _FDoAevCore()

            // Allocate space
            long cbNew = _track.Events()->Cb(iaev);
            if (!_FInsertAev(_state.nextEvent, cbNew, pvNil, &aev))
                return fFalse;
            iaev++;

            // Insert event
            _track.Events()->Put(_state.nextEvent, _track.Events()->QvGet(iaev));

            if (!_FDoAevCur())
                return fFalse;

            if (aev.aet != aetCost)
                fPositionBody = fTrue;
        }
    }

    if (fPositionBody)
    {
        _PositionBody(&_state.position);
    }

    Assert(_track.Events()->IvMac() > 0, "Logic Error");

LEnd:
    return FSetActionCore(0, 0, 0);
}

/***************************************************************************

    Locate the event of type aet with index closest to but smaller than iaevCur
    Return true if found, with its index in *piaevAdd

***************************************************************************/
bool Actor::_FFindPrevAevAet(long aet, long iaevCur, long *piaevAdd)
{
    AssertBaseThis(0);
    AssertIn(aet, 0, aetLim);
    AssertIn(iaevCur, 0, _track.Events()->IvMac());
    AssertVarMem(piaevAdd);

    Base aev;

    iaevCur--;
    for (; iaevCur >= 0; iaevCur--)
    {
        _track.Events()->GetFixed(iaevCur, &aev);
        if (aev.aet == aet)
        {
            *piaevAdd = iaevCur;
            return fTrue;
        }
    }

    *piaevAdd = ivNil;
    return fFalse;
}

/***************************************************************************

    Locate the event of type aet with index closest to but >= iaevCur.
    Return true if one is found, with its index in *piaevAdd

***************************************************************************/
bool Actor::_FFindNextAevAet(long aet, long iaevCur, long *piaevAdd)
{
    AssertBaseThis(0);
    AssertIn(aet, 0, aetLim);
    AssertIn(iaevCur, 0, _track.Events()->IvMac() + 1);
    AssertVarMem(piaevAdd);

    Base *paev;

    for (; iaevCur < _track.Events()->IvMac(); iaevCur++)
    {
        paev = (Base *)_track.Events()->QvFixedGet(iaevCur);
        if (paev->aet == aet)
        {
            *piaevAdd = iaevCur;
            return fTrue;
        }
    }

    *piaevAdd = ivNil;
    return fFalse;
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
    AssertBaseThis(0);
    AssertIn(iaevAdd, 0, _track.Events()->IvMac());
    if (iaevLim != ivNil)
        AssertIn(iaevLim, 0, _track.Events()->IvMac() + 1);
    AssertVarMem(piaevLast);

    Base aev;

    if (iaevLim == ivNil)
        iaevLim = _track.Events()->IvMac();

    _track.Events()->GetFixed(iaevLim - 1, &aev);

    if (_FIsStalled(iaevAdd, &aev.rtel, piaevLast))
    {
        // Last active event stored by _FIsStalled
        return;
    }

    *piaevLast = iaevLim - 1;
}

/***************************************************************************

    Remove the actor from the stage
    Add the event to the event list

***************************************************************************/
bool Actor::FRemFromStageCore(void)
{
    AssertThis(0);
    Base *paev;

    if (!_state.onStage)
        return fTrue;

    if (!_track.Events()->FEnsureSpace(1, kcbVarStep, fgrpNil)) // step
        return fFalse;

    AssertIn(_state.addEvent, 0, _track.Events()->IvMac());
    paev = (Base *)_track.Events()->QvFixedGet(_state.addEvent);
    if (_state.frame == paev->nfrm)
    {
        if (!_FDeleteEntireSubrte())
            return fFalse;
    }

    if (_state.onStage)
        return _FAddDoAev(aetRem, kcbVarZero, pvNil);
    return fTrue;
}

/***************************************************************************

    Delete the current entire subroute & reposition actor accordingly.
    Based on current state variables

***************************************************************************/
bool Actor::_FDeleteEntireSubrte(void)
{
    AssertThis(0);
    Base *paev;
    long nfrmSav;

    paev = (Base *)_track.Events()->QvFixedGet(_state.addEvent);
    Assert(paev->nfrm == _state.frame, "Logic error: trying to delete whole route from the middle");

    // Delete forward from here	(exclusive of current point)
    _DeleteFwdCore(fFalse, pvNil, _state.nextEvent);

    // Delete events & path for the sole remaining Add frame
    _DelAddFrame(_state.addEvent, _state.nextEvent);

    // A hide should be done separately from event execution
    // because no subroute exists in this case
    _Hide();
    if (_ptmpl != pvNil && _pbody != pvNil)
        _ptmpl->FSetDefaultCost(_pbody);
    _SetStateRewound();
    nfrmSav = _state.frame;
    _state.frame = knfrmInvalid;
    if (_track.Events()->IvMac() > 0)
    {
        paev = (Base *)_track.Events()->QvFixedGet(0);
        Assert(paev->aet == aetAdd, "Corrupt event list");
        _nfrmFirst = paev->nfrm;
        return FGotoFrame(nfrmSav);
    }
    _nfrmFirst = knfrmInvalid;
    return fTrue;
}

/***************************************************************************

    Delete the events and point corresponding to a single point subroute

***************************************************************************/
void Actor::_DelAddFrame(long iaevAdd, long iaevLim)
{
    AssertThis(0);
    long iaev;
    long irptAdd;
    Base *paev;

    AssertIn(iaevAdd, 0, iaevLim);

    paev = (Base *)_track.Events()->QvFixedGet(iaevAdd);
    irptAdd = paev->rtel.irpt;

    // Delete events in current frame of subroute
    for (iaev = iaevLim - 1; iaev >= iaevAdd; iaev--)
    {
        _RemoveAev(iaev);
    }

    // Delete current point	if unused
    if (iaevAdd > 0)
    {
        paev = (Base *)_track.Events()->QvFixedGet(iaevAdd - 1);
        if (paev->rtel.irpt == irptAdd || (paev->rtel.irpt == (irptAdd - 1) && paev->rtel.dwrOffset > rZero))
        {
            return;
        }
    }
    _AdjustAevForRteDel(irptAdd, iaevAdd);
    _track.Route()->Delete(irptAdd);
    if (_state.location.irpt > irptAdd)
        _state.location.irpt--;

    return;
}

/***************************************************************************

    Set the Costume for a body part
    Add the event to the event list

***************************************************************************/
bool Actor::FSetCostumeCore(long ibsetClicked, TAG *ptag, long cmid, tribool fCmtl)
{
    AssertThis(0);
    Assert(fCmtl || ibsetClicked >= 0, "Invalid ibsetClicked argument");
    AssertVarMem(ptag);

    Costume aevcost;
    PCustomMaterial_CMTL pcmtl;
    long ibsetApply;

    // For custom materials, the ibset is a property of the CustomMaterial_CMTL itself,
    // so read it from the CustomMaterial_CMTL rather than using the clicked ibset.
    if (fCmtl)
    {
        pcmtl = _ptmpl->PcmtlFetch(cmid);
        if (pvNil == pcmtl)
            return fFalse;
        ibsetApply = pcmtl->Ibset();
        ReleasePpo(&pcmtl);
    }
    else
    {
        ibsetApply = ibsetClicked;
    }

    aevcost.ibset = ibsetApply;
    aevcost.tag = *ptag;
    aevcost.cmid = cmid;
    aevcost.fCmtl = fCmtl;

    if (!_track.Events()->FEnsureSpace(1, kcbVarCost, fgrpNil))
        return fFalse;

    _PrepCostFill(_state.nextEvent, &aevcost);
    if (!_FAddDoAev(aetCost, kcbVarCost, &aevcost))
        return fFalse;

    return fTrue;
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
    AssertThis(0);
    AssertIn(_state.addEvent, 0, _track.Events()->IvMac());

    Add aevadd;

    _track.Events()->Get(_state.addEvent, &aevadd);

    if (pvNil == pdxyz)
    {
        aevadd.xa = xa;
        aevadd.ya = ya;
        aevadd.za = za;
    }
    else
    {
        BRS rWeight;
        BRS dwrNew = BR_LENGTH3(pdxyz->dxr, pdxyz->dyr, pdxyz->dzr);
        rWeight = BrsDiv(dwrNew, kdwrFast);
        rWeight = LwBound(rWeight, krOriWeightMin, rOne);

        if (grfbra & fbraRotateX)
            aevadd.xa = _BraAvgAngle(xa, aevadd.xa, rWeight);

        if (grfbra & fbraRotateY)
            aevadd.ya = _BraAvgAngle(ya, aevadd.ya, rWeight);

        if (grfbra & fbraRotateZ)
            aevadd.za = _BraAvgAngle(za, aevadd.za, rWeight);
    }

    // Modify the event
    _track.Events()->Put(_state.addEvent, &aevadd);
    _state.transform.xaPath = aevadd.xa;
    _state.transform.yaPath = aevadd.ya;
    _state.transform.zaPath = aevadd.za;
}

/***************************************************************************

    Average two angles by (rw1 * a1 + (1-rw1)*a2)
    rw1 is the weighting for a1

***************************************************************************/
BRA Actor::_BraAvgAngle(BRA a1, BRA a2, BRS rw1)
{
    AssertBaseThis(0);
    AssertIn(rw1, rZero, rOne + rEps);

    BRS rT = rZero;
    BRS r1 = BrAngleToScalar(a1);
    BRS r2 = BrAngleToScalar(a2);
    BRS rw2;

    rw2 = BrsSub(rOne, rw1);

    // Compensate for averaging across 0 degrees
    if (BrsAbs(BrsSub(r1, r2)) > rOneHalf)
    {
        if (r2 > r1)
        {
            // Add equiv of 360 degrees for each weight of r1
            rT = BrsAdd(rT, rw1);
        }
        else
        {
            // Add equiv of 360 degrees for each weight of r1
            rT = BrsAdd(rT, rw2);
        }
    }

    rT = BrsAdd(rT, BrsMul(r1, rw1));
    rT = BrsAdd(rT, BrsMul(r2, rw2));

    if (rT > rOne)
        rT = rT & rFractMax;

    return (BrScalarToAngle(rT));
}

/***************************************************************************

    Rotate the actor
    Add the event to the event list
    If !fFromHereFwd, the rotation is to apply to the current frame only

    WARNING: Rotations are non-abelian.  Ordering is x then y then z.
    If ordering other than this is desired, separate calls must be made.

***************************************************************************/
bool Actor::FRotate(BRA xa, BRA ya, BRA za, bool fFromHereFwd)
{
    AssertThis(0);

    BMAT34 *pbmat34;
    long aet;

    if (!_track.Events()->FEnsureSpace(1, kcbVarRot, fgrpNil))
        return fFalse;

    if (fFromHereFwd)
    {
        aet = aetRotF;
        pbmat34 = &_state.transform.bmat34Fwd;
        if (_state.useCurrentRotation) // _state.transform.bmat34Cur has last been used
        {
            // _state.transform.bmat34Cur stores the complete (no path added) orientation
            BrMatrix34Copy(pbmat34, &_state.transform.bmat34Cur);
            // Back the path orientation out from this matrix.
            // Otherwise the actor will jump in angle
            // This MUST be done in z then y then x order
            // Note: matrix inversion unnecessary
            if (_state.transform.zaPath != aZero)
                BrMatrix34PostRotateZ(pbmat34, -_state.transform.zaPath);
            if (_state.transform.yaPath != aZero)
                BrMatrix34PostRotateY(pbmat34, -_state.transform.yaPath);
            if (_state.transform.xaPath != aZero)
                BrMatrix34PostRotateX(pbmat34, -_state.transform.xaPath);
        }
    }
    else
    {
        // _state.transform.bmat34Cur is kept current at <all> frames so that
        // rotations can be post applied to it
        aet = aetRotH;
        pbmat34 = &_state.transform.bmat34Cur;
    }

    if (aZero != xa)
    {
        BrMatrix34PreRotateX(pbmat34, xa);
    }
    if (aZero != ya)
    {
        BrMatrix34PreRotateY(pbmat34, ya);
    }
    if (aZero != za)
    {
        BrMatrix34PreRotateZ(pbmat34, za);
    }

    Assert(_state.nextEvent <= _track.Events()->IvMac(), "_state.nextEvent bug");

    // Add the event
    if (fFromHereFwd)
        _PrepXfrmFill(aetRotF, pbmat34, kcbVarRot, _state.nextEvent, ivNil, faetNil);
    AssertDo(_FAddDoAev(aet, kcbVarRot, pbmat34), "Ensure space insufficient");

    _PositionBody(&_state.position);
    return fTrue;
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

    // No rotation requested -> no-op. Without this guard the rotation calls
    // below would still apply identity transforms via BrMatrix34Post*, and
    // accumulated FP error across many ticks (when the user holds the mouse
    // button without moving) would visibly drift the actor's matrix toward
    // a contraction.
    if (xa == aZero && ya == aZero && za == aZero)
        return fTrue;

    BMAT34 *pbmat34;
    long aet;

    if (!_track.Events()->FEnsureSpace(1, kcbVarRot, fgrpNil))
        return fFalse;

    if (fFromHereFwd)
    {
        aet = aetRotF;
        pbmat34 = &_state.transform.bmat34Fwd;
        if (_state.useCurrentRotation)
        {
            BrMatrix34Copy(pbmat34, &_state.transform.bmat34Cur);
            if (_state.transform.zaPath != aZero)
                BrMatrix34PostRotateZ(pbmat34, -_state.transform.zaPath);
            if (_state.transform.yaPath != aZero)
                BrMatrix34PostRotateY(pbmat34, -_state.transform.yaPath);
            if (_state.transform.xaPath != aZero)
                BrMatrix34PostRotateX(pbmat34, -_state.transform.xaPath);
        }
    }
    else
    {
        aet = aetRotH;
        pbmat34 = &_state.transform.bmat34Cur;
    }

    // BRender uses row-vector convention. PostRotate (right-multiply) of
    // bmat34 by R applies R as a world-frame rotation to the rendered
    // matrix pbmat = TS * bmat34. That gives a rigid-body group rotation
    // when paired with the world-axis orbit translation in MovieView.
    //
    // The user's tool labels reference the actor-canonical frame (after TS
    // is applied -- for the typical face-camera TS = Ry(90deg), "rotateX"
    // means tip around the actor's right axis = world Z; "rotateZ" means
    // lay around the actor's forward axis = world X). The world-frame
    // PostRotate above doesn't know about TS, so we swap X and Z here to
    // match what the tool icons claim to do. This is correct only for
    // actors with the canonical face-camera rest orientation, but that's
    // the overwhelming common case in 3DMM movies.
    if (za != aZero)
        BrMatrix34PostRotateX(pbmat34, za);
    if (ya != aZero)
        BrMatrix34PostRotateY(pbmat34, ya);
    if (xa != aZero)
        BrMatrix34PostRotateZ(pbmat34, xa);

    // Re-orthonormalise. Each PostRotate composes one matrix multiplication
    // worth of FP error; across hundreds of drag ticks that drifts the
    // rotation part of bmat34 away from length-preserving, which the user
    // sees as the group slowly shrinking. LPNormalise restores the rows to
    // unit length.
    BMAT34 bmatNorm;
    BrMatrix34LPNormalise(&bmatNorm, pbmat34);
    *pbmat34 = bmatNorm;

    if (fFromHereFwd)
        _PrepXfrmFill(aetRotF, pbmat34, kcbVarRot, _state.nextEvent, ivNil, faetNil);
    AssertDo(_FAddDoAev(aet, kcbVarRot, pbmat34), "Ensure space insufficient");

    _PositionBody(&_state.position);
    return fTrue;
}

/***************************************************************************

    "Normalize" the actor
    The revert tool
    Insert normalize transformation events into the event list

***************************************************************************/
bool Actor::FNormalizeCore(ulong grfnorm)
{
    AssertThis(0);
    long rScaleStepOld = _state.transform.rScaleStep;
    ulong faet;
    long cbVar = 0;

    if (grfnorm & fnormRotate)
        cbVar += kcbVarRot;
    if (grfnorm & fnormSize)
        cbVar += kcbVarSize + kcbVarPull;

    if (!_track.Events()->FEnsureSpace(3, cbVar, fgrpNil))
        return fFalse;

    if (grfnorm & fnormRotate)
    {
        _InitXfrmRot(&_state.transform.bmat34Fwd);
        _InitXfrmRot(&_state.transform.bmat34Cur);
        _PrepXfrmFill(aetRotF, &_state.transform.bmat34Fwd, kcbVarRot, _state.nextEvent);
        AssertDo(_FAddDoAev(aetRotF, kcbVarRot, &_state.transform.bmat34Fwd), "EnsureSpace insufficient");
    }

    if (grfnorm & fnormSize)
    {
        _state.transform.aevpull.rScaleX = rOne;
        _state.transform.aevpull.rScaleY = rOne;
        _state.transform.aevpull.rScaleZ = rOne;
        faet = (_state.transform.rScaleStep != rOne) ? faetTweak : faetNil;
        _state.transform.rScaleStep = rOne;
        _PrepXfrmFill(aetPull, &_state.transform.aevpull, kcbVarPull, _state.nextEvent);
        _PrepXfrmFill(aetSize, &_state.transform.rScaleStep, kcbVarSize, _state.nextEvent, ivNil, faet);

        AssertDo(_FAddDoAev(aetPull, kcbVarPull, &_state.transform.aevpull), "EnsureSpace insufficient");
        AssertDo(_FAddDoAev(aetSize, kcbVarSize, &_state.transform.rScaleStep), "EnsureSpace insufficient");

        // Possibly extend life of actor if shrunk
        if (rScaleStepOld > rOne && pvNil != _pscen && _state.location.irpt < _track.Route()->IvMac() - 1) // optimization
        {
            _fLifeDirty = fTrue;
            _pscen->InvalFrmRange();
        }
    }

    Assert(_state.nextEvent <= _track.Events()->IvMac(), "_state.nextEvent bug");

    _PositionBody(&_state.position);
    return fTrue;
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

    ulong faet = faetNil;
    long rScaleStep;
    long rScaleStepOld = _state.transform.rScaleStep;

    if (!_track.Events()->FEnsureSpace(1, kcbVarSize, fgrpNil))
        return fFalse;

    rScale = LwBound(rScale, krScaleMinFactor, krScaleMaxFactor);

    rScaleStep = BrsMul(_state.transform.rScaleStep, rScale);
    _state.transform.rScaleStep = LwBound(rScaleStep, krScaleMin, krScaleMax);

    // Remove tweaks when a transformation that changes stepsize occurs
    faet = (_state.transform.rScaleStep != rScale) ? faetTweak : faetNil;
    _PrepXfrmFill(aetSize, &_state.transform.rScaleStep, kcbVarSize, _state.nextEvent, ivNil, faet);

    if (!_FAddDoAev(aetSize, kcbVarSize, &_state.transform.rScaleStep))
        return fFalse;

    Assert(_state.nextEvent <= _track.Events()->IvMac(), "_state.nextEvent bug");

    if (_state.transform.rScaleStep != rScale)
        _GetXyzFromRtel(&_state.location, &_state.position);
    _PositionBody(&_state.position);

    // Possibly extend life of actor if shrunk
    if (rScaleStepOld != _state.transform.rScaleStep && pvNil != _pscen &&
        _state.location.irpt < _track.Route()->IvMac() - 1) // optimization
    {
        _fLifeDirty = fTrue;
        _pscen->InvalFrmRange();
    }

    return fTrue;
}

/***************************************************************************

    Drag the actor forwards or backwards in time
    NOTE** Drag this subroute only -> sliding other subroutes in time

***************************************************************************/
bool Actor::FSoonerLater(long dnfrm)
{
    AssertThis(0);

    Base aev;
    Base *paevPrev;
    long nfrmSav;
    long iaev;
    long fSuccess;
    long dnfrmSub;
    long dnfrmT;

    if (0 == dnfrm)
        return fTrue;

    if (_fLifeDirty)
    {
        if (!_FComputeLifetime(pvNil))
            PushErc(ercSocBadFrameSlider);
    }

    Assert(_state.addEvent >= 0, "Invalid value for _state.addEvent");

    // On a Sooner operation, slide the earlier subroutes
    // backward in time
    if (dnfrm < 0)
    {
        dnfrmT = (-dnfrm);
        // The times of past events need to be correct.
        for (iaev = _state.nextEvent - 1; dnfrmT > 0 && iaev >= 0; iaev--)
        {
            _track.Events()->GetFixed(iaev, &aev);
            if (aev.aet != aetAdd || iaev == 0)
            {
                aev.nfrm -= dnfrmT;
                _track.Events()->PutFixed(iaev, &aev);
                continue;
            }

            // Account for subroute gaps
            paevPrev = (Base *)_track.Events()->QvFixedGet(iaev - 1);
            dnfrmSub = aev.nfrm - (paevPrev->nfrm);
            aev.nfrm -= dnfrmT;
            _track.Events()->PutFixed(iaev, &aev);
            dnfrmT -= (dnfrmSub - 1);
        }
    }
    else
    {
        // Later
        dnfrmT = dnfrm;
        for (iaev = _state.addEvent; dnfrmT > 0 && iaev < _track.Events()->IvMac(); iaev++)
        {
            _track.Events()->GetFixed(iaev, &aev);
            if (aev.aet != aetAdd || iaev == _state.addEvent)
            {
                nfrmSav = aev.nfrm;
                aev.nfrm += dnfrmT;
                _track.Events()->PutFixed(iaev, &aev);
                continue;
            }
            // Adjust for the gap between subroutes
            dnfrmSub = aev.nfrm - nfrmSav;
            dnfrmT -= (dnfrmSub - 1);
            if (dnfrmT <= 0)
                break;
            aev.nfrm += dnfrmT;
            _track.Events()->PutFixed(iaev, &aev);
        }
    }
    _track.Events()->GetFixed(0, &aev);
    _nfrmFirst = aev.nfrm;
    _track.Events()->GetFixed(_track.Events()->IvMac() - 1, &aev);
    _nfrmLast = aev.nfrm;

    _state.frame += dnfrm;

    // Invalidate, but do not recompute the range
    _pscen->InvalFrmRange();

    if (fSuccess = _pscen->FGotoFrm(_pscen->Nfrm() + dnfrm))
    {
        _pscen->Pmvie()->Pmcc()->UpdateScrollbars();
    }
    return fSuccess;
}

/***************************************************************************

    Pull, stretch, squash the actor.
    Add the event to the event list

    Bound the allowable scaling.

***************************************************************************/
bool Actor::FPull(BRS rScaleX, BRS rScaleY, BRS rScaleZ)
{
    AssertThis(0);

    BRS rScaleXT;
    BRS rScaleYT;
    BRS rScaleZT;

    if (!_track.Events()->FEnsureSpace(1, kcbVarPull, fgrpNil))
        return fFalse;

    rScaleX = LwBound(rScaleX, krScaleMinFactor, krScaleMaxFactor);
    rScaleY = LwBound(rScaleY, krScaleMinFactor, krScaleMaxFactor);
    rScaleZ = LwBound(rScaleZ, krScaleMinFactor, krScaleMaxFactor);

    rScaleXT = BrsMul(_state.transform.aevpull.rScaleX, rScaleX);
    rScaleYT = BrsMul(_state.transform.aevpull.rScaleY, rScaleY);
    rScaleZT = BrsMul(_state.transform.aevpull.rScaleZ, rScaleZ);

    _state.transform.aevpull.rScaleX = LwBound(rScaleXT, krPullMin, krPullMax);
    _state.transform.aevpull.rScaleY = LwBound(rScaleYT, krPullMin, krPullMax);
    _state.transform.aevpull.rScaleZ = LwBound(rScaleZT, krPullMin, krPullMax);

    _PrepXfrmFill(aetPull, &_state.transform.aevpull, kcbVarPull, _state.nextEvent);

    if (!_FAddDoAev(aetPull, kcbVarPull, &_state.transform.aevpull))
        return fFalse;

    Assert(_state.nextEvent <= _track.Events()->IvMac(), "_state.nextEvent bug");

    _PositionBody(&_state.position);
    return fTrue;
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

    RoutePoint xyz;

    if (!_track.Events()->FEnsureSpace(1, kcbVarTweak, fgrpNil))
        return fFalse;

    xyz.dxr = BrsAdd(_state.position.dxr, dxr);
    xyz.dyr = BrsAdd(_state.position.dyr, dyr);
    xyz.dzr = BrsAdd(_state.position.dzr, dzr);

    if ((grfmaf & fmafGround) && (BrsAdd(_state.position.dyr, _state.routeTranslation.dyr) >= rZero) &&
        (BrsAdd(xyz.dyr, _state.routeTranslation.dyr) < rZero))
    {
        xyz.dyr = -_state.routeTranslation.dyr;
    }

    if (!_FAddDoAev(aetTweak, kcbVarTweak, &xyz))
        return fFalse;

    _PositionBody(&xyz);
    return fTrue;
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
    AssertNilOrVarMem(pfMoved);

    Add aevadd;
    Base *paev;
    long iaev;
    bool fMoved;
    LegacyUlong grfbra;
    RoutePoint dxyz;
    BRS yrCurOld;
    BRA xa;
    BRA ya;
    BRA za;

    fMoved = (rZero != dxr || rZero != dyr || rZero != dzr);
    if (pvNil != pfMoved)
        *pfMoved = fMoved;

    if (!fMoved)
        return fTrue;

    if (!_track.Events()->FEnsureSpace(2, kcbVarMove + kcbVarRot, fgrpNil))
        return fFalse;

    // Edit dyr to respect ground
    yrCurOld = BrsAdd(_state.position.dyr, _state.routeTranslation.dyr);
    if (FPure(grfmaf & fmafGround) && (yrCurOld >= rZero) && (BrsAdd(dyr, yrCurOld) < rZero))
    {
        dyr = BrsSub(rZero, yrCurOld);
    }

    // Update Actor's orientation
    if (FPure(grfmaf & fmafOrient) && !_ptmpl->FIsTdt())
    {
        dxyz.dxr = dxr;
        dxyz.dyr = dyr;
        dxyz.dzr = dzr;

        // Back the path orientation out from this matrix & replace
        // with new orientation
        if (_state.transform.zaPath != aZero)
            BrMatrix34PostRotateZ(&_state.transform.bmat34Cur, -_state.transform.zaPath);
        if (_state.transform.yaPath != aZero)
            BrMatrix34PostRotateY(&_state.transform.bmat34Cur, -_state.transform.yaPath);
        if (_state.transform.xaPath != aZero)
            BrMatrix34PostRotateX(&_state.transform.bmat34Cur, -_state.transform.xaPath);

        _ApplyRotFromVec(&dxyz, pvNil, &xa, &ya, &za, &grfbra);
        SetAddOrient(xa, ya, za, grfbra, &dxyz);

        // Force speed to be inversely proportional to angular rotation
        if (_state.addEvent != ivNil)
        {
            BRS dwra, drxa, drya, drza;
            BRS ra1, ra2;
#ifdef DEBUG
            {
                Base *paev;
                paev = (Base *)_track.Events()->QvFixedGet(_state.addEvent);
                Assert(_state.frame == paev->nfrm, "Unsupported use of fmafOrient");
            }
#endif // DEBUG
       //  Set dwra = angular change (in scalar form)
            _track.Events()->Get(_state.addEvent, &aevadd);
            ra1 = BrAngleToScalar(xa);
            ra2 = BrAngleToScalar(aevadd.xa);
            drxa = BrsAbs(BrsSub(ra1, ra2));
            if (drxa > rOneHalf)
            {
                if (ra2 > ra1)
                    ra1 = BrsAdd(rOne, ra1);
                else
                    ra2 = BrsAdd(rOne, ra2);
                drxa = BrsAbs(BrsSub(ra1, ra2));
            }

            ra1 = BrAngleToScalar(ya);
            ra2 = BrAngleToScalar(aevadd.ya);
            drya = BrsAbs(BrsSub(ra1, ra2));
            if (drya > rOneHalf)
            {
                if (ra2 > ra1)
                    ra1 = BrsAdd(rOne, ra1);
                else
                    ra2 = BrsAdd(rOne, ra2);
                drya = BrsAbs(BrsSub(ra1, ra2));
            }

            ra1 = BrAngleToScalar(za);
            ra2 = BrAngleToScalar(aevadd.za);
            drza = BrsAbs(BrsSub(ra1, ra2));
            if (drza > rOneHalf)
            {
                if (ra2 > ra1)
                    ra1 = BrsAdd(rOne, ra1);
                else
                    ra2 = BrsAdd(rOne, ra2);
                drza = BrsAbs(BrsSub(ra1, ra2));
            }

            dwra = BR_LENGTH3(drxa, drya, drza);

            // Compute a bounded inverse of the angular change
            dwra = LwBound(dwra, krAngleMin, krAngleMax);
            dwra = BrsDiv(BrsRcp(dwra), krAngleMinRcp);
            AssertIn(dwra, rZero, BrsAdd(rOne, rEps));

            // Adjust the distances
            dxr = BrsMul(dxr, dwra);
            dyr = BrsMul(dyr, dwra);
            dzr = BrsMul(dzr, dwra);
        }

        // _state.transform.bmat34Cur is to hold the current full rotation
        _LoadAddOrien(&aevadd, fTrue);

        // Update any type of rotate event in this frame
        for (iaev = _state.addEvent + 1; iaev < _state.nextEvent; iaev++)
        {
            paev = (Base *)_track.Events()->QvFixedGet(iaev);
            if (aetRotH == paev->aet)
                _track.Events()->Put(iaev, &_state.transform.bmat34Cur);
            if (aetRotF == paev->aet)
            {
                // Insert orientation-rotation event
                if (!_FAddDoAev(aetRotH, kcbVarRot, &_state.transform.bmat34Cur))
                {
                    Bug("Should have ensured space");
                    return fFalse;
                }
            }
        }
    }

    // Update actor's position
    if (FPure(grfmaf & fmafEntireScene))
    {
        _dxyzFullRte.dxr = BrsAdd(dxr, _dxyzFullRte.dxr);
        _dxyzFullRte.dyr = BrsAdd(dyr, _dxyzFullRte.dyr);
        _dxyzFullRte.dzr = BrsAdd(dzr, _dxyzFullRte.dzr);
    }
    else
    {
        if (_state.addEvent == ivNil)
            return fFalse;

        if (!FPure(grfmaf & fmafEntireSubrte))
        {
            // Move actor just from this frame on (this subpath only)
            // Note: FAddDoAev will update _state.subrouteTranslation
            dxyz.dxr = dxr;
            dxyz.dyr = dyr;
            dxyz.dzr = dzr;
            if (!_FAddDoAev(aetMove, kcbVarMove, &dxyz))
                return fFalse;
        }
        else
        {
            // Translating whole subroute
#ifdef DEBUG
            long cbVar = _track.Events()->Cb(_state.addEvent);
            Assert(cbVar == kcbVarAdd, "Corrupt aev");
#endif // DEBUG
       //  Adjust the translation state variables
            _state.subrouteTranslation.dxr = BrsAdd(dxr, _state.subrouteTranslation.dxr);
            _state.subrouteTranslation.dyr = BrsAdd(dyr, _state.subrouteTranslation.dyr);
            _state.subrouteTranslation.dzr = BrsAdd(dzr, _state.subrouteTranslation.dzr);

            // Adjust the position in the add event
            _track.Events()->Get(_state.addEvent, &aevadd);
            aevadd.dxr = BrsAdd(aevadd.dxr, dxr);
            aevadd.dyr = BrsAdd(aevadd.dyr, dyr);
            aevadd.dzr = BrsAdd(aevadd.dzr, dzr);
            _track.Events()->Put(_state.addEvent, &aevadd);
        }
    }
    _UpdateXyzRte();

    _pscen->MarkDirty();

    // Update Brender model
    fMoved = FPure(rZero != dxr || rZero != dyr || rZero != dzr);

    if (fMoved)
        _PositionBody(&_state.position);

    return fTrue;
}

/***************************************************************************

    Load starting point orientation into state variable _xfrm

***************************************************************************/
void Actor::_LoadAddOrien(Add *paevadd, bool fNoReset)
{
    AssertThis(0);

    // Set _state.transform.bmat34Cur always holds the current full rotation
    if (!fNoReset)
        _InitXfrmRot(&_state.transform.bmat34Cur);
    BrMatrix34PostRotateX(&_state.transform.bmat34Cur, paevadd->xa);
    BrMatrix34PostRotateY(&_state.transform.bmat34Cur, paevadd->ya);
    BrMatrix34PostRotateZ(&_state.transform.bmat34Cur, paevadd->za);
    _state.transform.xaPath = paevadd->xa;
    _state.transform.yaPath = paevadd->ya;
    _state.transform.zaPath = paevadd->za;
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
    Assert(aet == aetSize || aet == aetRotF || aet == aetPull, "Illegal argument aet");
    AssertPvCb(pvVar, cbVar);
    AssertIn(cbVar, 0, 100); // Approximate bound
    AssertIn(iaevMin, 0, _track.Events()->IvMac() + 1);
    AssertIn(iaevCmp, -1, _track.Events()->IvMac() + 1);
    Assert(aet != aetActn && aet != aetCost, "Illegal aet argument");

    long iaev;
    Base aev;
    bool fReplacePrev = fTrue;
    void *pvVarCmp = pvNil;
    LegacyLong cb;

    _track.Events()->Lock();

    if (ivNil != iaevCmp)
    {
        pvVarCmp = _track.Events()->QvGet(iaevCmp, &cb);
        Assert(cb == cbVar, "Logic error");
    }
    else
    {
        // Locate the most current event of this type
        // and store the ptr in pvVarCmp
        for (iaev = 0; iaev < iaevMin - 1; iaev++)
        {
            _track.Events()->GetFixed(iaev, &aev);
            if (aet != aev.aet)
                continue;

            pvVarCmp = _track.Events()->QvGet(iaev, &cb);
            Assert(cb == cbVar, "Logic error");
        }
    }

    if (pvNil == pvVarCmp)
        pvVarCmp = pvVar;

    for (iaev = iaevMin; iaev < _track.Events()->IvMac(); iaev++)
    {
        bool fDelete = fFalse;
        _track.Events()->GetFixed(iaev, &aev);

        // Stage entrance events are boundaries to edits
        if (aetAdd == aev.aet)
            goto LEnd;

        switch (aev.aet)
        {
        case aetSize:
        case aetPull:
        case aetRotF:
            void *pv1;
            void *pv2;
            if (aev.aet != aet)
                continue;

            if (fReplacePrev)
            {
                pv1 = pvVarCmp;
                pv2 = _track.Events()->QvGet(iaev);
                if (fcmpEq != FcmpCompareRgb(pv1, pv2, cbVar))
                {
                    // Prepare to test for a match with new event
                    fReplacePrev = fFalse;
                    pvVarCmp = pvVar;
                    fDelete = fFalse;
                }
                else
                {
                    fDelete = fTrue;
                    goto LDelete;
                }
            }

            Assert(!fReplacePrev, "Logic Error");
            pv1 = pvVarCmp;
            pv2 = _track.Events()->QvGet(iaev);
            if (fcmpEq != FcmpCompareRgb(pv1, pv2, cbVar))
                goto LEnd;

            // Events equal : delete event at iaev
            fDelete = fTrue;
            break;

        case aetTweak:
            if ((grfaet & faetTweak) && (aev.rtel >= _state.location))
                fDelete = fTrue;
            break;
        case aetRotH:
            if ((grfaet & faetRotF) && (aev.rtel == _state.location))
                fDelete = fTrue;
            break;
        case aetFreeze:
            if ((grfaet & faetFreeze) && (aev.rtel >= _state.location))
                fDelete = fTrue;
            break;
        case aetStep:
            if ((grfaet & faetStep) && (aev.rtel >= _state.location))
                fDelete = fTrue;
            ;
            break;

        default:
            break;
        }
    LDelete:
        if (fDelete)
        {
            _RemoveAev(iaev);
            iaev--;
        }
    }

LEnd:
    _track.Events()->Unlock();
}

/***************************************************************************

    Insert Aev.  Update state variables
    Since _track.Events() is getting a copy of a tag, a call to DupTag is required

***************************************************************************/
bool Actor::_FInsertAev(long iaev, long cbNew, void *pvVar, void *paev, bool fUpdateState)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _track.Events()->IvMac() + 1);
    AssertIn(cbNew, 0, 100); // approximate bound
    if (pvNil != pvVar)
        AssertPvCb(pvVar, cbNew);
    AssertPvCb(paev, size(Base));

    PTAG ptag;

    if (!_track.Events()->FInsert(iaev, cbNew, pvVar, paev))
        return fFalse;

    // If not simply allocating space
    if (pvVar != pvNil)
    {
        // Increment tag count
        _track.Events()->Lock();
        if (_FIsIaevTag(_track.Events(), iaev, &ptag))
            TagManager::DupTag(ptag);
        _track.Events()->Unlock();

        if (fUpdateState)
        {
            if (iaev < _state.nextEvent)
                _state.nextEvent++;
            if (iaev <= _state.actionEvent)
                _state.actionEvent++;
            if (iaev <= _state.addEvent)
                _state.addEvent++;
            if (iaev < _state.firstEventInFrame)
                _state.firstEventInFrame++;
        }
    }

    return fTrue;
}

/***************************************************************************

    Remove Aev.  Update state variables

***************************************************************************/
void Actor::_RemoveAev(long iaev, bool fUpdateState)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _track.Events()->IvMac());

    bool fUpdateSndFrame;
    PTAG ptag;
    PBase qaev;

    // First, close tags
    _track.Events()->Lock();
    if (_FIsIaevTag(_track.Events(), iaev, &ptag, &qaev))
        TagManager::CloseTag(ptag);

    /* Don't bother updating the frame sound indicator if we didn't change
        an event in the scene's current frame */
    fUpdateSndFrame = (qaev->aet == aetSnd) && (_pscen != pvNil) && (qaev->nfrm == _pscen->Nfrm());

    _track.Events()->Unlock(); // qaev is invalid past here!
    TrashVar(&qaev);

    _track.Events()->Delete(iaev);

    if (fUpdateState)
    {
        if (iaev < _state.nextEvent)
            _state.nextEvent--;
        if (iaev < _state.actionEvent)
            _state.actionEvent--;
        if (iaev < _state.addEvent)
            _state.addEvent--;
        if (iaev < _state.firstEventInFrame)
            _state.firstEventInFrame--;
    }

    if (fUpdateSndFrame)
        _pscen->UpdateSndFrame();
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
    AssertBaseThis(0);
    AssertIn(iaevMin, 0, _track.Events()->IvMac() + 1);

    Base aev;
    long iaev;
    long anid = anidPrev;

    for (iaev = iaevMin; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);

        // Stage entrance events are boundaries to edits
        if (aetAdd == aev.aet)
            return;

        if (aetActn == aev.aet)
        {
            Action aevactn;
            _track.Events()->Get(iaev, &aevactn);

            if (!(grfaet & faetActn))
            {
                if (aevactn.anid == anidNew)
                    continue;
                else
                    return;
            }

            if (aevactn.anid == anid)
            {
                _RemoveAev(iaev);
                iaev--;
            }
            else if (aevactn.anid == anidNew)
            {
                anid = anidNew;
                _RemoveAev(iaev);
                iaev--;
            }
            else
                return;
        }

        if (aev.rtel < _state.location)
            continue;

        if (aev.aet == aetTweak && (grfaet & faetTweak))
        {
            _RemoveAev(iaev);
            iaev--;
        }

        if (aev.aet == aetFreeze && (grfaet & faetFreeze))
        {
            // Do not remove end-of-subroute freeze events
            if (!_FIsDoneAevSub(iaev, aev.rtel))
            {
                _RemoveAev(iaev);
                iaev--;
            }
        }
    }
}

/***************************************************************************

    Remove redundant costume changes
    Note:  	Delete all costume events that match the costume being replaced
            Then delete all subsequent costume events that match the
            costume being inserted

***************************************************************************/
void Actor::_PrepCostFill(long iaevMin, Costume *paevcost)
{
    AssertBaseThis(0);
    AssertIn(iaevMin, 0, _track.Events()->IvMac() + 1);
    AssertVarMem(paevcost);

    long iaev;
    Base aev;
    Costume aevcost;
    bool fMtrl;
    bool fCmtl;
    Material_MTRL *pmtrlCmp;
    CustomMaterial_CMTL *pcmtlCmp;
    Material_MTRL *pmtrl = pvNil;
    CustomMaterial_CMTL *pcmtl = pvNil;
    bool fReplacePrev = fTrue;

    // Locate the most current costume for this body part
    _pbody->GetPartSetMaterial(paevcost->ibset, &fMtrl, &pmtrlCmp, &pcmtlCmp);
    fCmtl = !fMtrl;

    for (iaev = iaevMin; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);

        // Stage entrance events are boundaries to edits
        if (aetAdd == aev.aet)
            goto LEnd;

        if (aetCost != aev.aet)
            continue;

        _track.Events()->Get(iaev, &aevcost);
        if (aevcost.ibset != paevcost->ibset)
            continue;

        if (fReplacePrev)
        {
            // Delete the event if the costumes are the same
            if ((fCmtl == aevcost.fCmtl) &&
                ((!fCmtl && (pmtrlCmp == (pmtrl = (PMaterial_MTRL)vptagm->PbacoFetch(&aevcost.tag, Material_MTRL::FReadMtrl)))) ||
                 (fCmtl && (pcmtlCmp == (pcmtl = _ptmpl->PcmtlFetch(aevcost.cmid))))))
            {
                goto LDelete;
            }

            // This event does not match the one being replaced
            // Prepare to test for a match with the event being inserted
            fReplacePrev = fFalse;
            if (!paevcost->fCmtl)
            {
                pmtrlCmp = (PMaterial_MTRL)vptagm->PbacoFetch(&paevcost->tag, Material_MTRL::FReadMtrl);
                pcmtlCmp = pvNil;
            }
            else
            {
                pcmtlCmp = _ptmpl->PcmtlFetch(paevcost->cmid);
                pmtrlCmp = pvNil;
            }
        }

        ReleasePpo(&pcmtl);
        ReleasePpo(&pmtrl);
        Assert(!fReplacePrev, "Logic error");
        if (paevcost->fCmtl != aevcost.fCmtl ||
            (!aevcost.fCmtl && (pmtrlCmp != (pmtrl = (PMaterial_MTRL)vptagm->PbacoFetch(&aevcost.tag, Material_MTRL::FReadMtrl)))) ||
            (aevcost.fCmtl && (pcmtlCmp != (pcmtl = _ptmpl->PcmtlFetch(aevcost.cmid)))))
        {
            // If the costumes differ
            goto LEnd;
        }
    LDelete:
        ReleasePpo(&pcmtl);
        ReleasePpo(&pmtrl);
        _RemoveAev(iaev);
        iaev--;
    }
LEnd:
    if (!fReplacePrev)
    {
        ReleasePpo(&pmtrlCmp);
        ReleasePpo(&pcmtlCmp);
    }
    ReleasePpo(&pmtrl);
    ReleasePpo(&pcmtl);
}

/***************************************************************************

    AdjustAevForRte for an already inserted point at irptAdjust
    adjust the rtel's of the subsequent affected events

***************************************************************************/
void Actor::_AdjustAevForRteIns(long irptAdjust, long iaevMin)
{
    AssertBaseThis(0);
    long irptMac = _track.Route()->IvMac();
    AssertIn(irptAdjust, 0, irptMac);
    AssertIn(iaevMin, 0, _track.Events()->IvMac() + 1);

    long iaev;
    Base aev;
    RouteDistancePoint rptBack, rptAdjust;
    BRS dwrBack;

    if (irptAdjust > 0)
    {
        _track.Route()->Get(irptAdjust - 1, &rptBack);
        dwrBack = rptBack.dwr;
    }
    else
        dwrBack = rZero;

    _track.Route()->Get(irptAdjust, &rptAdjust);

    for (iaev = iaevMin; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.rtel.irpt >= irptAdjust) // offset from later point
        {
            aev.rtel.irpt++;
        }
        else if (aev.rtel.irpt < irptAdjust - 1)
        {
            continue;
        }
        else
        {
            if ((irptAdjust == irptMac - 1) || (rZero == dwrBack))
                continue;

            if (aev.rtel.dwrOffset >= dwrBack)
            {
                aev.rtel.dwrOffset = BrsSub(aev.rtel.dwrOffset, dwrBack);
                aev.rtel.irpt++;
            }
        }
        _track.Events()->PutFixed(iaev, &aev);
    }
}
/***************************************************************************

    AdjustAevForRte for a to-be deleted point at irptAdjust
    adjust the rtel's of the affected events

***************************************************************************/
void Actor::_AdjustAevForRteDel(long irptAdjust, long iaevMin)
{
    AssertBaseThis(0);
    long irptMac = _track.Route()->IvMac();
    AssertIn(irptAdjust, 0, irptMac);
    AssertIn(iaevMin, 0, _track.Events()->IvMac() + 1);

    long iaev;
    Base aev;
    RouteDistancePoint rptBack, rptAdjust;
    BRS dwrBack, dwrFwd;

    if (0 == irptAdjust)
        dwrBack = rZero;
    else
    {
        _track.Route()->Get(irptAdjust - 1, &rptBack);
        dwrBack = rptBack.dwr;
    }

    _track.Route()->Get(irptAdjust, &rptAdjust);
    dwrFwd = rptAdjust.dwr;

    for (iaev = iaevMin; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.rtel.irpt > irptAdjust) // offset from later point
        {
            aev.rtel.irpt--;
        }
        else if (aev.rtel.irpt == irptAdjust)
        {
            if (rptAdjust.dwr == rZero)
            {
                // Point will be deleted.  Event must be also
                _RemoveAev(iaev);
                iaev--;
                continue;
            }
            aev.rtel.dwrOffset = BrsAdd(dwrBack, aev.rtel.dwrOffset);
            aev.rtel.irpt--;
        }
        else
            continue;

        _track.Events()->PutFixed(iaev, &aev);
    }
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
    AssertBaseThis(0);
    AssertVarMem(pxyz);
    RoutePoint xyz;
    BMAT34 bmat34; // Final orientation matrix

    _MatrixRotUpdate(pxyz, &bmat34);

    xyz.dxr = BrsAdd(_state.routeTranslation.dxr, pxyz->dxr);
    xyz.dyr = BrsAdd(_state.routeTranslation.dyr, pxyz->dyr);
    xyz.dzr = BrsAdd(_state.routeTranslation.dzr, pxyz->dzr);
    _pbody->LocateOrient(xyz.dxr, xyz.dyr, xyz.dzr, &bmat34);
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
    AssertBaseThis(0);
    AssertNilOrVarMem(pnfrmLast);
    if (_fModeRecord)
    {
        if (pnfrmLast != pvNil)
            *pnfrmLast = _nfrmLast;
        _fLifeDirty = true;
        return true;
    }
    bool success = ActorTimeline::FComputeLifetime(_track, _nfrmFirst, _fRejoin, _StepSource(), &_nfrmLast);
    _fLifeDirty = !success;
    if (success && pnfrmLast != pvNil)
        *pnfrmLast = _nfrmLast;
    return success;
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
bool Actor::FIsRecordValid(BRS dxr, BRS dyr, BRS dzr, ulong tsCurrent)
{
    AssertThis(0);

    if (_fModeRecord)
        return fTrue;

    BRS dwrMouse;

    // Is this truly a motion fill or is there sufficient time or
    // distance travelled to make this an authentic route record?
    dwrMouse = BR_LENGTH3(dxr, dyr, dzr);

    if ((dwrMouse < kdwrThreshRte) && ((tsCurrent - _tsInsert) < kdtsThreshRte))
        return fFalse;

    return fTrue;
}

/***************************************************************************

    Begin a new route
    fReplace = ftrue if recording, fFalse if rerecording
    On failure, actor is restored from pactrRestore

***************************************************************************/
bool Actor::FBeginRecord(ulong tsCurrent, bool fReplace, PActor pactrRestore)
{
    AssertThis(0);
    Assert(tsCurrent >= 0, "Invalid type");

    RouteDistancePoint rpt;
    Base aev;
    long iaev;
    long nfrmAdd;
    long nfrmPrev;
    bool fClosestSubrte = fTrue;

    _track.Route()->Get(_state.location.irpt, &rpt);
    _fPathInserted = fFalse;

    // Do not rejoin to a one point offstage stub.
    _fRejoin = !fReplace && !(rZero == rpt.dwr);
    if (_fRejoin)
    {
        long iaev;
        Base aev;

        long irptNext = _state.location.irpt + 1;

        for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
        {
            _track.Events()->GetFixed(iaev, &aev);
            if (irptNext < aev.rtel.irpt)
                break;
            if (aetRem == aev.aet)
            {
                _fRejoin = fFalse;
                _RemoveAev(iaev);
                iaev--;
                _AdjustAevForRteDel(irptNext, _state.nextEvent);
                _track.Route()->Delete(irptNext);
            }
        }
    }

    // Recording must advance from the current point -> potentially
    // inserting the current point
    if (rZero != _state.location.dwrOffset)
    {
        BRS dwrCur = _state.location.dwrOffset;
        _GetXyzFromRtel(&_state.location, &rpt.xyz);
        _state.location.irpt++;
        _state.location.dwrOffset = rZero;
        _state.location.dnfrm = 0;
        if (!_FInsertGgRpt(_state.location.irpt, &rpt, dwrCur))
            goto LFail;
        _GetXyzFromRtel(&_state.location, &_state.position);
        _AdjustAevForRteIns(_state.location.irpt, 0);
    }

    // Determine the gaps between subroutes
    _dnfrmGap = 0;
    iaev = _state.nextEvent;
    if (_fLifeDirty)
    {
        // Validate future nfrm values
        if (!_FComputeLifetime())
        {
            PushErc(ercSocBadFrameSlider);
            goto LFail;
        }
    }

    for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.aet != aetAdd)
            continue;
        nfrmAdd = aev.nfrm;
        if (fClosestSubrte)
        {
            nfrmPrev = _state.frame;
            fClosestSubrte = fFalse;
        }
        else
        {
            _track.Events()->GetFixed(iaev - 1, &aev);
            nfrmPrev = aev.nfrm;
        }
        _dnfrmGap -= (nfrmAdd - nfrmPrev);
    }

    _rtelInsert = _state.location;
    _fModeRecord = fTrue;
    _dxyzRaw.dxr = rZero;
    _dxyzRaw.dyr = rZero;
    _dxyzRaw.dzr = rZero;
    return fTrue;

LFail:
    _RestoreFromUndo(pactrRestore);
    return fFalse;
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
    AssertThis(0);
    AssertVarMem(pfStepFrm);
    AssertVarMem(pfStepRte);

    RouteDistancePoint rptCur;
    RoutePoint xyzMouse;
    RoutePoint dxyzT;
    BRS dwrCur;
    RouteDistancePoint rptNew;
    BRS rFractMoved;
    BRS dwrMouse;

    Assert(_state.onStage, "Recording an actor that wasn't selectable??");
    if (!_fModeRecord)
        return fTrue; // Potential client call on motion fill

    if (!_track.Events()->FEnsureSpace(2, kcbVarStep + kcbVarFreeze, fgrpNil) || !_track.Route()->FEnsureSpace(1, fgrpNil))
    {
        goto LFail;
    }

    // Smooth the unpruned raw mouse movement *before* we create the path
    dxyzT.dxr = dxr;
    dxyzT.dyr = dyr;
    dxyzT.dzr = dzr;
    dxr = BR_CONST_DIV(BrsAdd(_dxyzRaw.dxr, BR_CONST_MUL(2, dxr)), 3);
    dyr = BR_CONST_DIV(BrsAdd(_dxyzRaw.dyr, BR_CONST_MUL(2, dyr)), 3);
    dzr = BR_CONST_DIV(BrsAdd(_dxyzRaw.dzr, BR_CONST_MUL(2, dzr)), 3);

    if (_state.frozen && !(grfmaf & fmafFreeze))
        AssertDo(_FUnfreeze(), "Ensurespace should have ensured space");
    if (!_state.frozen && (grfmaf & fmafFreeze))
        AssertDo(_FFreeze(), "Ensurespace should have ensured space");

    if (pvNil != pfStepFrm)
        *pfStepFrm = fFalse;
    if (pvNil != pfStepRte)
        *pfStepRte = fFalse;

    // Update distance from previously current point
    Assert(_track.Route()->IvMac() > 0, "Illegal empty path");

    // In record mode, know prev point on a route Node
    // Determine if threshhold distance has been advanced
    _track.Route()->Get(_state.location.irpt, &rptCur);
    xyzMouse.dxr = BrsAdd(dxr, rptCur.xyz.dxr);
    xyzMouse.dyr = BrsAdd(dyr, rptCur.xyz.dyr);
    xyzMouse.dzr = BrsAdd(dzr, rptCur.xyz.dzr);
    dwrMouse = BR_LENGTH3(dxr, dyr, dzr);

    if (!_FGetDwrRecord(&dwrCur))
        goto LFail;

    if (dwrMouse < dwrCur)
    {
        if ((tsCurrent - _tsInsert) < kdtsThreshRte)
        {
            // Insufficient length and insufficient time to record a point
            return fTrue;
        }

        if (rZero != _state.step)
        {
            AssertDo(FSetStep(rZero), "EnsureSpace insufficient");
        }

        _fPathInserted = fTrue;
        if (pvNil != pfStepFrm)
        {
            *pfStepFrm = fTrue;
        }
        if (!_pscen->FGotoFrm(_pscen->Nfrm() + 1))
            goto LFail;
        return fTrue;
    }

    _tsInsert = tsCurrent;
    _dxyzRaw = dxyzT;

    //
    // Insert new point.  (Overlays last point if offstage)
    // Length is sufficient.  Truncate to correct step size
    //
    rFractMoved = BrsDiv(dwrCur, dwrMouse);
    Assert(rZero != dwrCur, "Zero length step illegal in FRecordMove");

    _GetXyzOnLine(&rptCur.xyz, &xyzMouse, rFractMoved, &rptNew.xyz);
    rptNew.dwr = rZero;

    // Adjust the point for ground zero.  The rule is if the mouse
    // is decreasing from above to below ground, stop at ground level
    // The original dwr is used *by design*
    if ((grfmaf & fmafGround) && (BrsAdd(rptCur.xyz.dyr, _state.routeTranslation.dyr) >= rZero) &&
        (BrsAdd(rptNew.xyz.dyr, _state.routeTranslation.dyr) < rZero))
    {
        rptNew.xyz.dyr = -_state.routeTranslation.dyr;
    }

    if (!_track.Route()->FInsert(1 + _state.location.irpt, &rptNew))
        goto LFail;

    if (kdwrNil != _state.step)
    {
        AssertDo(FSetStep(kdwrNil), "EnsureSpace insufficient");
    }

    // Reset distance in previous xyz entry
    rptCur.dwr = dwrCur;
    Assert(dwrCur != rZero, "Bug in InsertRoute");
    _track.Route()->Put(_state.location.irpt, &rptCur);

    _AdjustAevForRteIns(1 + _state.location.irpt, _state.nextEvent);
    _fPathInserted = fTrue;

    if (pvNil != pfStepFrm)
        *pfStepFrm = fTrue;
    if (pvNil != pfStepRte)
        *pfStepRte = fTrue;

    _dnfrmGap++;
    if (_dnfrmGap > 0)
        _nfrmLast++;

    if (_pscen->FGotoFrm(_pscen->Nfrm() + 1))
        return fTrue;

LFail:
    _RestoreFromUndo(pactrRestore);
    return fFalse;
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

    Base aev;
    BRS dwr;
    long iaev;
    long irpt;
    RouteDistancePoint rpt;
    RouteDistancePoint rptJoin;
    RouteDistancePoint rptCur;
    BRS dwrMin = kdwrMax;
    RouteLocation rtelJoin = _state.location;
    bool fJoin = fFalse;
    long irptLim = _track.Route()->IvMac();
    long iaevJoinFirst = _state.nextEvent;
    long iaevNew;

    // Determine whether to rejoin to the path
    // REVIEW (*****): Can we assert _state.onStage?
    if (_fModeRecord && (!_state.onStage || fReplace))
    {
        // Delete remnant path
        _DeleteFwdCore(fFalse, pvNil, _state.nextEvent);
        if (!_FFreeze())
            goto LFail;
        if (!FSetStep(rZero))
            goto LFail;
        goto LEndRecord;
    }

    //
    // Motion Fill or Path-rejoining
    //
    // _fModeRecord == fFalse on motion fill
    if (!_fModeRecord || !_fPathInserted || !_state.onStage)
    {
        // Set Action necessarily destroys end of path freeze events
        // Reinsert an end of path freeze event	if at end of subroute
        if (_FIsDoneAevSub(_state.nextEvent, _state.location))
        {
            if (!_FFreeze())
                goto LFail;
        }
        goto LEndRecord; // Nothing inserted
    }

    // On !fReplace, force continuation to remainder of route.
    if (rZero == _state.step)
    {
        if (!FSetStep(kdwrNil))
            goto LFail;
    }

    if (!_fRejoin)
    {
        // Gather the events at later time for the current path point
        // Equivalent to setting the join time ahead
        rtelJoin.dnfrm = 1;
    }
    else
    {
        Assert((_state.location.irpt < _track.Route()->IvMac() - 1), "Not enough points to rejoin");

        for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
        {
            _track.Events()->GetFixed(iaev, &aev);
            if (aev.aet == aetAdd)
            {
                irptLim = aev.rtel.irpt;
                break;
            }
        }

        // Locate the Join node
        rtelJoin.irpt = _state.location.irpt + 1;
        rtelJoin.dwrOffset = rZero;
        rtelJoin.dnfrm = 0;
        for (irpt = 1 + _state.location.irpt; irpt < irptLim; irpt++)
        {
            _track.Route()->Get(irpt, &rpt);
            dwr = BR_LENGTH3(BrsSub(_state.position.dxr, rpt.xyz.dxr), BrsSub(_state.position.dyr, rpt.xyz.dyr),
                             BrsSub(_state.position.dzr, rpt.xyz.dzr));
            if (dwr < dwrMin)
            {
                dwrMin = dwr;
                rtelJoin.irpt = irpt;
            }

            // Do not join across subroutes
            if (rZero == rpt.dwr)
                break;
        }

        Assert(rtelJoin > _state.location, "Invalid Join point");
        Assert(rZero == _state.location.dwrOffset, "_state.location invalid");

        // Update the dwr of the most recently recorded point to be
        // the length to the join point on the path
        // NOTE: This cannot be zero (it would signal end of path)
        _track.Route()->Get(_state.location.irpt, &rptCur);
        _track.Route()->Get(rtelJoin.irpt, &rptJoin);
        rptCur.dwr =
            BR_LENGTH3(BrsSub(_state.position.dxr, rptJoin.xyz.dxr), BrsSub(_state.position.dyr, rptJoin.xyz.dyr),
                       BrsSub(_state.position.dzr, rptJoin.xyz.dzr));

        if (rZero == rptCur.dwr)
        {
            // Prevent pathological end-of-route case
            rptCur.dwr = rEps;
        }
        _track.Route()->Put(_state.location.irpt, &rptCur);
    }

    //
    // Note: Motion fill exited earlier.  This is rejoin-recording only (which
    // may or may not actually have a path point to rejoin to).
    // Move displaced events forward to the join point
    // Spec: Replace is viewed in UI as Action replace, not just path replace.	->
    // Delete intervening action, tweak, freeze, step and rem events.
    //
    for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);

#ifdef BUG1961
        // If we were rejoin-recording from a static segment, there is
        // no point to rejoin to (-> _fRejoin is fFalse).  So gather
        // events up to but not beyond the next add event.
        if (aev.aet == aetAdd || (_fRejoin && aev.rtel >= rtelJoin))
#else
        if (_fRejoin && aev.rtel >= rtelJoin)
#endif //! BUG1961
        {
            break;
        }

        if (aev.rtel > _rtelInsert)
        {
            if (aev.aet == aetTweak || aev.aet == aetStep || aev.aet == aetRem || aev.aet == aetActn ||
                aev.aet == aetFreeze)
            {
                _RemoveAev(iaev);
                iaev--;
                continue;
            }
            else
            {
                aev.rtel = rtelJoin;
                _track.Events()->PutFixed(iaev, &aev);
                _MergeAev(iaevJoinFirst, iaev, &iaevNew);
                if (iaevNew < iaev)
                    iaev--;
            }
        }
    }

    if (_fRejoin)
    {
        // Delete the path segment before the join point
        for (irpt = rtelJoin.irpt - 1; irpt > _state.location.irpt; irpt--)
        {
            _AdjustAevForRteDel(irpt, _state.nextEvent);
            _track.Route()->Delete(irpt);
        }

        // Spec: Do an action fill forward.
        _PrepActnFill(iaevJoinFirst, _state.action, _state.action, faetTweak | faetFreeze | faetActn);
    }

#ifdef BUG1961
    if (!fReplace && !_fRejoin)
    {
        if (!_FFreeze())
            goto LFail;
        if (!FSetStep(rZero))
            goto LFail;
    }
#endif // BUG1961

LEndRecord:
    _fLifeDirty = fTrue;
#ifdef BUG1973
    _fRejoin = fFalse;
#endif // BUG1973
    _fModeRecord = fFalse;
    _pscen->MarkDirty();
    _pscen->InvalFrmRange();
    AssertThis(fobjAssertFull);
    return fTrue;

LFail:
    _RestoreFromUndo(pactrRestore);
#ifdef BUG1973
    _fRejoin = fFalse;
#endif
    _fModeRecord = fFalse; // Redundant safety net
    return fFalse;
}

/***************************************************************************

    Delete the path and events beyond this frame - and -
    Insert a Freeze event to terminate the truncated event list

***************************************************************************/
void Actor::DeleteFwdCore(bool fDeleteAll, bool *pfAlive, long iaevCur)
{
    AssertThis(0);

    bool fAlive;
    long iaev;
    Base aev;

    _DeleteFwdCore(fDeleteAll, &fAlive, iaevCur);

    if (fAlive && _state.frame >= _nfrmFirst)
    {
        // If no space exists, truncation isn't changing a thing
        if (_track.Events()->FEnsureSpace(2, kcbVarFreeze + kcbVarStep, fgrpNil))
        {
#ifdef BUG1932
            if (_state.addEvent < 0 || _track.Events()->IvMac() == 0)
            {
                // Safety net code : don't add events if bug exists
                Bug("Invalid fAlive value");
                goto LEnd;
            }
#ifdef DEBUG
            _track.Events()->GetFixed(_state.addEvent, &aev);
            Assert(aetAdd == aev.aet, "Should be an add event at _state.addEvent");
#endif // DEBUG
            if (_state.onStage)
            {
                // Insert terminating events ONLY if the actor is shown, not hidden
                AssertDo(_FFreeze(), "Expected freeze event to succeed");
                AssertDo(FSetStep(rZero), "Expected set step event to succeed");
            }
            else
                goto LEnd;
#else  // BUG1932
            AssertDo(_FFreeze(), "Expected freeze event to succeed");
            AssertDo(FSetStep(rZero), "Expected set step event to succeed");
#endif //! BUG1932
       // By construct, step events SET the location of display
       // Tweak events override the last step event.
            for (iaev = _state.nextEvent - 1; iaev > 0; iaev--)
            {
                _track.Events()->GetFixed(iaev, &aev);
                if (aev.rtel != _state.location)
                    break;
                if (aev.aet == aetTweak)
                {
                    _track.Events()->Swap(iaev, _state.nextEvent - 1);
                    break;
                }
            }
        }
    }

LEnd:
    if (pvNil != pfAlive)
        *pfAlive = fAlive;
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

    long iaev, iaevDelLim;
    long irpt, irptDelLim;
    long irptDelFirst;
    RouteDistancePoint rpt;
    Base *paev;

    if (ivNil == iaevCur)
        iaevCur = _state.nextEvent;

#ifndef BUG1870
    // Delete this section of code: Placement orientation should no
    // 		longer be overwritten.
    // Preserve the current orientation
    if (_state.addEvent >= 0)
    {
        // If we are reducing the current subroute to a single point,
        // it is necessary to store the current orientation for wysiwyg
        paev = (Base *)_track.Events()->QvFixedGet(_state.addEvent);
        if (paev->nfrm == _state.frame)
            _SaveCurPathOrien();
    }
#endif //! BUG1870

    // If !fDeleteAll & only one subroute left, redefine fDeleteAll
    iaevDelLim = _track.Events()->IvMac();
    irptDelLim = _track.Route()->IvMac();
    if (!fDeleteAll)
    {
        fDeleteAll = fTrue;
        for (iaev = iaevCur; iaev < _track.Events()->IvMac(); iaev++)
        {
            paev = (Base *)_track.Events()->QvFixedGet(iaev);
            if (aetAdd == paev->aet)
            {
                fDeleteAll = fFalse;
                irptDelLim = paev->rtel.irpt;
                iaevDelLim = iaev;
                break;
            }
        }
    }

    // Remove events beyond the current one
    // excluding the last freeze event
    if (0 < iaevDelLim)
    {
        for (iaev = iaevDelLim - 1; iaev >= _state.nextEvent; iaev--)
        {
            _RemoveAev(iaev);
            _fLifeDirty = fTrue;
        }
    }

    // Prune the corresponding route
    irptDelFirst = _state.location.irpt + 1;
    if (0 < irptDelLim)
    {
        // Note: Last remaining point on path is dependent on the event stream
        if (_state.nextEvent <= 0 && fDeleteAll)
        {
            _track.Route()->FSetIvMac(0);
            _fLifeDirty = fTrue;
            _state.location.irpt = -1;
            goto LDone;
        }
        else
        {
            // Delete the current path node only if no other events
            // use the same node
            if (_state.location.dwrOffset > rZero)
            {
                // Shorten the distance between the last two nodes
                _TruncateSubRte(irptDelLim);
                irptDelFirst++;
            }
            else
            {
                // Delete remaining subroute
                for (irpt = irptDelLim - 1; irpt >= irptDelFirst; irpt--)
                {
                    _AdjustAevForRteDel(irpt, 0);
                    _track.Route()->Delete(irpt);
                    _fLifeDirty = fTrue;
                }
            }
        }

        // Adjust the distance on the tail point
        if (_state.location.irpt >= 0)
        {
            Assert(_track.Route()->IvMac() > 0, "Logic Error");
            _track.Route()->Get(_state.location.irpt, &rpt);
            rpt.dwr = rZero;
            _track.Route()->Put(_state.location.irpt, &rpt);
        }
    }

LDone:
    _pscen->MarkDirty();
    if (_state.frame < _nfrmFirst)
    {
        if (fDeleteAll)
        {
            if (pvNil != pfAlive)
                *pfAlive = fFalse;
            _InitState();
            return;
        }
        else if (_track.Events()->IvMac() > 0)
        {
            paev = (Base *)_track.Events()->QvFixedGet(0);
            _nfrmFirst = paev->nfrm;
            Assert(aetAdd == paev->aet, "Bug in Actor::DeleteFwdCore");
        }
    }

    if (pvNil != pfAlive)
        *pfAlive = fTrue;

    return;
}

/***************************************************************************

    Save the path specific part of the current orientation

***************************************************************************/
void Actor::_SaveCurPathOrien(void)
{
    AssertBaseThis(0);

    RouteDistancePoint rpt;
    Base aev;
    ulong grfbra = 0;

    if (_state.addEvent >= 0 && !_ptmpl->FIsTdt())
    {
        _track.Events()->GetFixed(_state.addEvent, &aev);
        _track.Route()->Get(aev.rtel.irpt, &rpt);
        if (rZero != rpt.dwr) // ie, non-static path segment
        {
            // _state.transform.waPath is current and CalcRteOrient() cannot be called
            // in all cases
            SetAddOrient(_state.transform.xaPath, _state.transform.yaPath, _state.transform.zaPath, grfbra);
        }
    }
}

/***************************************************************************

    Delete the path and events prior to this frame

***************************************************************************/
void Actor::DeleteBackCore(bool *pfAlive)
{
    AssertThis(0);
    AssertNilOrVarMem(pfAlive);

    long iaev;
    long iaevNew;
    Base aev;
    BRS dwrOffsetT;
    BRS dwrNew;
    BRS dwrOld;
    RouteDistancePoint rptOld;
    long dnrpt;

    // Nop if not yet at first frame
    if (_state.frame <= _nfrmFirst)
    {
        if (pvNil != pfAlive)
            *pfAlive = FPure(_track.Events()->IvMac() > 0);
        return;
    }

    // We need to update the nfrm field in aetAdd events in order to update
    // _nfrmFirst below.
    if (_fLifeDirty && !_FComputeLifetime())
    {
        // Below the new _nfrmFirst will be wrong, but that means the actor
        // will appear in the wrong frame (iff _fLifeDirty and the next subroute
        // got moved).  Not much we can do about it.
        PushErc(ercSocBadFrameSlider);
    }

    // If the current point is between nodes, a node will
    // be inserted as the new first node of the subroute.
    // Compute the dwr of the new node.
    _track.Route()->Get(_state.location.irpt, &rptOld);
    dwrOld = rptOld.dwr;
    dwrNew = BrsSub(dwrOld, _state.location.dwrOffset);

    // Preserve the current orientation
    _SaveCurPathOrien();

    // Adjust offsets for events occurring at points between the
    // previous and next nodes
    for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.rtel.irpt > _state.location.irpt)
            break;

        if (aev.rtel.dwrOffset == _state.location.dwrOffset)
        {
            aev.rtel.dwrOffset = rZero;
            if (aev.rtel.dnfrm < _state.location.dnfrm)
            {
                Bug("Event list out of order (dnfrm error)");
                aev.rtel.dnfrm = 0;
            }
            else
            {
                aev.rtel.dnfrm -= _state.location.dnfrm;
            }
        }

        if (dwrNew == rZero)
            aev.rtel.dwrOffset = rZero;
        else
        {
            // Set the event's dwrOffset to the correct
            // part of the new dwr
            dwrOffsetT = BrsSub(aev.rtel.dwrOffset, _state.location.dwrOffset);
            aev.rtel.dwrOffset = dwrOffsetT;
        }
        _track.Events()->PutFixed(iaev, &aev);
    }

    //
    // Alter the route's new first point to be _state.location.
    // with the correct new dwr.  Then update _state.location.
    //
    _GetXyzFromRtel(&_state.location, &rptOld.xyz);
    rptOld.dwr = dwrNew;
    _track.Route()->Put(_state.location.irpt, &rptOld);
    dnrpt = _state.location.irpt;
    _state.location.dnfrm = 0;
    _state.location.dwrOffset = rZero;
    _state.location.irpt = 0;

    //
    // Merge the earlier events
    // Update the necessary state variables
    //
    for (iaev = 0; iaev < _state.nextEvent; iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (!_state.onStage || iaev < _state.addEvent)
        {
            _RemoveAev(iaev);
            iaev--;
            continue;
        }

        switch (aev.aet)
        {
        case aetRotH:
#ifdef BUG1870
            // Supercede orient-rotate with current rotation
            // 		so that static segment orientation will be preserved
            // Note: events prior to _state.firstEventInFrame need to be included
            // 		due to orient-rotations lasting the lifetime of static
            // 		segments.
            _track.Events()->Put(iaev, &_state.transform.bmat34Cur);
            goto LDefault;
#endif // BUG1870
        case aetTweak:
        case aetSnd:
            if (iaev < _state.firstEventInFrame)
            {
                _RemoveAev(iaev);
                iaev--;
                break;
            }
            if (iaev > 0)
                goto LDefault;
            break;

        case aetActn:
            Action aevactn;
            _track.Events()->Get(iaev, &aevactn);
            aevactn.celn = _state.cel;
            _track.Events()->Put(iaev, &aevactn);
            goto LDefault;

        case aetRem:
            _RemoveAev(iaev);
            iaev--;
            Bug("Offstage case should be already handled");
            break;

        default:
        LDefault:
            aev.rtel = _state.location;
            aev.nfrm = _state.frame;
            _track.Events()->PutFixed(iaev, &aev);
            if (iaev > 0)
                _MergeAev(0, iaev, &iaevNew);
            else
                iaevNew = iaev;

            if (iaevNew < iaev)
                iaev--;
            if (aev.aet == aetAdd)
                _state.addEvent = iaevNew;
            if (aev.aet == aetActn)
                _state.actionEvent = iaevNew;
            break;
        }
    }

    //
    // Adjust the irpt's of the later events
    // Adjust the absolute beginning frame number
    // Delete the first section of the route
    // NOTE: There is no translation in time
    //
    if (dnrpt > 0)
    {
        // Delete the first section of the route
        // If offstage, also delete the current point
#ifdef BUG1866
        if (!_state.onStage)
            dnrpt++;
#endif // BUG1866
        for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
        {
            _track.Events()->GetFixed(iaev, &aev);
            aev.rtel.irpt -= dnrpt;
            _track.Events()->PutFixed(iaev, &aev);
        }
#ifndef BUG1866
        if (!_state.onStage)
            dnrpt++;
#endif //! BUG1866
        _track.Route()->Delete(0, dnrpt);
    }

    if (pvNil != pfAlive)
        *pfAlive = FPure(_track.Events()->IvMac() > 0);

    if (_track.Events()->IvMac() == 0)
    {
        _InitState();
    }
    else
    {
        // Adjust remaining state variables
        _track.Events()->GetFixed(0, &aev);
        Assert(aev.aet == aetAdd, "An aetAdd event should be the first event");
        _nfrmFirst = aev.nfrm;
        _fLifeDirty = fTrue;
        _state.firstEventInFrame = 0;
    }

    if (!_FComputeLifetime())
        PushErc(ercSocBadFrameSlider);
    _pscen->InvalFrmRange();

    return;
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

    Base aev;
    long iaev;
    RouteDistancePoint rptNode1;
    RouteDistancePoint rptNode2;
    long iaevLim = _state.nextEvent;
    long irpt = _state.location.irpt;

    if (_state.location.dwrOffset == rZero)
        return;

    // Store the new length between nodes
    _track.Route()->Get(irpt, &rptNode1);
    rptNode1.dwr = _state.location.dwrOffset;
    _track.Route()->Put(irpt, &rptNode1);

    // Move node 2 rather than inserting a node
    _GetXyzFromRtel(&_state.location, &rptNode2.xyz);
    rptNode2.dwr = rZero;
    Assert(irpt + 1 < _track.Route()->IvMac(), "Error in truncation");
    _track.Route()->Put(irpt + 1, &rptNode2);

    // Update _state.location
    _state.location.dnfrm = 0;
    _state.location.dwrOffset = rZero;
    _state.location.irpt++;

    // Update aev's rtel's
    for (iaev = _state.nextEvent - 1; iaev >= 0; iaev--)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.rtel.irpt < irpt)
            break;
        if (aev.rtel.dwrOffset >= rptNode1.dwr)
        {
            aev.rtel.dwrOffset = rZero;
            aev.rtel.irpt++;
            _track.Events()->PutFixed(iaev, &aev);
        }
    }

    // Delete remaining subroute
    for (irpt = irptDelLim - 1; irpt > _state.location.irpt; irpt--)
    {
        _AdjustAevForRteDel(irpt, 0);
        _track.Route()->Delete(irpt);
        _fLifeDirty = fTrue;
    }
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
    AssertPo(_pglsmm, 0);

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
    MarkMemObj(_pglsmm);
    _tagTmpl.MarkMem();
}

#endif // DEBUG

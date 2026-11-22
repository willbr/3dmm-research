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

    The route is translated by _owner.runtime.origin in class Actor.
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
#include "mm/actor_effect.h"
#include "mm/actor_placement.h"

ASSERTNAME

using namespace ActorEvent;

RTCLASS(Actor)

/***************************************************************************

    Constructor for Actor - private.

***************************************************************************/
Actor::Actor(void)
{
    mm_actor_owner_init(&_owner);
}

/***************************************************************************

    Destructor for an Actor

***************************************************************************/
Actor::~Actor(void)
{
    AssertBaseThis(0);
    const auto services=_OwnerServices(); mm_actor_owner_dispose(this,&services);
}

/***************************************************************************

    Initialize the actor
    The actor will not yet be grounded to any initial scene frame.

***************************************************************************/
void *Actor::_FetchTemplate(void *, const MMTag *value)
{
    TAG tag;
    TagFromCore(&tag, *value);
    return EngineTemplate::Fetch(&tag);
}

MMActorCreateServices Actor::_CreateServices()
{
    MMActorCreateServices s{}; s.owner=_OwnerServices();
    s.fetch_template = _FetchTemplate;
    s.track_allocator = CollectionAllocator();
    s.trash_unused = DebugShip(1, 0);
    s.action_flags=[](void *,void *p,MMI32 action,MMU32 *flags) {
        LegacyUlong value=*flags; bool ok=EngineTemplate::FGetGrfactn(static_cast<PTemplate>(p), action,&value);
        *flags=value; return int(ok);
    };
    s.create_body=[](void *,void *p)->void * { return EngineTemplate::PbodyCreate(static_cast<PTemplate>(p)); };
    s.bind_body=[](void *,void *body,void *scene) { BodyWorld::Bind(static_cast<PBody>(body),static_cast<PScene>(scene)->Pmvie()->Pbwld()); };
    s.diagnostic=[](void *,int fault) {
        if (fault==MM_CREATE_WRONG_SCENE) { Bug("SetPscen logic error"); }
        else { Assert(fault==MM_CREATE_BOUND_WITHOUT_BODY || fault==MM_CREATE_BODY_WITHOUT_SCENE,"Unknown binding fault"); Bug("Bad body pointer"); }
    };
    return s;
}
bool Actor::_FInit(TAG *ptagTmpl)
{
    AssertBaseThis(0); AssertVarMem(ptagTmpl);
    MMTag tag; std::memcpy(&tag,ptagTmpl,sizeof(tag)); const auto services=_CreateServices();
    return mm_actor_initialize(this,&tag,&services)!=0;
}

/***************************************************************************

    Initialize the transformation matrix & factors
    (Sets the absolute scale = 1, and use the rest orientation)

***************************************************************************/
void Actor::_InitXfrm(void)
{
    AssertThis(0);
    mm_actor_state_init_transform(&_owner.runtime.state);
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
    grounded in time (_owner.runtime.first) nor space (_owner.runtime.origin).

***************************************************************************/
PActor Actor::PactrNew(TAG *ptagTmpl)
{
    AssertVarMem(ptagTmpl);
    MMTag tag; std::memcpy(&tag,ptagTmpl,sizeof(tag)); const auto services=_CreateServices();
    return static_cast<PActor>(mm_actor_create(&tag,&services));
}

/***************************************************************************

    Create the groups _owner.track.events, _owner.track.route and motion-sound storage

***************************************************************************/
bool Actor::_FCreateGroups(void)
{
    AssertBaseThis(0); const auto services=_CreateServices();
    return mm_actor_create_groups(&_owner,&services)!=0;
}

/***************************************************************************

    Set the owning scene and the brender world for the body.

***************************************************************************/
void Actor::SetPscen(Scene *pscen)
{
    AssertBaseThis(0); AssertVarMem(pscen); const auto services=_CreateServices();
    mm_actor_bind_scene(&_owner.runtime,pscen,&services);
}

/***************************************************************************

    Hide the Actor and initialize the Actor State Variables

***************************************************************************/
void Actor::_InitState(void)
{
    AssertBaseThis(0);
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    mm_actor_route_edit_reset(&state,_owner.track.route,&tables,&services);
    _owner.runtime.state = state;
}

/***************************************************************************

    Set the actor state variables to the rewound position

***************************************************************************/
void Actor::_SetStateRewound(void)
{
    AssertBaseThis(0);
    const auto services = _OwnerServices();
    mm_actor_owner_rewind(this, &services);
}

/***************************************************************************

    Prepare the actor for display in frame nfrm.

    nfrm can be any frame number in the movie - it is actor independent.
    NOTE: The state of this actor is expected to be current for frame _nfrmCur
    at the time this routine is called.
    FGotoFrame exits with _nfrmCur == nfrm.
    (A scene level interrogation of all actors should find
    that each actor's _nfrmCur is the same).

    Position dirtiness spans all the intermediate frames of the seek.

***************************************************************************/
bool Actor::FGotoFrame(long nfrm, bool *pfSoundInFrame)
{
    AssertThis(0);
    AssertIn(nfrm, klwMin, klwMax);
    const auto effects = _PlaybackEffects();
    const auto timeline = _TimelineServices();
    int sound = 0;
    const bool success = mm_actor_owner_seek(&_owner, nfrm, &sound, &timeline, &effects) > 0;
    if (pfSoundInFrame && sound)
        *pfSoundInFrame = true;
    return success;
}

MMActorPlaybackEffects Actor::_PlaybackEffects()
{
    MMActorPlaybackEffects effects = {};
    effects.context = this;
    effects.hide = [](void *p) {
        if (auto body = static_cast<Actor *>(p)->_Body())
            EngineBody::Hide(body);
    };
    effects.reset_costume = [](void *p) -> int {
        auto actor = static_cast<Actor *>(p);
        return EngineTemplate::FSetDefaultCost(actor->_Template(), actor->_Body());
    };
    effects.apply_event = [](void *p, MMI32 index) -> int { return static_cast<Actor *>(p)->_FDoAevCore(index); };
    effects.suppress_sounds = [](void *p) -> int {
        return FPure(static_cast<Actor *>(p)->_Scene()->GrfScen() & fscenSounds);
    };
    effects.motion_sounds = [](void *p) -> int { return static_cast<Actor *>(p)->_FEnqueueSmmInMsq(); };
    effects.position_body = [](void *p) {
        auto actor = static_cast<Actor *>(p);
        actor->_PositionBody(&actor->_owner.runtime.state.position);
    };
    effects.select_cel = [](void *p) -> int {
        auto actor = static_cast<Actor *>(p);
        return EngineTemplate::FSetActnCel(actor->_Template(), actor->_Body(), actor->_owner.runtime.state.action, actor->_owner.runtime.state.cel, pvNil);
    };
    effects.update_pose = [](void *p) {
        auto actor = static_cast<Actor *>(p);
        BMAT34 matrix;
        actor->_MatrixRotUpdate(&actor->_owner.runtime.state.position, &matrix);
    };
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    effects.seek_failed = [](void *p, MMI32 frame) {
        const auto batch = std::getenv("THREEDMM_BATCH_TEST");
        if (batch && std::strcmp(batch, "1") == 0)
        {
            const auto &runtime = static_cast<Actor *>(p)->_owner.runtime;
            std::fprintf(stderr, "BATCH_ACTOR_FRAME_FAILED actor=%ld frame=%ld action=%ld cel=%ld event=%ld "
                                 "template_sid=%ld template_cno=%08lx\n",
                         static_cast<long>(runtime.actor), static_cast<long>(frame), static_cast<long>(runtime.state.action),
                         static_cast<long>(runtime.state.cel), static_cast<long>(runtime.state.nextEvent),
                         static_cast<long>(runtime.template_tag.sid), static_cast<unsigned long>(runtime.template_tag.number));
        }
    };
#endif
    return effects;
}

/***************************************************************************

    FReplayFrame : Replays the sound for the current frame
        -> Re-enqueues sounds for the current frame
    grfscen - Things that are supposed to be played right now.

***************************************************************************/
bool Actor::FReplayFrame(long grfscen)
{
    AssertThis(0);
    const auto motion = EngineActorMotion::Services();
    return mm_actor_replay_frame_sounds(&_owner.runtime.state, _owner.track.events, (grfscen & fscenSounds) != 0, &motion, this,
                                        [](void *p, MMI32 index) -> int { return static_cast<Actor *>(p)->_FDoAevCore(index); },
                                        [](void *p) -> int { return static_cast<Actor *>(p)->_FEnqueueSmmInMsq(); }) > 0;
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

    if (!EngineTemplate::FGetGrfactn(_Template(), anid, &grfactn))
        return fFalse;

    if (ivNil == _owner.runtime.state.actionEvent)
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
    Assert(_owner.track.events, "missing actor events");
    AssertIn(iaev, 0, mm_actor_events_count(_owner.track.events) + 1);
    const auto motion = EngineActorMotion::Services();
    return mm_actor_subroute_events_done(_owner.track.events, iaev, &rtel, &motion) != 0;
}

/***************************************************************************

    Return functional (sized) step size when playing
    Zero is a valid return value

***************************************************************************/
MMActorTimelineServices Actor::_TimelineServices()
{
    auto services = mm_actor_track_timeline_services();
    services.context = this;
    services.resolve = [](void *context, MMI32 action, MMI32 cel, MMI32 *out) -> int {
        BRS step = *out;
        const bool ok = EngineTemplate::FGetDwrActnCel(static_cast<Actor *>(context)->_Template(), action, cel, &step);
        *out = step;
        return ok;
    };
    services.diagnostic = EngineActorTimeline::Diagnostic;
    return services;
}

/***************************************************************************

    Return functional (sized) step size when recording.
    The returned size is expected to be authored > 0

***************************************************************************/
bool Actor::_FGetDwrRecord(BRS *pdwr)
{
    AssertBaseThis(0); AssertVarMem(pdwr);
    const auto state=_owner.runtime.state;
    const auto services=_SubrouteEditServices();
    MMI32 distance=0;
    bool ok=mm_actor_record_step(&state,&distance,&services.timeline)!=0;
    *pdwr=distance; return ok;
}

/***************************************************************************

    Execute the current event.
    State Variables are updated.

***************************************************************************/
bool Actor::_FDoAevCur(void)
{
    AssertBaseThis(0);

    if (!_FDoAevCore(_owner.runtime.state.nextEvent))
        return fFalse;

    _owner.runtime.state.nextEvent++;
    Assert(_owner.runtime.state.nextEvent <= mm_actor_events_count(_owner.track.events), "_owner.runtime.state.nextEvent bug");

    return fTrue;
}

/***************************************************************************

    Execute the event iaev.

***************************************************************************/
bool Actor::_FDoAevCore(long iaev)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, mm_actor_events_count(_owner.track.events));
    MMActorEffectRenderer renderer;
    mm_brender_actor_effect_renderer(&renderer);
    MMActorEffectHooks hooks = {};
    hooks.context = this;
    hooks.visibility = [](void *p, int shown) {
        auto body = static_cast<Actor *>(p)->_Body();
        if (shown)
            EngineBody::Show(body);
        else
            EngineBody::Hide(body);
    };
    hooks.fetch_material = [](void *, const MMTag *value) -> MMMaterialOwner * {
        TAG tag;
        TagFromCore(&tag, *value);
        return static_cast<MMMaterialOwner *>(vptagm->FetchResource(&tag, EngineMaterial::Read, mm_material_owner_services()));
    };
    hooks.enqueue_sound = [](void *p, MMI32 index) -> int {
        return static_cast<Actor *>(p)->_FEnqueueSnd(index);
    };
    return mm_actor_runtime_effect(&_owner.runtime.state, &_owner.track, iaev, &_owner.runtime.origin,
                                   _Template(), _Body(), &_owner.sounds, 0, &renderer, &hooks) != 0;
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
    Assert(_owner.runtime.state.onStage || aetNew == aetAdd, "Error! Beginning subroute without Add");
#ifndef BUG1870
#error C89 actor editing requires the enabled retail BUG1870 policy
#endif
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto edits=_EventEditServices();
    bool ok=mm_actor_edit_add_apply(&state,&_owner.sounds,_owner.track.events,_owner.runtime.actor,aetNew,cbNew,pvVar,
                                          &tables,&edits)!=0;
    _owner.runtime.state = state;
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
    AssertIn(iaevFirst, 0, mm_actor_events_count(_owner.track.events));
    AssertIn(iaevNew, iaevFirst, mm_actor_events_count(_owner.track.events));
    AssertNilOrVarMem(piaevRtn);
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto edits=_EventEditServices();
    MMI32 result=0;
    mm_actor_edit_merge(&state,&_owner.sounds,_owner.track.events,_owner.runtime.actor,iaevFirst,iaevNew,
                              piaevRtn ? &result : nullptr,&tables,&edits);
    _owner.runtime.state = state;
    if (piaevRtn) *piaevRtn=result;
}

/***************************************************************************

    Add (or replace) an action
    Add the event to the event list

***************************************************************************/
MMActorActionEditServices Actor::_ActionEditServices()
{
    static_assert(factnStatic==8 && kdwrNil==-65536,"actor action policy ABI");
    MMActorActionEditServices s={}; s.context=this; s.sounds=_SoundEditServices();
    s.action_flags=[](void *p,MMI32 action,MMU32 *flags) {
        LegacyUlong value=0; bool ok=EngineTemplate::FGetGrfactn(static_cast<Actor *>(p)->_Template(), action,&value); *flags=value; return int(ok);
    };
    s.text_template=[](void *p) { return int(EngineTemplate::FIsTdt(static_cast<Actor *>(p)->_Template())); };
    s.mark_life_dirty=[](void *p) { static_cast<Actor *>(p)->_owner.runtime.life_dirty=true; };
    return s;
}
MMActorSubrouteEditServices Actor::_SubrouteEditServices()
{
    MMActorSubrouteEditServices s={}; s.route=_RouteEditServices(); s.action=_ActionEditServices();
    s.timeline = _TimelineServices();
    s.lifetime = mm_actor_owner_lifetime(&_owner);
    s.pose_inputs=[](void *p,MMActorPoseInputs *inputs) {
        auto actor=static_cast<Actor *>(p); EngineTemplate::GetRestOrien(actor->_Template(), &inputs->restX,&inputs->restY,&inputs->restZ);
        inputs->fixedPathOrientation=EngineTemplate::FIsTdt(actor->_Template());
    };
    s.locate_body=[](void *p,const MMActorBodyPose *pose) {
        EngineBody::ApplyPose(static_cast<Actor *>(p)->_Body(), *pose);
    };
    s.live = &_owner;
    s.playback = _PlaybackEffects();
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case MM_SUBROUTE_BAD_LIFETIME: PushErc(ercSocBadFrameSlider); break;
        case MM_SUBROUTE_BAD_HOLD: Bug("Event list out of order (dnfrm error)"); break;
        case MM_SUBROUTE_OFFSTAGE_EVENT: Bug("Offstage case should be already handled"); break;
        }
    };
    return s;
}
/***************************************************************************

    Remove an actor from the stage
    NOTE: This is a low level API and has no effect on the event list

***************************************************************************/
void Actor::_Hide(void)
{
    AssertBaseThis(0);
    auto state=_owner.runtime.state;
    const auto services=_RouteEditServices();
    mm_actor_route_edit_hide(&state,&services);
    _owner.runtime.state = state;
}

/***************************************************************************

    Insert a node point *prpt in the route.
    Update *prpt.dwr, and the previous node's dwr.
    On input:
        *prpt.dwr == 0 iff the new node is a subpath terminating node
        *prpt.xyz has the new node coordinates
        dwrPrior is the distance from the previous node to the new one.

***************************************************************************/
MMActorRouteEditServices Actor::_RouteEditServices()
{
    MMActorRouteEditServices s={}; s.edits=_EventEditServices();
    mm_actor_route_edit_bind_tables(&s);
    s.life_dirty=[](void *p) { static_cast<Actor *>(p)->_owner.runtime.life_dirty=true; };
    s.recording=[](void *p,int enabled) { static_cast<Actor *>(p)->_owner.runtime.recording.recording=enabled!=0; };
    s.hide_body=[](void *p) { auto actor=static_cast<Actor *>(p); if (actor->_Body()) EngineBody::Hide(actor->_Body()); };
    s.default_costume=[](void *p) { auto actor=static_cast<Actor *>(p); if (actor->_Template() && actor->_Body()) EngineTemplate::FSetDefaultCost(actor->_Template(), actor->_Body()); };
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case MM_ROUTE_BAD_ALIVE: Bug("Invalid fAlive value"); break;
        case MM_ROUTE_FREEZE_FAILED: Assert(false,"Expected freeze event to succeed"); break;
        case MM_ROUTE_STEP_FAILED: Assert(false,"Expected set step event to succeed"); break;
        }
    };
    return s;
}
bool Actor::_FInsertGgRpt(long irpt, RouteDistancePoint *prpt, BRS dwrPrior)
{
    AssertBaseThis(0); AssertIn(irpt,0,mm_actor_path_count(_owner.track.route)+1); AssertVarMem(prpt);
    static_assert(rEps==1,"actor route epsilon ABI");
    MMActorRoutePoint point; std::memcpy(&point,prpt,sizeof(point));
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    bool ok=mm_actor_route_edit_insert(_owner.track.route,irpt,&point,dwrPrior,&tables,&services)!=0;
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
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_EventEditServices();
    bool ok=mm_actor_edit_set_step(&state,&_owner.sounds,_owner.track.events,_owner.runtime.actor,dwrStep,&tables,&services)!=0;
    _owner.runtime.state = state;
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
    EngineBackground::GetActorPlacePoint(_Scene()->Pbkgd(), &x, &y, &z);
    MMActorPoint point{x, y, z};
    MMActorPlacementServices services{};
    services.context = this;
    services.camera = [](void *context, MMActorMatrix *matrix, MMI32 *hither, MMI32 *yon) {
        auto *actor = static_cast<Actor *>(context);
        BMAT34 camera;
        BRS nearClip, farClip;
        actor->_Scene()->Pmvie()->Pbwld()->GetCamera(&camera, &nearClip, &farClip);
        Assert(nearClip >= 0 && farClip >= 0, "Negative camera values");
        *hither = nearClip;
        *yon = farClip;
        std::memcpy(matrix, &camera, sizeof(camera));
    };
    mm_actor_placement(&point, EngineTemplate::FIsTdt(_Template()), &services);
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
MMActorStageEditServices Actor::_StageEditServices()
{
    static_assert(knfrmInvalid==2147483647 && kcbVarAdd==20 && kcbVarActn==8 &&
                  kcbVarFreeze==4 && kcbVarStep==4 && kcbVarRot==48 && kcbVarSize==4 && kcbVarPull==12,
                  "Stage edit ABI");
    MMActorStageEditServices s{}; s.subroute=_SubrouteEditServices();
    TrashVar(&s.external_child);
    s.placement=[](void *p,MMActorPoint *point,MMActorPoint *origin,unsigned short *yaw) {
        auto actor=static_cast<Actor *>(p); BRS x,y,z;
        actor->_GetNewOrigin(&x,&y,&z); *point={x,y,z};
        std::memcpy(origin,&actor->_owner.runtime.origin,sizeof(*origin));
        *yaw=EngineBackground::BraRotYCamera(actor->_Scene()->Pbkgd());
    };
    s.diagnostic=[](void *,int fault) { Assert(fault==MM_STAGE_INSERT_FAILED,"Unknown stage edit fault"); Bug("Logic error"); };
    return s;
}
MMActorStageEditServices Actor::_BindStageEdit(void *actor)
{
    return static_cast<PActor>(actor)->_StageEditServices();
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
    AssertBaseThis(0); AssertIn(iaevLim,0,mm_actor_events_count(_owner.track.events)+1);
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    bool ok=mm_actor_subroute_from_previous(&state,&_owner.sounds,_owner.track.route,_owner.track.events,_owner.runtime.actor,
                                                  iaevLim,grfaet,&tables,&services)!=0;
    _owner.runtime.state = state;
    return ok;
}

/***************************************************************************

    Gather the initialization events of the subsequent subroute and insert
    them at the	current event index, _iaevCur.

***************************************************************************/
bool Actor::_FAddAevFromLater(void)
{
    AssertBaseThis(0); Assert(_owner.runtime.state.nextEvent==1,"_FAddAevFromLater logic error");
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    ChildChunkID external=0; TrashVar(&external);
    bool ok=mm_actor_subroute_from_later(&state,&_owner.sounds,_owner.track.route,_owner.track.events,_owner.runtime.actor,
                                               external,&tables,&services)!=0;
    _owner.runtime.state = state;
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
    *piaevAdd=mm_actor_route_edit_find(_owner.track.events,aet,iaevCur,1,&tables);
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
    *piaevAdd=mm_actor_route_edit_find(_owner.track.events,aet,iaevCur,0,&tables);
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
    *piaevLast=mm_actor_route_edit_last(_owner.track.events,iaevAdd,iaevLim,&tables);
}

/***************************************************************************

    Remove the actor from the stage
    Add the event to the event list

***************************************************************************/
/***************************************************************************

    Delete the events and point corresponding to a single point subroute

***************************************************************************/
void Actor::_DelAddFrame(long iaevAdd, long iaevLim)
{
    AssertThis(0); AssertIn(iaevAdd,0,iaevLim);
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    mm_actor_route_edit_delete_add(&state,_owner.track.route,_owner.track.events,iaevAdd,iaevLim,&tables,&services);
    _owner.runtime.state = state;
}

/***************************************************************************

    Set the Costume for a body part
    Add the event to the event list

***************************************************************************/
bool Actor::FSetCostumeCore(long ibsetClicked, TAG *ptag, long cmid, tribool fCmtl)
{
    AssertThis(0); Assert(fCmtl || ibsetClicked>=0,"Invalid body set"); AssertVarMem(ptag);
    MMTag tag; std::memcpy(&tag,ptag,sizeof(tag));
    const auto services=_CostumeEditServices();
    return mm_actor_owner_set_costume(&_owner,ibsetClicked,&tag,cmid,fCmtl,&services)!=0;
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
    AssertThis(0); AssertIn(_owner.runtime.state.addEvent,0,mm_actor_events_count(_owner.track.events));
    static_assert(kdwrFast==196608 && krOriWeightMin==6553,"actor orientation weight ABI");
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto edits=_EventEditServices();
    MMI32 distance=0;
    if (pdxyz) distance=BR_LENGTH3(pdxyz->dxr,pdxyz->dyr,pdxyz->dzr);
    mm_actor_edit_add_orientation(&state,_owner.track.events,xa,ya,za,grfbra,pdxyz?&distance:nullptr,&tables,&edits);
    _owner.runtime.state = state;
}

/***************************************************************************

    Average two angles by (rw1 * a1 + (1-rw1)*a2)
    rw1 is the weighting for a1

***************************************************************************/
BRA Actor::_BraAvgAngle(BRA a1, BRA a2, BRS rw1)
{
    AssertBaseThis(0); AssertIn(rw1,rZero,rOne+rEps);
    return mm_actor_edit_average_angle(a1,a2,rw1);
}

/***************************************************************************

    Rotate the actor
    Add the event to the event list
    If !fFromHereFwd, the rotation is to apply to the current frame only

    WARNING: Rotations are non-abelian.  Ordering is x then y then z.
    If ordering other than this is desired, separate calls must be made.

***************************************************************************/
MMActorTransformEditServices Actor::_TransformEditServices()
{
    static_assert(fnormSize==MM_NORMAL_SIZE && fnormRotate==MM_NORMAL_ROTATE &&
                  fmafGround==MM_MOVE_GROUND && fmafOrient==MM_MOVE_ORIENT &&
                  fmafEntireScene==MM_MOVE_SCENE && fmafEntireSubrte==MM_MOVE_SUBROUTE &&
                  krScaleMinFactor==6553 && krScaleMin==16384 && krScaleMax==655360 && krAngleMax==26214,
                  "Actor transform edit ABI");
    Assert(krScaleMaxFactor==mm_fixed_div(65536,6553) && krPullMax==262144 &&
           krAngleMinRcp==mm_fixed_div(65536,6553),"Actor transform bounds ABI");
    MMActorTransformEditServices s{}; s.subroute=_SubrouteEditServices(); s.fill=_FillEditServices();
    s.scene_present=[](void *p) { return int(static_cast<Actor *>(p)->_Scene()!=nullptr); };
    s.normalise=[](void *,MMActorMatrix *value) {
        BMAT34 matrix,normal; std::memcpy(&matrix,value,sizeof(matrix)); BrMatrix34LPNormalise(&normal,&matrix);
        std::memcpy(value,&normal,sizeof(normal));
    };
    s.distance=[](void *,const MMActorPoint *point) { return MMI32(BR_LENGTH3(point->dxr,point->dyr,point->dzr)); };
    s.publish_origin=[](void *p,const MMActorPoint *origin) { std::memcpy(&static_cast<Actor *>(p)->_owner.runtime.origin,origin,sizeof(*origin)); };
    s.seek_scene=[](void *p,MMActorState *state,MMI32 frame) {
        auto actor=static_cast<Actor *>(p); bool ok=actor->_Scene()->FGotoFrm(frame);
        *state=actor->_owner.runtime.state; return int(ok);
    };
    s.scrollbars=[](void *p) { static_cast<Actor *>(p)->_Scene()->Pmvie()->Pmcc()->UpdateScrollbars(); };
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case MM_TRANSFORM_BAD_LIFETIME: PushErc(ercSocBadFrameSlider); break;
        case MM_TRANSFORM_ADD_FAILED: Bug("EnsureSpace insufficient"); break;
        case MM_TRANSFORM_MOVE_ROTATE_FAILED: Bug("Should have ensured space"); break;
        }
    };
    return s;
}
bool Actor::FRotate(BRA xa, BRA ya, BRA za, bool fFromHereFwd)
{
    AssertThis(0);
    const unsigned short angles[] = {xa, ya, za};
    const auto services = _TransformEditServices();
    return mm_actor_owner_rotate(&_owner, angles, fFromHereFwd, 0, &services) != 0;
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
    const unsigned short angles[] = {xa, ya, za};
    const auto services = _TransformEditServices();
    return mm_actor_owner_rotate(&_owner, angles, fFromHereFwd, 1, &services) != 0;
}

/***************************************************************************

    "Normalize" the actor
    The revert tool
    Insert normalize transformation events into the event list

***************************************************************************/
bool Actor::FNormalizeCore(ulong grfnorm)
{
    AssertThis(0);
    const auto services=_TransformEditServices();
    return mm_actor_owner_normalize(&_owner,grfnorm,&services)!=0;
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
    const auto services = _TransformEditServices();
    return mm_actor_owner_scale(&_owner, rScale, &services) != 0;
}

/***************************************************************************

    Drag the actor forwards or backwards in time
    NOTE** Drag this subroute only -> sliding other subroutes in time

***************************************************************************/
bool Actor::FSoonerLater(long dnfrm)
{
    AssertThis(0);
    const auto services = _TransformEditServices();
    return mm_actor_owner_shift_time(&_owner, dnfrm, &services) != 0;
}

/***************************************************************************

    Pull, stretch, squash the actor.
    Add the event to the event list

    Bound the allowable scaling.

***************************************************************************/
bool Actor::FPull(BRS rScaleX, BRS rScaleY, BRS rScaleZ)
{
    AssertThis(0);
    const auto services = _TransformEditServices();
    const MMActorStretch stretch{rScaleX, rScaleY, rScaleZ};
    return mm_actor_owner_pull(&_owner, &stretch, &services) != 0;
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
    const auto services = _TransformEditServices();
    const MMActorPoint delta{dxr, dyr, dzr};
    return mm_actor_owner_tweak(&_owner, &delta, grfmaf, &services) != 0;
}

/***************************************************************************

    Update xyzRte from the overall origin & the subroute translation

***************************************************************************/
void Actor::_UpdateXyzRte(void)
{
    AssertBaseThis(0);
    mm_actor_state_origin(&_owner.runtime.state, &_owner.runtime.origin);
}

/***************************************************************************

    Move the current subroute.	(Compose tool)

    If fmafOrient is set, the actor is oriented and moved.

    If fmafEntireSubrte is set, this does not add an event to the aev list.
    It alters the data in the Add event & _dxyzSubRte.	Return fTrue.

    If fmafEntireScene is set, _owner.runtime.origin is modified. Return fTrue.

    If !fmafEntireSubrte && !fmafEntireScene, a move event is added
    to the event list. This can therefore fail.

    Return success or failure
    Return *pfmoved indicating whether or not the path moved.

***************************************************************************/
bool Actor::FMoveRoute(BRS dxr, BRS dyr, BRS dzr, bool *pfMoved, ulong grfmaf)
{
    AssertThis(0);
    AssertNilOrVarMem(pfMoved);
    const auto services = _TransformEditServices();
    const MMActorPoint delta{dxr, dyr, dzr};
    MMActorMoveResult result{};
    result.context = pfMoved;
    result.publish = [](void *p, int moved) { if (p) *static_cast<bool *>(p) = moved != 0; };
    return mm_actor_owner_move(&_owner, &delta, grfmaf, &result, &services) != 0;
}

/***************************************************************************

    Load starting point orientation into state variable _xfrm

***************************************************************************/
void Actor::_LoadAddOrien(Add *paevadd, bool fNoReset)
{
    AssertThis(0);
    static_assert(sizeof(Add) == sizeof(MMActorEntrance) && offsetof(Add, xa) == 12 &&
                      offsetof(Add, ya) == 14 && offsetof(Add, za) == 16,
                  "actor entrance ABI");
    MMActorEntrance add;
    std::memcpy(&add, paevadd, sizeof(add));
    auto state = _owner.runtime.state;
    mm_actor_add_orientation(&state, &add, fNoReset);
    _owner.runtime.state = state;
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
    AssertIn(iaevMin,0,mm_actor_events_count(_owner.track.events)+1); AssertIn(iaevCmp,-1,mm_actor_events_count(_owner.track.events)+1);
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_FillEditServices();
    mm_actor_edit_transform_fill(&state,_owner.track.events,aet,pvVar,cbVar,iaevMin,iaevCmp,grfaet,&tables,&services);
    _owner.runtime.state = state;
}

/***************************************************************************

    Insert Aev.  Update state variables
    Since _owner.track.events is getting a copy of a tag, a call to DupTag is required

***************************************************************************/
MMActorFillServices Actor::_FillEditServices()
{
    MMActorFillServices services={};
    services.edits=_EventEditServices();
    services.lock_events=[](void *p) { mm_actor_events_lock(static_cast<MMActorEventTable *>(p)); };
    services.unlock_events=[](void *p) { mm_actor_events_unlock(static_cast<MMActorEventTable *>(p)); };
    services.payload_view=[](void *p, MMI32 index)->const void * { return mm_actor_events_payload(static_cast<MMActorEventTable *>(p), index, nullptr); };
    return services;
}
bool Actor::_FInsertAev(long iaev, long cbNew, void *pvVar, void *paev, bool fUpdateState)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, mm_actor_events_count(_owner.track.events)+1); AssertIn(cbNew, 0, 100);
    AssertPvCb(paev,sizeof(Base));
    MMActorEvent event; std::memcpy(&event,paev,sizeof(event));
    auto state=_owner.runtime.state;
    const auto edits=_EventEditServices();
    bool ok=mm_actor_edit_insert(&state,iaev,cbNew,pvVar,&event,fUpdateState,&edits)!=0;
    _owner.runtime.state = state;
    return ok;
}

/***************************************************************************

    Remove Aev.  Update state variables

***************************************************************************/
MMActorEditServices Actor::_EventEditServices()
{
    MMActorEditServices services = {};
    services.context = this;
    services.tables = mm_actor_edit_tables(_owner.track.events, &EngineActorTagServices());
    services.scene_frame = [](void *context, MMI32 *frame) {
        auto actor = static_cast<Actor *>(context);
        if (!actor->_Scene()) return 0;
        *frame = actor->_Scene()->Nfrm();
        return 1;
    };
    services.update_sound_frame = [](void *context) { static_cast<Actor *>(context)->_Scene()->UpdateSndFrame(); };
    services.publish = [](void *context, const MMActorState *state) {
        static_cast<Actor *>(context)->_owner.runtime.state = *state;
    };
    services.apply_event = [](void *p, MMActorState *state, MMI32 index) {
        auto actor=static_cast<Actor *>(p);
        actor->_owner.runtime.state = *state;
        bool ok=actor->_FDoAevCore(index);
        *state=actor->_owner.runtime.state;
        return int(ok);
    };
    services.dirty = [](void *p) { static_cast<Actor *>(p)->_Scene()->MarkDirty(); };
    services.get_range = [](void *p, MMActorEditRange *range) {
        auto actor=static_cast<Actor *>(p); range->first=actor->_owner.runtime.first; range->last=actor->_owner.runtime.last;
    };
    services.invalidate_range = [](void *p) { static_cast<Actor *>(p)->_Scene()->InvalFrmRange(); };
    services.publish_range = [](void *p, const MMActorEditRange *range) {
        auto actor=static_cast<Actor *>(p); actor->_owner.runtime.first=range->first; actor->_owner.runtime.last=range->last;
    };
    return services;
}
void Actor::_RemoveAev(long iaev, bool fUpdateState)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, mm_actor_events_count(_owner.track.events));
    auto state = _owner.runtime.state;
    const auto tables = EngineActorMotion::Services();
    const auto edits = _EventEditServices();
    mm_actor_edit_remove(&state, _owner.track.events, iaev, fUpdateState, &tables, &edits);
    _owner.runtime.state = state;
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
    AssertBaseThis(0); AssertIn(iaevMin,0,mm_actor_events_count(_owner.track.events)+1);
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_EventEditServices();
    mm_actor_edit_action_fill(&state,_owner.track.events,iaevMin,anidPrev,anidNew,grfaet,&tables,&services);
    _owner.runtime.state = state;
}

/***************************************************************************

    Remove redundant costume changes
    Note:  	Delete all costume events that match the costume being replaced
            Then delete all subsequent costume events that match the
            costume being inserted

***************************************************************************/
MMActorCostumeEditServices Actor::_CostumeEditServices()
{
    MMActorCostumeEditServices services={}; services.context=this; services.edits=_EventEditServices();
    services.current=[](void *p,MMI32 set,int *custom)->void * {
        bool ordinary; PMaterial_MTRL material; PCustomMaterial_CMTL costume;
        EngineBody::GetPartSetMaterial(static_cast<Actor *>(p)->_Body(), set,&ordinary,&material,&costume);
        *custom=!ordinary;
        return ordinary ? static_cast<void *>(material) : static_cast<void *>(costume);
    };
    services.fetch_material=[](void *,const MMTag *value)->void * {
        TAG tag; std::memcpy(&tag,value,sizeof(tag));
        return vptagm->FetchResource(&tag, EngineMaterial::Read, mm_material_owner_services());
    };
    services.fetch_custom=[](void *p,MMI32 costume)->void * { return EngineTemplate::PcmtlFetch(static_cast<Actor *>(p)->_Template(), costume); };
    services.body_set=[](void *,void *resource)->MMI32 { return EngineCostume::Set(static_cast<PCustomMaterial_CMTL>(resource)); };
    services.release=[](void *,void *resource) { mm_material_owner_release(static_cast<MMMaterialOwner *>(resource)); };
    return services;
}
void Actor::_PrepCostFill(long iaevMin, Costume *paevcost)
{
    AssertBaseThis(0); AssertIn(iaevMin,0,mm_actor_events_count(_owner.track.events)+1); AssertVarMem(paevcost);
    MMActorCostume costume; std::memcpy(&costume,paevcost,sizeof(costume));
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_CostumeEditServices();
    mm_actor_edit_costume_fill(&state,_owner.track.events,iaevMin,&costume,&tables,&services);
    _owner.runtime.state = state;
}

/***************************************************************************

    AdjustAevForRte for an already inserted point at irptAdjust
    adjust the rtel's of the subsequent affected events

***************************************************************************/
void Actor::_AdjustAevForRteIns(long irptAdjust, long iaevMin)
{
    AssertBaseThis(0); AssertIn(irptAdjust,0,mm_actor_path_count(_owner.track.route)); AssertIn(iaevMin,0,mm_actor_events_count(_owner.track.events)+1);
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    mm_actor_route_edit_adjust_insert(_owner.track.route,_owner.track.events,irptAdjust,iaevMin,&tables,&services);
}
/***************************************************************************

    AdjustAevForRte for a to-be deleted point at irptAdjust
    adjust the rtel's of the affected events

***************************************************************************/
void Actor::_AdjustAevForRteDel(long irptAdjust, long iaevMin)
{
    AssertBaseThis(0); AssertIn(irptAdjust,0,mm_actor_path_count(_owner.track.route)); AssertIn(iaevMin,0,mm_actor_events_count(_owner.track.events)+1);
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    mm_actor_route_edit_adjust_delete(&state,_owner.track.route,_owner.track.events,irptAdjust,iaevMin,&tables,&services);
    _owner.runtime.state = state;
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
    ActorRouteEvaluator::Advance(_owner.track, dwrStep, prtel, iaevCur, nfrmCur, _owner.runtime.first, !_owner.runtime.recording.recording && !_owner.runtime.recording.rejoining,
                                 pfEndRoute);
}

/***************************************************************************

    Convert a route location (rtel) to an xyz point (in *pxyz)

***************************************************************************/
void Actor::_GetXyzFromRtel(RouteLocation *prtel, PRoutePoint pxyz)
{
    AssertBaseThis(0);
    ActorRouteEvaluator::Position(_owner.track, prtel, pxyz);
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
    const auto tables = EngineActorMotion::Services();
    const auto services = _SubrouteEditServices();
    const auto position = *pxyz;
    mm_actor_subroute_position_body(&_owner.runtime.state, _owner.track.route, _owner.track.events, &position, &tables, &services);
}

/***************************************************************************

    Update the orientation matrices
    _owner.runtime.state.transform.bmat34Cur must be kept current

***************************************************************************/
void Actor::_MatrixRotUpdate(RoutePoint *pxyz, BMAT34 *pbmat34)
{
    AssertBaseThis(0);
    ActorPoseInputs inputs;
    EngineTemplate::GetRestOrien(_Template(), &inputs.restX, &inputs.restY, &inputs.restZ);
    inputs.fixedPathOrientation = EngineTemplate::FIsTdt(_Template());
    ActorPoseEvaluator::Update(_owner.runtime.state, _owner.track, inputs, pxyz, pbmat34);
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
    ActorPoseEvaluator::ApplyVectorRotation(_owner.runtime.state.actionFlags, pxyz, pbmat34, pxa, pya, pza, pgrfbra);
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

    if (klwMax == _owner.runtime.first)
        return fFalse;

    if (pvNil != pnfrmFirst)
        *pnfrmFirst = _owner.runtime.first;

    if (pvNil == pnfrmLast)
        return fTrue;

    if (!_owner.runtime.life_dirty)
    {
        *pnfrmLast = _owner.runtime.last;
        Assert(_owner.runtime.last >= _owner.runtime.first, "fLifeDirty incorrect");
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
    index if the first aev edited when _owner.runtime.life_dirty is set.

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
    const auto timeline = _TimelineServices();
    const auto life = mm_actor_owner_lifetime(&_owner);
    MMI32 last;
    const bool ok = mm_actor_refresh_lifetime(_owner.track.route, _owner.track.events, pnfrmLast ? &last : nullptr,
                                              &life, &timeline) != 0;
    if (ok && pnfrmLast)
        *pnfrmLast = last;
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
    const bool stalled = ActorTimeline::FIsStalled(_owner.track, iaevFirst, prtel, piaevLast ? &lastEvent : pvNil);
    if (stalled && piaevLast)
        *piaevLast = lastEvent;
    return stalled;
}

/***************************************************************************

    Bind the Studio recording owner to native distance and scene services.

***************************************************************************/
MMActorRecordServices Actor::_RecordServices(const MMActorOwnerServices *owners)
{
#if !defined(BUG1961) || !defined(BUG1973)
#error C89 actor recording requires the enabled retail BUG1961/BUG1973 policies
#endif
    static_assert(kdtsThreshRte==750 && kdwrThreshRte==131072 && kdwrMax==2147418112 &&
                  fmafFreeze==MM_RECORD_FREEZE && fmafGround==MM_RECORD_GROUND && sizeof(ulong)==4,
                  "Actor recording ABI");
    MMActorRecordServices s{}; s.subroute=_SubrouteEditServices();
    s.live_actor=this; s.owners=owners;
    s.distance=[](void *,const MMActorPoint *point) { return MMI32(BR_LENGTH3(point->dxr,point->dyr,point->dzr)); };
    s.seek_scene=[](void *p,MMActorState *state,MMActorRecording *record,MMI32 frame) {
        auto actor=static_cast<Actor *>(p); bool ok=actor->_Scene()->FGotoFrm(frame);
        *state=actor->_owner.runtime.state; *record=actor->_owner.runtime.recording; return int(ok);
    };
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case MM_RECORD_BAD_LIFETIME: PushErc(ercSocBadFrameSlider); break;
        case MM_RECORD_UNFREEZE_FAILED: case MM_RECORD_FREEZE_FAILED: Bug("Ensurespace should have ensured space"); break;
        case MM_RECORD_STEP_FAILED: Bug("EnsureSpace insufficient"); break;
        }
    };
    return s;
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
    const auto services=_RouteEditServices();
    int alive=0;
    mm_actor_owner_delete_forward(&_owner,fDeleteAll,pfAlive?&alive:nullptr,iaevCur,&services);
    if (pfAlive) *pfAlive=alive!=0;
}

/***************************************************************************

    Delete the path and events beyond this frame
    **NOTE:  This does not send the actor offstage. See mm_actor_owner_stage_edit.
    NOTE: While _owner.runtime.life_dirty flags needed lifetime recomputation, scene's
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
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    int alive=0;
    mm_actor_route_edit_delete_forward_core(&state,_owner.track.route,_owner.track.events,fDeleteAll,pfAlive?&alive:nullptr,iaevCur,&tables,&services);
    _owner.runtime.state = state;
    if (pfAlive) *pfAlive=alive!=0;
}

/***************************************************************************

    Save the path specific part of the current orientation

***************************************************************************/
void Actor::_SaveCurPathOrien(void)
{
    AssertBaseThis(0);
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    mm_actor_subroute_save_orientation(&state,_owner.track.route,_owner.track.events,&tables,&services);
    _owner.runtime.state = state;
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
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_SubrouteEditServices();
    int alive=0;
    mm_actor_subroute_delete_back(&state,&_owner.sounds,_owner.track.route,_owner.track.events,_owner.runtime.actor,
                                        pfAlive?&alive:nullptr,&tables,&services);
    _owner.runtime.state = state;
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
    auto state=_owner.runtime.state;
    const auto tables=EngineActorMotion::Services();
    const auto services=_RouteEditServices();
    mm_actor_route_edit_truncate(&state,_owner.track.route,_owner.track.events,irptDelLim,&tables,&services);
    _owner.runtime.state = state;
}

/***************************************************************************

    Is the mouse point within this actor.

***************************************************************************/
bool Actor::FPtIn(long xp, long yp, long *pibset)
{
    AssertThis(0);
    AssertVarMem(pibset);

    return BodyWorld::FPtInBody(_Body(), _Scene()->Pmvie()->Pbwld(), xp, yp, pibset);
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
    Assert(nfrmRenderLast >= _owner.runtime.state.frame, "Invalid argument to FMustRender");
    Assert(!_owner.runtime.life_dirty, "FMustRender was called when nfrm values were invalid");

    const auto motion = EngineActorMotion::Services();
    return mm_actor_must_render(&_owner.runtime.state, _owner.track.route, _owner.track.events, _owner.runtime.first,
                                _owner.runtime.last, nfrmRenderLast, &motion) != 0;
}

/***************************************************************************

    Get actor name

***************************************************************************/
void Actor::GetName(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    if (!_Scene()->Pmvie()->FGetName(_owner.runtime.actor, pstn))
    {
        EngineTemplate::GetName(Ptmpl(), pstn);
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
    const auto motion = EngineActorMotion::Services();
    mm_actor_world_position(&_owner.runtime.state, _owner.track.route, &xyz, &motion);
    if (pxr)
        *pxr = xyz.dxr;
    if (pyr)
        *pyr = xyz.dyr;
    if (pzr)
        *pzr = xyz.dzr;
}

/***************************************************************************

    Hilite the actor

***************************************************************************/
void Actor::Hilite(void)
{
    AssertThis(0);

    EngineBody::SetHiliteColor(_owner.runtime.time_frozen ? kiclrTimeFreezeHilite : kiclrNormalHilite);
    EngineBody::Hilite(_Body());
}

/***************************************************************************

    Change this actor's template.  This gets called if the actor is based
    on a 3-D Text template and the text gets edited.  In addition to
    changing the template itself, this function updates _Body() to conform
    to the new template, and removes all costume events on body part sets
    that no longer exist.  For instance, if a three-letter text actor was
    changed to two letters, all costume events on the third letter are
    deleted.

***************************************************************************/
bool Actor::FChangeTagTmpl(const MMTag &tag)
{
    AssertThis(0);

    MMActorTemplateServices services = {};
    services.context = this;
    services.tags = &EngineActorTagServices();
    services.fetch = _FetchTemplate;
    services.release = [](void *, void *object) { mm_template_owner_release(static_cast<PTemplate>(object)); };
    services.text = [](void *, void *object) -> int { return EngineTemplate::FIsTdt(static_cast<PTemplate>(object)); };
    services.adjust_body = [](void *, void *object, void *body) -> int {
        return EngineText::FAdjustBody(static_cast<PThreeDText>(object), static_cast<PBody>(body));
    };
    services.select_cel = [](void *, void *object, void *body, MMI32 action, MMI32 cel) -> int {
        return EngineTemplate::FSetActnCel(static_cast<PTemplate>(object), static_cast<PBody>(body), action, cel);
    };
    services.position = [](void *p) {
        auto actor = static_cast<PActor>(p);
        actor->_PositionBody(&actor->_owner.runtime.state.position);
    };
    services.body_sets = [](void *, void *body) -> MMI32 { return EngineBody::SetCount(static_cast<PBody>(body)); };
    const auto edits = _EventEditServices();
    return mm_actor_replace_template(&_owner.runtime, _owner.track.events, &tag, &services, &edits) != 0;
}

#ifdef DEBUG
/***************************************************************************

    Assert the validity of the Actor.

***************************************************************************/
void Actor::AssertValid(LegacyUlong grfobj)
{

    Actor_PAR::AssertValid(fobjAllocated);
    EngineBody::Validate(_Body());
    EngineTemplate::Validate(_Template());
    Assert(_owner.track.events, "missing actor events");
    Assert(_owner.track.route, "missing actor path");
    Assert(_owner.sounds.count <= _owner.sounds.storage.buffers[0].capacity / sizeof(MMActorMotionSound),
           "invalid C motion sound storage");

    long iaevMac = mm_actor_events_count(_owner.track.events);
    long irptMac = mm_actor_path_count(_owner.track.route);
    bool fTracing = FPure(grfobj & fobjAssertFull);

    AssertIn(_owner.runtime.state.nextEvent, -1, iaevMac + 1);
    if (irptMac > 0)
        AssertIn(_owner.runtime.state.location.irpt, 0, irptMac);
    if (ivNil != _owner.runtime.state.actionEvent)
        AssertIn(_owner.runtime.state.actionEvent, 0, iaevMac);
    if (_owner.runtime.state.addEvent != ivNil)
        AssertIn(_owner.runtime.state.addEvent, 0, iaevMac);

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
            mm_actor_path_get(_owner.track.route, irpt, &rpt);
        }

        AssertIn(_owner.runtime.state.location.irpt, 0, irptMac + 1); // Supply a debug readable view of the event stream
        mm_actor_events_get_fixed(_owner.track.events, 0, &aev);
        Assert(aetAdd == aev.aet, "BUG: No add event at front of list");

        for (iaev = 0; iaev < mm_actor_events_count(_owner.track.events); iaev++)
        {
            mm_actor_events_get_fixed(_owner.track.events, iaev, &aev);
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
            mm_actor_path_get(_owner.track.route, aev.rtel.irpt, &rpt);
            Assert(aev.rtel.dwrOffset < rpt.dwr || rpt.dwr == rZero, "Invalid rtel.dwrOffset");

            // Variable portion of aev retrieved for debug viewing
            switch (aev.aet)
            {
            case aetAdd: {
                Add aevadd;
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarAdd, "Corrupt size in event list");
                mm_actor_events_get_payload(_owner.track.events, iaev, &aevadd);
                break;
            }
            case aetActn: {
                Action aevactn;
                mm_actor_events_get_payload(_owner.track.events, iaev, &aevactn);
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarActn, "Corrupt size in event list");
                break;
            }
            case aetCost:
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarCost, "Corrupt size in event list");
                break;
            case aetRotF:
            case aetRotH:
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarRot, "Corrupt size in event list");
                break;
            case aetSize:
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarSize, "Corrupt size in event list");
                break;
            case aetPull:
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarPull, "Corrupt size in event list");
                break;
            case aetSnd:
                break;
            case aetFreeze: {
                int32_t faevfrz;
                mm_actor_events_get_payload(_owner.track.events, iaev, &faevfrz);
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarFreeze, "Corrupt size in event list");
                break;
            }
            case aetTweak:
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarTweak, "Corrupt size in event list");
                break;
            case aetStep: {
                BRS dwrAev;
                mm_actor_events_get_payload(_owner.track.events, iaev, &dwrAev);
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarStep, "Corrupt size in event list");
                break;
            }
            case aetRem:
                break;
            case aetMove:
                Assert(mm_actor_events_size(_owner.track.events, iaev) == kcbVarMove, "Corrupt size in event list");
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

    Actor_PAR::MarkMem();
    const auto events = _owner.track.events;
    MMTag tag;
    for (MMI32 i = 0; i < mm_actor_events_count(events); ++i)
        if (mm_actor_track_get_tag(events, i, &tag) && tag.sid == ksidUseCrf && tag.cache)
            mm_resource_cache_mark(static_cast<MMResourceCache *>(tag.cache));
    MarkPv(events);
    MarkPv(events->storage.buffers[0].data);
    MarkPv(events->storage.buffers[1].data);
    if (_owner.track.route)
    {
        MarkPv(_owner.track.route);
        MarkPv(_owner.track.route->storage.buffers[0].data);
    }
    EngineBody::Mark(_Body());
    EngineTemplate::Mark(_Template());
    if (_owner.runtime.template_tag.sid == ksidUseCrf && _owner.runtime.template_tag.cache)
        mm_resource_cache_mark(static_cast<MMResourceCache *>(_owner.runtime.template_tag.cache));
}

#endif // DEBUG

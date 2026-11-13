/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    Actor Edit.   Cut/Copy/Paste/Undo

    Primary authors:
        ActorClipboard::(clipbd)	Seanse
        ActorUndo::(undo)	Seanse
        Actor::(undo)	Seanse
        Actor::(vacuum)  *****
        Actor::(dup/restore) *****
    Review Status:  Reviewed

***************************************************************************/

#include "soc.h"
#include "engine_actor_motion.h"
#include "three_mm/actor_paste.h"

ASSERTNAME

using namespace ActorEvent;

RTCLASS(ActorUndo)

/***************************************************************************

    Duplicate the actor from this frame through
    the end of subroute or (if fEntireScene) the end of the scene

***************************************************************************/
ThreeMMActorCopyServices Actor::_CopyServices()
{
    static_assert(sizeof(Sound)==sizeof(ThreeMMActorSound),"Copy sound ABI");
    static_assert(sizeof(Action)==2*sizeof(ThreeMMI32),"Copy action ABI");
    static_assert(sizeof(RouteDistancePoint)==sizeof(ThreeMMActorRoutePoint),"Copy point ABI");
    ThreeMMActorCopyServices s{}; s.context=this; s.motion=EngineActorMotion::Services();
    s.create_actor=[](void *p)->void * { return PactrNew(&static_cast<Actor *>(p)->_tagTmpl); };
    s.release_actor=[](void *,void *p) { static_cast<PActor>(p)->Release(); };
    s.route=[](void *,void *p)->void * { return static_cast<PActor>(p)->_track.Route(); };
    s.events=[](void *,void *p)->void * { return static_cast<PActor>(p)->_track.Events(); };
    s.lock_events=[](void *,void *p) { static_cast<PGeneralGroup>(p)->Lock(); };
    s.unlock_events=[](void *,void *p) { static_cast<PGeneralGroup>(p)->Unlock(); };
    s.payload_view=[](void *,void *p,ThreeMMI32 i)->const void * { return static_cast<PGeneralGroup>(p)->QvGet(i); };
    s.write_payload=[](void *,void *p,ThreeMMI32 i,const void *data) { static_cast<PGeneralGroup>(p)->Put(i,const_cast<void *>(data)); };
    s.resolve_sound=[](void *p,ThreeMMActorSound *value) {
        Sound sound; std::memcpy(&sound,value,sizeof(sound));
        bool ok=static_cast<Actor *>(p)->_pscen->Pmvie()->FResolveSndTag(&sound.tag,sound.chid);
        std::memcpy(value,&sound,sizeof(sound)); return int(ok);
    };
    s.insert_event=[](void *,void *p,ThreeMMI32 i,ThreeMMU32 bytes,const void *data,const ThreeMMActorEvent *value) {
        Base event; std::memcpy(&event,value,sizeof(event));
        return int(static_cast<PActor>(p)->_FInsertAev(i,bytes,const_cast<void *>(data),&event,false));
    };
    s.merge_event=[](void *,void *p,ThreeMMI32 first,ThreeMMI32 added) { static_cast<PActor>(p)->_MergeAev(first,added); };
    s.insert_point=[](void *,void *p,ThreeMMI32 i,const ThreeMMActorRoutePoint *value) {
        RouteDistancePoint point; std::memcpy(&point,value,sizeof(point)); return int(static_cast<PDynamicArray>(p)->FInsert(i,&point));
    };
    s.write_point=[](void *,void *p,ThreeMMI32 i,const ThreeMMActorRoutePoint *value) {
        RouteDistancePoint point; std::memcpy(&point,value,sizeof(point)); static_cast<PDynamicArray>(p)->Put(i,&point);
    };
    s.resize_route=[](void *,void *p,ThreeMMI32 count) { return int(static_cast<PDynamicArray>(p)->FSetIvMac(count)); };
    s.reserve_route=[](void *,void *p,ThreeMMI32 count) { return int(static_cast<PDynamicArray>(p)->FEnsureSpace(count,fgrpNil)); };
    s.copy_points=[](void *,void *source,ThreeMMI32 first,void *destination,ThreeMMI32 to,ThreeMMI32 count) {
        CopyPb(static_cast<PDynamicArray>(source)->QvGet(first),static_cast<PDynamicArray>(destination)->QvGet(to),LwMul(count,size(RouteDistancePoint)));
    };
    s.distance=[](void *,const ThreeMMActorPoint *v)->ThreeMMI32 { return BR_LENGTH3(v->dxr,v->dyr,v->dzr); };
    s.rewind_actor=[](void *,void *p) { static_cast<PActor>(p)->_SetStateRewound(); };
    s.unknown_event=[](void *) { Bug("Unknown event type... Copy or Not?"); };
    return s;
}

bool Actor::FCopy(PActor *ppactr, bool fEntireScene)
{
    AssertThis(0); AssertVarMem(ppactr); const auto state=EngineActorMotion::Core(_state); const auto services=_CopyServices();
    ThreeMMActorCopyResult out{}; out.context=ppactr;
    out.publish=[](void *p,void *actor) { *static_cast<PActor *>(p)=static_cast<PActor>(actor); };
#ifdef BUG1950
    const int postponed=1;
#else
    const int postponed=0;
#endif
    bool ok=three_mm_actor_copy(&state,_track.Route(),_track.Events(),fEntireScene,postponed,&out,&services)!=0;
    if (ok) { AssertPo(*ppactr,0); }
    return ok;
}

/***************************************************************************

    Duplicate the entire actor from *this onto *ppactr
    This is not from this frame on.  The entire actor is duplicated.
    This does not duplicate the Brender body.
    Default is fReset = fFalse;

    NOTE:
    Upon exit, the new actor will still be in the scene.
    If (!fReset), all state information will have been retained.

***************************************************************************/
ThreeMMActorRuntime Actor::_Runtime() const
{
    ThreeMMActorRuntime r{}; r.state=EngineActorMotion::Core(_state); r.recording=_RecordingState();
    r.template_object=_ptmpl; r.body=_pbody; r.scene=_pscen;
    std::memcpy(&r.template_tag,&_tagTmpl,sizeof(r.template_tag)); std::memcpy(&r.sound_tag,&_tagSnd,sizeof(r.sound_tag));
    std::memcpy(&r.origin,&_dxyzFullRte,sizeof(r.origin)); r.first=_nfrmFirst; r.last=_nfrmLast; r.actor=_arid;
    r.life_dirty=_fLifeDirty; r.prerendered=_fPrerendered; r.time_frozen=_fTimeFrozen; return r;
}
void Actor::_PublishRuntime(const ThreeMMActorRuntime &r)
{
    EngineActorMotion::Publish(_state,r.state); _PublishRecordingState(r.recording);
    _ptmpl=static_cast<PTemplate>(r.template_object); _pbody=static_cast<PBody>(r.body); _pscen=static_cast<PScene>(r.scene);
    std::memcpy(&_tagTmpl,&r.template_tag,sizeof(_tagTmpl)); std::memcpy(&_tagSnd,&r.sound_tag,sizeof(_tagSnd));
    std::memcpy(&_dxyzFullRte,&r.origin,sizeof(_dxyzFullRte)); _nfrmFirst=r.first; _nfrmLast=r.last; _arid=r.actor;
    _fLifeDirty=r.life_dirty!=0; _fPrerendered=r.prerendered!=0; _fTimeFrozen=r.time_frozen!=0;
}
ThreeMMActorOwnerServices Actor::_OwnerServices()
{
    static_assert(sizeof(TAG)==sizeof(ThreeMMTag),"Actor owner tag ABI");
    ThreeMMActorOwnerServices s{}; s.motion=EngineActorMotion::Services();
    s.read=[](void *,void *p,ThreeMMActorRuntime *r) { *r=static_cast<Actor *>(p)->_Runtime(); };
    s.write=[](void *,void *p,const ThreeMMActorRuntime *r) { static_cast<Actor *>(p)->_PublishRuntime(*r); };
    s.create=[](void *)->void * { return NewObj Actor(); };
    s.retain=[](void *,void *p) { static_cast<BASE *>(p)->AddRef(); };
    s.release=[](void *,void *p) { static_cast<BASE *>(p)->Release(); };
    s.clone_track=[](void *,void *d,void *p) { return int(static_cast<Actor *>(d)->_track.FCloneFrom(static_cast<Actor *>(p)->_track)); };
    s.clone_sounds=[](void *,void *d,void *p) { return int(static_cast<Actor *>(d)->_motionSounds.FCloneFrom(static_cast<Actor *>(p)->_motionSounds)); };
    s.swap_track=[](void *,void *a,void *b) { static_cast<Actor *>(a)->_track.Swap(static_cast<Actor *>(b)->_track); };
    s.swap_sounds=[](void *,void *a,void *b) { static_cast<Actor *>(a)->_motionSounds.Swap(static_cast<Actor *>(b)->_motionSounds); };
    s.duplicate_tag=[](void *,const ThreeMMTag *tag) { TAG value; std::memcpy(&value,tag,sizeof(value)); TagManager::DupTag(&value); };
    s.close_tag=[](void *,ThreeMMTag *tag) { TAG value; std::memcpy(&value,tag,sizeof(value)); TagManager::CloseTag(&value); std::memcpy(tag,&value,sizeof(value)); };
    s.close_track_tags=[](void *,void *p) { static_cast<Actor *>(p)->_track.CloseTags(); };
    s.clear_track=[](void *,void *p) { static_cast<Actor *>(p)->_track.Clear(); };
    s.dispose_sounds=[](void *,void *p) { three_mm_actor_sounds_dispose(static_cast<Actor *>(p)->_motionSounds.Core()); };
    s.route=[](void *,void *p)->void * { return static_cast<Actor *>(p)->_track.Route(); };
    s.route_services=[](void *,void *p,ThreeMMActorRouteEditServices *r) { *r=static_cast<Actor *>(p)->_RouteEditServices(); };
    s.seek=[](void *,void *p,ThreeMMI32 frame) { return int(static_cast<Actor *>(p)->FGotoFrame(frame)); };
    s.invalidate_views=[](void *,void *scene) { static_cast<PScene>(scene)->Pmvie()->InvalViews(); };
    return s;
}
bool Actor::FDup(PActor *ppactr, bool fReset)
{
    AssertThis(0); AssertVarMem(ppactr);
    const auto services=_OwnerServices(); ThreeMMActorOwnerResult result{}; result.context=ppactr;
    result.publish=[](void *p,void *actor) { *static_cast<PActor *>(p)=static_cast<PActor>(actor); };
    return three_mm_actor_owner_duplicate(this,fReset,&result,&services)!=0;
}

/***************************************************************************

    Restore the actor from *ppactr onto *this

***************************************************************************/
void Actor::Restore(PActor pactr)
{
    AssertThis(0); AssertVarMem(pactr);
    Assert(pactr->_ptmpl==_ptmpl,"Restore ptmpl logic error"); Assert(pactr->_pbody==_pbody,"Restore pbody logic error");
    const auto services=_OwnerServices(); three_mm_actor_owner_restore(this,pactr,&services);
}

/***************************************************************************

    Restore this actor from an undo object pactrRestore.

***************************************************************************/
void Actor::_RestoreFromUndo(PActor pactrRestore)
{
    AssertBaseThis(0); AssertVarMem(pactrRestore); Assert(!pactrRestore->_pbody,"Not restoring from undo object");
    const auto services=_OwnerServices(); three_mm_actor_owner_restore_undo(this,pactrRestore,&services);
}

// Copy evaluated/recording state without copying owning tables or BASE identity.
void Actor::_CopyRuntimeStateFrom(const Actor &source)
{
    const auto services=_OwnerServices(); three_mm_actor_owner_copy(this,const_cast<Actor *>(&source),&services);
}

/***************************************************************************

    Duplicate the indicated portion of the route from this frame on
    from actor "this" to actor *ppactr.
    Note **: The point on the route may not land on a node.  If between
    nodes, insert a point to make the two route sections identical.

***************************************************************************/
bool Actor::FCopyRte(PActor *ppactr, bool fEntireScene)
{
    AssertThis(0); AssertVarMem(ppactr); const auto state=EngineActorMotion::Core(_state); const auto services=_CopyServices();
    ThreeMMActorCopyResult out{}; out.context=ppactr;
    out.publish=[](void *p,void *actor) { *static_cast<PActor *>(p)=static_cast<PActor>(actor); };
    return three_mm_actor_copy_route(&state,_track.Route(),fEntireScene,&out,&services)!=0;
}

/***************************************************************************

    Paste the rte from the clipboard pactr to the current actor's current
    frame onward.
    On failure, unwinding is expected to be via the dup'd actor from undo.
    This overwrites the end of the current subroute.
    NOTE:  To be meaningful, the pasted route section is translated to
    extend from the current point.

***************************************************************************/
bool Actor::FPasteRte(PActor pactr)
{
    AssertThis(0); AssertVarMem(pactr);
    static_assert(kdwrNil==-65536 && factnStatic==8 && kcbVarStep==4 && kcbVarFreeze==4,"Route paste ABI");
    auto state=EngineActorMotion::Core(_state); const auto motion=EngineActorMotion::Services();
    ThreeMMActorRoutePasteServices s{}; s.subroute=_SubrouteEditServices();
    s.reserve_points=[](void *route,ThreeMMU32 count) { return int(static_cast<PDynamicArray>(route)->FEnsureSpace(count,fgrpNil)); };
    s.distance=[](void *,const ThreeMMActorPoint *p)->ThreeMMI32 { return BR_LENGTH3(p->dxr,p->dyr,p->dzr); };
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case THREE_MM_ROUTE_PASTE_EMPTY: PushErc(ercSocNothingToPaste); break;
        case THREE_MM_ROUTE_PASTE_INSERT_FAILED: Bug("Logic error"); break;
        case THREE_MM_ROUTE_PASTE_NO_ACTION: Assert(false,"Actor has no action"); break;
        }
    };
#ifdef STATIC
    const int static_mode=1;
#else
    const int static_mode=0;
#endif
    bool ok=three_mm_actor_route_paste(&state,_motionSounds.Core(),_track.Route(),_track.Events(),_arid,
        pactr->_track.Route(),static_mode,&motion,&s)!=0;
    EngineActorMotion::Publish(_state,state); return ok;
}

/***************************************************************************

    Put an already existing actor in this scene.

***************************************************************************/
bool Actor::FPaste(long nfrm, Scene *pscen)
{
    AssertThis(0);
    static_assert(sizeof(Add)==sizeof(ThreeMMActorEntrance) && sizeof(Sound)==sizeof(ThreeMMActorSound),"Paste payload ABI");
    static_assert(ksidUseCrf==0,"Paste document tag ABI");
    ThreeMMActorPasteServices s{}; s.context=this;
    s.read=[](void *p,ThreeMMActorRuntime *r) { *r=static_cast<Actor *>(p)->_Runtime(); };
    s.write=[](void *p,const ThreeMMActorRuntime *r) { static_cast<Actor *>(p)->_PublishRuntime(*r); };
    s.is_text=[](void *,void *p) { return int(static_cast<PTemplate>(p)->FIsTdt()); };
    s.ensure_autosave=[](void *,void *p,void **cache) {
        PChunkyResourceFile resource=nullptr; bool ok=static_cast<PScene>(p)->Pmvie()->FEnsureAutosave(&resource);
        *cache=resource; return int(ok);
    };
    s.duplicate_tag=[](void *,const ThreeMMTag *v) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); TagManager::DupTag(&tag);
    };
    s.close_tag=[](void *,ThreeMMTag *v) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); TagManager::CloseTag(&tag); std::memcpy(v,&tag,sizeof(tag));
    };
    s.save_template=[](void *,ThreeMMTag *v,void *cache) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); bool ok=TagManager::FSaveTag(&tag,static_cast<PChunkyResourceFile>(cache),true);
        std::memcpy(v,&tag,sizeof(tag)); return int(ok);
    };
    s.fetch_template=[](void *,const ThreeMMTag *v)->void * {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); return vptagm->PbacoFetch(&tag,Template::FReadTmpl);
    };
    s.release_template=[](void *,void *p) { static_cast<PTemplate>(p)->Release(); };
    s.bind_scene=[](void *p,void *scene) { static_cast<Actor *>(p)->SetPscen(static_cast<PScene>(scene)); };
    s.placement=[](void *p,ThreeMMActorPoint *point) {
        BRS x,y,z; static_cast<Actor *>(p)->_GetNewOrigin(&x,&y,&z); *point={x,y,z};
    };
    s.sound_exists=[](void *,const ThreeMMTag *tag) {
        return int(static_cast<PChunkyResourceFile>(tag->cache)->Pcfl()->FFind(tag->type,tag->number));
    };
    s.save_sound=[](void *,void *scene,ThreeMMTag *v) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); bool ok=static_cast<PScene>(scene)->Pmvie()->FSaveTagSnd(&tag);
        std::memcpy(v,&tag,sizeof(tag)); return int(ok);
    };
    s.sound_child=[](void *,void *scene,ThreeMMU32 number,ThreeMMU32 *child) {
        ChildChunkID value=*child; bool ok=static_cast<PScene>(scene)->Pmvie()->FChidFromUserSndCno(number,&value);
        *child=value; return int(ok);
    };
    s.write_event=[](void *p,ThreeMMI32 i,const ThreeMMActorEvent *v) {
        Base event; std::memcpy(&event,v,sizeof(event)); static_cast<PGeneralGroup>(p)->PutFixed(i,&event);
    };
    s.write_payload=[](void *p,ThreeMMI32 i,const void *v) { static_cast<PGeneralGroup>(p)->Put(i,const_cast<void *>(v)); };
    s.remove_event=[](void *p,ThreeMMI32 i) { static_cast<Actor *>(p)->_RemoveAev(i); };
    s.camera_yaw=[](void *,void *p) { return static_cast<PScene>(p)->Pbkgd()->BraRotYCamera(); };
    s.dirty=[](void *,void *p) { static_cast<PScene>(p)->MarkDirty(); };
    s.diagnostic=[](void *,int fault) {
        switch (fault)
        {
        case THREE_MM_PASTE_TEXT_NOT_DOCUMENT: Bug("ThreeDTexts should be stored in a document!"); break;
        case THREE_MM_PASTE_EMPTY: Bug("Nothing to paste!"); break;
        case THREE_MM_PASTE_MISSING_SOUND: PushErc(ercSocNoSndOnPaste); break;
        case THREE_MM_PASTE_SAVE_SOUND_FAILED: Bug("Expected to locate user sound chunk"); break;
        default: Bug("Unknown paste diagnostic"); break;
        }
    };
#ifdef BUG1888
    const int migrate_text=1;
#else
    const int migrate_text=0;
#endif
    const auto motion=EngineActorMotion::Services();
    return three_mm_actor_paste(_track.Route(),_track.Events(),nfrm,pscen,migrate_text,&motion,&s)!=0;
}

/***************************************************************************

    Make an actor look like they were just read in, and never in a scene.

***************************************************************************/
void Actor::Reset(void)
{
    const auto services=_OwnerServices(); three_mm_actor_owner_reset(this,&services);
}

//
//
//
//
//  BEGIN CLIPBOARD STUFF
//
//
//
//

RTCLASS(ActorClipboard)

/***************************************************************************

    Create an actor clipboard object
    This is from the current frame forward

***************************************************************************/
PActorClipboard ActorClipboard::PaclpNew(PActor pactr, bool fRteOnly, bool fEntireScene)
{
    AssertPo(pactr, 0);
    Assert(!fRteOnly || !fEntireScene, "Expecting subroute only");

    PActorClipboard paclp;
    PActor pactrTmp;
    String stn, stnCopyOf;

    paclp = NewObj ActorClipboard();

    if (paclp == pvNil)
    {
        return (pvNil);
    }

    if (fRteOnly)
    {
        if (!pactr->FCopyRte(&pactrTmp, fEntireScene))
        {
            ReleasePpo(&paclp);
            return (pvNil);
        }
    }
    else
    {
        if (!pactr->FCopy(&pactrTmp, fEntireScene))
        {
            ReleasePpo(&paclp);
            return (pvNil);
        }
        AssertPo(pactrTmp, 0);

        pactr->GetName(&stn);

        pactr->Pscen()->Pmvie()->Pmcc()->GetStn(idsEngineCopyOf, &stnCopyOf);

        if (!FEqualRgb(stn.Psz(), stnCopyOf.Psz(), CchSz(stnCopyOf.Psz()) * size(achar)))
        {
            paclp->_stnName = stnCopyOf;
            if (!paclp->_stnName.FAppendCh(kchSpace) || !paclp->_stnName.FAppendStn(&stn))
            {
                PushErc(ercSocNameTooLong);
                ReleasePpo(&paclp);
                return (pvNil);
            }
        }
        else
        {
            paclp->_stnName = stn;
        }
    }

    paclp->_pactr = pactrTmp;
    paclp->_fRteOnly = fRteOnly;
    AssertPo(paclp, 0);

    return (paclp);
}

/***************************************************************************

    Destroys an actor clipboard object

***************************************************************************/
ActorClipboard::~ActorClipboard(void)
{
    ReleasePpo(&_pactr);
}

/***************************************************************************

    Pastes an actor clipboard object

***************************************************************************/
bool ActorClipboard::FPaste(PMovie pmvie)
{
    AssertThis(0);
    AssertPo(pmvie, 0);

    PActor pactrNew;

    if (_fRteOnly)
    {
        return (pmvie->FPasteActrPath(_pactr));
    }

    //
    // Duplicate the actor
    //
    if (!_pactr->FDup(&pactrNew, fTrue))
    {
        return (fFalse);
    }
    AssertPo(pactrNew, 0);

    if (!pmvie->FPasteActr(pactrNew))
    {
        ReleasePpo(&pactrNew);
        return (fFalse);
    }

    if (!pmvie->FNameActr(pactrNew->Arid(), &_stnName))
    {
        pmvie->Pscen()->RemActrCore(pactrNew->Arid());
        ReleasePpo(&pactrNew);
        return (fFalse);
    }

    ReleasePpo(&pactrNew);
    return (fTrue);
}

#ifdef DEBUG

/****************************************************
 * Mark memory used by the ActorClipboard
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void ActorClipboard::MarkMem(void)
{
    ActorClipboard_PAR::MarkMem();
    MarkMemObj(_pactr);
}

/***************************************************************************
 * Assert the validity of the ActorClipboard
 *
 * Parameters:
 *  grf - bit array of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void ActorClipboard::AssertValid(LegacyUlong grf)
{
    ActorClipboard_PAR::AssertValid(fobjAllocated);
    _pactr->AssertValid(grf);
}

#endif

/***************************************************************************

    Create an undo object

***************************************************************************/
bool Actor::FCreateUndo(PActor pactrDup, bool fSndUndo, PString pstn)
{
    AssertPo(pactrDup,0); AssertNilOrPo(pstn,0);
    ThreeMMActorUndoCreateServices s{}; s.context=this;
    s.create=[](void *)->void * { return ActorUndo::PaundNew(); };
    s.set_actor=[](void *,void *p,void *actor) { static_cast<PActorUndo>(p)->SetPactr(static_cast<PActor>(actor)); };
    s.set_id=[](void *,void *p,ThreeMMI32 id) { static_cast<PActorUndo>(p)->SetArid(id); };
    s.set_sound=[](void *,void *p,int sound) { static_cast<PActorUndo>(p)->SetSndUndo(sound!=0); };
    s.set_name=[](void *,void *p,const void *name) { static_cast<PActorUndo>(p)->SetStn(static_cast<PString>(const_cast<void *>(name))); };
    s.add=[](void *p,void *undo) { return int(static_cast<PActor>(p)->_pscen->Pmvie()->FAddUndo(static_cast<PActorUndo>(undo))); };
    s.clear=[](void *p) { static_cast<PActor>(p)->_pscen->Pmvie()->ClearUndo(); };
    s.release=[](void *,void *p) { static_cast<PActorUndo>(p)->Release(); };
    s.reset_actor=[](void *,void *p) { static_cast<PActor>(p)->Reset(); };
    return three_mm_actor_undo_create(pactrDup,_arid,fSndUndo,pstn,&s)!=0;
}

/***************************************************************************

    Add (or replace) an action, and create an undo object

***************************************************************************/
struct Actor::EditContext
{
    Actor *actor;
    long action,cel;
    bool freeze;
    ulong normalization;
    long body_set,material;
    TAG *tag;
    tribool custom;
};
ThreeMMActorTransactionServices Actor::_TransactionServices(EditContext *context)
{
    ThreeMMActorTransactionServices s{}; s.context=context;
    s.duplicate=[](void *p,void **snapshot) {
        PActor value=nullptr; bool ok=static_cast<EditContext *>(p)->actor->FDup(&value); *snapshot=value; return int(ok);
    };
    s.restore=[](void *p,void *snapshot) { static_cast<EditContext *>(p)->actor->Restore(static_cast<PActor>(snapshot)); };
    s.create_undo=[](void *p,void *snapshot) { return int(static_cast<EditContext *>(p)->actor->FCreateUndo(static_cast<PActor>(snapshot))); };
    s.release=[](void *,void *snapshot) { static_cast<PActor>(snapshot)->Release(); };
    return s;
}
ThreeMMActorDeleteServices Actor::_DeleteServices(EditContext *context)
{
    ThreeMMActorDeleteServices s{}; s.context=context;
    s.read=[](void *p,ThreeMMActorDeleteState *state) {
        auto a=static_cast<EditContext *>(p)->actor; state->frame=a->_state.frame; state->add_event=a->_state.addEvent;
        state->next_event=a->_state.nextEvent; state->scene_present=a->_pscen!=nullptr;
    };
    s.add_frame=[](void *p,ThreeMMI32 i)->ThreeMMI32 {
        Base event; static_cast<EditContext *>(p)->actor->_track.Events()->GetFixed(i,&event); return event.nfrm;
    };
    s.event_count=[](void *p)->ThreeMMI32 { return static_cast<EditContext *>(p)->actor->_track.Events()->IvMac(); };
    s.delete_subroute=[](void *p) { return int(static_cast<EditContext *>(p)->actor->_FDeleteEntireSubrte()); };
    s.seek=[](void *p,ThreeMMI32 frame) { return int(static_cast<EditContext *>(p)->actor->FGotoFrame(frame)); };
    s.delete_forward=[](void *p,int all,int wants_alive,ThreeMMI32 original) {
        bool alive=false; static_cast<EditContext *>(p)->actor->DeleteFwdCore(all!=0,wants_alive?&alive:nullptr,original); return int(alive);
    };
    s.lifetime=[](void *p) { return int(static_cast<EditContext *>(p)->actor->_FComputeLifetime()); };
    s.sound_frame=[](void *p) { static_cast<EditContext *>(p)->actor->_pscen->UpdateSndFrame(); };
    s.diagnostic=[](void *,int fault) { PushErc(fault==THREE_MM_TRANSACTION_SEEK_FAILED?ercSocGotoFrameFailure:ercSocBadFrameSlider); };
    return s;
}

bool Actor::FSetAction(long anid, long celn, bool fFreeze, PActor *ppactrDup)
{
    AssertThis(0); AssertNilOrVarMem(ppactrDup); EditContext context{this}; context.action=anid; context.cel=celn; context.freeze=fFreeze;
    auto s=_TransactionServices(&context);
    s.apply=[](void *p) { auto c=static_cast<EditContext *>(p); return int(c->actor->FSetActionCore(c->action,c->cel,c->freeze)); };
    ThreeMMActorTransactionResult out{}; out.context=ppactrDup;
    out.publish=[](void *p,void *snapshot) { *static_cast<PActor *>(p)=static_cast<PActor>(snapshot); };
    return three_mm_actor_transaction_apply(THREE_MM_TRANSACTION_ACTION,ppactrDup?&out:nullptr,&s)!=0;
}

/***************************************************************************

    Add the event to the event list: Add actor on the stage, and create undo
    object.

***************************************************************************/
bool Actor::FAddOnStage(void)
{
    AssertThis(0); EditContext context{this}; auto s=_TransactionServices(&context);
    s.apply=[](void *p) { return int(static_cast<EditContext *>(p)->actor->FAddOnStageCore()); };
    return three_mm_actor_transaction_apply(THREE_MM_TRANSACTION_ADD,nullptr,&s)!=0;
}

/***************************************************************************

    Normalize an actor.

***************************************************************************/
bool Actor::FNormalize(ulong grfnorm)
{
    AssertThis(0); EditContext context{this}; context.normalization=grfnorm; auto s=_TransactionServices(&context);
    s.apply=[](void *p) { auto c=static_cast<EditContext *>(p); return int(c->actor->FNormalizeCore(c->normalization)); };
    return three_mm_actor_transaction_apply(THREE_MM_TRANSACTION_NORMALIZE,nullptr,&s)!=0;
}

/***************************************************************************

    Set the Costume for a body part
    Add the event to the event list

***************************************************************************/
bool Actor::FSetCostume(long ibset, TAG *ptag, long cmid, tribool fCmtl)
{
    AssertThis(0); Assert(fCmtl || ibset>=0,"Invalid ibset argument"); AssertVarMem(ptag);
    EditContext context{this}; context.body_set=ibset; context.tag=ptag; context.material=cmid; context.custom=fCmtl;
    auto s=_TransactionServices(&context);
    s.apply=[](void *p) { auto c=static_cast<EditContext *>(p); return int(c->actor->FSetCostumeCore(c->body_set,c->tag,c->material,c->custom)); };
    return three_mm_actor_transaction_apply(THREE_MM_TRANSACTION_COSTUME,nullptr,&s)!=0;
}

/***************************************************************************

    Delete the path and events from this frame and beyond
    **NOTE:  This does not send the actor offstage.	See FRemFromStageCore.

    On Input: fDeleteAll specifies full route vs subroute deletion
    Returns *pfAlive = false if all events and route for this actor
    have been deleted

***************************************************************************/
bool Actor::FDelete(bool *pfAlive, bool fDeleteAll)
{
    AssertThis(0);
#ifndef BUG1870
#error C89 actor edit transactions require enabled retail BUG1870 policy
#endif
    EditContext context{this}; const auto s=_TransactionServices(&context); const auto d=_DeleteServices(&context);
    ThreeMMActorDeleteResult out{}; out.context=pfAlive;
    out.publish=[](void *p,int alive,int trashed) { if (trashed) { TrashVar(static_cast<bool *>(p)); } else *static_cast<bool *>(p)=alive!=0; };
    return three_mm_actor_transaction_delete(fDeleteAll,pfAlive?&out:nullptr,&s,&d)!=0;
}

/***************************************************************************

    Add the event to the event list: Remove actor from the stage, and an Undo.
    NOTE: This should be called <before> the call to place the actor offstage
***************************************************************************/
bool Actor::FRemFromStage(void)
{
    AssertThis(0); EditContext context{this}; auto s=_TransactionServices(&context);
    s.apply=[](void *p) { return int(static_cast<EditContext *>(p)->actor->FRemFromStageCore()); };
    return three_mm_actor_transaction_apply(THREE_MM_TRANSACTION_REMOVE,nullptr,&s)!=0;
}

/****************************************************
 *
 * Public constructor for actor undo objects.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PActorUndo ActorUndo::PaundNew()
{
    PActorUndo paund;
    paund = NewObj ActorUndo();
    AssertNilOrPo(paund, 0);
    return (paund);
}

/****************************************************
 *
 * Destructor for actor undo objects
 *
 ****************************************************/
struct ActorUndo::UndoContext
{
    ActorUndo *owner;
    PDocumentBase document;
    String captured;
};
ThreeMMActorUndoState ActorUndo::_UndoState() const
{
    ThreeMMActorUndoState s{}; s.movie=_pmvie; s.actor=_pactr; s.scene=_iscen; s.frame=_nfrm;
    s.last_frame=_nfrmLast; s.actor_id=_arid; s.sooner_later=_fSoonerLater; s.sound=_fSndUndo; return s;
}
void ActorUndo::_PublishUndo(const ThreeMMActorUndoState &s)
{
    _pmvie=static_cast<PMovie>(s.movie); _pactr=static_cast<PActor>(s.actor); _iscen=s.scene; _nfrm=s.frame;
    _nfrmLast=s.last_frame; _arid=s.actor_id; _fSoonerLater=s.sooner_later!=0; _fSndUndo=s.sound!=0;
}
ThreeMMActorUndoServices ActorUndo::_UndoServices(UndoContext *context)
{
    ThreeMMActorUndoServices s{}; s.context=context;
    s.read=[](void *p,ThreeMMActorUndoState *state) { *state=static_cast<UndoContext *>(p)->owner->_UndoState(); };
    s.publish=[](void *p,const ThreeMMActorUndoState *state) { static_cast<UndoContext *>(p)->owner->_PublishUndo(*state); };
    s.invoke_undo=[](void *p) { auto c=static_cast<UndoContext *>(p); return int(c->owner->FUndo(c->document)); };
    s.switch_scene=[](void *,void *p,ThreeMMI32 i) { return int(static_cast<PMovie>(p)->FSwitchScen(i)); };
    s.current_scene=[](void *,void *p)->void * { return static_cast<PMovie>(p)->Pscen(); };
    s.seek=[](void *,void *p,ThreeMMI32 frame) { return int(static_cast<PScene>(p)->FGotoFrm(frame)); };
    s.clear_undo=[](void *,void *p) { static_cast<PMovie>(p)->ClearUndo(); };
    s.flush=[](void *,void *p) { static_cast<PMovie>(p)->Pmsq()->FlushMsq(); };
    s.play=[](void *,void *p) { static_cast<PMovie>(p)->Pmsq()->PlayMsq(); };
    s.find_actor=[](void *,void *p,ThreeMMI32 id)->void * { return static_cast<PScene>(p)->PactrFromArid(id); };
    s.retain=[](void *,void *p) { static_cast<PActor>(p)->AddRef(); };
    s.release=[](void *,void *p) { static_cast<PActor>(p)->Release(); };
    s.actor_id=[](void *,void *p)->ThreeMMI32 { return static_cast<PActor>(p)->Arid(); };
    s.remove_actor=[](void *,void *p,ThreeMMI32 id) { static_cast<PScene>(p)->RemActrCore(id); };
    s.add_actor=[](void *,void *p,void *actor) { return int(static_cast<PScene>(p)->FAddActrCore(static_cast<PActor>(actor))); };
    s.has_name=[](void *p) { return int(static_cast<UndoContext *>(p)->owner->_stn.Cch()!=0); };
    s.capture_name=[](void *p,void *movie,ThreeMMI32 id) { return int(static_cast<PMovie>(movie)->FGetName(id,&static_cast<UndoContext *>(p)->captured)); };
    s.assign_name=[](void *p,void *movie,ThreeMMI32 id) { static_cast<PMovie>(movie)->FNameActr(id,&static_cast<UndoContext *>(p)->owner->_stn); };
    s.store_name=[](void *p) { auto c=static_cast<UndoContext *>(p); c->owner->_stn=c->captured; };
    s.view=[](void *,void *p)->void * { return static_cast<PMovie>(p)->PddgGet(0); };
    s.text_mode=[](void *,void *p) { return int(static_cast<PMovieView>(p)->FTextMode()); };
    s.select_actor=[](void *,void *p,void *actor) { static_cast<PScene>(p)->SelectActr(static_cast<PActor>(actor)); };
    s.invalidate_range=[](void *,void *p) { static_cast<PScene>(p)->InvalFrmRange(); };
    s.reset_actor=[](void *,void *p) { static_cast<PActor>(p)->Reset(); };
    return s;
}

ActorUndo::~ActorUndo(void)
{
    AssertBaseThis(0); UndoContext context{this,nullptr};
    const auto services=_UndoServices(&context); auto state=_UndoState();
    three_mm_actor_undo_dispose(&state,&services);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool ActorUndo::FDo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb,0); UndoContext context{this,pdocb};
    const auto services=_UndoServices(&context); auto state=_UndoState();
    return three_mm_actor_undo_redo(&state,&services)!=0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool ActorUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb,0); UndoContext context{this,pdocb};
    const auto services=_UndoServices(&context); auto state=_UndoState();
    return three_mm_actor_undo_toggle(&state,&services)!=0;
}

/****************************************************
 * Set the actor for this undo object.
 *
 * Parameters:
 * 	pactr - Actor to use
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void ActorUndo::SetPactr(PActor pactr)
{
    AssertThis(0); UndoContext context{this,nullptr};
    const auto services=_UndoServices(&context); auto state=_UndoState();
    three_mm_actor_undo_set_actor(&state,pactr,&services);
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the ActorUndo
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void ActorUndo::MarkMem(void)
{
    AssertThis(0);
    ActorUndo_PAR::MarkMem();
    MarkMemObj(_pactr);
}

/***************************************************************************
    Assert the validity of the ActorUndo.
***************************************************************************/
void ActorUndo::AssertValid(LegacyUlong grf)
{
    AssertNilOrPo(_pactr, 0);
}
#endif

RTCLASS(ActorMoveGroupUndo)

/****************************************************
 *
 * Allocates a new ActorMoveGroupUndo composite undo object.
 *
 * Returns:
 *  A pointer to the object, or pvNil if it could not be created.
 *
 ****************************************************/
ThreeMMActorGroupState ActorMoveGroupUndo::_GroupState() const
{
    ThreeMMActorGroupState s{}; s.entries=_pglpaund; s.movie=_pmvie; s.scene=_iscen; s.frame=_nfrm; return s;
}
ThreeMMActorGroupServices ActorMoveGroupUndo::_GroupServices(ActorMoveGroupUndo *owner)
{
    ThreeMMActorGroupServices s{}; s.context=owner;
    s.count=[](void *,void *p)->ThreeMMI32 { return static_cast<PDynamicArray>(p)->IvMac(); };
    s.child=[](void *,void *p,ThreeMMI32 i)->void * { PActorUndo child; static_cast<PDynamicArray>(p)->Get(i,&child); return child; };
    s.retain=[](void *,void *p) { static_cast<PActorUndo>(p)->AddRef(); };
    s.release=[](void *,void *p) { static_cast<PActorUndo>(p)->Release(); };
    s.append_child=[](void *,void *p,void *child) { PActorUndo value=static_cast<PActorUndo>(child); return int(static_cast<PDynamicArray>(p)->FAdd(&value)); };
    s.child_context=[](void *,void *p,void *movie,ThreeMMI32 scene,ThreeMMI32 frame) {
        auto child=static_cast<PActorUndo>(p); child->SetPmvie(static_cast<PMovie>(movie)); child->SetIscen(scene); child->SetNfrm(frame);
    };
    s.undo_child=[](void *,void *p,void *document) { return int(static_cast<PActorUndo>(p)->FUndo(static_cast<PDocumentBase>(document))); };
    s.release_entries=[](void *,void *p) { static_cast<PDynamicArray>(p)->Release(); };
    s.publish_entries=[](void *p,void *entries) { static_cast<ActorMoveGroupUndo *>(p)->_pglpaund=static_cast<PDynamicArray>(entries); };
    return s;
}
long ActorMoveGroupUndo::Cchild(void)
{
    const auto state=_GroupState(); const auto services=_GroupServices(this); return three_mm_actor_group_count(&state,&services);
}

PActorMoveGroupUndo ActorMoveGroupUndo::PamguNew(void)
{
    ThreeMMActorGroupFactory s{};
    s.create_owner=[](void *)->void * { return NewObj ActorMoveGroupUndo(); };
    s.create_entries=[](void *)->void * { return DynamicArray::PglNew(size(PActorUndo)); };
    s.publish_entries=[](void *,void *p,void *entries) { static_cast<PActorMoveGroupUndo>(p)->_pglpaund=static_cast<PDynamicArray>(entries); };
    s.release_owner=[](void *,void *p) { static_cast<PActorMoveGroupUndo>(p)->Release(); };
    return static_cast<PActorMoveGroupUndo>(three_mm_actor_group_create(&s));
}

/****************************************************
 *
 * Destructor.  Releases each owned child ActorUndo.
 *
 ****************************************************/
ActorMoveGroupUndo::~ActorMoveGroupUndo(void)
{
    AssertBaseThis(0); auto state=_GroupState(); const auto services=_GroupServices(this);
    three_mm_actor_group_dispose(&state,1,&services);
}

/****************************************************
 *
 * Adds a child ActorUndo to the composite, taking a reference.
 *
 * Parameters:
 *  paund - the child undo to add.
 *
 * Returns:
 *  fTrue on success, fFalse on OOM.
 *
 ****************************************************/
bool ActorMoveGroupUndo::FAddChild(PActorUndo paund)
{
    AssertThis(0); AssertPo(paund,0); const auto state=_GroupState(); const auto services=_GroupServices(this);
    return three_mm_actor_group_add_child(&state,paund,&services)!=0;
}

/****************************************************
 *
 * Undoes the composite group drag by undoing each child in order.
 *
 ****************************************************/
bool ActorMoveGroupUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb,0); const auto state=_GroupState(); const auto services=_GroupServices(this);
    return three_mm_actor_group_move_toggle(&state,pdocb,&services)!=0;
}

/****************************************************
 *
 * Redoes the composite group drag.  ActorUndo::FDo delegates to
 * FUndo (it's a swap-based undo), so the composite redo is the same
 * loop as the undo direction.
 *
 ****************************************************/
bool ActorMoveGroupUndo::FDo(PDocumentBase pdocb)
{
    return FUndo(pdocb);
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the ActorMoveGroupUndo and its children.
 ****************************************************/
void ActorMoveGroupUndo::MarkMem(void)
{
    AssertValid(0);
    ActorMoveGroupUndo_PAR::MarkMem();
    MarkMemObj(_pglpaund);
    if (_pglpaund != pvNil)
    {
        for (long iaund = 0; iaund < _pglpaund->IvMac(); iaund++)
        {
            PActorUndo paund;
            _pglpaund->Get(iaund, &paund);
            MarkMemObj(paund);
        }
    }
}

/***************************************************************************
    Assert the validity of the ActorMoveGroupUndo.

    NOTE: deliberately does NOT chain to MovieUndo_PAR::AssertValid, mirroring
    ActorUndo::AssertValid. The inherited _pmvie/_iscen/_nfrm slots are only
    populated by Movie::FAddUndo at commit time; before that (during mousedown
    while we're building the composite via FAddChild) _pmvie is nil and the
    parent chain would trip its AssertPo(_pmvie, 0).
***************************************************************************/
void ActorMoveGroupUndo::AssertValid(LegacyUlong grf)
{
    AssertPo(_pglpaund, 0);
}
#endif

/****************************************************
 *
 * ActorRenameGroupUndo: composite undo for batched actor renames driven by
 * the tag-group hotkeys (Ctrl+G / Ctrl+Shift+G). Each entry is a pair
 * (arid, name-to-restore-on-toggle); FUndo and FDo both swap each actor's
 * current name with the stored name, so undo and redo alternate between the
 * pre- and post-rename states.
 *
 ****************************************************/

RTCLASS(ActorRenameGroupUndo)

struct ActorRenameGroupUndo::GroupContext
{
    ActorRenameGroupUndo *owner;
    String stored,current;
};
ThreeMMActorGroupState ActorRenameGroupUndo::_GroupState() const
{
    ThreeMMActorGroupState s{}; s.entries=_pgstNames; s.movie=_pmvie; s.scene=_iscen; s.frame=_nfrm; return s;
}
ThreeMMActorGroupServices ActorRenameGroupUndo::_GroupServices(GroupContext *context)
{
    ThreeMMActorGroupServices s{}; s.context=context;
    s.count=[](void *,void *p)->ThreeMMI32 { return static_cast<PVirtualStringTable>(p)->IvMac(); };
    s.release_entries=[](void *,void *p) { static_cast<PVirtualStringTable>(p)->Release(); };
    s.publish_entries=[](void *p,void *entries) { static_cast<GroupContext *>(p)->owner->_pgstNames=static_cast<PVirtualStringTable>(entries); };
    s.read_name=[](void *p,void *entries,ThreeMMI32 i)->ThreeMMI32 {
        auto c=static_cast<GroupContext *>(p); auto table=static_cast<PVirtualStringTable>(entries); long id;
        table->GetStn(i,&c->stored); table->GetExtra(i,&id); return id;
    };
    s.capture_name=[](void *p,void *movie,ThreeMMI32 id) { return int(static_cast<PMovie>(movie)->FGetName(id,&static_cast<GroupContext *>(p)->current)); };
    s.rename=[](void *p,void *movie,ThreeMMI32 id) { return int(static_cast<PMovie>(movie)->FNameActr(id,&static_cast<GroupContext *>(p)->stored)); };
    s.store_name=[](void *p,void *entries,ThreeMMI32 i) { return int(static_cast<PVirtualStringTable>(entries)->FPutStn(i,&static_cast<GroupContext *>(p)->current)); };
    s.append_name=[](void *,void *entries,ThreeMMI32 id,const void *name) {
        long value=id; return int(static_cast<PVirtualStringTable>(entries)->FAddStn(static_cast<PString>(const_cast<void *>(name)),&value));
    };
    return s;
}
long ActorRenameGroupUndo::Cchild(void)
{
    GroupContext context{this}; const auto state=_GroupState(); const auto services=_GroupServices(&context);
    return three_mm_actor_group_count(&state,&services);
}

PActorRenameGroupUndo ActorRenameGroupUndo::PargNew(void)
{
    ThreeMMActorGroupFactory s{};
    s.create_owner=[](void *)->void * { return NewObj ActorRenameGroupUndo(); };
    s.create_entries=[](void *)->void * { return StringTable_GST::PgstNew(size(long)); };
    s.publish_entries=[](void *,void *p,void *entries) { static_cast<PActorRenameGroupUndo>(p)->_pgstNames=static_cast<PVirtualStringTable>(entries); };
    s.release_owner=[](void *,void *p) { static_cast<PActorRenameGroupUndo>(p)->Release(); };
    return static_cast<PActorRenameGroupUndo>(three_mm_actor_group_create(&s));
}

ActorRenameGroupUndo::~ActorRenameGroupUndo(void)
{
    AssertBaseThis(0); GroupContext context{this}; auto state=_GroupState(); const auto services=_GroupServices(&context);
    three_mm_actor_group_dispose(&state,0,&services);
}

bool ActorRenameGroupUndo::FAddChild(long arid, PString pstnPrev)
{
    AssertThis(0); AssertPo(pstnPrev,0); GroupContext context{this}; const auto state=_GroupState(); const auto services=_GroupServices(&context);
    return three_mm_actor_group_add_name(&state,arid,pstnPrev,&services)!=0;
}

bool ActorRenameGroupUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb,0); AssertPo(_pmvie,0); GroupContext context{this};
    const auto state=_GroupState(); const auto services=_GroupServices(&context);
    return three_mm_actor_group_name_toggle(&state,&services)!=0;
}

bool ActorRenameGroupUndo::FDo(PDocumentBase pdocb)
{
    return FUndo(pdocb);
}

#ifdef DEBUG
void ActorRenameGroupUndo::MarkMem(void)
{
    AssertValid(0);
    ActorRenameGroupUndo_PAR::MarkMem();
    MarkMemObj(_pgstNames);
}

void ActorRenameGroupUndo::AssertValid(LegacyUlong grf)
{
    AssertPo(_pgstNames, 0);
}
#endif

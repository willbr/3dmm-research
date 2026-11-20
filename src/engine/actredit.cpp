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
#include "mm/actor_paste.h"

ASSERTNAME

using namespace ActorEvent;

RTCLASS(ActorUndo)

/***************************************************************************

    Duplicate the actor from this frame through
    the end of subroute or (if fEntireScene) the end of the scene

***************************************************************************/
MMActorCopyServices Actor::_CopyServices()
{
    static_assert(sizeof(Sound)==sizeof(MMActorSound),"Copy sound ABI");
    MMActorCopyServices s{};
    s.context = this;
    s.create = _CreateServices();
    s.resolve_sound=[](void *p,MMActorSound *value) {
        Sound sound; std::memcpy(&sound,value,sizeof(sound));
        bool ok=static_cast<Actor *>(p)->_Scene()->Pmvie()->FResolveSndTag(&sound.tag,sound.chid);
        std::memcpy(value,&sound,sizeof(sound)); return int(ok);
    };
    s.distance=[](void *,const MMActorPoint *v)->MMI32 { return BR_LENGTH3(v->dxr,v->dyr,v->dzr); };
    s.unknown_event=[](void *) { Bug("Unknown event type... Copy or Not?"); };
    return s;
}

bool Actor::FCopy(PActor *ppactr, bool fEntireScene)
{
    AssertThis(0); AssertVarMem(ppactr); const auto services=_CopyServices();
    MMActorOwnerResult out{}; out.context=ppactr;
    out.publish=[](void *p,void *actor) { *static_cast<PActor *>(p)=static_cast<PActor>(actor); };
#ifdef BUG1950
    const int postponed=1;
#else
    const int postponed=0;
#endif
    bool ok=mm_actor_copy(&_owner,fEntireScene,postponed,&out,&services)!=0;
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
MMActorOwnerServices Actor::_OwnerServices()
{
    MMActorOwnerServices s = {};
    s.tags = &EngineActorTagServices();
    s.owner = [](void *, void *p) { return &static_cast<PActor>(p)->_owner; };
    s.create = [](void *) -> void * { return NewObj Actor(); };
    s.retain = [](void *, void *p) { static_cast<BASE *>(p)->AddRef(); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.route_services=[](void *,void *p,MMActorRouteEditServices *r) { *r=static_cast<Actor *>(p)->_RouteEditServices(); };
    s.seek=[](void *,void *p,MMI32 frame) { return int(static_cast<Actor *>(p)->FGotoFrame(frame)); };
    s.invalidate_views=[](void *,void *scene) { static_cast<PScene>(scene)->Pmvie()->InvalViews(); };
    return s;
}
bool Actor::FDup(PActor *ppactr, bool fReset)
{
    AssertThis(0); AssertVarMem(ppactr);
    const auto services=_OwnerServices(); MMActorOwnerResult result{}; result.context=ppactr;
    result.publish=[](void *p,void *actor) { *static_cast<PActor *>(p)=static_cast<PActor>(actor); };
    return mm_actor_owner_duplicate(this,fReset,&result,&services)!=0;
}

/***************************************************************************

    Restore the actor from *ppactr onto *this

***************************************************************************/
void Actor::Restore(PActor pactr)
{
    AssertThis(0); AssertVarMem(pactr);
    Assert(pactr->_Template()==_Template(),"Restore ptmpl logic error"); Assert(pactr->_Body()==_Body(),"Restore pbody logic error");
    const auto services=_OwnerServices(); mm_actor_owner_restore(this,pactr,&services);
}

/***************************************************************************

    Restore this actor from an undo object pactrRestore.

***************************************************************************/
void Actor::_RestoreFromUndo(PActor pactrRestore)
{
    AssertBaseThis(0); AssertVarMem(pactrRestore); Assert(!pactrRestore->_Body(),"Not restoring from undo object");
    const auto services=_OwnerServices(); mm_actor_owner_restore_undo(this,pactrRestore,&services);
}

/***************************************************************************

    Duplicate the indicated portion of the route from this frame on
    from actor "this" to actor *ppactr.
    Note **: The point on the route may not land on a node.  If between
    nodes, insert a point to make the two route sections identical.

***************************************************************************/
bool Actor::FCopyRte(PActor *ppactr, bool fEntireScene)
{
    AssertThis(0); AssertVarMem(ppactr); const auto services=_CopyServices();
    MMActorOwnerResult out{}; out.context=ppactr;
    out.publish=[](void *p,void *actor) { *static_cast<PActor *>(p)=static_cast<PActor>(actor); };
    return mm_actor_copy_route(&_owner,fEntireScene,&out,&services)!=0;
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
    auto state=_owner.runtime.state; const auto motion=EngineActorMotion::Services();
    MMActorRoutePasteServices s{}; s.subroute=_SubrouteEditServices();
    s.reserve_points=[](void *route,MMU32 count) { return int(mm_array_reserve(&static_cast<MMActorPath *>(route)->rows, count, 0) == MM_STORAGE_OK); };
    s.distance=[](void *,const MMActorPoint *p)->MMI32 { return BR_LENGTH3(p->dxr,p->dyr,p->dzr); };
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case MM_ROUTE_PASTE_EMPTY: PushErc(ercSocNothingToPaste); break;
        case MM_ROUTE_PASTE_INSERT_FAILED: Bug("Logic error"); break;
        case MM_ROUTE_PASTE_NO_ACTION: Assert(false,"Actor has no action"); break;
        }
    };
#ifdef STATIC
    const int static_mode=1;
#else
    const int static_mode=0;
#endif
    bool ok=mm_actor_route_paste(&state,&_owner.sounds,_owner.track.route,_owner.track.events,_owner.runtime.actor,
        pactr->_owner.track.route,static_mode,&motion,&s)!=0;
    _owner.runtime.state = state; return ok;
}

/***************************************************************************

    Put an already existing actor in this scene.

***************************************************************************/
bool Actor::FPaste(long nfrm, Scene *pscen)
{
    AssertThis(0);
    static_assert(sizeof(Add)==sizeof(MMActorEntrance) && sizeof(Sound)==sizeof(MMActorSound),"Paste payload ABI");
    static_assert(ksidUseCrf==0,"Paste document tag ABI");
    MMActorPasteServices s{}; s.context=this;
    s.is_text=[](void *,void *p) { return int(static_cast<PTemplate>(p)->FIsTdt()); };
    s.ensure_autosave=[](void *,void *p,void **cache) {
        PChunkyResourceFile resource=nullptr; bool ok=static_cast<PScene>(p)->Pmvie()->FEnsureAutosave(&resource);
        *cache=resource; return int(ok);
    };
    s.duplicate_tag=[](void *,const MMTag *v) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); TagManager::DupTag(&tag);
    };
    s.close_tag=[](void *,MMTag *v) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); TagManager::CloseTag(&tag); std::memcpy(v,&tag,sizeof(tag));
    };
    s.save_template=[](void *,MMTag *v,void *cache) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); bool ok=TagManager::FSaveTag(&tag,static_cast<PChunkyResourceFile>(cache),true);
        std::memcpy(v,&tag,sizeof(tag)); return int(ok);
    };
    s.fetch_template=[](void *,const MMTag *v)->void * {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); return vptagm->PbacoFetch(&tag,Template::FReadTmpl);
    };
    s.release_template=[](void *,void *p) { static_cast<PTemplate>(p)->Release(); };
    s.bind_scene=[](void *p,void *scene) { static_cast<Actor *>(p)->SetPscen(static_cast<PScene>(scene)); };
    s.placement=[](void *p,MMActorPoint *point) {
        BRS x,y,z; static_cast<Actor *>(p)->_GetNewOrigin(&x,&y,&z); *point={x,y,z};
    };
    s.sound_exists=[](void *,const MMTag *tag) {
        return int(static_cast<PChunkyResourceFile>(tag->cache)->Pcfl()->FFind(tag->type,tag->number));
    };
    s.save_sound=[](void *,void *scene,MMTag *v) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); bool ok=static_cast<PScene>(scene)->Pmvie()->FSaveTagSnd(&tag);
        std::memcpy(v,&tag,sizeof(tag)); return int(ok);
    };
    s.sound_child=[](void *,void *scene,MMU32 number,MMU32 *child) {
        ChildChunkID value=*child; bool ok=static_cast<PScene>(scene)->Pmvie()->FChidFromUserSndCno(number,&value);
        *child=value; return int(ok);
    };
    s.write_event=[](void *p,MMI32 i,const MMActorEvent *v) {
        mm_actor_events_set_fixed(static_cast<MMActorEventTable *>(p), i, v);
    };
    s.write_payload=[](void *p,MMI32 i,const void *v) { mm_actor_events_set_payload(static_cast<MMActorEventTable *>(p), i, v); };
    s.remove_event=[](void *p,MMI32 i) { static_cast<Actor *>(p)->_RemoveAev(i); };
    s.camera_yaw=[](void *,void *p) { return static_cast<PScene>(p)->Pbkgd()->BraRotYCamera(); };
    s.dirty=[](void *,void *p) { static_cast<PScene>(p)->MarkDirty(); };
    s.diagnostic=[](void *,int fault) {
        switch (fault)
        {
        case MM_PASTE_TEXT_NOT_DOCUMENT: Bug("ThreeDTexts should be stored in a document!"); break;
        case MM_PASTE_EMPTY: Bug("Nothing to paste!"); break;
        case MM_PASTE_MISSING_SOUND: PushErc(ercSocNoSndOnPaste); break;
        case MM_PASTE_SAVE_SOUND_FAILED: Bug("Expected to locate user sound chunk"); break;
        default: Bug("Unknown paste diagnostic"); break;
        }
    };
#ifdef BUG1888
    const int migrate_text=1;
#else
    const int migrate_text=0;
#endif
    const auto motion=EngineActorMotion::Services();
    return mm_actor_paste(&_owner.runtime,_owner.track.route,_owner.track.events,nfrm,pscen,migrate_text,&motion,&s)!=0;
}

/***************************************************************************

    Make an actor look like they were just read in, and never in a scene.

***************************************************************************/
void Actor::Reset(void)
{
    const auto services=_OwnerServices(); mm_actor_owner_reset(this,&services);
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
MMActorClipboardServices ActorClipboard::_Services()
{
    MMActorClipboardServices s{}; s.unit=sizeof(achar);
    s.create=[](void *)->void * { return NewObj ActorClipboard(); };
    s.release_clipboard=[](void *,void *p) { static_cast<PActorClipboard>(p)->Release(); };
    s.copy=[](void *,void *p,int route,int entire,void **out) {
        PActor actor=nullptr; bool ok=route?static_cast<PActor>(p)->FCopyRte(&actor,entire!=0):static_cast<PActor>(p)->FCopy(&actor,entire!=0);
        *out=actor; return int(ok);
    };
    s.source_name=[](void *,void *p,void *name) { static_cast<PActor>(p)->GetName(static_cast<PString>(name)); };
    s.copy_prefix=[](void *,void *p,void *name) { static_cast<PActor>(p)->Pscen()->Pmvie()->Pmcc()->GetStn(idsEngineCopyOf,static_cast<PString>(name)); };
    s.text=[](void *p)->const void * { return static_cast<PString>(p)->Psz(); };
    s.assign_name=[](void *p,void *name) { static_cast<PActorClipboard>(p)->_stnName=*static_cast<PString>(name); };
    s.append_space=[](void *p) { return int(static_cast<PActorClipboard>(p)->_stnName.FAppendCh(kchSpace)); };
    s.append_name=[](void *p,void *name) { return int(static_cast<PActorClipboard>(p)->_stnName.FAppendStn(static_cast<PString>(name))); };
    s.name_too_long=[](void *) { PushErc(ercSocNameTooLong); };
    s.set=[](void *p,void *actor,int route) {
        auto clipboard=static_cast<PActorClipboard>(p); clipboard->_clipboard.actor=actor; clipboard->_clipboard.route_only=route;
    };
    s.release_actor=[](void *,void *p) { static_cast<PActor>(p)->Release(); };
    s.duplicate=[](void *,void *p,int reset,void **out) {
        PActor actor=nullptr; bool ok=static_cast<PActor>(p)->FDup(&actor,reset!=0); *out=actor; return int(ok);
    };
    s.paste_route=[](void *,void *movie,void *actor) { return int(static_cast<PMovie>(movie)->FPasteActrPath(static_cast<PActor>(actor))); };
    s.paste_actor=[](void *,void *movie,void *actor) { return int(static_cast<PMovie>(movie)->FPasteActr(static_cast<PActor>(actor))); };
    s.actor_id=[](void *p)->MMI32 { return static_cast<PActor>(p)->Arid(); };
    s.rename=[](void *,void *movie,MMI32 actor,void *name) { return int(static_cast<PMovie>(movie)->FNameActr(actor,static_cast<PString>(name))); };
    s.remove=[](void *,void *movie,MMI32 actor) { static_cast<PMovie>(movie)->Pscen()->RemActrCore(actor); };
    return s;
}
PActorClipboard ActorClipboard::PaclpNew(PActor pactr, bool fRteOnly, bool fEntireScene)
{
    AssertPo(pactr,0); Assert(!fRteOnly || !fEntireScene,"Expecting subroute only");
    String name,prefix; auto services=_Services(); services.name=&name; services.prefix=&prefix;
    auto clipboard=static_cast<PActorClipboard>(mm_actor_clipboard_create(pactr,fRteOnly,fEntireScene,&services));
    AssertNilOrPo(clipboard,0); return clipboard;
}
ActorClipboard::~ActorClipboard(void)
{
    const auto services=_Services(); mm_actor_clipboard_dispose(&_clipboard,&services);
}
bool ActorClipboard::FPaste(PMovie pmvie)
{
    AssertThis(0); AssertPo(pmvie,0); const auto services=_Services();
    return mm_actor_clipboard_paste(&_clipboard,&_stnName,pmvie,&services)!=0;
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
    MarkMemObj(static_cast<PActor>(_clipboard.actor));
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
    static_cast<PActor>(_clipboard.actor)->AssertValid(grf);
}

#endif

/***************************************************************************

    Create an undo object

***************************************************************************/
bool Actor::FCreateUndo(PActor pactrDup, bool fSndUndo, PString pstn)
{
    AssertPo(pactrDup,0); AssertNilOrPo(pstn,0);
    MMActorUndoCreateServices s{}; s.context=this;
    s.create=[](void *)->void * { return ActorUndo::PaundNew(); };
    s.set_actor=[](void *,void *p,void *actor) { static_cast<PActorUndo>(p)->SetPactr(static_cast<PActor>(actor)); };
    s.set_id=[](void *,void *p,MMI32 id) { static_cast<PActorUndo>(p)->SetArid(id); };
    s.set_sound=[](void *,void *p,int sound) { static_cast<PActorUndo>(p)->SetSndUndo(sound!=0); };
    s.set_name=[](void *,void *p,const void *name) { static_cast<PActorUndo>(p)->SetStn(static_cast<PString>(const_cast<void *>(name))); };
    s.add=[](void *p,void *undo) { return int(static_cast<PActor>(p)->_Scene()->Pmvie()->FAddUndo(static_cast<PActorUndo>(undo))); };
    s.clear=[](void *p) { static_cast<PActor>(p)->_Scene()->Pmvie()->ClearUndo(); };
    s.release=[](void *,void *p) { static_cast<PActorUndo>(p)->Release(); };
    s.reset_actor=[](void *,void *p) { static_cast<PActor>(p)->Reset(); };
    return mm_actor_undo_create(pactrDup,_owner.runtime.actor,fSndUndo,pstn,&s)!=0;
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
MMActorTransactionServices Actor::_TransactionServices(EditContext *context)
{
    MMActorTransactionServices s{}; s.context=context;
    s.duplicate=[](void *p,void **snapshot) {
        PActor value=nullptr; bool ok=static_cast<EditContext *>(p)->actor->FDup(&value); *snapshot=value; return int(ok);
    };
    s.restore=[](void *p,void *snapshot) { static_cast<EditContext *>(p)->actor->Restore(static_cast<PActor>(snapshot)); };
    s.create_undo=[](void *p,void *snapshot) { return int(static_cast<EditContext *>(p)->actor->FCreateUndo(static_cast<PActor>(snapshot))); };
    s.release=[](void *,void *snapshot) { static_cast<PActor>(snapshot)->Release(); };
    return s;
}
MMActorDeleteServices Actor::_DeleteServices(EditContext *context)
{
    MMActorDeleteServices s{}; s.context=context;
    s.read=[](void *p,MMActorDeleteState *state) {
        auto a=static_cast<EditContext *>(p)->actor; state->frame=a->_owner.runtime.state.frame; state->add_event=a->_owner.runtime.state.addEvent;
        state->next_event=a->_owner.runtime.state.nextEvent; state->scene_present=a->_Scene()!=nullptr;
    };
    s.add_frame=[](void *p,MMI32 i)->MMI32 {
        Base event; mm_actor_events_get_fixed(static_cast<EditContext *>(p)->actor->_owner.track.events, i,&event); return event.nfrm;
    };
    s.event_count=[](void *p)->MMI32 { return mm_actor_events_count(static_cast<EditContext *>(p)->actor->_owner.track.events); };
    s.delete_subroute=[](void *p) { return int(static_cast<EditContext *>(p)->actor->_FDeleteEntireSubrte()); };
    s.seek=[](void *p,MMI32 frame) { return int(static_cast<EditContext *>(p)->actor->FGotoFrame(frame)); };
    s.delete_forward=[](void *p,int all,int wants_alive,MMI32 original) {
        bool alive=false; static_cast<EditContext *>(p)->actor->DeleteFwdCore(all!=0,wants_alive?&alive:nullptr,original); return int(alive);
    };
    s.lifetime=[](void *p) { return int(static_cast<EditContext *>(p)->actor->_FComputeLifetime()); };
    s.sound_frame=[](void *p) { static_cast<EditContext *>(p)->actor->_Scene()->UpdateSndFrame(); };
    s.diagnostic=[](void *,int fault) { PushErc(fault==MM_TRANSACTION_SEEK_FAILED?ercSocGotoFrameFailure:ercSocBadFrameSlider); };
    return s;
}

bool Actor::FSetAction(long anid, long celn, bool fFreeze, PActor *ppactrDup)
{
    AssertThis(0); AssertNilOrVarMem(ppactrDup); EditContext context{this}; context.action=anid; context.cel=celn; context.freeze=fFreeze;
    auto s=_TransactionServices(&context);
    s.apply=[](void *p) { auto c=static_cast<EditContext *>(p); return int(c->actor->FSetActionCore(c->action,c->cel,c->freeze)); };
    MMActorTransactionResult out{}; out.context=ppactrDup;
    out.publish=[](void *p,void *snapshot) { *static_cast<PActor *>(p)=static_cast<PActor>(snapshot); };
    return mm_actor_transaction_apply(MM_TRANSACTION_ACTION,ppactrDup?&out:nullptr,&s)!=0;
}

/***************************************************************************

    Add the event to the event list: Add actor on the stage, and create undo
    object.

***************************************************************************/
bool Actor::FAddOnStage(void)
{
    AssertThis(0); EditContext context{this}; auto s=_TransactionServices(&context);
    s.apply=[](void *p) { return int(static_cast<EditContext *>(p)->actor->FAddOnStageCore()); };
    return mm_actor_transaction_apply(MM_TRANSACTION_ADD,nullptr,&s)!=0;
}

/***************************************************************************

    Normalize an actor.

***************************************************************************/
bool Actor::FNormalize(ulong grfnorm)
{
    AssertThis(0); EditContext context{this}; context.normalization=grfnorm; auto s=_TransactionServices(&context);
    s.apply=[](void *p) { auto c=static_cast<EditContext *>(p); return int(c->actor->FNormalizeCore(c->normalization)); };
    return mm_actor_transaction_apply(MM_TRANSACTION_NORMALIZE,nullptr,&s)!=0;
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
    return mm_actor_transaction_apply(MM_TRANSACTION_COSTUME,nullptr,&s)!=0;
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
    MMActorDeleteResult out{}; out.context=pfAlive;
    out.publish=[](void *p,int alive,int trashed) { if (trashed) { TrashVar(static_cast<bool *>(p)); } else *static_cast<bool *>(p)=alive!=0; };
    return mm_actor_transaction_delete(fDeleteAll,pfAlive?&out:nullptr,&s,&d)!=0;
}

/***************************************************************************

    Add the event to the event list: Remove actor from the stage, and an Undo.
    NOTE: This should be called <before> the call to place the actor offstage
***************************************************************************/
bool Actor::FRemFromStage(void)
{
    AssertThis(0); EditContext context{this}; auto s=_TransactionServices(&context);
    s.apply=[](void *p) { return int(static_cast<EditContext *>(p)->actor->FRemFromStageCore()); };
    return mm_actor_transaction_apply(MM_TRANSACTION_REMOVE,nullptr,&s)!=0;
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
MMActorUndoServices ActorUndo::_UndoServices(UndoContext *context)
{
    MMActorUndoServices s{}; s.context=context;
    s.invoke_undo=[](void *p) { auto c=static_cast<UndoContext *>(p); return int(c->owner->FUndo(c->document)); };
    s.switch_scene=[](void *,void *p,MMI32 i) { return int(static_cast<PMovie>(p)->FSwitchScen(i)); };
    s.current_scene=[](void *,void *p)->void * { return static_cast<PMovie>(p)->Pscen(); };
    s.seek=[](void *,void *p,MMI32 frame) { return int(static_cast<PScene>(p)->FGotoFrm(frame)); };
    s.clear_undo=[](void *,void *p) { static_cast<PMovie>(p)->ClearUndo(); };
    s.flush=[](void *,void *p) { static_cast<PMovie>(p)->Pmsq()->FlushMsq(); };
    s.play=[](void *,void *p) { static_cast<PMovie>(p)->Pmsq()->PlayMsq(); };
    s.find_actor=[](void *,void *p,MMI32 id)->void * { return static_cast<PScene>(p)->PactrFromArid(id); };
    s.retain=[](void *,void *p) { static_cast<PActor>(p)->AddRef(); };
    s.release=[](void *,void *p) { static_cast<PActor>(p)->Release(); };
    s.actor_id=[](void *,void *p)->MMI32 { return static_cast<PActor>(p)->Arid(); };
    s.remove_actor=[](void *,void *p,MMI32 id) { static_cast<PScene>(p)->RemActrCore(id); };
    s.add_actor=[](void *,void *p,void *actor) { return int(static_cast<PScene>(p)->FAddActrCore(static_cast<PActor>(actor))); };
    s.capture_name=[](void *p,void *movie,MMI32 id) { return int(static_cast<PMovie>(movie)->FGetName(id,&static_cast<UndoContext *>(p)->captured)); };
    s.assign_name=[](void *p,void *movie,MMI32 id) {
        const auto &value=static_cast<UndoContext *>(p)->owner->_undoState.name;
        String name;
        name.SetRgch(static_cast<achar *>(const_cast<void *>(mm_movie_title_text(&value))),value.count);
        static_cast<PMovie>(movie)->FNameActr(id,&name);
    };
    s.store_name=[](void *p) { auto c=static_cast<UndoContext *>(p); c->owner->SetStn(&c->captured); };
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
    const auto services=_UndoServices(&context);
    mm_actor_undo_dispose(&_undoState,&services);
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
    const auto services=_UndoServices(&context);
    return mm_actor_undo_redo(&_undoState,&services)!=0;
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
    const auto services=_UndoServices(&context);
    return mm_actor_undo_toggle(&_undoState,&services)!=0;
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
    const auto services=_UndoServices(&context);
    mm_actor_undo_set_actor(&_undoState,pactr,&services);
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
    MarkMemObj(static_cast<PActor>(_undoState.actor));
}

/***************************************************************************
    Assert the validity of the ActorUndo.
***************************************************************************/
void ActorUndo::AssertValid(LegacyUlong grf)
{
    AssertNilOrPo(static_cast<PActor>(_undoState.actor), 0);
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
MMActorGroupServices ActorMoveGroupUndo::_GroupServices(ActorMoveGroupUndo *owner)
{
    MMActorGroupServices s{}; s.context=owner;
    s.count=[](void *,void *p)->MMI32 { return static_cast<PDynamicArray>(p)->IvMac(); };
    s.child=[](void *,void *p,MMI32 i)->void * { PActorUndo child; static_cast<PDynamicArray>(p)->Get(i,&child); return child; };
    s.retain=[](void *,void *p) { static_cast<PActorUndo>(p)->AddRef(); };
    s.release=[](void *,void *p) { static_cast<PActorUndo>(p)->Release(); };
    s.append_child=[](void *,void *p,void *child) { PActorUndo value=static_cast<PActorUndo>(child); return int(static_cast<PDynamicArray>(p)->FAdd(&value)); };
    s.child_context=[](void *,void *p,void *movie,MMI32 scene,MMI32 frame) {
        auto child=static_cast<PActorUndo>(p); child->SetPmvie(static_cast<PMovie>(movie)); child->SetIscen(scene); child->SetNfrm(frame);
    };
    s.undo_child=[](void *,void *p,void *document) { return int(static_cast<PActorUndo>(p)->FUndo(static_cast<PDocumentBase>(document))); };
    s.release_entries=[](void *,void *p) { static_cast<PDynamicArray>(p)->Release(); };
    return s;
}
long ActorMoveGroupUndo::Cchild(void)
{
    const auto services=_GroupServices(this); return mm_actor_group_count(&_group,&services);
}

PActorMoveGroupUndo ActorMoveGroupUndo::PamguNew(void)
{
    MMActorGroupFactory s{};
    s.create_owner=[](void *)->void * { return NewObj ActorMoveGroupUndo(); };
    s.create_entries=[](void *)->void * { return DynamicArray::PglNew(size(PActorUndo)); };
    s.publish_entries=[](void *,void *p,void *entries) { static_cast<PActorMoveGroupUndo>(p)->_group.entries=entries; };
    s.release_owner=[](void *,void *p) { static_cast<PActorMoveGroupUndo>(p)->Release(); };
    return static_cast<PActorMoveGroupUndo>(mm_actor_group_create(&s));
}

/****************************************************
 *
 * Destructor.  Releases each owned child ActorUndo.
 *
 ****************************************************/
ActorMoveGroupUndo::~ActorMoveGroupUndo(void)
{
    AssertBaseThis(0); const auto services=_GroupServices(this);
    mm_actor_group_dispose(&_group,1,&services);
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
    AssertThis(0); AssertPo(paund,0); const auto services=_GroupServices(this);
    return mm_actor_group_add_child(&_group,paund,&services)!=0;
}

/****************************************************
 *
 * Undoes the composite group drag by undoing each child in order.
 *
 ****************************************************/
bool ActorMoveGroupUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb,0); const auto services=_GroupServices(this);
    return mm_actor_group_move_toggle(&_group,pdocb,&services)!=0;
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
    MarkMemObj(_Entries());
    if (_Entries() != pvNil)
    {
        for (long iaund = 0; iaund < _Entries()->IvMac(); iaund++)
        {
            PActorUndo paund;
            _Entries()->Get(iaund, &paund);
            MarkMemObj(paund);
        }
    }
}

/***************************************************************************
    Assert the validity of the ActorMoveGroupUndo.

    NOTE: deliberately does NOT chain to MovieUndo_PAR::AssertValid, mirroring
    ActorUndo::AssertValid. The inherited C movie/scene/frame context are only
    populated by Movie::FAddUndo at commit time; before that (during mousedown
    while we're building the composite via FAddChild) the movie handle is nil and the
    parent chain would trip its movie validation.
***************************************************************************/
void ActorMoveGroupUndo::AssertValid(LegacyUlong grf)
{
    AssertPo(_Entries(), 0);
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
MMActorGroupServices ActorRenameGroupUndo::_GroupServices(GroupContext *context)
{
    MMActorGroupServices s{}; s.context=context;
    s.count=[](void *,void *p)->MMI32 { return static_cast<PVirtualStringTable>(p)->IvMac(); };
    s.release_entries=[](void *,void *p) { static_cast<PVirtualStringTable>(p)->Release(); };
    s.read_name=[](void *p,void *entries,MMI32 i)->MMI32 {
        auto c=static_cast<GroupContext *>(p); auto table=static_cast<PVirtualStringTable>(entries); long id;
        table->GetStn(i,&c->stored); table->GetExtra(i,&id); return id;
    };
    s.capture_name=[](void *p,void *movie,MMI32 id) { return int(static_cast<PMovie>(movie)->FGetName(id,&static_cast<GroupContext *>(p)->current)); };
    s.rename=[](void *p,void *movie,MMI32 id) { return int(static_cast<PMovie>(movie)->FNameActr(id,&static_cast<GroupContext *>(p)->stored)); };
    s.store_name=[](void *p,void *entries,MMI32 i) { return int(static_cast<PVirtualStringTable>(entries)->FPutStn(i,&static_cast<GroupContext *>(p)->current)); };
    s.append_name=[](void *,void *entries,MMI32 id,const void *name) {
        long value=id; return int(static_cast<PVirtualStringTable>(entries)->FAddStn(static_cast<PString>(const_cast<void *>(name)),&value));
    };
    return s;
}
long ActorRenameGroupUndo::Cchild(void)
{
    GroupContext context{this}; const auto services=_GroupServices(&context);
    return mm_actor_group_count(&_group,&services);
}

PActorRenameGroupUndo ActorRenameGroupUndo::PargNew(void)
{
    MMActorGroupFactory s{};
    s.create_owner=[](void *)->void * { return NewObj ActorRenameGroupUndo(); };
    s.create_entries=[](void *)->void * { return StringTable_GST::PgstNew(size(long)); };
    s.publish_entries=[](void *,void *p,void *entries) { static_cast<PActorRenameGroupUndo>(p)->_group.entries=entries; };
    s.release_owner=[](void *,void *p) { static_cast<PActorRenameGroupUndo>(p)->Release(); };
    return static_cast<PActorRenameGroupUndo>(mm_actor_group_create(&s));
}

ActorRenameGroupUndo::~ActorRenameGroupUndo(void)
{
    AssertBaseThis(0); GroupContext context{this}; const auto services=_GroupServices(&context);
    mm_actor_group_dispose(&_group,0,&services);
}

bool ActorRenameGroupUndo::FAddChild(long arid, PString pstnPrev)
{
    AssertThis(0); AssertPo(pstnPrev,0); GroupContext context{this}; const auto services=_GroupServices(&context);
    return mm_actor_group_add_name(&_group,arid,pstnPrev,&services)!=0;
}

bool ActorRenameGroupUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb,0); AssertPo(Pmvie(),0); GroupContext context{this};
    const auto services=_GroupServices(&context);
    return mm_actor_group_name_toggle(&_group,&services)!=0;
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
    MarkMemObj(_Names());
}

void ActorRenameGroupUndo::AssertValid(LegacyUlong grf)
{
    AssertPo(_Names(), 0);
}
#endif

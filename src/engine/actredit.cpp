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
#include "mm/actor_effect.h"
#include "mm/actor_paste.h"

ASSERTNAME

using namespace ActorEvent;

RTCLASS(ActorUndo)

#ifdef BUG1950
static const int kPostponedActorCopy = 1;
#else
static const int kPostponedActorCopy = 0;
#endif

/***************************************************************************

    Duplicate the actor from this frame through
    the end of subroute or (if fEntireScene) the end of the scene

***************************************************************************/
MMActorCopyServices Actor::_CopyServices()
{
    MMActorCopyServices s{};
    s.context = this;
    s.create = _CreateServices();
    s.resolve_sound=[](void *p,MMActorSound *value) {
        return int(static_cast<Actor *>(p)->_Scene()->Pmvie()->FResolveSndTag(value->tag, value->child));
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
    bool ok=mm_actor_copy(&_owner,fEntireScene,kPostponedActorCopy,&out,&services)!=0;
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
    MMActorRoutePasteServices s{}; s.subroute=_SubrouteEditServices();
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
    return mm_actor_route_paste(&_owner,pactr->_owner.track.route,static_mode,&s)!=0;
}

/***************************************************************************

    Put an already existing actor in this scene.

***************************************************************************/
bool Actor::FPaste(long nfrm, Scene *pscen)
{
    AssertThis(0);
    static_assert(sizeof(Add)==sizeof(MMActorEntrance),"Paste payload ABI");
    static_assert(ksidUseCrf==0,"Paste document tag ABI");
    MMActorPasteServices s{};
    s.context = this;
    s.create = _CreateServices();
    s.edits = _EventEditServices();
    s.is_text=[](void *,void *p) { return int(static_cast<PTemplate>(p)->FIsTdt()); };
    s.ensure_autosave=[](void *,void *p,void **cache) {
        PChunkyResourceFile resource=nullptr; bool ok=static_cast<PScene>(p)->Pmvie()->FEnsureAutosave(&resource);
        *cache=resource; return int(ok);
    };
    s.save_template=[](void *,MMTag *v,void *cache) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); bool ok=TagManager::FSaveTag(&tag,static_cast<PChunkyResourceFile>(cache),true);
        std::memcpy(v,&tag,sizeof(tag)); return int(ok);
    };
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
    return mm_actor_paste(&_owner,nfrm,pscen,migrate_text,&s)!=0;
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
    MMActorClipboardServices s{};
    s.create=[](void *, MMActorClipboard **storage)->void * {
        auto clipboard = NewObj ActorClipboard();
        if (clipboard)
            *storage = &clipboard->_clipboard;
        return clipboard;
    };
    s.release_clipboard=[](void *,void *p) { static_cast<PActorClipboard>(p)->Release(); };
    s.source_name=[](void *, void *p, const MMStringValue *value) {
        String name;
        static_cast<PActor>(p)->GetName(&name);
        mm_string_value_set(value, name.Prgch(), name.Cch());
    };
    s.copy_prefix=[](void *, void *p, const MMStringValue *value) {
        String prefix;
        static_cast<PActor>(p)->Pscen()->Pmvie()->Pmcc()->GetStn(idsEngineCopyOf, &prefix);
        mm_string_value_set(value, prefix.Prgch(), prefix.Cch());
    };
    s.name_too_long=[](void *) { PushErc(ercSocNameTooLong); };
    s.paste_route=[](void *,void *movie,void *actor) { return int(static_cast<PMovie>(movie)->FPasteActrPath(static_cast<PActor>(actor))); };
    s.paste_actor=[](void *,void *movie,void *actor) { return int(static_cast<PMovie>(movie)->FPasteActr(static_cast<PActor>(actor))); };
    s.rename=[](void *, void *movie, MMI32 actor, const void *text, MMI32 count) {
        return int(static_cast<PMovie>(movie)->FNameActr(actor, static_cast<const achar *>(text), count));
    };
    s.remove=[](void *,void *movie,MMI32 actor) { static_cast<PMovie>(movie)->Pscen()->RemActrCore(actor); };
    return s;
}
PActorClipboard ActorClipboard::PaclpNew(PActor pactr, bool fRteOnly, bool fEntireScene)
{
    AssertPo(pactr,0); Assert(!fRteOnly || !fEntireScene,"Expecting subroute only");
    const auto services = _Services();
    const auto copy = pactr->_CopyServices();
    auto clipboard = static_cast<PActorClipboard>(
        mm_actor_clipboard_create(pactr, fRteOnly, fEntireScene, kPostponedActorCopy, &copy, &services));
    AssertNilOrPo(clipboard,0); return clipboard;
}
ActorClipboard::~ActorClipboard(void)
{
    const auto owner = Actor::_OwnerServices();
    mm_actor_clipboard_dispose(&_clipboard, &owner);
}
bool ActorClipboard::FPaste(PMovie pmvie)
{
    AssertThis(0); AssertPo(pmvie,0);
    const auto owner = Actor::_OwnerServices();
    const auto services = _Services();
    return mm_actor_clipboard_paste(&_clipboard, pmvie, &owner, &services) != 0;
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
    s.create=[](void *,void **handle)->MMActorUndoState * {
        auto undo=ActorUndo::PaundNew();
        *handle=undo;
        return undo ? &undo->_undoState : nullptr;
    };
    s.retain_actor=[](void *,void *actor) { static_cast<PActor>(actor)->AddRef(); };
    s.add=[](void *p,void *undo) { return int(static_cast<PActor>(p)->_Scene()->Pmvie()->FAddUndo(static_cast<PActorUndo>(undo))); };
    s.clear=[](void *p) { static_cast<PActor>(p)->_Scene()->Pmvie()->ClearUndo(); };
    s.release=[](void *,void *p) { static_cast<PActorUndo>(p)->Release(); };
    s.reset_actor=[](void *,void *p) { static_cast<PActor>(p)->Reset(); };
    return mm_actor_undo_create(pactrDup,_owner.runtime.actor,fSndUndo,
                                pstn ? pstn->Prgch() : nullptr,pstn ? pstn->Cch() : 0,&s)!=0;
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
    tribool loop,queue,motion_match;
    long volume,style;
};
MMActorTransactionServices Actor::_TransactionServices(EditContext *context)
{
    MMActorTransactionServices s{}; s.context=context;
    s.duplicate=[](void *p,void **snapshot) {
        PActor value=nullptr; bool ok=static_cast<EditContext *>(p)->actor->FDup(&value); *snapshot=value; return int(ok);
    };
    s.restore=[](void *p,void *snapshot) { static_cast<EditContext *>(p)->actor->Restore(static_cast<PActor>(snapshot)); };
    s.create_undo=[](void *p,void *snapshot,int sound) {
        return int(static_cast<EditContext *>(p)->actor->FCreateUndo(static_cast<PActor>(snapshot),sound!=0));
    };
    s.release=[](void *,void *snapshot) { static_cast<PActor>(snapshot)->Release(); };
    return s;
}
MMActorDeleteServices Actor::_DeleteServices()
{
    MMActorDeleteServices s{};
    s.context=this;
    s.timeline=_TimelineServices();
    s.delete_subroute=[](void *p) { return int(static_cast<Actor *>(p)->_FDeleteEntireSubrte()); };
    s.seek=[](void *p,MMI32 frame) { return int(static_cast<Actor *>(p)->FGotoFrame(frame)); };
    s.delete_forward=[](void *p,int all,int wants_alive,MMI32 original) {
        bool alive=false; static_cast<Actor *>(p)->DeleteFwdCore(all!=0,wants_alive?&alive:nullptr,original); return int(alive);
    };
    s.sound_frame=[](void *p) { static_cast<Actor *>(p)->_Scene()->UpdateSndFrame(); };
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

    Add a sound event to the actor event list, and create an undo object

***************************************************************************/
bool Actor::FSetSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fMotionMatch, long vlm, long sty)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertIn(sty, 0, styLim);

    EditContext context{this};
    context.tag = ptag;
    context.loop = fLoop;
    context.queue = fQueue;
    context.motion_match = fMotionMatch;
    context.volume = vlm;
    context.style = sty;
    auto services = _TransactionServices(&context);
    services.apply = [](void *p) {
        const auto &c = *static_cast<EditContext *>(p);
        return int(c.actor->FSetSndCore(c.tag, c.loop, c.queue, c.motion_match, c.volume, c.style));
    };
    return mm_actor_transaction_apply(MM_TRANSACTION_SOUND, nullptr, &services) != 0;
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
    EditContext context{this}; const auto s=_TransactionServices(&context); const auto d=_DeleteServices();
    MMActorDeleteResult out{}; out.context=pfAlive;
    out.publish=[](void *p,int alive,int trashed) { if (trashed) { TrashVar(static_cast<bool *>(p)); } else *static_cast<bool *>(p)=alive!=0; };
    return mm_actor_transaction_delete(&_owner,fDeleteAll,pfAlive?&out:nullptr,&s,&d)!=0;
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
    s.find_name=[](void *,void *movie,MMI32 id,const void **text,MMU32 *count) {
        return int(static_cast<PMovie>(movie)->FGetName(id,text,count));
    };
    s.rename=[](void *,void *movie,MMI32 id,const void *text,MMU32 count) {
        static_cast<PMovie>(movie)->FNameActr(id,static_cast<const achar *>(text),count);
    };
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

#ifdef DEBUG
static constexpr int kTrashGroupPadding = 1;
#else
static constexpr int kTrashGroupPadding = 0;
#endif

ActorMoveGroupUndo::ActorMoveGroupUndo()
{
    const auto allocator = Group::CollectionAllocator();
    AssertDo(mm_actor_move_group_init(&_group, &_undoContext, &allocator, kTrashGroupPadding),
             "invalid move undo storage");
}

/****************************************************
 *
 * Allocates a new ActorMoveGroupUndo composite undo object.
 *
 * Returns:
 *  A pointer to the object, or pvNil if it could not be created.
 *
 ****************************************************/
MMActorMoveGroupServices ActorMoveGroupUndo::_GroupServices()
{
    MMActorMoveGroupServices services{};
    services.retain = [](void *, void *p) { static_cast<PUndoBase>(p)->AddRef(); };
    services.release = [](void *, void *p) { static_cast<PUndoBase>(p)->Release(); };
    services.child_context = [](void *, void *p, void *movie, MMI32 scene, MMI32 frame) {
        auto child = static_cast<PActorUndo>(static_cast<PUndoBase>(p));
        child->SetPmvie(static_cast<PMovie>(movie));
        child->SetIscen(scene);
        child->SetNfrm(frame);
    };
    services.undo_child = [](void *, void *p, void *document) {
        return int(static_cast<PUndoBase>(p)->FUndo(static_cast<PDocumentBase>(document)));
    };
    return services;
}

MMActorDragUndoServices ActorMoveGroupUndo::DragUndoServices(PMovie movie)
{
    MMActorDragUndoServices services{};
    services.context = movie;
    services.composite = _GroupServices();
    services.create_actor = [](void *, void **handle) -> MMActorUndoState * {
        auto undo = ActorUndo::PaundNew();
        *handle = static_cast<PUndoBase>(undo);
        return undo ? &undo->_undoState : nullptr;
    };
    services.create_group = [](void *, void **handle) -> MMActorMoveGroup * {
        auto undo = PamguNew();
        *handle = static_cast<PUndoBase>(undo);
        return undo ? &undo->_group : nullptr;
    };
    services.commit = [](void *p, void *undo) {
        auto movieUndo = static_cast<PMovieUndo>(static_cast<PUndoBase>(undo));
        return int(static_cast<PMovie>(p)->FAddUndo(movieUndo));
    };
    services.release = services.composite.release;
    services.clear = [](void *p) { static_cast<PMovie>(p)->ClearUndo(); };
    services.not_undoable = [](void *) { PushErc(ercSocNotUndoable); };
    return services;
}

long ActorMoveGroupUndo::Cchild(void)
{
    return _group.count;
}

PActorMoveGroupUndo ActorMoveGroupUndo::PamguNew(void)
{
    return NewObj ActorMoveGroupUndo();
}

/****************************************************
 *
 * Destructor.  Releases each owned child ActorUndo.
 *
 ****************************************************/
ActorMoveGroupUndo::~ActorMoveGroupUndo(void)
{
    AssertBaseThis(0);
    const auto services = _GroupServices();
    mm_actor_move_group_dispose(&_group, &services);
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
    AssertThis(0);
    AssertPo(paund, 0);
    const auto services = _GroupServices();
    return mm_actor_move_group_add(&_group, static_cast<PUndoBase>(paund), &services) != 0;
}

/****************************************************
 *
 * Undoes the composite group drag by undoing each child in order.
 *
 ****************************************************/
bool ActorMoveGroupUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    const auto services = _GroupServices();
    return mm_actor_move_group_toggle(&_group, pdocb, &services) != 0;
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
    MarkPv(_group.storage.buffers[0].data);
    for (MMU32 index = 0; index < _group.count; ++index)
    {
        auto child = static_cast<PUndoBase>(mm_actor_move_group_child(&_group, index));
        MarkMemObj(child);
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
    Assert(_group.count <= _group.storage.buffers[0].capacity / sizeof(PUndoBase), "short move undo table");
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

ActorRenameGroupUndo::ActorRenameGroupUndo()
{
    const auto allocator = Group::CollectionAllocator();
    AssertDo(mm_actor_rename_group_init(&_group, &_undoContext, sizeof(achar), koskCur, &allocator,
                                       kTrashGroupPadding),
             "invalid rename undo storage");
}

long ActorRenameGroupUndo::Cchild(void)
{
    return _group.names.count;
}

PActorRenameGroupUndo ActorRenameGroupUndo::PargNew(void)
{
    return NewObj ActorRenameGroupUndo();
}

ActorRenameGroupUndo::~ActorRenameGroupUndo(void)
{
    AssertBaseThis(0);
    mm_actor_rename_group_dispose(&_group);
}

bool ActorRenameGroupUndo::FAddChild(long arid, PString pstnPrev)
{
    AssertThis(0);
    AssertPo(pstnPrev, 0);
    return mm_actor_rename_group_add(&_group, arid, pstnPrev->Prgch(), pstnPrev->Cch()) != 0;
}

bool ActorRenameGroupUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    AssertPo(Pmvie(), 0);
    MMActorRenameGroupServices services{};
    services.find_name = [](void *, void *movie, MMI32 actor, const void **text, MMU32 *count) {
        return int(static_cast<PMovie>(movie)->FGetName(actor, text, count));
    };
    services.rename = [](void *, void *movie, MMI32 actor, const void *text, MMU32 count) {
        return int(static_cast<PMovie>(movie)->FNameActr(actor, static_cast<const achar *>(text), count));
    };
    return mm_actor_rename_group_toggle(&_group, &services) != 0;
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
    MarkPv(_group.names.storage.buffers[0].data);
    MarkPv(_group.names.storage.buffers[1].data);
}

void ActorRenameGroupUndo::AssertValid(LegacyUlong grf)
{
    const auto &names = _group.names;
    Assert(names.unit == sizeof(achar) && names.stride == sizeof(MMI32) + 4, "invalid rename undo row");
    Assert(names.bytes <= names.storage.buffers[0].capacity, "short rename undo names");
    Assert(names.count <= names.storage.buffers[1].capacity / (sizeof(MMI32) + 4), "short rename undo rows");
}
#endif

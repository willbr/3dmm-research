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

ASSERTNAME

using namespace ActorEvent;

RTCLASS(ActorUndo)

/***************************************************************************

    Duplicate the actor from this frame through
    the end of subroute or (if fEntireScene) the end of the scene

***************************************************************************/
bool Actor::FCopy(PActor *ppactr, bool fEntireScene)
{
    AssertThis(0);
    AssertVarMem(ppactr);

    long iaev;
    long iaevLast;
    Base aev;
    Action aevactn;
    Sound aevsnd;
    RouteDistancePoint rpt;
    RouteDistancePoint rptOld;
    RouteDistancePoint *prptSrc;
    RouteDistancePoint *prptDest;

    (*ppactr) = PactrNew(&_tagTmpl);

    if (*ppactr == pvNil)
    {
        return fFalse;
    }

    // If the current point is between nodes, a node will
    // be inserted.  Compute its dwr.
    _track.Route()->Get(_state.location.irpt, &rpt);
    rptOld.dwr = rpt.dwr;
    rpt.dwr = BrsSub(rptOld.dwr, _state.location.dwrOffset);

    //
    // Gather all earlier events & move to the current one
    // It is sufficient to begin with the current subroute
    // Note: Add events will require later translation of nfrm
    //
    for (iaev = _state.addEvent; iaev < _state.nextEvent; iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);

        // Is this an event we want to copy?
        switch (aev.aet)
        {
        case aetActn: // Copy, editing current cel only
            _track.Events()->Get(iaev, &aevactn);
            aevactn.celn = _state.cel;
            break;

        // copy
        case aetAdd:
        case aetCost:
        case aetPull:
        case aetSize:
        case aetRotF:
        case aetFreeze:
        case aetStep:
        case aetMove:
            break;

        // The following events are not automatically copied
        case aetSnd:
            _track.Events()->Get(iaev, &aevsnd);
            // Update the cno for the chid from the original movie
            // The scene this came from may be lost later
            if (!_pscen->Pmvie()->FResolveSndTag(&aevsnd.tag, aevsnd.chid))
            {
                goto LFail;
            }
            _track.Events()->Put(iaev, &aevsnd);
            if (aevsnd.celn == smmNil && iaev >= _state.firstEventInFrame)
                break;
            if (iaev > _state.actionEvent && aevsnd.celn != smmNil)
            {
                // Retain current motion match sounds
                break;
            }
            continue;
        case aetTweak:
        case aetRotH:
#ifdef BUG1950
            // REVIEW (*****):  Postponed till v2.0
            if (iaev >= _state.nextEvent) // Code change not yet verified
#else                             //! BUG1950
            if (iaev >= _state.firstEventInFrame)
#endif                            //! BUG1950
            {
                // Retain these events from this frame
                break;
            }
            continue;
        case aetRem:
            continue; // Do not copy

        default:
            Bug("Unknown event type... Copy or Not?");
            break;
        }

        // set the event to happen right here.
        aev.rtel.dnfrm = 0;
        aev.rtel.irpt = 0;
        aev.rtel.dwrOffset = 0;
        iaevLast = (*ppactr)->_track.Events()->IvMac();

        // Insert aev.  Tag ref count will be updated.
        _track.Events()->Lock();
        if (!(*ppactr)->_FInsertAev(iaevLast, _track.Events()->Cb(iaev),
                                    (aev.aet == aetActn) ? &aevactn : _track.Events()->QvGet(iaev), &aev, fFalse))
        {
            _track.Events()->Unlock();
            goto LFail;
        }
        _track.Events()->Unlock();
        (*ppactr)->_MergeAev(0, iaevLast);
    }

    //
    // Copy remaining events, adjusting their locations
    //
    for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);

        if ((!fEntireScene) && (aetAdd == aev.aet) && (*ppactr)->_track.Events()->IvMac() > 0)
        {
            break;
        }

        // Adjust the event indicies.
        aev.rtel.irpt -= _state.location.irpt;

        if (aev.rtel.irpt == 0)
        {
            if (aev.rtel.dwrOffset == _state.location.dwrOffset)
            {
                aev.rtel.dwrOffset = rZero;
                if (aev.rtel.dnfrm < _state.location.dnfrm)
                {
                    aev.rtel.dnfrm = 0;
                }
                else
                {
                    aev.rtel.dnfrm -= _state.location.dnfrm;
                }
            }
            else
            {
                if (rpt.dwr == rZero)
                    aev.rtel.dwrOffset = rZero;
                else
                {
                    aev.rtel.dwrOffset = BrsSub(aev.rtel.dwrOffset, _state.location.dwrOffset);
                }
            }
        }

        // Insert will update the tag ref count
        iaevLast = (*ppactr)->_track.Events()->IvMac();
        _track.Events()->Lock();
        if (!(*ppactr)->_FInsertAev(iaevLast, _track.Events()->Cb(iaev), _track.Events()->QvGet(iaev), &aev, fFalse))
        {
            _track.Events()->Unlock();
            goto LFail;
        }
        _track.Events()->Unlock();
    }

    //
    // Add the point we are at.
    //
    if (!(*ppactr)->_track.Route()->FInsert(0, &rpt))
    {
        goto LFail;
    }

    //
    // Copy the rest of the subroute or route
    //
    if ((_state.location.irpt + 1) < _track.Route()->IvMac())
    {
        // Locate the amount of route to copy
        //
        long irptLim = _track.Route()->IvMac();
        long irpt;

        if (!fEntireScene)
        {
            for (irpt = _state.location.irpt; irpt < irptLim; irpt++)
            {
                _track.Route()->Get(irpt, &rpt);
                if (rZero == rpt.dwr)
                {
                    irptLim = irpt + 1;
                    break;
                }
            }
        }

        if (!(*ppactr)->_track.Route()->FSetIvMac((*ppactr)->_track.Route()->IvMac() + irptLim -
                                                (_state.location.irpt + 1)))
        {
            goto LFail;
        }

        prptSrc = (RouteDistancePoint *)_track.Route()->QvGet(_state.location.irpt + 1);
        prptDest = (RouteDistancePoint *)(*ppactr)->_track.Route()->QvGet(1);
        CopyPb(prptSrc, prptDest, LwMul(irptLim - (_state.location.irpt + 1), size(RouteDistancePoint)));
    }
    else
    {
        // Mark the end of the path
        rpt.dwr = rZero;
        (*ppactr)->_track.Route()->Put(0, &rpt);
    }

    (*ppactr)->_SetStateRewound();

    AssertPo(*ppactr, 0);
    return fTrue;

LFail:
    ReleasePpo(ppactr);
    return fFalse;
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
    AssertThis(0);
    AssertVarMem(ppactr);

    long irpt;
    long dnrpt;
    long irptLim;
    RouteDistancePoint rpt;
    RouteDistancePoint rpt1;
    RouteDistancePoint rptNode;

    (*ppactr) = PactrNew(&_tagTmpl);
    if (*ppactr == pvNil)
    {
        return fFalse;
    }

    //
    // Insert the current point
    //
    _GetXyzFromRtel(&_state.location, &rpt.xyz);
    _track.Route()->Get(_state.location.irpt, &rptNode);
    rpt.dwr = rZero;

    if (_state.location.dwrOffset == rZero)
    {
        rpt.dwr = rptNode.dwr;
    }
    else if (rptNode.dwr > rZero && _state.location.irpt < _track.Route()->IvMac() - 1)
    {
        _track.Route()->Get(_state.location.irpt + 1, &rpt1);
        rpt.dwr = BR_LENGTH3(BrsSub(rpt1.xyz.dxr, rpt.xyz.dxr), BrsSub(rpt1.xyz.dyr, rpt.xyz.dyr),
                             BrsSub(rpt1.xyz.dzr, rpt.xyz.dzr));
        if (rZero == rpt.dwr)
        {
            rpt.dwr = rEps; // Epsilon.  Prevent pathological incorrect end-of-path
        }
    }

    if (!(*ppactr)->_track.Route()->FInsert(0, &rpt))
    {
        goto LFail;
    }

    if ((!fEntireScene) && rpt.dwr == rZero)
    {
        goto LEnd;
    }

    //
    // If copying subroute only, determine amount to copy
    //
    irptLim = _track.Route()->IvMac();
    if (!fEntireScene)
    {
        for (irpt = _state.location.irpt + 1; irpt < irptLim; irpt++)
        {
            _track.Route()->Get(irpt, &rpt);
            if (rZero == rpt.dwr)
            {
                irptLim = irpt + 1;
                break;
            }
        }
    }

    //
    // Copy indicated portion of the route
    //
    dnrpt = irptLim - (_state.location.irpt + 1);
    if (dnrpt > 0 && !(*ppactr)->_track.Route()->FEnsureSpace(dnrpt, fgrpNil))
        goto LFail;

    for (irpt = _state.location.irpt + 1; irpt < irptLim; irpt++)
    {
        _track.Route()->Get(irpt, &rpt);

        if (!(*ppactr)->_track.Route()->FInsert(irpt - _state.location.irpt, &rpt))
        {
            goto LFail;
        }

        if ((!fEntireScene) && (rZero == rpt.dwr))
        {
            break;
        }
    }

LEnd:
    (*ppactr)->_SetStateRewound();
    return fTrue;

LFail:
    ReleasePpo(ppactr);
    return fFalse;
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
    AssertThis(0);
    AssertVarMem(pactr);

    Base aev;
    RouteDistancePoint rpt;
    RouteDistancePoint rptCur;
    RoutePoint dxyz;
    long iaev;
    long irpt;
#ifdef STATIC
    bool fStatic;
#endif // STATIC
    long crptDel = 0;
    long crptNew = pactr->_track.Route()->IvMac() - 1;

    if (crptNew <= 0)
    {
        PushErc(ercSocNothingToPaste);
        return fFalse;
    }

    if (!_track.Route()->FEnsureSpace(crptNew, fgrpNil) || !_track.Events()->FEnsureSpace(1, kcbVarStep, fgrpNil))
    {
        return fFalse;
    }

    //
    // May be positioned between nodes -> potentially
    // insert the current point
    //
    _GetXyzFromRtel(&_state.location, &rptCur.xyz);
    if (rZero != _state.location.dwrOffset)
    {
        BRS dwrCur = _state.location.dwrOffset;
        _state.location.irpt++;
        _state.location.dwrOffset = rZero;
        _state.location.dnfrm = 0;
        if (!_FInsertGgRpt(_state.location.irpt, &rptCur, dwrCur))
            return fFalse;
        _GetXyzFromRtel(&_state.location, &_state.position);
        _AdjustAevForRteIns(_state.location.irpt, 0);
    }

    //
    // Delete to the end of this *sub*route
    //
    for (irpt = _state.location.irpt + 1; irpt < _track.Route()->IvMac(); irpt++)
    {
        _track.Route()->Get(irpt, &rpt);
        crptDel++;
        if (rpt.dwr == rZero)
        {
            break;
        }
    }
    if (crptDel > 0)
    {
        _track.Route()->Delete(_state.location.irpt, crptDel);
    }

    //
    // Remove events until the end of the *sub*route
    // Space optimization: should precede paste
    // Update location pointer of events of later subroutes
    //
    for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.rtel.irpt > _state.location.irpt + crptDel)
        {
            aev.rtel.irpt += crptNew - crptDel;
            _track.Events()->PutFixed(iaev, &aev);
            continue;
        }
        else
        {
            _RemoveAev(iaev, fFalse);
            iaev--;
        }
    }

    //
    // Paste in the new route.
    // Translate the points of this section of route
    // Adjust the aev presently
    //
    pactr->_track.Route()->Get(0, &rpt);
    dxyz.dxr = BrsSub(rptCur.xyz.dxr, rpt.xyz.dxr);
    dxyz.dyr = BrsSub(rptCur.xyz.dyr, rpt.xyz.dyr);
    dxyz.dzr = BrsSub(rptCur.xyz.dzr, rpt.xyz.dzr);
    for (irpt = 1; irpt <= crptNew; irpt++)
    {
        pactr->_track.Route()->Get(irpt, &rpt);
        rpt.xyz.dxr = BrsAdd(rpt.xyz.dxr, dxyz.dxr);
        rpt.xyz.dyr = BrsAdd(rpt.xyz.dyr, dxyz.dyr);
        rpt.xyz.dzr = BrsAdd(rpt.xyz.dzr, dxyz.dzr);
        AssertDo(_track.Route()->FInsert(_state.location.irpt + irpt, &rpt), "Logic error");
    }

    //
    // Set the right dwr distance from the current point to
    // the first point on the new section of route
    //
    _track.Route()->Get(_state.location.irpt + 1, &rpt);
    rptCur.dwr = BR_LENGTH3(BrsSub(rpt.xyz.dxr, rptCur.xyz.dxr), BrsSub(rpt.xyz.dyr, rptCur.xyz.dyr),
                            BrsSub(rpt.xyz.dzr, rptCur.xyz.dzr));
    if (rZero == rptCur.dwr)
        rptCur.dwr = rEps; // Epsilon.  Prevent pathological incorrect end-of-path
    _track.Route()->Put(_state.location.irpt, &rptCur);

#ifdef STATIC
    //
    // Force floating behavior on a static action
    //
    Assert(_state.actionEvent >= 0, "Actor has no action");
    if (!_FGetStatic(_state.action, &fStatic))
        return fFalse;
    if (fStatic)
    {
        if (!FSetStep(kdwrNil))
            return fFalse;
    }
#else  //! STATIC
    // Force continuation onto newly pasted path
    if (!FSetStep(kdwrNil))
        return fFalse;
#endif //! STATIC

    //
    // Set new end of path freeze & step events
    //
    int32_t faevfrz = (int32_t)fTrue;
    BRS dwrStep = rZero;
    aev.aet = aetFreeze;
    aev.rtel.irpt = _state.location.irpt + crptNew;
    aev.rtel.dnfrm = 0;
    aev.rtel.dwrOffset = rZero;
    aev.nfrm = _state.frame; // will be updated in ComputeLifetime()
    if (!_FInsertAev(_state.nextEvent, kcbVarFreeze, &faevfrz, &aev))
        return fFalse;
    aev.aet = aetStep;
    if (!_FInsertAev(_state.nextEvent, kcbVarStep, &dwrStep, &aev))
        return fFalse;

    _fLifeDirty = fTrue;
    _pscen->InvalFrmRange();
    _pscen->MarkDirty();

    _PositionBody(&_state.position);
    return fTrue;
}

/***************************************************************************

    Put an already existing actor in this scene.

***************************************************************************/
bool Actor::FPaste(long nfrm, Scene *pscen)
{
    AssertThis(0);

    Base aev;
    RouteDistancePoint rpt;
    Add aevadd;
    Sound aevsnd;
    BRS xrCam = rZero;
    BRS yrCam = rZero;
    BRS zrCam = kzrDefault;
    BRS xr, yr, zr;
    long iaev;
    long dnfrm;
#ifdef BUG1888
    PTemplate ptmpl;
    PChunkyResourceFile pcrf;
    TAG tag;

    //
    // Ensure that the tag to the ThreeDText being pasted is in the current movie.
    //
    if (FIsTdt())
    {
        Assert(_tagTmpl.sid == ksidUseCrf, "ThreeDTexts should be stored in a document!");

        if (!pscen->Pmvie()->FEnsureAutosave(&pcrf))
        {
            return fFalse;
        }
        if (pcrf != _tagTmpl.pcrf)
        {
            // Need to save this actor's tagTmpl in this movie because it came from another movie

            tag = _tagTmpl;
            TagManager::DupTag(&tag);
            // Save the tag to the movie's _pcrfAutosave.  The tag now
            // points to the copy in this movie.
            if (!TagManager::FSaveTag(&tag, pcrf, fTrue))
            {
                TagManager::CloseTag(&tag);
                return fFalse;
            }
            // Get a template based on the new tag
            ptmpl = (PTemplate)vptagm->PbacoFetch(&tag, Template::FReadTmpl);
            if (pvNil == ptmpl)
            {
                TagManager::CloseTag(&tag);
                return fFalse;
            }
            // Change the actor to use the new tag and template
            TagManager::CloseTag(&_tagTmpl);
            _tagTmpl = tag;
            ReleasePpo(&_ptmpl);
            _ptmpl = ptmpl;
        }
    }
#endif // BUG1888

    //
    // Update lifetime
    //
    _nfrmFirst = nfrm;
    _fLifeDirty = fTrue;
    _state.frame = nfrm - 1;
    SetPscen(pscen);

    // Always place actors on the "floor"
    _GetNewOrigin(&xr, &yr, &zr);

    Assert(_track.Events()->IvMac() > 0, "Nothing to paste!");
    _track.Events()->GetFixed(0, &aev);
    if (aev.aet != aetAdd)
        return fFalse;
    dnfrm = aev.nfrm - nfrm;

    //
    // Begin by locating the actor at (xr,yr,zr)
    // There are no Full path or Sub path translations at this point.
    // Note: In order to place a pasted actor at the insertion point,
    // the translation needs to compensate for the distance
    // recorded in each path point -> subtract the first path point.
    _track.Route()->Get(0, &rpt);
    _state.subrouteTranslation.dxr = rZero;
    _state.subrouteTranslation.dyr = rZero;
    _state.subrouteTranslation.dzr = rZero;
    _dxyzFullRte.dxr = BrsSub(xr, rpt.xyz.dxr);
    _dxyzFullRte.dyr = BrsSub(yr, rpt.xyz.dyr);
    _dxyzFullRte.dzr = BrsSub(zr, rpt.xyz.dzr);
    _track.Events()->Get(0, &aevadd);
    _dxyzFullRte.dxr = BrsSub(_dxyzFullRte.dxr, aevadd.dxr);
    _dxyzFullRte.dyr = BrsSub(_dxyzFullRte.dyr, aevadd.dyr);
    _dxyzFullRte.dzr = BrsSub(_dxyzFullRte.dzr, aevadd.dzr);

    //
    // Translate the new actor in time
    // Update sound events
    for (iaev = 0; iaev < _track.Events()->IvMac(); iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (dnfrm != 0)
        {
            aev.nfrm -= dnfrm;
            _track.Events()->PutFixed(iaev, &aev);
        }
        if (aetSnd != aev.aet)
            continue;
        _track.Events()->Get(iaev, &aevsnd);
        if (aevsnd.tag.sid != ksidUseCrf)
            continue;
        // Save tag (this may be a new movie)
        if (!aevsnd.tag.pcrf->Pcfl()->FFind(aevsnd.tag.ctg, aevsnd.tag.cno))
        {
            PushErc(ercSocNoSndOnPaste);
            _RemoveAev(iaev);
            iaev--;
            continue;
        }
        if (!_pscen->Pmvie()->FSaveTagSnd(&aevsnd.tag))
        {
            Bug("Expected to locate user sound chunk");
            _RemoveAev(iaev);
            iaev--;
        }
        else
        {
            // Adopt this sound into the new scene
            if (!_pscen->Pmvie()->FChidFromUserSndCno(aevsnd.tag.cno, &aevsnd.chid))
                return fFalse;
            // Update event
            _track.Events()->Put(iaev, &aevsnd);
        }
    }

    // Rotate 3D spletters to face the camera
    if (_ptmpl->FIsTdt())
    {
        _track.Events()->Get(0, &aevadd);
        aevadd.ya = _pscen->Pbkgd()->BraRotYCamera();
        _track.Events()->Put(0, &aevadd);
    }

    _UpdateXyzRte();
    _pscen->MarkDirty();
    return fTrue;
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
    AssertPo(pactrDup, 0);
    AssertNilOrPo(pstn, 0);

    PActorUndo paund;

    paund = ActorUndo::PaundNew();

    if (paund == pvNil)
    {
        return fFalse;
    }

    paund->SetPactr(pactrDup);
    paund->SetArid(_arid);
    paund->SetSndUndo(fSndUndo);
    if (pvNil != pstn)
        paund->SetStn(pstn);

    if (!_pscen->Pmvie()->FAddUndo(paund))
    {
        _pscen->Pmvie()->ClearUndo();
        ReleasePpo(&paund);
        return (fFalse);
    }

    ReleasePpo(&paund);

    //
    // Detach from the scene
    //
    pactrDup->Reset();

    return (fTrue);
}

/***************************************************************************

    Add (or replace) an action, and create an undo object

***************************************************************************/
bool Actor::FSetAction(long anid, long celn, bool fFreeze, PActor *ppactrDup)
{
    AssertThis(0);
    AssertNilOrVarMem(ppactrDup);

    PActor pactrDup;

    if (!FDup(&pactrDup))
    {
        return (fFalse);
    }

    if (!FSetActionCore(anid, celn, fFreeze))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    if (pvNil == ppactrDup)
        ReleasePpo(&pactrDup);
    else
        *ppactrDup = pactrDup;

    return fTrue;
}

/***************************************************************************

    Add the event to the event list: Add actor on the stage, and create undo
    object.

***************************************************************************/
bool Actor::FAddOnStage(void)
{
    AssertThis(0);

    PActor pactrDup;

    if (!FDup(&pactrDup))
    {
        return (fFalse);
    }

    if (!FAddOnStageCore())
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    ReleasePpo(&pactrDup);
    return fTrue;
}

/***************************************************************************

    Normalize an actor.

***************************************************************************/
bool Actor::FNormalize(ulong grfnorm)
{
    AssertThis(0);

    PActor pactrDup;

    if (!FDup(&pactrDup))
    {
        return (fFalse);
    }

    if (!FNormalizeCore(grfnorm))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    ReleasePpo(&pactrDup);
    return fTrue;
}

/***************************************************************************

    Set the Costume for a body part
    Add the event to the event list

***************************************************************************/
bool Actor::FSetCostume(long ibset, TAG *ptag, long cmid, tribool fCmtl)
{
    AssertThis(0);
    Assert(fCmtl || ibset >= 0, "Invalid ibset argument");
    AssertVarMem(ptag);

    PActor pactrDup;

    FDup(&pactrDup);

    if (!FSetCostumeCore(ibset, ptag, cmid, fCmtl))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    ReleasePpo(&pactrDup);
    return fTrue;
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
    PActor pactrDup;
    long iaevCurSav;
    Base *paev;

    if (!FDup(&pactrDup))
    {
        if (pvNil != pfAlive)
            TrashVar(pfAlive);
        return fFalse;
    }

    // Unless we are deleting to the end of the scene,
    // we need to special case deletion that begins at
    // the same frame as the Add event - otherwise, the
    // code backs up one frame, putting the current frame
    // on the previous subroute.
    // Note: FDelete() does not require that the current
    // frame be	later than _nfrmFirst.
    if (_state.addEvent >= 0 && !fDeleteAll)
    {
        paev = (Base *)_track.Events()->QvFixedGet(_state.addEvent);
        if (_state.frame == paev->nfrm)
        {
            if (!_FDeleteEntireSubrte())
                goto LFail;
            if (pvNil != pfAlive)
                *pfAlive = FPure(_track.Events()->IvMac() > 0);
            goto LDeleted;
        }
    }

    // Go to the previous frame to update state variables
    iaevCurSav = _state.nextEvent;
    if (!FGotoFrame(_state.frame - 1))
    {
        goto LFail;
    }

#ifndef BUG1870
    // The next two lines are obsolete & cause placement orientation bugs
    // Save the current orientation
    _SaveCurPathOrien();
#endif //! BUG1870

    DeleteFwdCore(fDeleteAll, pfAlive, iaevCurSav);

    // Return to original frame
    // _state.frame was decremented above
    if (!FGotoFrame(_state.frame + 1))
    {
        goto LFail;
    }

    /* Might have deleted some sound events */
    if (Pscen() != pvNil)
        Pscen()->UpdateSndFrame();

LDeleted:
    // The frame slider never required lifetime recomputation at this point.
    // Motion match sounds and prerendering both do, however.
    if (!_FComputeLifetime())
        PushErc(ercSocBadFrameSlider);

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    ReleasePpo(&pactrDup);
    return fTrue;

LFail:
    PushErc(ercSocGotoFrameFailure);
    ReleasePpo(&pactrDup);
    return fFalse;
}

/***************************************************************************

    Add the event to the event list: Remove actor from the stage, and an Undo.
    NOTE: This should be called <before> the call to place the actor offstage
***************************************************************************/
bool Actor::FRemFromStage(void)
{
    AssertThis(0);

    PActor pactr;

    if (!FDup(&pactr))
    {
        return (fFalse);
    }

    if (!FRemFromStageCore())
    {
        Restore(pactr);
        return fFalse;
    }

    if (!FCreateUndo(pactr))
    {
        Restore(pactr);
        ReleasePpo(&pactr);
        return (fFalse);
    }

    ReleasePpo(&pactr);
    return fTrue;
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
ActorUndo::~ActorUndo(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pactr);
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
    AssertThis(0);
    AssertPo(pdocb, 0);

    long nfrmTmp;
    bool fRet;

    if (!_fSoonerLater)
    {
        return (FUndo(pdocb));
    }

    nfrmTmp = _nfrm;
    _nfrm = _nfrmLast;

    fRet = FUndo(pdocb);

    _nfrm = nfrmTmp;

    return (fRet);
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
    AssertThis(0);
    AssertPo(pdocb, 0);

    PActor pactr;
    PMovieView pmvu;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        return (fFalse);
    }

    if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
    {
        _pmvie->ClearUndo();
        return (fFalse);
    }

    _pmvie->Pmsq()->FlushMsq();

    pactr = _pmvie->Pscen()->PactrFromArid(_arid);
    AssertNilOrPo(pactr, 0);

    if (pactr != pvNil)
    {
        pactr->AddRef();
    }

    //
    // Have scene replace the old actor with this one
    //
    if (_pactr == pvNil)
    {
        _pmvie->Pscen()->RemActrCore(pactr->Arid());
    }
    else
    {
        if (_stn.Cch() != 0)
        {
            // Undo actor name change
            String stn;
            if (_pmvie->FGetName(_arid, &stn))
            {
                // If FNameActr fails, the actor will not have
                // the correct name...not great, but the user's document
                // won't be corrupted or anything.  Someone will push a
                // ercOom, so I ignore the return value here.
                _pmvie->FNameActr(_pactr->Arid(), &_stn);
                _stn = stn;
            }
        }

        if (_arid != _pactr->Arid())
        {
            _pmvie->Pscen()->RemActrCore(_arid);
            _arid = _pactr->Arid();
        }

        if (!_pmvie->Pscen()->FAddActrCore(_pactr))
        {
            ReleasePpo(&pactr);
            return (fFalse);
        }

        pmvu = (PMovieView)_pmvie->PddgGet(0);
        AssertNilOrPo(pmvu, 0);

        if ((pmvu != pvNil) && !pmvu->FTextMode())
        {
            _pmvie->Pscen()->SelectActr(_pactr);
        }

        ReleasePpo(&_pactr);
    }

    _pmvie->Pscen()->InvalFrmRange();

    _pactr = pactr;

    if (pactr != pvNil)
    {
        pactr->Reset();
    }

    if (_fSndUndo)
    {
        _pmvie->Pmsq()->PlayMsq();
    }
    else
    {
        _pmvie->Pmsq()->FlushMsq();
    }

    return (fTrue);
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
    AssertThis(0);

    _pactr = pactr;
    pactr->AddRef();
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
PActorMoveGroupUndo ActorMoveGroupUndo::PamguNew(void)
{
    PActorMoveGroupUndo pamgu;
    pamgu = NewObj ActorMoveGroupUndo();
    if (pamgu == pvNil)
    {
        return pvNil;
    }
    pamgu->_pglpaund = DynamicArray::PglNew(size(PActorUndo));
    if (pamgu->_pglpaund == pvNil)
    {
        ReleasePpo(&pamgu);
        return pvNil;
    }
    return pamgu;
}

/****************************************************
 *
 * Destructor.  Releases each owned child ActorUndo.
 *
 ****************************************************/
ActorMoveGroupUndo::~ActorMoveGroupUndo(void)
{
    AssertBaseThis(0);
    if (_pglpaund != pvNil)
    {
        for (long iaund = 0; iaund < _pglpaund->IvMac(); iaund++)
        {
            PActorUndo paund;
            _pglpaund->Get(iaund, &paund);
            ReleasePpo(&paund);
        }
        ReleasePpo(&_pglpaund);
    }
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

    paund->AddRef();
    if (!_pglpaund->FAdd(&paund))
    {
        ReleasePpo(&paund);
        return fFalse;
    }
    return fTrue;
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

    for (long iaund = 0; iaund < _pglpaund->IvMac(); iaund++)
    {
        PActorUndo paund;
        _pglpaund->Get(iaund, &paund);
        if (paund == pvNil)
        {
            continue;
        }
        // Mirror MovieUndo's iscen/nfrm onto each child so its FUndo
        // navigates to the correct frame.
        paund->SetPmvie(_pmvie);
        paund->SetIscen(_iscen);
        paund->SetNfrm(_nfrm);
        // Stop on the first failure: an ActorUndo failure can trigger
        // Movie::ClearUndo, after which continuing perturbs further state.
        if (!paund->FUndo(pdocb))
        {
            return fFalse;
        }
    }
    return fTrue;
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

PActorRenameGroupUndo ActorRenameGroupUndo::PargNew(void)
{
    PActorRenameGroupUndo parg;
    parg = NewObj ActorRenameGroupUndo();
    if (parg == pvNil)
        return pvNil;
    parg->_pgstNames = (PVirtualStringTable)StringTable_GST::PgstNew(size(long));
    if (parg->_pgstNames == pvNil)
    {
        ReleasePpo(&parg);
        return pvNil;
    }
    return parg;
}

ActorRenameGroupUndo::~ActorRenameGroupUndo(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pgstNames);
}

bool ActorRenameGroupUndo::FAddChild(long arid, PString pstnPrev)
{
    AssertThis(0);
    AssertPo(pstnPrev, 0);
    return _pgstNames->FAddStn(pstnPrev, &arid);
}

bool ActorRenameGroupUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    AssertPo(_pmvie, 0);

    String stnStored, stnCurrent;
    long arid;
    long cstn = _pgstNames->IvMac();

    for (long istn = 0; istn < cstn; istn++)
    {
        _pgstNames->GetStn(istn, &stnStored);
        _pgstNames->GetExtra(istn, &arid);
        // Capture the current (post-rename) name so a subsequent FDo/FUndo
        // can flip back to it. If the actor isn't in roll-call any more
        // (e.g. removed since the rename), skip but keep the entry so a
        // round-trip stays well-formed.
        if (!_pmvie->FGetName(arid, &stnCurrent))
            continue;
        if (!_pmvie->FNameActr(arid, &stnStored))
            return fFalse;
        // Swap: store the just-replaced name so the next toggle restores it.
        _pgstNames->FPutStn(istn, &stnCurrent);
    }
    return fTrue;
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

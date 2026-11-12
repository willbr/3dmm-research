/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    Actor Sound.

    Primary Author: ******
    Status:  Reviewed

***************************************************************************/

#include "soc.h"
#include <cstring>

ASSERTNAME

using namespace ActorEvent;

/***************************************************************************

    Add a sound event to the actor event list, and create an undo object

***************************************************************************/
bool Actor::FSetSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fMotionMatch, long vlm, long sty)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertIn(sty, 0, styLim);

    PActor pactrDup;

    if (!FDup(&pactrDup))
    {
        return fFalse;
    }

    if (!FSetSndCore(ptag, fLoop, fQueue, fMotionMatch, vlm, sty))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup, fTrue))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    ReleasePpo(&pactrDup);
    return fTrue;
}

/***************************************************************************

    Add a sound event to the actor event list
    For user sounds, the chid must also be fetched for the argument *ptag
    and stored with the event, since for user sounds, the cno can change
    (eg on scene import)
    Note: ptag is current because the sound browser has just completed building
    a current tag.

***************************************************************************/
bool Actor::FSetSndCore(PTAG ptag, tribool fLoop, tribool fQueue, tribool fMotionMatch, long vlm, long sty)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertIn(sty, 0, styLim);

    PMovieSoundMSND pmsnd = pvNil;
    Sound aevsnd;
    LegacyLong ccel;
    long iaev;
    Base *paev;
    long sqn;
    long cbVar;

    // Verify sound before including in the event list
    pmsnd = (PMovieSoundMSND)vptagm->PbacoFetch(ptag, MovieSoundMSND::FReadMsnd);
    if (pvNil == pmsnd)
        goto LFail;

    if (pmsnd->Sty() == styMidi)
    {
        // There are no midi actor sounds in the product
        PushErc(ercSocNoActrMidi);
        goto LFail;
    }

    // Verify space up front
    cbVar = kcbVarStep + kcbVarSnd;
    if (!_track.Events()->FEnsureSpace(2, cbVar, fgrpNil))
        return fFalse;

    aevsnd.fLoop = fLoop;
    aevsnd.fQueue = fQueue;
    aevsnd.vlm = (vlm == vlmNil) ? pmsnd->Vlm() : vlm;
    aevsnd.tag = *ptag;
    aevsnd.fNoSound = pmsnd->FNoSound();
    aevsnd.sty = (sty == styNil) ? pmsnd->Sty() : sty;
    sty = aevsnd.sty;
    vlm = aevsnd.vlm;

    if (ptag->sid != ksidUseCrf)
        TrashVar(&aevsnd.chid);
    else
    {
        if (!_pscen->Pmvie()->FChidFromUserSndCno(aevsnd.tag.cno, &aevsnd.chid))
            goto LFail;
    }

    if (!fMotionMatch)
        aevsnd.celn = smmNil; // Not a motion match sound
    else
    {
        if (!_ptmpl->FGetCcelActn(_state.action, &ccel))
            goto LFail;
        aevsnd.celn = _state.cel % ccel;
    }

    if (!_FAddDoAev(aetSnd, kcbVarSnd, &aevsnd))
        goto LFail;

    ReleasePpo(&pmsnd);

    // Remove "no sounds" of this sqn between here and the next
    // sound of this sqn
    sqn = MovieSoundMSND::SqnActr(sty, _arid);
    for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
    {
        Sound aevsndT;
        paev = (Base *)_track.Events()->QvFixedGet(iaev);
        if (aetAdd == paev->aet)
            break;
        if (aetSnd != paev->aet)
            continue;
        _track.Events()->Get(iaev, &aevsndT);
        if (MovieSoundMSND::SqnActr(aevsndT.sty, _arid) != sqn)
            continue;

        // Quit when reach real sound of same sqn
        if (!aevsndT.fNoSound)
            break;
        _RemoveAev(iaev);
        iaev--;
    }

    // Enqueue the motion match sounds in the Msq
    // Allow the edit even if all sounds do not play
    if (!(_pscen->GrfScen() & fscenSounds))
        _FEnqueueSmmInMsq(); // Ignore errors : other sounds involved

    Pscen()->Pmvie()->Pmcc()->SetSndFrame(fTrue);
    return fTrue;

LFail:
    ReleasePpo(&pmsnd);
    return fFalse;
}

/***************************************************************************

    Enqueue the sound at event iaev.
    Enter motion match sounds in the smm gl.
    Enter non-motion match sounds in the Msq

***************************************************************************/
ThreeMMActorSoundServices Actor::_SoundServices()
{
    ThreeMMActorSoundServices services = {};
    services.context = this;
    services.cel_count = [](void *context, ThreeMMI32 action, ThreeMMI32 *out) {
        LegacyLong count = *out;
        bool ok = static_cast<Actor *>(context)->_ptmpl->FGetCcelActn(action, &count);
        *out = count;
        return int(ok);
    };
    services.resolve = [](void *context, ThreeMMTag *value, ThreeMMU32 child) {
        TAG tag;
        std::memcpy(&tag, value, sizeof(tag));
        bool ok = static_cast<Actor *>(context)->_pscen->Pmvie()->FResolveSndTag(&tag, child);
        std::memcpy(value, &tag, sizeof(tag));
        return int(ok);
    };
    services.write_event = [](void *context, ThreeMMI32 index, const ThreeMMActorSound *value) {
        Sound sound;
        std::memcpy(&sound, value, sizeof(sound));
        static_cast<Actor *>(context)->_track.Events()->Put(index, &sound);
    };
    services.resolution_failed = [](void *) { Bug("Actrsnd: Expected to resolve snd tag"); };
    services.fetch = [](void *, const ThreeMMTag *value) -> void * {
        TAG tag;
        std::memcpy(&tag, value, sizeof(tag));
        return vptagm->PbacoFetch(&tag, MovieSoundMSND::FReadMsnd);
    };
    services.priority = [](void *, void *resource, int kind) -> ThreeMMI32 {
        const long tools[] = {toolSounder, toolLooper, toolMatcher};
        return static_cast<PMovieSoundMSND>(resource)->Spr(tools[kind]);
    };
    services.enqueue = [](void *context, void *resource, const ThreeMMActorSound *sound, ThreeMMI32 priority, int low) {
        auto actor = static_cast<Actor *>(context);
        return int(actor->_pscen->Pmvie()->Pmsq()->FEnqueue(static_cast<PMovieSoundMSND>(resource), actor->_arid,
                   sound->loop, sound->queue, sound->volume, priority, fTrue, 0, low != 0));
    };
    services.release = [](void *, void *resource) {
        auto sound = static_cast<PMovieSoundMSND>(resource);
        ReleasePpo(&sound);
    };
    return services;
}
bool Actor::_FEnqueueSnd(long iaev)
{
    AssertThis(0);
    AssertIn(iaev, 0, _track.Events()->IvMac());
    ThreeMMActorEvent event;
    ThreeMMActorSound sound;
    _track.Events()->GetFixed(iaev, &event);
    _track.Events()->Get(iaev, &sound);
    const auto services = _SoundServices();
    return three_mm_actor_sound_event(_motionSounds.Core(), &event, &sound, iaev, &services) != 0;
}
bool Actor::_FEnqueueSmmInMsq(void)
{
    AssertThis(0);
    const auto services = _SoundServices();
    return three_mm_actor_sound_motion(_motionSounds.Core(), _state.action, _state.cel, &services) != 0;
}

/***************************************************************************

    Delete old motion sounds in event list

***************************************************************************/
bool Actor::_FRemoveAevMm(long anid)
{
    AssertThis(0);

    long iaev;
    Base *paev;
    Sound aevsnd;
    Action aevactn;

    // Remove motion match sounds from the previous action
    for (iaev = _state.nextEvent; iaev < _track.Events()->IvMac(); iaev++)
    {
        paev = (Base *)_track.Events()->QvFixedGet(iaev);
        if (aetAdd == paev->aet)
            break;
        if (aetActn == paev->aet)
        {
            _track.Events()->Get(iaev, &aevactn);
            if (aevactn.anid != anid)
                break;
            continue;
        }
        if (aetSnd != paev->aet)
            continue;

        // Test if sound is motion match
        _track.Events()->Get(iaev, &aevsnd);
        if (smmNil == aevsnd.celn)
            continue;
        _RemoveAev(iaev);
        iaev--;
    }

    return fTrue;
}

/***************************************************************************

    Insert default motion match sounds in event list

***************************************************************************/
bool Actor::_FAddAevDefMm(long anid)
{
    AssertThis(0);
    TAG tag;
    PMovieSoundMSND pmsnd;
    long iceln;
    LegacyLong ccel;
    long vlm;
    long sty;
    bool fSoundExists;

    // Insert new motion match sounds
    if (!_ptmpl->FGetCcelActn(anid, &ccel))
        return fFalse;

    for (iceln = 0; iceln < ccel; iceln++)
    {
        if (!_ptmpl->FGetSndActnCel(anid, iceln, &fSoundExists, &tag))
            continue; // Ignore failure
        if (!fSoundExists)
            continue;

        pmsnd = (PMovieSoundMSND)vptagm->PbacoFetch(&tag, MovieSoundMSND::FReadMsnd);
        if (pvNil == pmsnd)
            continue; // Ignore failure

        vlm = pmsnd->Vlm();
        sty = pmsnd->Sty();

        if (!FSetSndCore(&tag, tribool::tNo, (tribool)vlm, tribool::tNo, sty, tribool::tYes))
        {
            goto LFail; // Continuing pointless
        }

        ReleasePpo(&pmsnd);
    }

    return fTrue;

LFail:
    ReleasePpo(&pmsnd);
    return fFalse;
}

/***************************************************************************

    Set the Volume for sounds in the current frame of the specified sty

***************************************************************************/
bool Actor::FSetVlmSnd(long sty, bool fMotionMatch, long vlm)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertIn(vlm, 0, kvlmFull + 1);

    Sound aevsnd;
    bool fActnThisFrame = fFalse;
    long celn;
    LegacyLong ccel;
    long iaev;
    long ismm;
    Base aev;
    SoundMotionMatch smm;
    long nfrmMM = ivNil;

    if (!_ptmpl->FGetCcelActn(_state.action, &ccel))
        return fFalse;

    celn = _state.cel % ccel;

    // Set the volume for any events in the actor list for this frame
    for (iaev = _state.firstEventInFrame; iaev < _state.nextEvent; iaev++)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (aev.aet == aetActn)
            fActnThisFrame = fTrue;

        if (aev.aet != aetSnd)
            continue;

        _track.Events()->Get(iaev, &aevsnd);
        if (FPure(aevsnd.celn != smmNil) != fMotionMatch)
            continue;

        if (aevsnd.sty != sty)
            continue;
        aevsnd.vlm = vlm;
        _track.Events()->Put(iaev, &aevsnd);
    }

    // Set the volume for any earlier motion match sounds
    // which land in this cel
    if (fMotionMatch && !fActnThisFrame)
    {
        for (iaev = _state.firstEventInFrame - 1; iaev > 0; iaev--)
        {
            _track.Events()->GetFixed(iaev, &aev);

            // Quit when the action changes
            if (aev.aet == aetActn || (nfrmMM != ivNil && nfrmMM > aev.nfrm))
                break;

            if (aev.aet != aetSnd)
                continue;
            _track.Events()->Get(iaev, &aevsnd);

            if (aevsnd.sty != sty)
                continue;

            // Only adjust sounds that match this cel
            if (aevsnd.celn != celn)
                continue;
            aevsnd.vlm = vlm;
            _track.Events()->Put(iaev, &aevsnd);
            nfrmMM = aev.nfrm;
            // There are no queued motion match sounds by spec
            break;
        }
    }

    if (fMotionMatch)
    {
        // Adjust the volume in the smm also
        for (ismm = 0; ismm < _motionSounds.IvMac(); ismm++)
        {
            _motionSounds.Get(ismm, &smm);
            Assert(smm.aevsnd.celn != smmNil, "Logic error in smm");

            if (smm.aevsnd.celn != celn)
                continue;

            if (smm.aevsnd.sty != sty)
                continue;

            // Set the volume in the smm
            smm.aevsnd.vlm = vlm;
            _motionSounds.Put(ismm, &smm);
        }
    }

    _pscen->MarkDirty();
    return fTrue;
}

/***************************************************************************

    Query actor for current frame sounds

***************************************************************************/
bool Actor::FQuerySnd(long sty, bool fMotionMatch, PDynamicArray *pglTagSnd, long *pvlm, bool *pfLoop)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertVarMem(pvlm);
    AssertVarMem(pfLoop);

    PDynamicArray pgltag = pvNil;
    Sound aevsnd;
    LegacyLong ccel;
    long celn;
    Base aev;
    long iaev;
    long ismm;
    SoundMotionMatch smm;

    if (pvNil == (pgltag = DynamicArray::PglNew(size(TAG), kctagSndGrow)))
        return fFalse;
    pgltag->SetMinGrow(kctagSndGrow);

    if (!fMotionMatch)
    {
        // Non-motion match sounds
        for (iaev = _state.firstEventInFrame; iaev < _state.nextEvent; iaev++)
        {
            _track.Events()->GetFixed(iaev, &aev);
            if (aev.aet != aetSnd)
                continue;

            _track.Events()->Get(iaev, &aevsnd);
            if (aevsnd.sty != sty)
                continue;

            // Handle motion match sounds later
            if (aevsnd.celn != smmNil)
                continue;
            if (aevsnd.tag.sid == ksidUseCrf)
            {
                if (!_pscen->Pmvie()->FResolveSndTag(&aevsnd.tag, aevsnd.chid))
                    goto LFail;
            }
            if (!pgltag->FAdd(&aevsnd.tag))
                goto LFail;
            *pvlm = aevsnd.vlm;
            *pfLoop = aevsnd.fLoop;
        }
    }

    if (fMotionMatch)
    {
        if (!_ptmpl->FGetCcelActn(_state.action, &ccel))
            goto LFail;

        celn = _state.cel % ccel;

        // Motion match sounds
        for (ismm = 0; ismm < _motionSounds.IvMac(); ismm++)
        {
            _motionSounds.Get(ismm, &smm);
            if (smm.aevsnd.sty != sty)
                continue;
            if (smm.aevsnd.celn != celn)
                continue;
            if (smm.aevsnd.tag.sid == ksidUseCrf)
            {
                if (!_pscen->Pmvie()->FResolveSndTag(&smm.aevsnd.tag, smm.aevsnd.chid))
                    goto LFail;
            }
            if (!pgltag->FAdd(&smm.aevsnd.tag))
                goto LFail;
            *pvlm = smm.aevsnd.vlm;
            *pfLoop = smm.aevsnd.fLoop;
            break;
        }
    }

    if (pgltag->IvMac() == 0)
    {
        ReleasePpo(&pgltag);
        *pglTagSnd = pvNil;
        return fTrue;
    }

    *pglTagSnd = pgltag;
    return fTrue;

LFail:
    ReleasePpo(&pgltag);
    return fFalse;
}

/***************************************************************************

    Delete a sound in this frame

***************************************************************************/
bool Actor::FDeleteSndCore(long sty, bool fMotionMatch)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);

    Sound aevsnd;
    long iaevFirst;
    long iaev;
    long ismm;
    LegacyLong ccel;
    long celn;
    Base aev;
    SoundMotionMatch smm;
    long nfrmMM = ivNil; // For motion match, this may be an earlier frame

    // First the actor event list
    if (!_ptmpl->FGetCcelActn(_state.action, &ccel))
        return fFalse;
    celn = _state.cel % ccel;

    iaevFirst = (fMotionMatch) ? _state.actionEvent : _state.firstEventInFrame;
    for (iaev = _state.nextEvent - 1; iaev >= iaevFirst; iaev--)
    {
        _track.Events()->GetFixed(iaev, &aev);
        if (fMotionMatch && nfrmMM != ivNil && nfrmMM > aev.nfrm)
            break;

        if (aev.aet != aetSnd)
            continue;

        _track.Events()->Get(iaev, &aevsnd);
        if (aevsnd.sty != sty)
            continue;
        if ((aevsnd.celn != smmNil) != fMotionMatch)
            continue;

        // Only delete the sound that match this cel
        if (fMotionMatch && aevsnd.celn != celn)
            continue;

        // Delete this sound & continue to remove any
        // other sounds which are possibly chained
        _RemoveAev(iaev);
        nfrmMM = aev.nfrm;
    }

    if (fMotionMatch)
    {
        // Also the motion match sound list : smm
        if (!_ptmpl->FGetCcelActn(_state.action, &ccel))
            return fFalse;
        celn = _state.cel % ccel;

        // Motion match sounds
        for (ismm = 0; ismm < _motionSounds.IvMac(); ismm++)
        {
            _motionSounds.Get(ismm, &smm);
            if (smm.aevsnd.sty != sty)
                continue;
            if (smm.aevsnd.celn != celn)
                continue;
            _motionSounds.Delete(ismm);
            ismm--;
        }
    }

    _pscen->MarkDirty();
    return fTrue;
}

/******************************************************************************
    FSoundInFrm
        Enumerates all actor events for current frame looking for a sound
        event.

    Returns: fTrue if a sound event was found, fFalse otherwise

************************************************************ PETED ***********/
bool Actor::FSoundInFrm(void)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    /* Can't provide useful info if we're not at the current scene frame */
    if (Pscen()->Nfrm() != _state.frame)
        return fFalse;

    for (long iaev = _state.firstEventInFrame; iaev < _state.nextEvent; iaev++)
    {
        Base aev;

        _track.Events()->GetFixed(iaev, &aev);
        if (aev.aet == aetSnd)
            return fTrue;
    }
    return fFalse;
}

/******************************************************************************
    Resolve all sound tags

******************************************************************************/
bool Actor::FResolveAllSndTags(ChunkNumber cnoScen)
{
    AssertThis(0);
    long iaev;
    Base *paev;
    Sound aevsnd;
    bool fSuccess = fTrue;

    for (iaev = 0; iaev < _track.Events()->IvMac(); iaev++)
    {
        paev = (Base *)_track.Events()->QvFixedGet(iaev);
        if (paev->aet != aetSnd)
            continue;
        _track.Events()->Get(iaev, &aevsnd);
        if (aevsnd.tag.sid == ksidUseCrf)
        {
            if (!_pscen->Pmvie()->FResolveSndTag(&aevsnd.tag, aevsnd.chid, cnoScen))
            {
                fSuccess = fFalse;
                continue;
            }
            _track.Events()->Put(iaev, &aevsnd);
        }
    }
    return fSuccess;
}

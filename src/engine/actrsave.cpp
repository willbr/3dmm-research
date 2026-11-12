/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    actrsave.cpp: Actor load/save code

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    Here's the chunk hierarchy:

    Actor // contains an ActorChunkOnFile (origin, arid, nfrmFirst, tagTmpl...)
     |
     +---PATH (chid 0) // _pglxyz (actor path)
     |
     +---GGAE (chid 0) // _track.Events() (actor events)


***************************************************************************/
#include "soc.h"
ASSERTNAME

using namespace ActorEvent;

const ChildChunkID kchidPath = 0;
const ChildChunkID kchidGgae = 0;

/***************************************************************************
    Write the actor out to disk.  Store the root chunk in the given ChunkNumber.
    If this function returns false, it is the client's responsibility to
    delete the actor chunks.
***************************************************************************/
bool Actor::FWrite(PChunkyFile pcfl, ChunkNumber cnoActr, ChunkNumber cnoScene)
{
    AssertThis(0);
    AssertPo(pcfl, 0);

    ActorChunkOnFile actf;
    ChunkNumber cnoPath;
    ChunkNumber cnoGgae;
    ChunkNumber cnoTmpl;
    DataBlock blck;
    ChildChunkIdentification kid;
    long iaev;
    Base *paev;
    Sound aevsnd;
    long nfrmFirst;
    long nfrmLast;

    // Validate the actor's lifetime if not done already
    if (knfrmInvalid != _nfrmFirst)
    {
        if (!FGetLifetime(&nfrmFirst, &nfrmLast))
            return fFalse;
    }
#ifdef DEBUG
    if (knfrmInvalid == _nfrmFirst)
        Warn("Dev: Why are we saving an actor who has no first frame number?");
#endif // DEBUG

    // Save and adopt Template chunk if it's a ksidUseCrf chunk
    if (_tagTmpl.sid == ksidUseCrf)
    {
        Assert(_ptmpl->FIsTdt(), "only ThreeDTexts should be embedded in user doc");
        if (!pcfl->FFind(_tagTmpl.ctg, _tagTmpl.cno))
        {
            if (!((PThreeDText)_ptmpl)->FWrite(pcfl, _tagTmpl.ctg, &cnoTmpl))
                return fFalse;
            // Keep ChunkNumber the same
            pcfl->Move(_tagTmpl.ctg, cnoTmpl, _tagTmpl.ctg, _tagTmpl.cno);
        }

        if (tNo == pcfl->TIsDescendent(kctgActr, cnoActr, _tagTmpl.ctg, _tagTmpl.cno))
        {
            if (!pcfl->FAdoptChild(kctgActr, cnoActr, _tagTmpl.ctg,
                                   _tagTmpl.cno)) // clears loner bit
            {
                return fFalse;
            }
        }
    }

    // Write the Actor chunk:
    actf.bo = kboCur;
    actf.osk = koskCur;
    actf.dxyzFullRte = _dxyzFullRte;
    actf.arid = _arid;
    actf.nfrmFirst = _nfrmFirst;
    actf.nfrmLast = _nfrmLast;
    actf.tagTmpl.From(_tagTmpl);
    if (!ActorData::FWriteHeader(pcfl, cnoActr, &actf))
        return fFalse;

    // Now write the PATH chunk:
    if (!pcfl->FAddChild(kctgActr, cnoActr, kchidPath, _track.Route()->CbOnFile(), kctgPath, &cnoPath, &blck))
    {
        return fFalse;
    }
    if (!_track.Route()->FWrite(&blck))
        return fFalse;

    // Now write the GGAE chunk. The runtime _track.Events() holds Costume/Sound (with
    // full TAG including runtime pcrf) for aetCost/aetSnd entries; on x64 those
    // are wider than the wire format. Marshal into a transient on-file GG that
    // embeds CostumeOnFile/SoundOnFile (with TAGOnFile) for the wire layout,
    // then FWrite that.
    {
        PGeneralGroup pggaevOnFile = ActorData::PggWriteEvents(_track.Events());
        if (pggaevOnFile == pvNil)
            return fFalse;
        if (!pcfl->FAddChild(kctgActr, cnoActr, kchidGgae, pggaevOnFile->CbOnFile(), kctgGgae, &cnoGgae, &blck))
        {
            ReleasePpo(&pggaevOnFile);
            return fFalse;
        }
        if (!pggaevOnFile->FWrite(&blck))
        {
            ReleasePpo(&pggaevOnFile);
            return fFalse;
        }
        ReleasePpo(&pggaevOnFile);
    }

    // Adopt actor sounds into the scene
    for (iaev = 0; iaev < _track.Events()->IvMac(); iaev++)
    {
        paev = (Base *)(_track.Events()->QvFixedGet(iaev));
        if (aetSnd != paev->aet)
            continue;
        _track.Events()->Get(iaev, &aevsnd);
        if (aevsnd.tag.sid != ksidUseCrf)
            continue;

        // For user sounds, the tag's cno must already be correct.
        // Moreover, FResolveSndTag can't succeed if the msnd chunk is
        // not yet a child of the current scene.

        // If the msnd chunk already exists as this chid of this scene, continue
        if (pcfl->FGetKidChidCtg(kctgScen, cnoScene, aevsnd.chid, kctgMsnd, &kid))
            continue;

        // If the msnd does not exist in this file, it exists in the main movie
        if (!pcfl->FFind(kctgMsnd, aevsnd.tag.cno))
            continue;

        // The msnd chunk has not been adopted into the scene as the specified chid
        if (!pcfl->FAdoptChild(kctgScen, cnoScene, kctgMsnd, aevsnd.tag.cno, aevsnd.chid))
        {
            return fFalse;
        }
    }

    return fTrue;
}

/***************************************************************************
    Read the actor data from disk, (re-)construct the actor, and return a
    pointer to it.
***************************************************************************/
PActor Actor::PactrRead(PChunkyResourceFile pcrf, ChunkNumber cnoActr)
{
    AssertPo(pcrf, 0);

    Actor *pactr;
    ChildChunkIdentification kid;
    PChunkyFile pcfl = pcrf->Pcfl();

    pactr = NewObj Actor;
    if (pvNil == pactr)
        goto LFail;
    if (!pactr->_FReadActor(pcfl, cnoActr))
        goto LFail;
    if (!pcfl->FGetKidChidCtg(kctgActr, cnoActr, kchidPath, kctgPath, &kid))
        goto LFail;
    if (!pactr->_FReadRoute(pcfl, kid.cki.cno))
        goto LFail;
    if (!pcfl->FGetKidChidCtg(kctgActr, cnoActr, kchidGgae, kctgGgae, &kid))
        goto LFail;
    if (!pactr->_FReadEvents(pcfl, kid.cki.cno))
        goto LFail;
    if (!pactr->_FOpenTags(pcrf))
        goto LFail;
    if (!pactr->_motionSounds.FCreate())
        goto LFail;

    // Now that the tags are open, fetch the Template
    pactr->_ptmpl = (PTemplate)vptagm->PbacoFetch(&pactr->_tagTmpl, Template::FReadTmpl);
    if (pvNil == pactr->_ptmpl)
        goto LFail;

    if (knfrmInvalid == pactr->_nfrmLast && knfrmInvalid != pactr->_nfrmFirst)
    {
        long nfrmFirst, nfrmLast;

        if (!pactr->FGetLifetime(&nfrmFirst, &nfrmLast))
            goto LFail;
    }

    AssertPo(pactr, 0);
    return pactr;
LFail:
    Warn("PactrRead failed");
    ReleasePpo(&pactr);
    return pvNil;
}

/***************************************************************************
    Read the ActorChunkOnFile. This handles converting an ActorChunkOnFile that doesn't have an
    nfrmLast.
***************************************************************************/
/***************************************************************************
    Read the Actor chunk
***************************************************************************/
bool Actor::_FReadActor(PChunkyFile pcfl, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    ActorChunkOnFile actf;
    DataBlock blck;

    if (!pcfl->FFind(kctgActr, cno, &blck) || !ActorData::FReadHeader(&blck, &actf))
        return fFalse;

    Assert(kboCur == actf.bo, "bad ActorChunkOnFile");
    _dxyzFullRte = actf.dxyzFullRte;
    _arid = actf.arid;
    _nfrmFirst = actf.nfrmFirst;
    _nfrmLast = actf.nfrmLast;
    TagFromOnFile(&_tagTmpl, actf.tagTmpl);
    _fLifeDirty = (knfrmInvalid == _nfrmFirst) || (knfrmInvalid == _nfrmLast);

    if (_tagTmpl.sid == ksidUseCrf)
    {
        // Actor is a ThreeDText.  Tag might be wrong if this actor was imported,
        // so look for child Template.
        ChildChunkIdentification kid;

        if (!pcfl->FGetKidChidCtg(kctgActr, cno, 0, kctgTmpl, &kid))
        {
            Bug("where's the child Template?");
            return fTrue; // hope the tag is correct
        }
        _tagTmpl.cno = kid.cki.cno;
    }

    return fTrue;
}

/******************************************************************************
    FAdjustAridOnFile
        Given a chunky file, a ChunkNumber and a delta for the arid, updates the
        arid for the actor on file.

    Arguments:
        PChunkyFile pcfl   -- the file the actor's on
        ChunkNumber cno     -- the ChunkNumber of the actor
        long darid  -- the change of the arid

    Returns: fTrue if everything went well, fFalse otherwise

************************************************************ PETED ***********/
bool Actor::FAdjustAridOnFile(PChunkyFile pcfl, ChunkNumber cno, long darid)
{
    return ActorData::FAdjustId(pcfl, cno, darid);
}

/***************************************************************************
    Read the PATH (_track.Route()) chunk
***************************************************************************/
bool Actor::_FReadRoute(PChunkyFile pcfl, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    DataBlock block;
    if (!pcfl->FFind(kctgPath, cno, &block))
        return fFalse;
    _track.AdoptRoute(ActorData::PglReadRoute(&block));
    return _track.Route() != pvNil;
}

/***************************************************************************
    Read the GGAE (_track.Events()) chunk.

    The wire-format GG embeds CostumeOnFile/SoundOnFile (with TAGOnFile) for
    aetCost/aetSnd entries; the runtime GG holds Costume/Sound (with full TAG
    including the runtime pcrf). On x86 the two layouts coincide; on x64 the
    runtime variant is wider. We marshal entry-by-entry: read into a transient
    on-file GG, walk it into a fresh runtime GG, then drop the on-file copy.
***************************************************************************/
bool Actor::_FReadEvents(PChunkyFile pcfl, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    DataBlock block;
    if (!pcfl->FFind(kctgGgae, cno, &block))
        return fFalse;
    _track.AdoptEvents(ActorData::PggReadEvents(&block));
    return _track.Events() != pvNil;
}

/***************************************************************************
    Marshal a wire-format pggaev into a fresh runtime pggaev. See header
    comment in actor.h for ownership rules.
***************************************************************************/
/***************************************************************************
    SwapBytes all events in pggaev
***************************************************************************/
/***************************************************************************
    Open all tags for this actor
***************************************************************************/
bool Actor::_FOpenTags(PChunkyResourceFile pcrf)
{
    AssertBaseThis(0);
    AssertPo(pcrf, 0);

    long iaev = 0;
    PTAG ptag;

    if (!TagManager::FOpenTag(&_tagTmpl, pcrf))
        return fFalse;

    _track.Events()->Lock();
    for (iaev = 0; iaev < _track.Events()->IvMac(); iaev++)
    {
        if (_FIsIaevTag(_track.Events(), iaev, &ptag))
        {
            if (!TagManager::FOpenTag(ptag, pcrf))
                goto LFail;
        }
    }
    _track.Events()->Unlock();
    return fTrue;
LFail:
    // Close the tags that were opened before failure
    while (--iaev >= 0)
    {
        if (_FIsIaevTag(_track.Events(), iaev, &ptag))
            TagManager::CloseTag(ptag);
    }
    _track.Events()->Unlock();
    return fFalse;
}

/***************************************************************************
    Close all tags in this actor's event stream
***************************************************************************/
void Actor::_CloseTags(void)
{
    AssertBaseThis(0); // because destructor calls this function

    TagManager::CloseTag(&_tagTmpl);
    _track.CloseTags();
}

/***************************************************************************
    Get all the tags that the actor uses
***************************************************************************/
PDynamicArray Actor::PgltagFetch(PChunkyFile pcfl, ChunkNumber cno, bool *pfError)
{
    AssertPo(pcfl, 0);
    AssertVarMem(pfError);

    ActorChunkOnFile actf;
    DataBlock blck;
    short bo;
    PTAG ptag;
    PDynamicArray pgltag;
    PGeneralGroup pggaev = pvNil;
    long iaev;
    ChildChunkIdentification kid;

    pgltag = DynamicArray::PglNew(size(TAG), 0);
    if (pvNil == pgltag)
        goto LFail;

    // Read the ActorChunkOnFile so we can insert tagTmpl:
    if (!pcfl->FFind(kctgActr, cno, &blck) || !ActorData::FReadHeader(&blck, &actf))
        goto LFail;

    if (actf.tagTmpl.sid == ksidUseCrf)
    {
        PDynamicArray pgltagTmpl;

        // Actor is a ThreeDText.  Tag might be wrong if this actor was imported,
        // so look for child Template.
        if (pcfl->FGetKidChidCtg(kctgActr, cno, 0, kctgTmpl, &kid))
        {
            actf.tagTmpl.cno = kid.cki.cno;
        }
        else
        {
            Bug("where's the child Template?");
        }

        pgltagTmpl = Template::PgltagFetch(pcfl, actf.tagTmpl.ctg, actf.tagTmpl.cno, pfError);
        if (*pfError)
        {
            ReleasePpo(&pgltagTmpl);
            goto LFail;
        }
        if (pvNil != pgltagTmpl)
        {
            long itag;
            TAG tag;

            for (itag = 0; itag < pgltagTmpl->IvMac(); itag++)
            {
                pgltagTmpl->Get(itag, &tag);
                if (!pgltag->FAdd(&tag))
                {
                    ReleasePpo(&pgltagTmpl);
                    goto LFail;
                }
            }
            ReleasePpo(&pgltagTmpl);
        }
    }

    {
        TAG tagTmpl;
        TagFromOnFile(&tagTmpl, actf.tagTmpl);
        if (!pgltag->FInsert(0, &tagTmpl))
            goto LFail;
    }

    // Pull all tags out of the event list:
    if (!pcfl->FGetKidChidCtg(kctgActr, cno, kchidGgae, kctgGgae, &kid))
        goto LFail;
    if (!pcfl->FFind(kctgGgae, kid.cki.cno, &blck))
        goto LFail;
    pggaev = ActorData::PggReadEvents(&blck);
    if (pvNil == pggaev)
        goto LFail;
    pggaev->Lock();
    for (iaev = 0; iaev < pggaev->IvMac(); iaev++)
    {
        if (_FIsIaevTag(pggaev, iaev, &ptag))
        {
            if (!pgltag->FAdd(ptag))
            {
                pggaev->Unlock();
                goto LFail;
            }
        }
    }
    pggaev->Unlock();
    *pfError = fFalse;
    ReleasePpo(&pggaev);
    return pgltag;
LFail:
    *pfError = fTrue;
    ReleasePpo(&pgltag);
    ReleasePpo(&pggaev);
    return pvNil;
}

/***************************************************************************
    If the iaev'th event of pggaev has a tag, sets *pptag to point to it.
    WARNING: unless you locked pggaev, *pptag is a qtag!
***************************************************************************/
bool Actor::_FIsIaevTag(PGeneralGroup pggaev, long iaev, PTAG *pptag, PBase *pqaev)
{
    return ActorTrack::FEventTag(pggaev, iaev, pptag, pqaev);
}

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    actrsave.cpp: Actor load/save code

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    Here's the chunk hierarchy:

    Actor // contains an ACTF (origin, arid, nfrmFirst, tagTmpl...)
     |
     +---PATH (chid 0) // _pglxyz (actor path)
     |
     +---GGAE (chid 0) // _pggaev (actor events)


***************************************************************************/
#include "soc.h"
ASSERTNAME

using namespace ActorEvent;

const ChildChunkID kchidPath = 0;
const ChildChunkID kchidGgae = 0;

struct ACTF // Actor chunk on file
{
    short bo;        // Byte order
    short osk;       // OS kind
    RoutePoint dxyzFullRte; // Translation of the route
    long arid;       // Unique id assigned to this actor.
    long nfrmFirst;  // First frame in this actor's stage life
    long nfrmLast;   // Last frame in this actor's stage life
    TAG tagTmpl;     // Tag to actor's template
};
const ByteOrderMask kbomActf = 0x5ffc0000 | kbomTag;

/***************************************************************************
    Write the actor out to disk.  Store the root chunk in the given ChunkNumber.
    If this function returns false, it is the client's responsibility to
    delete the actor chunks.
***************************************************************************/
bool Actor::FWrite(PChunkyFile pcfl, ChunkNumber cnoActr, ChunkNumber cnoScene)
{
    AssertThis(0);
    AssertPo(pcfl, 0);

    ACTF actf;
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

    // Save and adopt TMPL chunk if it's a ksidUseCrf chunk
    if (_tagTmpl.sid == ksidUseCrf)
    {
        Assert(_ptmpl->FIsTdt(), "only TDTs should be embedded in user doc");
        if (!pcfl->FFind(_tagTmpl.ctg, _tagTmpl.cno))
        {
            if (!((PTDT)_ptmpl)->FWrite(pcfl, _tagTmpl.ctg, &cnoTmpl))
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
    actf.tagTmpl = _tagTmpl;
    if (!pcfl->FPutPv(&actf, size(ACTF), kctgActr, cnoActr))
        return fFalse;

    // Now write the PATH chunk:
    if (!pcfl->FAddChild(kctgActr, cnoActr, kchidPath, _pglrpt->CbOnFile(), kctgPath, &cnoPath, &blck))
    {
        return fFalse;
    }
    if (!_pglrpt->FWrite(&blck))
        return fFalse;

    // Now write the GGAE chunk:
    if (!pcfl->FAddChild(kctgActr, cnoActr, kchidGgae, _pggaev->CbOnFile(), kctgGgae, &cnoGgae, &blck))
    {
        return fFalse;
    }
    if (!_pggaev->FWrite(&blck))
        return fFalse;

    // Adopt actor sounds into the scene
    for (iaev = 0; iaev < _pggaev->IvMac(); iaev++)
    {
        paev = (Base *)(_pggaev->QvFixedGet(iaev));
        if (aetSnd != paev->aet)
            continue;
        _pggaev->Get(iaev, &aevsnd);
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
    if (pvNil == (pactr->_pglsmm = DynamicArray::PglNew(size(SMM), kcsmmGrow)))
        goto LFail;
    pactr->_pglsmm->SetMinGrow(kcsmmGrow);

    // Now that the tags are open, fetch the TMPL
    pactr->_ptmpl = (PTMPL)vptagm->PbacoFetch(&pactr->_tagTmpl, TMPL::FReadTmpl);
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
    Read the ACTF. This handles converting an ACTF that doesn't have an
    nfrmLast.
***************************************************************************/
bool _FReadActf(PDataBlock pblck, ACTF *pactf)
{
    AssertPo(pblck, 0);
    AssertVarMem(pactf);
    bool fOldActf = fFalse;

    if (!pblck->FUnpackData())
        return fFalse;

    if (pblck->Cb() != size(ACTF))
    {
        if (pblck->Cb() != size(ACTF) - size(long))
            return fFalse;
        fOldActf = fTrue;
    }

    if (!pblck->FReadRgb(pactf, pblck->Cb(), 0))
        return fFalse;

    if (fOldActf)
    {
        BltPb(&pactf->nfrmLast, &pactf->nfrmLast + 1, size(ACTF) - offset(ACTF, nfrmLast) - size(long));
    }

    if (kboOther == pactf->bo)
        SwapBytesBom(pactf, kbomActf);
    if (kboCur != pactf->bo)
    {
        Bug("Corrupt ACTF");
        return fFalse;
    }

    if (fOldActf)
        pactf->nfrmLast = knfrmInvalid;
    return fTrue;
}

/***************************************************************************
    Read the Actor chunk
***************************************************************************/
bool Actor::_FReadActor(PChunkyFile pcfl, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    ACTF actf;
    DataBlock blck;

    if (!pcfl->FFind(kctgActr, cno, &blck) || !_FReadActf(&blck, &actf))
        return fFalse;

    Assert(kboCur == actf.bo, "bad ACTF");
    _dxyzFullRte = actf.dxyzFullRte;
    _arid = actf.arid;
    _nfrmFirst = actf.nfrmFirst;
    _nfrmLast = actf.nfrmLast;
    _tagTmpl = actf.tagTmpl;
    _fLifeDirty = (knfrmInvalid == _nfrmFirst) || (knfrmInvalid == _nfrmLast);

    if (_tagTmpl.sid == ksidUseCrf)
    {
        // Actor is a TDT.  Tag might be wrong if this actor was imported,
        // so look for child TMPL.
        ChildChunkIdentification kid;

        if (!pcfl->FGetKidChidCtg(kctgActr, cno, 0, kctgTmpl, &kid))
        {
            Bug("where's the child TMPL?");
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
    AssertPo(pcfl, 0);
    Assert(darid != 0, "Why call this with darid == 0?");

    ACTF actf;
    DataBlock blck;

    if (!pcfl->FFind(kctgActr, cno, &blck) || !_FReadActf(&blck, &actf))
        return fFalse;

    Assert(kboCur == actf.bo, "bad ACTF");
    actf.arid += darid;
    return pcfl->FPutPv(&actf, size(ACTF), kctgActr, cno);
}

/***************************************************************************
    Read the PATH (_pglrpt) chunk
***************************************************************************/
bool Actor::_FReadRoute(PChunkyFile pcfl, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    DataBlock blck;
    short bo;

    if (!pcfl->FFind(kctgPath, cno, &blck))
        return fFalse;
    _pglrpt = DynamicArray::PglRead(&blck, &bo);
    if (pvNil == _pglrpt)
        return fFalse;
    AssertBomRglw(kbomRpt, size(RouteDistancePoint));
    if (kboOther == bo)
    {
        SwapBytesRglw(_pglrpt->QvGet(0), LwMul(_pglrpt->IvMac(), size(RouteDistancePoint) / size(long)));
    }
    return fTrue;
}

/***************************************************************************
    Read the GGAE (_pggaev) chunk
***************************************************************************/
bool Actor::_FReadEvents(PChunkyFile pcfl, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    DataBlock blck;
    short bo;

    if (!pcfl->FFind(kctgGgae, cno, &blck))
        return fFalse;
    _pggaev = GeneralGroup::PggRead(&blck, &bo);
    if (pvNil == _pggaev)
        return fFalse;
    if (kboOther == bo)
        _SwapBytesPggaev(_pggaev);
    return fTrue;
}

/***************************************************************************
    SwapBytes all events in pggaev
***************************************************************************/
void Actor::_SwapBytesPggaev(PGeneralGroup pggaev)
{
    AssertPo(pggaev, 0);

    long iaev;

    for (iaev = 0; iaev < pggaev->IvMac(); iaev++)
    {
        SwapBytesBom(pggaev->QvFixedGet(iaev), kbomAev);
        switch (((Base *)pggaev->QvFixedGet(iaev))->aet)
        {
        case aetCost:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevcost);
            break;
        case aetSnd:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevsnd);
            break;
        case aetSize:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevsize);
            break;
        case aetPull:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevpull);
            break;
        case aetRotF:
        case aetRotH:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevrot);
            break;
        case aetActn:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevactn);
            break;
        case aetAdd:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevadd);
            break;
        case aetFreeze:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevfreeze);
            break;
        case aetMove:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevmove);
            break;
        case aetTweak:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevtweak);
            break;
        case aetStep:
            SwapBytesBom(pggaev->QvGet(iaev), kbomAevstep);
            break;
        case aetRem:
            // no var data
            break;
        default:
            Bug("Unknown AET");
            break;
        }
    }
}

/***************************************************************************
    Open all tags for this actor
***************************************************************************/
bool Actor::_FOpenTags(PChunkyResourceFile pcrf)
{
    AssertBaseThis(0);
    AssertPo(pcrf, 0);

    long iaev = 0;
    PTAG ptag;

    if (!TAGM::FOpenTag(&_tagTmpl, pcrf))
        goto LFail;

    _pggaev->Lock();
    for (iaev = 0; iaev < _pggaev->IvMac(); iaev++)
    {
        if (_FIsIaevTag(_pggaev, iaev, &ptag))
        {
            if (!TAGM::FOpenTag(ptag, pcrf))
                goto LFail;
        }
    }
    _pggaev->Unlock();
    return fTrue;
LFail:
    // Close the tags that were opened before failure
    while (--iaev >= 0)
    {
        if (_FIsIaevTag(_pggaev, iaev, &ptag))
            TAGM::CloseTag(ptag);
    }
    _pggaev->Unlock();
    return fFalse;
}

/***************************************************************************
    Close all tags in this actor's event stream
***************************************************************************/
void Actor::_CloseTags(void)
{
    AssertBaseThis(0); // because destructor calls this function

    long iaev;
    PTAG ptag;

    TAGM::CloseTag(&_tagTmpl);

    if (pvNil == _pggaev)
        return;

    _pggaev->Lock();
    for (iaev = 0; iaev < _pggaev->IvMac(); iaev++)
    {
        if (_FIsIaevTag(_pggaev, iaev, &ptag))
            TAGM::CloseTag(ptag);
    }
    _pggaev->Unlock();
    return;
}

/***************************************************************************
    Get all the tags that the actor uses
***************************************************************************/
PDynamicArray Actor::PgltagFetch(PChunkyFile pcfl, ChunkNumber cno, bool *pfError)
{
    AssertPo(pcfl, 0);
    AssertVarMem(pfError);

    ACTF actf;
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

    // Read the ACTF so we can insert tagTmpl:
    if (!pcfl->FFind(kctgActr, cno, &blck) || !_FReadActf(&blck, &actf))
        goto LFail;

    if (actf.tagTmpl.sid == ksidUseCrf)
    {
        PDynamicArray pgltagTmpl;

        // Actor is a TDT.  Tag might be wrong if this actor was imported,
        // so look for child TMPL.
        if (pcfl->FGetKidChidCtg(kctgActr, cno, 0, kctgTmpl, &kid))
        {
            actf.tagTmpl.cno = kid.cki.cno;
        }
        else
        {
            Bug("where's the child TMPL?");
        }

        pgltagTmpl = TMPL::PgltagFetch(pcfl, actf.tagTmpl.ctg, actf.tagTmpl.cno, pfError);
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

    if (!pgltag->FInsert(0, &actf.tagTmpl))
        goto LFail;

    // Pull all tags out of the event list:
    if (!pcfl->FGetKidChidCtg(kctgActr, cno, kchidGgae, kctgGgae, &kid))
        goto LFail;
    if (!pcfl->FFind(kctgGgae, kid.cki.cno, &blck))
        goto LFail;
    pggaev = GeneralGroup::PggRead(&blck, &bo);
    if (pvNil == pggaev)
        goto LFail;
    if (kboOther == bo)
        _SwapBytesPggaev(pggaev);
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
    AssertPo(pggaev, 0);
    AssertIn(iaev, 0, pggaev->IvMac());
    AssertVarMem(pptag);
    AssertNilOrVarMem(pqaev);

    Base *qaev;
    qaev = (Base *)pggaev->QvFixedGet(iaev);
    if (pqaev != pvNil)
        *pqaev = qaev;

    switch (qaev->aet)
    {
    case aetCost:
        if (!((Costume *)pggaev->QvGet(iaev))->fCmtl)
        {
            *pptag = &((Costume *)pggaev->QvGet(iaev))->tag;
            return fTrue;
        }
        break;
    case aetSnd:
        *pptag = &((Sound *)pggaev->QvGet(iaev))->tag;
        return fTrue;
    case aetSize:
    case aetPull:
    case aetRotF:
    case aetRotH:
    case aetActn:
    case aetAdd:
    case aetFreeze:
    case aetTweak:
    case aetStep:
    case aetRem:
    case aetMove:
        break;
    default:
        Bug("Unknown AET");
        break;
    }
    *pptag = pvNil;
    return fFalse;
}

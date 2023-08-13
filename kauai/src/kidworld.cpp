/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    This is a class that knows how to create GOKs, Help Balloons and
    Kidspace script interpreters. It exists so an app can customize
    default behavior.

***************************************************************************/
#include "kidframe.h"
ASSERTNAME

using GraphicalObjectRepresentation::PKidspaceGraphicObject;

RTCLASS(KidspaceGraphicObjectDescriptor)
RTCLASS(KidspaceGraphicObjectDescriptorLocation)
RTCLASS(WorldOfKidspace)

/***************************************************************************
    Static method to read a KidspaceGraphicObjectDescriptorLocation from the ChunkyResourceFile. This is a ChunkyResourceFile object reader.
***************************************************************************/
bool KidspaceGraphicObjectDescriptorLocation::FReadGkds(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb)
{
    PKidspaceGraphicObjectDescriptorLocation pgkds;
    GOKDF gokdf;
    HQ hq;
    LOP *qlop;
    long cb;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;

    if (!pblck->FUnpackData())
        return fFalse;
    *pcb = pblck->Cb();

    if (*pcb < size(GOKDF) + size(LOP) || CbRoundToLong(*pcb) != *pcb)
    {
        Bug("Bad KidspaceGraphicObjectDescriptor");
        return fFalse;
    }

    if (!pblck->FReadRgb(&gokdf, size(GOKDF), 0))
        return fFalse;
    if (gokdf.bo == kboOther)
        SwapBytesBom(&gokdf, kbomGokdf);
    else if (gokdf.bo != kboCur)
    {
        Bug("Bad KidspaceGraphicObjectDescriptor 2");
        return fFalse;
    }

    if (!pblck->FMoveMin(size(gokdf)))
        return fFalse;
    hq = pblck->HqFree();
    if (hqNil == hq)
        return fFalse;
    cb = CbOfHq(hq);

    if (pvNil == (pgkds = NewObj KidspaceGraphicObjectDescriptorLocation))
    {
        FreePhq(&hq);
        return fFalse;
    }
    pgkds->_hqData = hq;
    pgkds->_gokk = gokdf.gokk;
    if (gokdf.bo == kboOther)
        SwapBytesRglw(QvFromHq(hq), cb / size(long));

    qlop = (LOP *)QvFromHq(hq);
    for (pgkds->_clop = 0;; qlop++)
    {
        if (cb < size(LOP))
        {
            Bug("Bad LOP list in KidspaceGraphicObjectDescriptor");
            ReleasePpo(&pgkds);
            return fFalse;
        }
        pgkds->_clop++;
        cb -= size(LOP);
        if (hidNil == qlop->hidPar)
            break;
    }
    if ((cb % size(CursorMapEntry)) != 0)
    {
        Bug("Bad CursorMapEntry list in KidspaceGraphicObjectDescriptor");
        ReleasePpo(&pgkds);
        return fFalse;
    }
    pgkds->_ccume = cb / size(CursorMapEntry);
    *ppbaco = pgkds;
    return fTrue;
}

/***************************************************************************
    Destructor for a KidspaceGraphicObjectDescriptorLocation object.
***************************************************************************/
KidspaceGraphicObjectDescriptorLocation::~KidspaceGraphicObjectDescriptorLocation(void)
{
    FreePhq(&_hqData);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a KidspaceGraphicObjectDescriptorLocation.
***************************************************************************/
void KidspaceGraphicObjectDescriptorLocation::AssertValid(ulong grf)
{
    LOP *qrglop;

    KidspaceGraphicObjectDescriptorLocation_PAR::AssertValid(0);
    AssertHq(_hqData);
    AssertIn(_clop, 0, kcbMax);
    AssertIn(_ccume, 0, kcbMax);
    Assert(LwMul(_clop, size(LOP)) + LwMul(_ccume, size(CursorMapEntry)) == CbOfHq(_hqData), "KidspaceGraphicObjectDescriptorLocation _hqData wrong size");
    qrglop = (LOP *)QvFromHq(_hqData);
    Assert(qrglop[_clop - 1].hidPar == hidNil, "bad rglop in KidspaceGraphicObjectDescriptorLocation");
}

/***************************************************************************
    Mark memory for the KidspaceGraphicObjectDescriptorLocation.
***************************************************************************/
void KidspaceGraphicObjectDescriptorLocation::MarkMem(void)
{
    AssertValid(0);
    KidspaceGraphicObjectDescriptorLocation_PAR::MarkMem();
    MarkHq(_hqData);
}
#endif // DEBUG

/***************************************************************************
    Return the KidspaceGraphicObject kind id.
***************************************************************************/
long KidspaceGraphicObjectDescriptorLocation::Gokk(void)
{
    AssertThis(0);
    return _gokk;
}

/***************************************************************************
    Look for a cursor map entry in this KidspaceGraphicObjectDescriptorLocation.
***************************************************************************/
bool KidspaceGraphicObjectDescriptorLocation::FGetCume(ulong grfcust, long sno, CursorMapEntry *pcume)
{
    AssertThis(0);
    AssertVarMem(pcume);
    CursorMapEntry *qcume;
    long ccume;
    ulong fbitSno = (1L << (sno & 0x1F));

    if (0 == _ccume)
        return fFalse;

    qcume = (CursorMapEntry *)PvAddBv(QvFromHq(_hqData), LwMul(_clop, size(LOP)));
    for (ccume = _ccume; ccume > 0; ccume--, qcume++)
    {
        if ((qcume->grfbitSno & fbitSno) && (qcume->grfcustMask & grfcust) == qcume->grfcust)
        {
            *pcume = *qcume;
            return fTrue;
        }
    }
    TrashVar(pcume);
    return fFalse;
}

/***************************************************************************
    Get the location map entry from the parent id.
***************************************************************************/
void KidspaceGraphicObjectDescriptorLocation::GetLop(long hidPar, LOP *plop)
{
    AssertThis(0);
    AssertVarMem(plop);
    LOP *qlop;

    for (qlop = (LOP *)QvFromHq(_hqData);; qlop++)
    {
        if (hidNil == qlop->hidPar || hidPar == qlop->hidPar)
            break;
    }
    *plop = *qlop;
}

/***************************************************************************
    Constructor for a World of Kidspace GraphicsObject.
***************************************************************************/
WorldOfKidspace::WorldOfKidspace(GraphicsObjectBlock *pgcb, PStringRegistry pstrg)
    : WorldOfKidspace_PAR(pgcb), _clokAnim(CMH::HidUnique()), _clokNoSlip(CMH::HidUnique(), fclokNoSlip),
      _clokGen(CMH::HidUnique()), _clokReset(CMH::HidUnique(), fclokReset)
{
    AssertThis(0);
    AssertNilOrPo(pstrg, 0);

    if (pvNil == pstrg)
        pstrg = &_strg;
    _pstrg = pstrg;
    _pstrg->AddRef();

    _clokAnim.Start(0);
    _clokNoSlip.Start(0);
    _clokGen.Start(0);
    _clokReset.Start(0);
}

/***************************************************************************
    Destructor for a kidspace world.
***************************************************************************/
WorldOfKidspace::~WorldOfKidspace(void)
{
    ReleasePpo(&_pstrg);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a WorldOfKidspace.
***************************************************************************/
void WorldOfKidspace::AssertValid(ulong grf)
{
    WorldOfKidspace_PAR::AssertValid(0);
    AssertPo(&_strg, 0);
}

/***************************************************************************
    Mark memory for the WorldOfKidspace.
***************************************************************************/
void WorldOfKidspace::MarkMem(void)
{
    AssertValid(0);
    WorldOfKidspace_PAR::MarkMem();
    MarkMemObj(&_strg);
}
#endif // DEBUG

/***************************************************************************
    Return whether the GraphicsObject is in this kidspace world.
***************************************************************************/
bool WorldOfKidspace::FGobIn(PGraphicsObject pgob)
{
    AssertThis(0);
    AssertPo(pgob, 0);

    for (; pgob != this; pgob = pgob->PgobPar())
    {
        if (pgob == pvNil)
            return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    Get a KidspaceGraphicObjectDescriptor from the given chunk.
***************************************************************************/
PKidspaceGraphicObjectDescriptor WorldOfKidspace::PgokdFetch(ChunkTag ctg, ChunkNumber cno, PRCA prca)
{
    AssertThis(0);
    AssertPo(prca, 0);

    return (PKidspaceGraphicObjectDescriptor)prca->PbacoFetch(ctg, cno, KidspaceGraphicObjectDescriptorLocation::FReadGkds);
}

/***************************************************************************
    Create a new gob in this kidspace world.
***************************************************************************/
PKidspaceGraphicObject WorldOfKidspace::PgokNew(PGraphicsObject pgobPar, long hid, ChunkNumber cnoGokd, PRCA prca)
{
    AssertThis(0);
    AssertNilOrPo(pgobPar, 0);
    PKidspaceGraphicObjectDescriptor pgokd;
    PKidspaceGraphicObject pgok;

    if (pgobPar == pvNil)
        pgobPar = this;
    else if (!FGobIn(pgobPar))
    {
        // parent isn't in this kidspace world
        Bug("Parent is not in this kidspace world");
        return pvNil;
    }

    if (hidNil == hid)
        hid = CMH::HidUnique();
    else if (pvNil != PcmhFromHid(hid))
    {
        BugVar("command handler with this ID already exists", &hid);
        return pvNil;
    }

    if (pvNil == (pgokd = PgokdFetch(kctgGokd, cnoGokd, prca)))
        return pvNil;

    pgok = KidspaceGraphicObject::PgokNew(this, pgobPar, hid, pgokd, prca);
    ReleasePpo(&pgokd);

    return pgok;
}

/***************************************************************************
    Create a new script interpreter for this kidspace world.
***************************************************************************/
PGraphicsObjectInterpreter WorldOfKidspace::PscegNew(PRCA prca, PGraphicsObject pgob)
{
    AssertThis(0);
    AssertPo(prca, 0);
    AssertPo(pgob, 0);

    return NewObj GraphicsObjectInterpreter(this, prca, pgob);
}

/***************************************************************************
    Create a new help balloon.
***************************************************************************/
PBalloon WorldOfKidspace::PhbalNew(PGraphicsObject pgobPar, PRCA prca, ChunkNumber cnoTopic, PTopic phtop)
{
    AssertThis(0);
    AssertNilOrPo(pgobPar, 0);
    AssertPo(prca, 0);
    AssertNilOrVarMem(phtop);

    if (pgobPar == pvNil)
        pgobPar = this;
    else if (!FGobIn(pgobPar))
    {
        // parent isn't in this kidspace world
        Bug("Parent is not in this kidspace world");
        return pvNil;
    }

    return Balloon::PhbalCreate(this, pgobPar, prca, cnoTopic, phtop);
}

/***************************************************************************
    Get the command handler for this hid.
***************************************************************************/
PCMH WorldOfKidspace::PcmhFromHid(long hid)
{
    AssertThis(0);
    PCMH pcmh;

    switch (hid)
    {
    case hidNil:
        return pvNil;

    case khidApp:
        return vpappb;
    }

    if (pvNil != (pcmh = PclokFromHid(hid)))
        return pcmh;
    return PgobFromHid(hid);
}

/***************************************************************************
    Get the clock having the given hid.
***************************************************************************/
PCLOK WorldOfKidspace::PclokFromHid(long hid)
{
    AssertThis(0);

    if (khidClokGokGen == hid)
        return &_clokGen;
    if (khidClokGokReset == hid)
        return &_clokReset;

    return CLOK::PclokFromHid(hid);
}

/***************************************************************************
    Get the parent gob of the given gob. This is here so a kidspace world
    can limit what scripts can get to.
***************************************************************************/
PGraphicsObject WorldOfKidspace::PgobParGob(PGraphicsObject pgob)
{
    AssertThis(0);
    AssertPo(pgob, 0);

    pgob = pgob->PgobPar();
    if (pvNil == pgob || !FGobIn(pgob))
        return pvNil;
    return pgob;
}

/***************************************************************************
    Find a file given a string.
***************************************************************************/
bool WorldOfKidspace::FFindFile(PSTN pstnSrc, PFilename pfni)
{
    AssertThis(0);
    AssertPo(pstnSrc, 0);
    AssertPo(pfni, 0);

    return pfni->FBuildFromPath(pstnSrc);
}

/***************************************************************************
    Put up an alert (and don't return until it is dismissed).
***************************************************************************/
tribool WorldOfKidspace::TGiveAlert(PSTN pstn, long bk, long cok)
{
    AssertThis(0);

    return vpappb->TGiveAlertSz(pstn->Psz(), bk, cok);
}

/***************************************************************************
    Put up an alert (and don't return until it is dismissed).
***************************************************************************/
void WorldOfKidspace::Print(PSTN pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    // REVIEW shonk: implement WorldOfKidspace::Print better
#ifdef WIN
    OutputDebugString(pstn->Psz());
    OutputDebugString(PszLit("\n"));
#endif // WIN
}

/***************************************************************************
    Return the current cursor state. This takes the frame cursor state from
    vpappb and the rest from this kidspace world.
***************************************************************************/
ulong WorldOfKidspace::GrfcustCur(bool fAsynch)
{
    AssertThis(0);

    return GrfcustAdjust(vpappb->GrfcustCur(fAsynch));
}

/***************************************************************************
    Modify the current cursor state. This sets the frame values in vpappb
    and the rest in this kidspace world.
***************************************************************************/
void WorldOfKidspace::ModifyGrfcust(ulong grfcustOr, ulong grfcustXor)
{
    AssertThis(0);

    // write all the bits to the app. Also adjust our internal _grfcust
    vpappb->ModifyGrfcust(grfcustOr, grfcustXor);

    _grfcust |= grfcustOr;
    _grfcust ^= grfcustXor;
}

/***************************************************************************
    Adjust the given grfcust (take the Frame bits from it and combine with
    our other bits).
***************************************************************************/
ulong WorldOfKidspace::GrfcustAdjust(ulong grfcust)
{
    AssertThis(0);

    grfcust &= kgrfcustFrame;
    grfcust |= _grfcust & (kgrfcustKid | kgrfcustApp);
    return grfcust;
}

/***************************************************************************
    Do a modal help topic.
***************************************************************************/
bool WorldOfKidspace::FModalTopic(PRCA prca, ChunkNumber cnoTopic, long *plwRet)
{
    AssertThis(0);
    AssertPo(prca, 0);
    AssertVarMem(plwRet);

    GraphicsObjectBlock gcb;
    PWorldOfKidspace pwoksModal;
    GTE gte;
    PGraphicsObject pgob;
    ulong grfgte;
    bool fRet = fFalse;

    gte.Init(this, fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (!(grfgte & fgtePre) || !pgob->FIs(kclsKidspaceGraphicObject))
            continue;

        ((PKidspaceGraphicObject)pgob)->Suspend();
    }

    if (vpappb->FPushModal())
    {
        gcb.Set(CMH::HidUnique(), this, fgobNil, kginMark);
        gcb._rcRel.Set(0, 0, krelOne, krelOne);

        if (pvNil != (pwoksModal = NewObj WorldOfKidspace(&gcb, _pstrg)))
        {
            vpsndm->PauseAll();
            vpcex->SetModalGob(pwoksModal);
            if (pvNil != pwoksModal->PhbalNew(pwoksModal, prca, cnoTopic))
                fRet = FPure(vpappb->FModalLoop(plwRet));
            vpcex->SetModalGob(pvNil);
            vpsndm->ResumeAll();
        }

        ReleasePpo(&pwoksModal);
        vpappb->PopModal();
    }

    gte.Init(this, fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (!(grfgte & fgtePre) || !pgob->FIs(kclsKidspaceGraphicObject))
            continue;

        ((PKidspaceGraphicObject)pgob)->Resume();
    }

    return fRet;
}

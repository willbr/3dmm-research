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

RTCLASS(WorldOfKidspace)

/***************************************************************************
    Constructor for a World of Kidspace GraphicsObject.
***************************************************************************/
WorldOfKidspace::WorldOfKidspace(GraphicsObjectBlock *pgcb, PStringRegistry pstrg)
    : WorldOfKidspace_PAR(pgcb), _clokAnim(CommandHandler::HidUnique()), _clokNoSlip(CommandHandler::HidUnique(), fclokNoSlip),
      _clokGen(CommandHandler::HidUnique()), _clokReset(CommandHandler::HidUnique(), fclokReset)
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
void WorldOfKidspace::AssertValid(LegacyUlong grf)
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
PKidspaceGraphicObjectDescriptor WorldOfKidspace::PgokdFetch(ChunkTagOrType ctg, ChunkNumber cno, PResourceCache prca)
{
    AssertThis(0);
    AssertPo(prca, 0);

    return (PKidspaceGraphicObjectDescriptor)prca->PbacoFetch(ctg, cno, KidspaceGraphicObjectDescriptorLocation::FReadGkds);
}

/***************************************************************************
    Create a new gob in this kidspace world.
***************************************************************************/
PKidspaceGraphicObject WorldOfKidspace::PgokNew(PGraphicsObject pgobPar, long hid, ChunkNumber cnoGokd, PResourceCache prca)
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
        hid = CommandHandler::HidUnique();
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
PGraphicsObjectInterpreter WorldOfKidspace::PscegNew(PResourceCache prca, PGraphicsObject pgob)
{
    AssertThis(0);
    AssertPo(prca, 0);
    AssertPo(pgob, 0);

    return NewObj GraphicsObjectInterpreter(this, prca, pgob);
}

/***************************************************************************
    Create a new help balloon.
***************************************************************************/
PBalloon WorldOfKidspace::PhbalNew(PGraphicsObject pgobPar, PResourceCache prca, ChunkNumber cnoTopic, PTopic phtop)
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
PCommandHandler WorldOfKidspace::PcmhFromHid(long hid)
{
    AssertThis(0);
    PCommandHandler pcmh;

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
PClock WorldOfKidspace::PclokFromHid(long hid)
{
    AssertThis(0);

    if (khidClokGokGen == hid)
        return &_clokGen;
    if (khidClokGokReset == hid)
        return &_clokReset;

    return Clock::PclokFromHid(hid);
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
bool WorldOfKidspace::FFindFile(PString pstnSrc, PFilename pfni)
{
    AssertThis(0);
    AssertPo(pstnSrc, 0);
    AssertPo(pfni, 0);

    return pfni->FBuildFromPath(pstnSrc);
}

/***************************************************************************
    Put up an alert (and don't return until it is dismissed).
***************************************************************************/
tribool WorldOfKidspace::TGiveAlert(PString pstn, long bk, long cok)
{
    AssertThis(0);

    return vpappb->TGiveAlertSz(pstn->Psz(), bk, cok);
}

/***************************************************************************
    Put up an alert (and don't return until it is dismissed).
***************************************************************************/
void WorldOfKidspace::Print(PString pstn)
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
bool WorldOfKidspace::FModalTopic(PResourceCache prca, ChunkNumber cnoTopic, long *plwRet)
{
    AssertThis(0);
    AssertPo(prca, 0);
    AssertVarMem(plwRet);

    GraphicsObjectBlock gcb;
    PWorldOfKidspace pwoksModal;
    GraphicsObjectTreeEnumerator gte;
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
        gcb.Set(CommandHandler::HidUnique(), this, fgobNil, kginMark);
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

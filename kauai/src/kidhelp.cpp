/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Code for implementing help balloons in kidspace.

***************************************************************************/
#include "kidframe.h"
ASSERTNAME

namespace Help {

BEGIN_CMD_MAP(TopicGraphicsObject, RichTextDocumentGraphicsObject)
ON_CID_GEN(cidSelIdle, pvNil, pvNil)
ON_CID_ME(cidActivateSel, pvNil, pvNil)
END_CMD_MAP_NIL()

RTCLASS(TextDocument)
RTCLASS(TopicGraphicsObject)
RTCLASS(Balloon)
RTCLASS(BalloonButton)

const achar kchHelpString = '~';

/***************************************************************************
    Constructor for a help text document.
***************************************************************************/
TextDocument::TextDocument(PRCA prca, PDocumentBase pdocb, ulong grfdoc) : TextDocument_PAR(pdocb, grfdoc)
{
    AssertPo(prca, 0);
    _prca = prca;
    _prca->AddRef();
    _htop.cnoBalloon = cnoNil;
    _htop.cnoScript = cnoNil;
    _htop.ckiSnd.ctg = ctgNil;
    _htop.ckiSnd.cno = cnoNil;
}

/***************************************************************************
    Destructor for a help text document.
***************************************************************************/
TextDocument::~TextDocument(void)
{
    ReleasePpo(&_prca);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a TextDocument.
***************************************************************************/
void TextDocument::AssertValid(ulong grf)
{
    TextDocument_PAR::AssertValid(0);
    AssertPo(_prca, 0);
}

/***************************************************************************
    Mark memory for the TextDocument.
***************************************************************************/
void TextDocument::MarkMem(void)
{
    AssertValid(0);
    TextDocument_PAR::MarkMem();
    MarkMemObj(_prca);
}
#endif // DEBUG

/***************************************************************************
    Static method to read a help text document from the given (pcfl, ctg, cno)
    and using the given prca as the source for pictures and buttons.
***************************************************************************/
PTextDocument TextDocument::PtxhdReadChunk(PRCA prca, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, PStringRegistry pstrg, ulong grftxhd)
{
    AssertPo(prca, 0);
    AssertPo(pcfl, 0);
    PTextDocument ptxhd;

    if (pvNil == (ptxhd = NewObj TextDocument(prca)) || !ptxhd->_FReadChunk(pcfl, ctg, cno, pstrg, grftxhd))
    {
        PushErc(ercHelpReadFailed);
        ReleasePpo(&ptxhd);
    }

    AssertNilOrPo(ptxhd, fobjAssertFull);
    return ptxhd;
}

/***************************************************************************
    Read the given chunk into this RichTextDocument.
***************************************************************************/
bool TextDocument::_FReadChunk(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, PStringRegistry pstrg, ulong grftxhd)
{
    AssertPo(pcfl, 0);
    AssertNilOrPo(pstrg, 0);
    DataBlock blck;
    ChildChunkIdentification kid;
    TopicFile htopf;
    long stid, lw;
    long cp, cpMac, cpMin;
    STN stn;
    bool fRet = fFalse;

    if (pcfl->FForest(ctg, cno))
    {
        ChunkIdentification cki;

        if (pvNil == (pcfl = pcfl->PcflReadForest(ctg, cno, fFalse)))
            goto LFail;
        if (!pcfl->FGetCkiCtg(ctg, 0, &cki))
            goto LFail;
        cno = cki.cno;
    }
    else
        pcfl->AddRef();

    // The old version of Topic didn't have the ckiSnd - accept both old and new
    // versions.
    htopf.htop.ckiSnd.ctg = ctgNil;
    htopf.htop.ckiSnd.cno = cnoNil;
    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData() ||
        size(TopicFile) != blck.Cb() && offset(TopicFile, htop.ckiSnd) != blck.Cb() || !blck.FRead(&htopf))
    {
        goto LFail;
    }

    if (htopf.bo == kboOther)
        SwapBytesBom(&htopf.htop, kbomHtop);
    else if (htopf.bo != kboCur)
        goto LFail;

    if (!pcfl->FGetKidChidCtg(ctg, cno, 0, kctgRichText, &kid))
        goto LFail;

    if (!TextDocument_PAR::_FReadChunk(pcfl, kid.cki.ctg, kid.cki.cno, FPure(grftxhd & ftxhdCopyText)))
    {
        goto LFail;
    }

    if ((grftxhd & ftxhdExpandStrings) && pvNil != pstrg)
    {
        SetInternal();
        SuspendUndo();
        cpMac = CpMac();
        for (cp = 0; cp < cpMac;)
        {
            if (_ChFetch(cp) != kchHelpString)
            {
                cp++;
                continue;
            }

            cpMin = cp++;
            for (stid = 0; cp < cpMac && FIn(lw = _ChFetch(cp) - '0', 0, 10); cp++)
                stid = stid * 10 + lw;
            if (!pstrg->FGet(stid, &stn))
                Warn("string missing");
            if (FReplaceRgch(stn.Psz(), stn.Cch(), cpMin, cp - cpMin, fdocNil))
            {
                cp = cpMin + stn.Cch();
                cpMac = CpMac();
            }
        }
    }

    fRet = fTrue;
    _htop = htopf.htop;
    AssertThis(0);

LFail:
    // Release our hold on the ChunkyFile
    ReleasePpo(&pcfl);

    return fRet;
}

/***************************************************************************
    Do any necessary munging of the AllocatedGroup entry on open.  Return false if
    we don't recognize this argument type.
***************************************************************************/
bool TextDocument::_FOpenArg(long icact, byte sprm, short bo, short osk)
{
    ChunkTag ctg;
    ChunkNumber cno;
    long cb;
    long rglw[2];
    long clw;

    if (TextDocument_PAR::_FOpenArg(icact, sprm, bo, osk))
        return fTrue;

    cb = _pagcact->Cb(icact);
    switch (sprm)
    {
    case sprmGroup:
        if (cb < size(byte) + size(ChunkNumber))
            return fFalse;
        if (bo == kboOther)
        {
            _pagcact->GetRgb(icact, size(byte), size(ChunkNumber), &cno);
            SwapBytesRglw(&cno, 1);
            _pagcact->PutRgb(icact, size(byte), size(ChunkNumber), &cno);
        }
        break;

    case sprmObject:
        if (cb < size(ChunkTag))
            return fFalse;
        _pagcact->GetRgb(icact, 0, size(ChunkTag), &ctg);
        if (bo == kboOther)
        {
            SwapBytesRglw(&ctg, 1);
            _pagcact->PutRgb(icact, 0, size(ChunkTag), &ctg);
        }
        cb -= size(ChunkTag);

        switch (ctg)
        {
        case kctgMbmp:
        case kctgEditControl:
            clw = 1;
            goto LSwapBytes;

        case kctgGokd:
            clw = 2;
        LSwapBytes:
            AssertIn(clw, 1, CvFromRgv(rglw) + 1);
            if (cb < clw * size(long))
                return fFalse;

            if (bo == kboOther)
            {
                _pagcact->GetRgb(icact, size(ChunkTag), clw * size(long), rglw);
                SwapBytesRglw(rglw, clw);
                _pagcact->PutRgb(icact, size(ChunkTag), clw * size(long), rglw);
            }
            break;

        default:
            return fFalse;
        }
        break;

    default:
        return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    Save a help topic to the given chunky file.  Fill in *pcki with where
    we put the root chunk.
***************************************************************************/
bool TextDocument::FSaveToChunk(PChunkyFile pcfl, ChunkIdentification *pcki, bool fRedirectText)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertVarMem(pcki);
    DataBlock blck;
    ChunkIdentification cki;
    TopicFile htopf;

    pcki->ctg = kctgHelpTopic;
    htopf.bo = kboCur;
    htopf.osk = koskCur;
    htopf.htop = _htop;
    if (!pcfl->FAdd(size(TopicFile), pcki->ctg, &pcki->cno, &blck))
    {
        PushErc(ercHelpSaveFailed);
        return fFalse;
    }
    if (!blck.FWrite(&htopf))
        goto LFail;

    if (!TextDocument_PAR::FSaveToChunk(pcfl, &cki, fRedirectText))
        goto LFail;

    // add the text chunk and write it
    if (!pcfl->FAdoptChild(pcki->ctg, pcki->cno, cki.ctg, cki.cno))
    {
        pcfl->Delete(cki.ctg, cki.cno);
    LFail:
        pcfl->Delete(pcki->ctg, pcki->cno);
        PushErc(ercHelpSaveFailed);
        return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    Get the bounding rectangle for the given object.
***************************************************************************/
bool TextDocument::_FGetObjectRc(long icact, byte sprm, PGNV pgnv, PCHP pchp, RC *prc)
{
    AssertThis(0);
    AssertIn(icact, 0, _pagcact->IvMac());
    AssertIn(sprm, sprmMinObj, 0x100);
    AssertPo(pgnv, 0);
    AssertVarMem(pchp);
    AssertVarMem(prc);
    long cb;
    PMBMP pmbmp;
    PChunkyResourceFile pcrf;
    ChildChunkIdentification kid;
    long rglw[2];

    if (sprmObject != sprm)
        return fFalse;

    Assert(size(ChunkTag) == size(long), 0);
    cb = _pagcact->Cb(icact);
    if (cb < size(rglw))
        return fFalse;
    _pagcact->GetRgb(icact, 0, size(rglw), rglw);
    switch ((ChunkTag)rglw[0])
    {
    case kctgMbmp:
        pmbmp = (PMBMP)_prca->PbacoFetch(rglw[0], rglw[1], MBMP::FReadMbmp);
        goto LHaveMbmp;

    case kctgGokd:
        pcrf = _prca->PcrfFindChunk(rglw[0], rglw[1]);
        if (pvNil == pcrf)
            return fFalse;
        if (!pcrf->Pcfl()->FGetKidChidCtg(rglw[0], rglw[1], 0x10000, kctgMbmp, &kid))
        {
            return fFalse;
        }
        pmbmp = (PMBMP)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, MBMP::FReadMbmp);
    LHaveMbmp:
        if (pvNil == pmbmp)
            return fFalse;
        pmbmp->GetRc(prc);
        ReleasePpo(&pmbmp);
        prc->Offset(-prc->xpLeft, -prc->ypBottom);
        return fTrue;

    case kctgEditControl:
        pgnv->SetFont(pchp->onn, pchp->grfont, pchp->dypFont, tahLeft, tavBaseline);
        pgnv->GetRcFromRgch(prc, pvNil, 0);
        prc->Inset(0, -1);
        prc->xpLeft = 0;
        prc->xpRight = rglw[1];
        return fTrue;

    default:
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Draw the given object.
***************************************************************************/
bool TextDocument::_FDrawObject(long icact, byte sprm, PGNV pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip)
{
    AssertIn(icact, 0, _pagcact->IvMac());
    Assert(sprm >= sprmObject, 0);
    AssertPo(pgnv, 0);
    AssertVarMem(pxp);
    AssertVarMem(pchp);
    AssertVarMem(prcClip);
    long cb;
    RC rc;
    PMBMP pmbmp;
    PChunkyResourceFile pcrf;
    ChildChunkIdentification kid;
    long rglw[2];
    bool fDrawMbmp = fTrue;

    if (sprmObject != sprm)
        return fFalse;

    cb = _pagcact->Cb(icact);
    if (cb < size(rglw))
        return fFalse;
    _pagcact->GetRgb(icact, 0, size(rglw), rglw);
    switch ((ChunkTag)rglw[0])
    {
    case kctgMbmp:
        pmbmp = (PMBMP)_prca->PbacoFetch(rglw[0], rglw[1], MBMP::FReadMbmp);
        goto LHaveMbmp;

    case kctgGokd:
        fDrawMbmp = !_fHideButtons;
        pcrf = _prca->PcrfFindChunk(rglw[0], rglw[1]);
        if (pvNil == pcrf)
            return fFalse;
        if (!pcrf->Pcfl()->FGetKidChidCtg(rglw[0], rglw[1], ChidFromSnoDchid(ksnoInit, 0), kctgMbmp, &kid))
        {
            return fFalse;
        }
        pmbmp = (PMBMP)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, MBMP::FReadMbmp);
    LHaveMbmp:
        if (pvNil == pmbmp)
            return fFalse;
        pmbmp->GetRc(&rc);
        rc.Offset(*pxp - rc.xpLeft, yp - rc.ypBottom);
        if (kacrClear != pchp->acrBack)
            pgnv->FillRc(&rc, pchp->acrBack);
        if (fDrawMbmp)
            pgnv->DrawMbmp(pmbmp, &rc);
        ReleasePpo(&pmbmp);
        if (pchp->grfont & fontBoxed)
        {
            pgnv->SetPenSize(1, 1);
            pgnv->FrameRcApt(&rc, &vaptGray, pchp->acrFore, kacrClear);
        }
        *pxp += rc.Dxp();
        return fTrue;

    case kctgEditControl:
        pgnv->SetFont(pchp->onn, pchp->grfont, pchp->dypFont, tahLeft, tavBaseline);
        pgnv->GetRcFromRgch(&rc, pvNil, 0, 0, yp);
        rc.Inset(0, -1);
        rc.xpLeft = *pxp;
        rc.xpRight = rc.xpLeft + rglw[1];
        *pxp = rc.xpRight;
        pgnv->SetPenSize(1, 1);
        pgnv->FrameRc(&rc, kacrBlack);
        rc.Inset(1, 1);
        pgnv->FillRc(&rc, pchp->acrBack);
        return fTrue;

    default:
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Insert a picture into the help text document.
***************************************************************************/
bool TextDocument::FInsertPicture(ChunkNumber cno, void *pvExtra, long cbExtra, long cp, long ccpDel, PCHP pchp, ulong grfdoc)
{
    AssertThis(0);
    AssertPvCb(pvExtra, cbExtra);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    AssertNilOrVarMem(pchp);
    ChunkIdentification cki;
    void *pv = &cki;
    bool fRet = fFalse;

    cki.ctg = kctgMbmp;
    cki.cno = cno;
    if (cbExtra > 0)
    {
        if (!FAllocPv(&pv, size(ChunkIdentification) + cbExtra, fmemNil, mprNormal))
            return fFalse;
        CopyPb(&cki, pv, size(ChunkIdentification));
        CopyPb(pvExtra, PvAddBv(pv, size(ChunkIdentification)), cbExtra);
    }

    fRet = FInsertObject(pv, size(ChunkIdentification) + cbExtra, cp, ccpDel, pchp, grfdoc);

    if (pv != &cki)
        FreePpv(&pv);
    return fRet;
}

/***************************************************************************
    Insert a new button
***************************************************************************/
bool TextDocument::FInsertButton(ChunkNumber cno, ChunkNumber cnoTopic, void *pvExtra, long cbExtra, long cp, long ccpDel, PCHP pchp,
                         ulong grfdoc)
{
    AssertThis(0);
    AssertPvCb(pvExtra, cbExtra);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    AssertNilOrVarMem(pchp);
    byte rgb[size(ChunkIdentification) + size(long)];
    ChunkIdentification *pcki = (ChunkIdentification *)rgb;
    ChunkNumber *pcnoTopic = (ChunkNumber *)(pcki + 1);
    ;
    void *pv = rgb;
    bool fRet = fFalse;

    pcki->ctg = kctgGokd;
    pcki->cno = cno;
    *pcnoTopic = cnoTopic;
    if (cbExtra > 0)
    {
        if (!FAllocPv(&pv, size(rgb) + cbExtra, fmemNil, mprNormal))
            return fFalse;
        CopyPb(rgb, pv, size(rgb));
        CopyPb(pvExtra, PvAddBv(pv, size(rgb)), cbExtra);
    }

    fRet = FInsertObject(pv, size(rgb) + cbExtra, cp, ccpDel, pchp, grfdoc);

    if (pv != rgb)
        FreePpv(&pv);

    return fRet;
}

/***************************************************************************
    Group the given text into the given group.  lw == 0 indicates no group.
    Any non-zero number is a group.
***************************************************************************/
bool TextDocument::FGroupText(long cp1, long cp2, byte bGroup, ChunkNumber cnoTopic, PSTN pstnTopic)
{
    AssertThis(0);
    AssertNilOrPo(pstnTopic, 0);
    AssertIn(cp1, 0, CpMac());
    AssertIn(cp2, 0, CpMac());
    SPVM spvm;

    SortLw(&cp1, &cp2);
    if (cp1 == cp2)
        return fTrue;

    if (!FSetUndo(cp1, cp2, cp2 - cp1))
        return fFalse;

    spvm.sprm = sprmGroup;
    spvm.lwMask = -1;
    if (bGroup == 0)
    {
        // means no grouping
        spvm.lw = 0;
    }
    else
    {
        byte rgb[size(byte) + size(ChunkNumber) + kcbMaxDataStn];
        long cb = size(byte) + size(ChunkNumber);

        rgb[0] = bGroup;
        CopyPb(&cnoTopic, rgb + size(byte), size(ChunkNumber));
        if (pvNil != pstnTopic && pstnTopic->Cch() > 0)
        {
            pstnTopic->GetData(rgb + cb);
            cb += pstnTopic->CbData();
        }

        if (!_FEnsureInAg(sprmGroup, rgb, cb, &spvm.lw))
        {
            CancelUndo();
            return fFalse;
        }
    }

    if (!_pglmpe->FEnsureSpace(2))
    {
        _ReleaseRgspvm(&spvm, 1);
        CancelUndo();
        return fFalse;
    }

    _ApplyRgspvm(cp1, cp2 - cp1, &spvm, 1);
    CommitUndo();

    AssertThis(0);
    InvalAllDdg(cp1, cp2 - cp1, cp2 - cp1);
    return fTrue;
}

/***************************************************************************
    Determine if the given cp is in a grouped text range.
***************************************************************************/
bool TextDocument::FGrouped(long cp, long *pcpMin, long *pcpLim, byte *pbGroup, ChunkNumber *pcnoTopic, PSTN pstnTopic)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac());
    AssertNilOrVarMem(pcpMin);
    AssertNilOrVarMem(pcpLim);
    AssertNilOrVarMem(pbGroup);
    AssertNilOrVarMem(pcnoTopic);
    AssertNilOrPo(pstnTopic, 0);
    MPE mpe;
    byte bGroup = 0;

    if (!_FFindMpe(_SpcpFromSprmCp(sprmGroup, cp), &mpe, pcpLim))
    {
        mpe.lw = 0;
        mpe.spcp = 0;
    }

    if (mpe.lw > 0)
    {
        byte *prgb;
        long cb;

        prgb = (byte *)_pagcact->PvLock(mpe.lw - 1, &cb);
        cb -= size(byte) + size(ChunkNumber); // group number, cnoTopic
        if (cb < 0)
            goto LFail;

        bGroup = prgb[0];
        if (bGroup == 0)
        {
        LFail:
            Bug("bad group data");
            _pagcact->Unlock();
            goto LNotGrouped;
        }

        if (pvNil != pcnoTopic)
            CopyPb(prgb + size(byte), pcnoTopic, size(ChunkNumber));
        if (pvNil != pstnTopic)
        {
            if (cb > 0)
                pstnTopic->FSetData(prgb + size(byte) + size(ChunkNumber), cb);
            else
                pstnTopic->SetNil();
        }
        _pagcact->Unlock();
    }
    else
    {
    LNotGrouped:
        if (pvNil != pcnoTopic)
            *pcnoTopic = cnoNil;
        if (pvNil != pstnTopic)
            pstnTopic->SetNil();
    }

    if (pvNil != pbGroup)
        *pbGroup = bGroup;
    if (pvNil != pcpMin)
        *pcpMin = _CpFromSpcp(mpe.spcp);

    return bGroup != 0;
}

/***************************************************************************
    Get the help topic information.
***************************************************************************/
void TextDocument::GetHtop(PTopic phtop)
{
    AssertThis(0);
    AssertVarMem(phtop);

    *phtop = _htop;
}

/***************************************************************************
    Set the topic info.
***************************************************************************/
void TextDocument::SetHtop(PTopic phtop)
{
    AssertThis(0);
    AssertVarMem(phtop);

    _htop = *phtop;
    SetDirty();
}

/***************************************************************************
    Constructor for a TopicGraphicsObject.
***************************************************************************/
TopicGraphicsObject::TopicGraphicsObject(PWorldOfKidspace pwoks, PTextDocument ptxhd, PGCB pgcb) : RichTextDocumentGraphicsObject(ptxhd, pgcb)
{
    AssertBaseThis(0);
    AssertPo(pwoks, 0);

    _pwoks = pwoks;
}

/***************************************************************************
    Create a new help topic display gob.
***************************************************************************/
PTopicGraphicsObject TopicGraphicsObject::PtxhgNew(PWorldOfKidspace pwoks, PTextDocument ptxhd, PGCB pgcb)
{
    PTopicGraphicsObject ptxhg;

    if (pvNil == (ptxhg = NewObj TopicGraphicsObject(pwoks, ptxhd, pgcb)))
        return pvNil;
    if (!ptxhg->_FInit())
    {
        ReleasePpo(&ptxhg);
        return pvNil;
    }
    return ptxhg;
}

/***************************************************************************
    Inititalize the display gob for a help balloon topic.
***************************************************************************/
bool TopicGraphicsObject::_FInit(void)
{
    AssertBaseThis(0);
    PRCA prca;
    long cp, cb;
    void *pv;
    ChunkIdentification *pcki;
    long dxp;
    ChunkNumber cno;
    long xp, ypBase;
    ChunkNumber cnoTopic;
    byte bGroup;
    long lwMax;
    RuntimeVariableName rtvn;
    long hid;
    CHP chp;
    RC rc;
    EDPAR edpar;
    STN stn;
    PTextDocument ptxhd = Ptxhd();

    if (!TopicGraphicsObject_PAR::_FInit())
        return fFalse;

    // find the max of the group numbers
    lwMax = 0;
    for (cp = 0; cp < ptxhd->CpMac();)
    {
        ptxhd->FGrouped(cp, pvNil, &cp, &bGroup);
        lwMax = LwMax((long)bGroup, lwMax);
    }

    // find a base hid that covers lwMax buttons
    _hidBase = 0;
    if (lwMax > 0)
    {
        _hidBase = CMH::HidUnique(lwMax) - 1;
        stn = PszLit("_gidBase");
        rtvn.SetFromStn(&stn);
        if (!FAssignRtvm(PgobPar()->Ppglrtvm(), &rtvn, _hidBase))
            return fFalse;
    }

    prca = Ptxhd()->Prca();
    for (cp = 0; Ptxhd()->FFetchObject(cp, &cp, &pv, &cb); cp++)
    {
        if (pvNil == pv)
            continue;

        if (cb < size(ChunkTag))
            goto LContinue;

        switch (*(ChunkTag *)pv)
        {
        case kctgEditControl:
            if (cb < size(EditControl))
                goto LContinue;
            dxp = ((EditControl *)pv)->dxp;
            FreePpv(&pv);

            // get the bounding rectangle
            _GetXpYpFromCp(cp, pvNil, pvNil, &xp, &ypBase, fFalse);
            _FetchChp(cp, &chp);
            _pgnv->SetFont(chp.onn, chp.grfont, chp.dypFont, tahLeft, tavBaseline);
            _pgnv->GetRcFromRgch(&rc, pvNil, 0);
            rc.Offset(0, ypBase + chp.dypOffset);
            rc.xpLeft = xp + 1;
            rc.xpRight = xp + dxp - 1;

            Ptxhd()->FGrouped(cp, pvNil, pvNil, &bGroup);
            if (bGroup == 0 || _pwoks->PcmhFromHid(hid = _hidBase + bGroup) != pvNil)
            {
                hid = CMH::HidUnique();
            }
            if (chp.acrBack == kacrClear)
                chp.acrBack = kacrWhite;
            edpar.Set(hid, this, fgobNil, kginMark, &rc, pvNil, chp.onn, chp.grfont, chp.dypFont, tahLeft, tavTop,
                      chp.acrFore, chp.acrBack);
            if (pvNil == EDSL::PedslNew(&edpar))
                return fFalse;
            break;

        case kctgGokd:
            if (cb < size(ChunkIdentification) + size(ChunkNumber))
                goto LContinue;
            pcki = (ChunkIdentification *)pv;
            cno = pcki->cno;
            cnoTopic = *(ChunkNumber *)(pcki + 1);
            FreePpv(&pv);

            _GetXpYpFromCp(cp, pvNil, pvNil, &xp, &ypBase, fFalse);
            _FetchChp(cp, &chp);
            Ptxhd()->FGrouped(cp, pvNil, pvNil, &bGroup, cnoTopic == cnoNil ? &cnoTopic : pvNil);
            if (bGroup == 0 || _pwoks->PcmhFromHid(hid = _hidBase + bGroup) != pvNil)
            {
                hid = CMH::HidUnique();
            }
            if (pvNil == BalloonButton::PhbtnNew(_pwoks, this, hid, cno, prca, bGroup, cnoTopic, xp, ypBase + chp.dypOffset))
            {
                return fFalse;
            }
            break;

        default:
        LContinue:
            FreePpv(&pv);
            break;
        }
    }

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Return whether the point is over hot (marked text).
***************************************************************************/
bool TopicGraphicsObject::FPtIn(long xp, long yp)
{
    AssertThis(0);

    if (!TopicGraphicsObject_PAR::FPtIn(xp, yp))
        return fFalse;
    return FGroupFromPt(xp, yp);
}

/***************************************************************************
    Track the mouse.
***************************************************************************/
bool TopicGraphicsObject::FCmdTrackMouse(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    pcmd->grfcust = _pwoks->GrfcustAdjust(pcmd->grfcust);
    if (pcmd->cid == cidMouseDown)
    {
        // first response to mouse down
        Assert(vpcex->PgobTracking() == pvNil, "mouse already being tracked!");

        if (!FGroupFromPt(pcmd->xp, pcmd->yp, &_bTrack, &_cnoTrack))
            return fTrue;

        vpcex->TrackMouse(this);
        SetCursor(pcmd->grfcust);
        _grfcust = pcmd->grfcust;
    }
    else
    {
        Assert(vpcex->PgobTracking() == this, "not tracking mouse!");
        Assert(pcmd->cid == cidTrackMouse, 0);
        if (!(pcmd->grfcust & fcustMouse))
        {
            byte bGroup;
            ChunkNumber cnoTopic;
            vpcex->EndMouseTracking();

            if (FGroupFromPt(pcmd->xp, pcmd->yp, &bGroup, &cnoTopic) && bGroup == _bTrack && cnoTopic == _cnoTrack)
            {
                DoHit(bGroup, cnoTopic, _grfcust, hidNil);
            }
        }
    }

    return fTrue;
}

/***************************************************************************
    An edit control got a bad key.
***************************************************************************/
bool TopicGraphicsObject::FCmdBadKey(PCMD_BADKEY pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (!FIn(pcmd->hid, _hidBase + 1, _hidBase + 257))
        return fFalse;

    pcmd->grfcust = _pwoks->GrfcustAdjust(pcmd->grfcust);
    _FRunScript((byte)(pcmd->hid - _hidBase), pcmd->grfcust, pcmd->hid, (achar)pcmd->ch);
    return fTrue;
}

/***************************************************************************
    Return the number of the group text that the given point is in.
***************************************************************************/
bool TopicGraphicsObject::FGroupFromPt(long xp, long yp, byte *pbGroup, ChunkNumber *pcnoTopic)
{
    AssertThis(0);
    AssertNilOrVarMem(pbGroup);
    AssertNilOrVarMem(pcnoTopic);
    long cp;

    if (!_FGetCpFromPt(xp, yp, &cp, fFalse))
        return 0;
    return Ptxhd()->FGrouped(cp, pvNil, pvNil, pbGroup, pcnoTopic);
}

/***************************************************************************
    A child button was hit, take action.
***************************************************************************/
void TopicGraphicsObject::DoHit(byte bGroup, ChunkNumber cnoTopic, ulong grfcust, long hidHit)
{
    AssertThis(0);
    long lwRet = 0;

    // run the script
    if (!_FRunScript(bGroup, grfcust, hidHit, chNil, cnoTopic, &lwRet))
        return;

    if (cnoNil != cnoTopic && !lwRet)
        _pwoks->PhbalNew(PgobPar()->PgobPar(), Ptxhd()->Prca(), cnoTopic);
}

/***************************************************************************
    Run the script. Returns false iff the TopicGraphicsObject doesn't exist after
    running the script.
***************************************************************************/
bool TopicGraphicsObject::_FRunScript(byte bGroup, ulong grfcust, long hidHit, achar ch, ChunkNumber cnoTopic, long *plwRet)
{
    AssertThis(0);
    AssertNilOrVarMem(plwRet);

    PScript pscpt;
    PGraphicsObjectInterpreter psceg;
    Topic htop;
    bool fRet = fTrue;
    PTextDocument ptxhd = Ptxhd();
    PRCA prca = ptxhd->Prca();

    if (pvNil != plwRet)
        *plwRet = 0;

    ptxhd->GetHtop(&htop);
    if (cnoNil == htop.cnoScript)
        return fTrue;

    pscpt = (PScript)prca->PbacoFetch(kctgScript, htop.cnoScript, Script::FReadScript);
    if (pvNil != pscpt && pvNil != (psceg = _pwoks->PscegNew(prca, this)))
    {
        AssertPo(pscpt, 0);
        AssertPo(psceg, 0);

        PWorldOfKidspace pwoks = _pwoks;
        long grid = Grid();
        long rglw[5];

        rglw[0] = (long)bGroup;
        rglw[1] = grfcust;
        rglw[2] = hidHit;
        rglw[3] = (long)(byte)ch;
        rglw[4] = cnoTopic;

        // be careful not to use TopicGraphicsObject variables here in case the TopicGraphicsObject is
        // freed while the script is running.
        if (!psceg->FRunScript(pscpt, rglw, 5, plwRet) && pvNil != plwRet)
            *plwRet = 0;
        ReleasePpo(&psceg);

        fRet = (this == pwoks->PgobFromGrid(grid));
    }
    ReleasePpo(&pscpt);

    return fRet;
}

/***************************************************************************
    This handles cidMouseMove.
***************************************************************************/
bool TopicGraphicsObject::FCmdMouseMove(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    ulong grfcust = _pwoks->GrfcustAdjust(pcmd->grfcust);

    if (FGroupFromPt(pcmd->xp, pcmd->yp))
        grfcust |= fcustHotText;

    SetCursor(grfcust);
    return fTrue;
}

/***************************************************************************
    Set the cursor for this TopicGraphicsObject and the given cursor state.
***************************************************************************/
void TopicGraphicsObject::SetCursor(ulong grfcust)
{
    AssertThis(0);
    PGraphicsObject pgob;

    for (pgob = this;;)
    {
        pgob = pgob->PgobPar();
        if (pvNil == pgob)
        {
            vpappb->SetCurs(pvNil);
            break;
        }
        if (pgob->FIs(kclsKidspaceGraphicObject))
        {
            ((PKidspaceGraphicObject)pgob)->SetCursor(grfcust | fcustChildGok);
            break;
        }
    }
}

/***************************************************************************
    Create a new help topic balloon based on the given topic number.
***************************************************************************/
PBalloon Balloon::PhbalCreate(PWorldOfKidspace pwoks, PGraphicsObject pgobPar, PRCA prca, ChunkNumber cnoTopic, PTopic phtop)
{
    AssertPo(pwoks, 0);
    AssertPo(pgobPar, 0);
    AssertPo(prca, 0);
    AssertNilOrVarMem(phtop);
    PChunkyResourceFile pcrf;
    PTextDocument ptxhd;
    PBalloon phbal;

    pcrf = prca->PcrfFindChunk(kctgHelpTopic, cnoTopic);
    if (pvNil == pcrf)
        return pvNil;

    ptxhd = TextDocument::PtxhdReadChunk(prca, pcrf->Pcfl(), kctgHelpTopic, cnoTopic, pwoks->Pstrg());
    if (pvNil == ptxhd)
        return pvNil;

    ptxhd->HideButtons();
    phbal = PhbalNew(pwoks, pgobPar, prca, ptxhd, phtop);
    ReleasePpo(&ptxhd);

    return phbal;
}

/***************************************************************************
    Static method to create a new help balloon based on the given help
    topic document and htop.
***************************************************************************/
PBalloon Balloon::PhbalNew(PWorldOfKidspace pwoks, PGraphicsObject pgobPar, PRCA prca, PTextDocument ptxhd, PTopic phtop)
{
    AssertPo(pwoks, 0);
    AssertPo(pgobPar, 0);
    AssertPo(ptxhd, 0);
    AssertPo(prca, 0);
    AssertNilOrVarMem(phtop);
    Topic htop;
    GraphicsObjectBlock gcb;
    PBalloon phbal;
    long grid;

    ptxhd->GetHtop(&htop);
    if (pvNil != phtop)
    {
        // merge the given htop with the topic's htop.
        if (cnoNil != phtop->cnoBalloon)
            htop.cnoBalloon = phtop->cnoBalloon;
        if (hidNil != phtop->hidThis)
            htop.hidThis = phtop->hidThis;
        if (hidNil != phtop->hidTarget)
            htop.hidTarget = phtop->hidTarget;
        if (cnoNil != phtop->cnoScript)
            htop.cnoScript = phtop->cnoScript;
        htop.dxp += phtop->dxp;
        htop.dyp += phtop->dyp;
        if (cnoNil != phtop->ckiSnd.cno && ctgNil != phtop->ckiSnd.ctg)
            htop.ckiSnd = phtop->ckiSnd;
    }

    if (htop.hidThis == hidNil)
        htop.hidThis = CMH::HidUnique();
    else if (pvNil != (phbal = (PBalloon)pwoks->PcmhFromHid(htop.hidThis)))
    {
        if (!phbal->FIs(kclsBalloon))
        {
            Bug("command handler with this ID already exists");
            return pvNil;
        }

        AssertPo(phbal, 0);

#ifdef REVIEW // shonk: this makes little sense and is bug-prone
        if (htop.cnoBalloon == phbal->_pgokd->Cno() && prca == phbal->_prca)
        {
            // same hid, same KidspaceGraphicObjectDescriptor, same prca, so just change the topic
            if (!phbal->FSetTopic(ptxhd, &htop, prca))
                return pvNil;
            return phbal;
        }
#endif // REVIEW

        // free the balloon and create the new one.
        ReleasePpo(&phbal);
    }

    gcb.Set(htop.hidThis, pgobPar, fgobNil, kginMark);
    if (pvNil == (phbal = NewObj Balloon(&gcb)))
        return pvNil;
    grid = phbal->Grid();

    if (!phbal->_FInit(pwoks, ptxhd, &htop, prca))
    {
        ReleasePpo(&phbal);
        return pvNil;
    }

    if (!phbal->_FEnterState(ksnoInit))
    {
        Warn("Balloon immediately destroyed!");
        return pvNil;
    }

    // initialize the topic
    phbal->_ptxhg->DoHit(0, cnoNil, fcustNil, hidNil);
    if (phbal != pwoks->PgobFromGrid(grid))
    {
        Warn("Balloon immediately destroyed 2!");
        return pvNil;
    }

    AssertPo(phbal, 0);
    return phbal;
}

/***************************************************************************
    Constructor for a help balloon.
***************************************************************************/
Balloon::Balloon(GraphicsObjectBlock *pgcb) : Balloon_PAR(pgcb)
{
}

/***************************************************************************
    Initialize the help balloon.
***************************************************************************/
bool Balloon::_FInit(PWorldOfKidspace pwoks, PTextDocument ptxhd, Topic *phtop, PRCA prca)
{
    AssertBaseThis(0);
    AssertPo(ptxhd, 0);
    AssertVarMem(phtop);
    AssertPo(prca, 0);

    if (!Balloon_PAR::_FInit(pwoks, phtop->cnoBalloon, prca))
        return fFalse;

    return _FSetTopic(ptxhd, phtop, prca);
}

/***************************************************************************
    Set the topic for this balloon.  Returns false if setting the topic
    fails or if the balloon is instantly killed by a script.
***************************************************************************/
bool Balloon::FSetTopic(PTextDocument ptxhd, PTopic phtop, PRCA prca)
{
    AssertThis(0);
    AssertPo(ptxhd, 0);
    AssertVarMem(phtop);
    AssertPo(prca, 0);

    if (!_FSetTopic(ptxhd, phtop, prca))
        return fFalse;

    return _FEnterState(ksnoInit);
}

/***************************************************************************
    Set the topic in the help balloon.  Don't enter the initial state.
***************************************************************************/
bool Balloon::_FSetTopic(PTextDocument ptxhd, PTopic phtop, PRCA prca)
{
    AssertBaseThis(0);
    AssertPo(ptxhd, 0);
    AssertVarMem(phtop);
    AssertPo(prca, 0);

    PGraphicsObject pgob;
    GraphicsObjectBlock gcb;
    PT pt, ptReg;
    STN stn;
    RuntimeVariableName rtvn;
    PTopicGraphicsObject ptxhgSave = _ptxhg;

    // create the topic DocumentDisplayGraphicsObject.
    gcb.Set(CMH::HidUnique(), this, fgobNil, kginMark);
    if (pvNil == (_ptxhg = TopicGraphicsObject::PtxhgNew(_pwoks, ptxhd, &gcb)))
        goto LFail;

    // set the sound variables
    stn = PszLit("_ctgSound");
    rtvn.SetFromStn(&stn);
    if (!FAssignRtvm(_ptxhg->Ppglrtvm(), &rtvn, phtop->ckiSnd.ctg))
        goto LFail;
    stn = PszLit("_cnoSound");
    rtvn.SetFromStn(&stn);
    if (!FAssignRtvm(_ptxhg->Ppglrtvm(), &rtvn, phtop->ckiSnd.cno))
    {
    LFail:
        ReleasePpo(&_ptxhg);

        // restore the previous topic DocumentDisplayGraphicsObject
        _ptxhg = ptxhgSave;
        return fFalse;
    }
    ReleasePpo(&ptxhgSave);

    _ptxhg->GetNaturalSize(&_dxpPref, &_dypPref);
    if (hidNil == phtop->hidTarget || pvNil == (pgob = _pwoks->PgobFromHid(phtop->hidTarget)))
    {
        pgob = PgobPar();
    }

    if (pgob->FIs(kclsKidspaceGraphicObject))
        ((PKidspaceGraphicObject)pgob)->GetPtReg(&pt);
    else
    {
        RC rc;

        pgob->GetRc(&rc, cooParent);
        pt.xp = rc.XpCenter();
        pt.yp = rc.YpCenter();
    }
    pgob->MapPt(&pt, cooParent, cooGlobal);

    // point the balloon at the gob
    PgobPar()->MapPt(&pt, cooGlobal, cooLocal);
    GetPtReg(&ptReg);
    _SetGorp(_pgorp, pt.xp - ptReg.xp + phtop->dxp, pt.yp - ptReg.yp + phtop->dyp);

    return fTrue;
}

/***************************************************************************
    Our representation is changing, so make sure we stay inside our parent
    and reposition the TopicGraphicsObject.
***************************************************************************/
void Balloon::_SetGorp(PGORP pgorp, long dxp, long dyp)
{
    RC rc1, rc2, rc3;

    Balloon_PAR::_SetGorp(pgorp, dxp, dyp);

    // make sure we stay inside our parent
    GetRc(&rc1, cooParent);
    PgobPar()->GetRc(&rc2, cooLocal);
    rc3.FIntersect(&rc1, &rc2);
    if (rc3 != rc1)
    {
        rc1.PinToRc(&rc2);
        SetPos(&rc1);
    }

    // position the TopicGraphicsObject.
    GetRcContent(&rc1);
    rc2.Set(0, 0, _dxpPref, _dypPref);
    rc2.CenterOnRc(&rc1);
    _ptxhg->SetPos(&rc2);
}

/***************************************************************************
    Constructor for a help balloon button.
***************************************************************************/
BalloonButton::BalloonButton(GraphicsObjectBlock *pgcb) : BalloonButton_PAR(pgcb)
{
}

/***************************************************************************
    Create a new help balloon button
***************************************************************************/
PBalloonButton BalloonButton::PhbtnNew(PWorldOfKidspace pwoks, PGraphicsObject pgobPar, long hid, ChunkNumber cno, PRCA prca, byte bGroup, ChunkNumber cnoTopic, long xpLeft,
                     long ypBottom)
{
    AssertPo(pwoks, 0);
    AssertNilOrPo(pgobPar, 0);
    Assert(hid != hidNil, "nil ID");
    AssertPo(prca, 0);
    GraphicsObjectBlock gcb;
    PBalloonButton phbtn;
    RC rcAbs;

    if (pvNil != pwoks->PcmhFromHid(hid))
    {
        Bug("command handler with this ID already exists");
        return pvNil;
    }

    gcb.Set(hid, pgobPar, fgobNil, kginMark);
    if (pvNil == (phbtn = NewObj BalloonButton(&gcb)))
        return pvNil;

    phbtn->_bGroup = bGroup;
    phbtn->_cnoTopic = cnoTopic;
    if (!phbtn->_FInit(pwoks, cno, prca))
    {
        ReleasePpo(&phbtn);
        return pvNil;
    }

    if (!phbtn->_FEnterState(ksnoInit))
    {
        Warn("KidspaceGraphicObject immediately destroyed!");
        return pvNil;
    }
    phbtn->GetRc(&rcAbs, cooParent);
    rcAbs.Offset(xpLeft - rcAbs.xpLeft, ypBottom - rcAbs.ypBottom);
    phbtn->SetPos(&rcAbs, pvNil);

    AssertPo(phbtn, 0);
    return phbtn;
}

/***************************************************************************
    Test whether the given point is in this button or its related text.
***************************************************************************/
bool BalloonButton::FPtIn(long xp, long yp)
{
    AssertThis(0);
    PTopicGraphicsObject ptxhg;
    PT pt(xp, yp);
    byte bGroup;
    ChunkNumber cnoTopic;

    if (BalloonButton_PAR::FPtIn(xp, yp))
        return fTrue;

    if (_bGroup == 0 || !PgobPar()->FIs(kclsTopicGraphicsObject))
        return fFalse;

    ptxhg = (PTopicGraphicsObject)PgobPar();
    MapPt(&pt, cooLocal, cooParent);

    if (!ptxhg->FGroupFromPt(pt.xp, pt.yp, &bGroup, &cnoTopic))
        return fFalse;
    return bGroup == _bGroup && cnoTopic == _cnoTopic;
}

/***************************************************************************
    The button has been clicked on.  Tell the TopicGraphicsObject to do its thing.
***************************************************************************/
bool BalloonButton::FCmdClicked(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PTopicGraphicsObject ptxhg;
    long hid = Hid();

    if (!PgobPar()->FIs(kclsTopicGraphicsObject))
    {
        Bug("why isn't my parent a TopicGraphicsObject?");
        return fTrue;
    }

    ptxhg = (PTopicGraphicsObject)PgobPar();
    ptxhg->DoHit(_bGroup, _cnoTopic, pcmd->grfcust, hid);

    return fTrue;
}

} // end of namespace Help

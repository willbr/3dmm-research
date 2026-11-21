/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    For editing a text file or text stream as a document.  Unlike the edit
    controls in text.h/text.cpp, all the text need not be in memory (this
    uses a FileByteStream) and there can be multiple views on the same text.

***************************************************************************/
#include "frame.h"
#include "clipboard_view_host.h"
#include "mm/text_copy.h"
ASSERTNAME
#include "text_file_host.h"

RTCLASS(TextDocumentByteStream)
RTCLASS(TextDocumentByteStreamDisplay)

/***************************************************************************
    Constructor for a text document.
***************************************************************************/
TextDocumentByteStream::TextDocumentByteStream(PDocumentBase pdocb, ulong grfdoc) : DocumentBase(pdocb, grfdoc)
{
    _pbsf = pvNil;
    _pfil = pvNil;
}

/***************************************************************************
    Destructor for a text document.
***************************************************************************/
TextDocumentByteStream::~TextDocumentByteStream(void)
{
    TextFileDocumentHost host(this, _pbsf, _pfil);
    auto objects = host.Objects();
    mm_text_document_dispose(0, &objects);
}

/***************************************************************************
    Create a new document based on the given text file and or text stream.
***************************************************************************/
PTextDocumentByteStream TextDocumentByteStream::PtxdcNew(PFilename pfni, PFileByteStream pbsf, PDocumentBase pdocb, ulong grfdoc)
{
    AssertNilOrPo(pfni, ffniFile);
    AssertNilOrPo(pbsf, 0);
    AssertNilOrPo(pdocb, 0);
    struct Factory : TextDocumentFactoryHost
    {
        PFilename name;
        PFileByteStream stream;
        PDocumentBase parent;
        ulong flags;
    } host;
    host.name = pfni;
    host.stream = pbsf;
    host.parent = pdocb;
    host.flags = grfdoc;
    const MMTextFactoryServices services = {
        &host,
        [](void *v, void **out) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard([&] {
                *out = NewObj TextDocumentByteStream(h.parent, h.flags);
                return *out != nullptr;
            });
        },
        [](void *v, void *p) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard([&] { return static_cast<PTextDocumentByteStream>(p)->_FInit(h.name, h.stream); });
        },
        [](void *, void *p) { static_cast<PTextDocumentByteStream>(p)->Release(); }, nullptr};
    void *object;
    mm_text_document_create(&object, 0, &services);
    host.Rethrow();
    return static_cast<PTextDocumentByteStream>(object);
}

/***************************************************************************
    Initialize the TextDocumentByteStream.
***************************************************************************/
bool TextDocumentByteStream::_FInit(PFilename pfni, PFileByteStream pbsf)
{
    AssertNilOrPo(pfni, ffniFile);
    AssertNilOrPo(pbsf, 0);

    TextFileDocumentHost host(this, _pbsf, _pfil);
    auto services = host.Base();
    int result = mm_text_base_init(MM_TEXT_RAW, nullptr, pfni, pbsf, 0, &services);
    host.Rethrow();
    if (result > 0)
        AssertThis(0);
    return result > 0;
}

/***************************************************************************
    Create a new TextDocumentByteStreamDisplay to display the TextDocumentByteStream.
***************************************************************************/
PDocumentDisplayGraphicsObject TextDocumentByteStream::PddgNew(PGraphicsObjectBlock pgcb)
{
    AssertThis(0);
    return TextDocumentByteStreamDisplay::PtxddNew(this, pgcb, _pbsf, vpappb->OnnDefFixed(), fontNil, vpappb->DypTextDef());
}

/***************************************************************************
    Get the current Filename for the doc.  Return false if the doc is not
    currently based on an Filename (it's a new doc or an internal one).
***************************************************************************/
bool TextDocumentByteStream::FGetFni(Filename *pfni)
{
    AssertThis(0);
    AssertBasePo(pfni, 0);
    TextFileDocumentHost host(this, _pbsf, _pfil);
    auto services = host.File();
    int result = mm_text_document_name(MM_TEXT_RAW, pfni, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Save the document and optionally set this fni as the current one.
    If the doc is currently based on an Filename, pfni may be nil, indicating
    that this is a normal save (not save as).  If pfni is not nil and
    fSetFni is false, this just writes a copy of the doc but doesn't change
    the doc one bit.
***************************************************************************/
bool TextDocumentByteStream::FSaveToFni(Filename *pfni, bool fSetFni)
{
    AssertThis(0);
    AssertNilOrPo(pfni, ffniFile);
    Filename fallback;
    TextFileDocumentHost host(this, _pbsf, _pfil);
    auto services = host.File();
    int result = mm_text_document_save(MM_TEXT_RAW, nullptr, pfni, &fallback, fSetFni, &services);
    host.Rethrow();
    return result > 0;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a TextDocumentByteStream.
***************************************************************************/
void TextDocumentByteStream::AssertValid(LegacyUlong grf)
{
    TextDocumentByteStream_PAR::AssertValid(0);
    AssertPo(_pbsf, 0);
    AssertNilOrPo(_pfil, 0);
}

/***************************************************************************
    Mark memory for the TextDocumentByteStream.
***************************************************************************/
void TextDocumentByteStream::MarkMem(void)
{
    AssertValid(0);
    TextDocumentByteStream_PAR::MarkMem();
    MarkMemObj(_pbsf);
}
#endif // DEBUG

/***************************************************************************
    Constructor for a text document display gob.
***************************************************************************/
TextDocumentByteStreamDisplay::TextDocumentByteStreamDisplay(PDocumentBase pdocb, PGraphicsObjectBlock pgcb, PFileByteStream pbsf, long onn, ulong grfont, long dypFont) : DocumentDisplayGraphicsObject(pdocb, pgcb)
{
    AssertPo(pbsf, 0);
    Assert(vntl.FValidOnn(onn), "bad onn");
    AssertIn(dypFont, 1, kswMax);

    _pbsf = pbsf;
    _onn = onn;
    _grfont = grfont;
    _dypFont = dypFont;

    // get the _dypLine and _dxpTab values
    RC rc;
    achar ch = kchSpace;
    GraphicsEnvironment gnv(this);

    gnv.SetFont(_onn, _grfont, _dypFont);
    gnv.GetRcFromRgch(&rc, &ch, 1, 0, 0);
    _dypLine = rc.Dyp();
    _dxpTab = LwMul(rc.Dxp(), 4);
}

/***************************************************************************
    Destructor for TextDocumentByteStreamDisplay.
***************************************************************************/
TextDocumentByteStreamDisplay::~TextDocumentByteStreamDisplay(void)
{
    ReleasePpo(&_pglichStarts);
}

/***************************************************************************
    Create a new TextDocumentByteStreamDisplay.
***************************************************************************/
PTextDocumentByteStreamDisplay TextDocumentByteStreamDisplay::PtxddNew(PDocumentBase pdocb, PGraphicsObjectBlock pgcb, PFileByteStream pbsf, long onn, ulong grfont, long dypFont)
{
    PTextDocumentByteStreamDisplay ptxdd;

    if (pvNil == (ptxdd = NewObj TextDocumentByteStreamDisplay(pdocb, pgcb, pbsf, onn, grfont, dypFont)))
        return pvNil;

    if (!ptxdd->_FInit())
    {
        ReleasePpo(&ptxdd);
        return pvNil;
    }
    ptxdd->Activate(fTrue);

    AssertPo(ptxdd, 0);
    return ptxdd;
}

/***************************************************************************
    Initialize the TextDocumentByteStreamDisplay.
***************************************************************************/
bool TextDocumentByteStreamDisplay::_FInit(void)
{
    long ib;

    if (!TextDocumentByteStreamDisplay_PAR::_FInit())
        return fFalse;
    if (pvNil == (_pglichStarts = DynamicArray::PglNew(size(long))))
        return fFalse;

    _pglichStarts->SetMinGrow(20);
    ib = 0;
    if (!_pglichStarts->FPush(&ib))
        return fFalse;
    _Reformat(0);
    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    The TextDocumentByteStreamDisplay has changed sizes, set the _clnDisp.
***************************************************************************/
void TextDocumentByteStreamDisplay::_NewRc(void)
{
    AssertThis(0);
    RC rc;

    GetRc(&rc, cooLocal);
    _clnDisp = LwMax(1, LwDivAway(rc.Dyp(), _dypLine));
    _clnDispWhole = LwMax(1, rc.Dyp() / _dypLine);
    _Reformat(_clnDisp);
    TextDocumentByteStreamDisplay_PAR::_NewRc();
}

/***************************************************************************
    Deactivate the TextDocumentByteStreamDisplay - turn off the selection.
***************************************************************************/
void TextDocumentByteStreamDisplay::_Activate(bool fActive)
{
    AssertThis(0);
    TextDocumentByteStreamDisplay_PAR::_Activate(fActive);
    if (!fActive)
        _SwitchSel(fFalse, fFalse);
}

/***************************************************************************
    Find new line starts starting at lnMin.
***************************************************************************/
void TextDocumentByteStreamDisplay::_Reformat(long lnMin, long *pclnIns, long *pclnDel)
{
    AssertThis(0);
    AssertIn(lnMin, 0, kcbMax);
    long ich, ichT;
    long ln, clnIns, clnDel;

    lnMin = LwMin(lnMin, _pglichStarts->IvMac() - 1);
    ich = *_QichLn(lnMin);
    clnIns = clnDel = 0;
    for (ln = lnMin + 1; ln <= _clnDisp; ln++)
    {
        if (!_FFindNextLineStart(ich, &ich))
        {
            _pglichStarts->FSetIvMac(ln);
            return;
        }

        // delete starts that are before ich
        for (;;)
        {
            if (ln >= _pglichStarts->IvMac())
                break;
            ichT = *_QichLn(ln);
            if (ichT == ich)
                goto LDone;
            if (ichT > ich)
                break;
            _pglichStarts->Delete(ln);
            clnDel++;
        }

        // add the new line start
        if (!_pglichStarts->FInsert(ln, &ich))
        {
            if (ln >= _pglichStarts->IvMac())
            {
                Warn("Reformatting failed");
                return;
            }
            *_QichLn(ln) = ich;
        }
        clnIns++;
    }

LDone:
    // truncate the list of line starts
    if (_pglichStarts->IvMac() > _clnDisp + 1)
        _pglichStarts->FSetIvMac(_clnDisp + 1);
    if (pvNil != pclnIns)
        *pclnIns = clnIns;
    if (pvNil != pclnDel)
        *pclnDel = clnDel;
}

/***************************************************************************
    Find new line starts starting at lnMin.
***************************************************************************/
void TextDocumentByteStreamDisplay::_ReformatEdit(long ichMinEdit, long cchIns, long cchDel, long *plnNew, long *pclnIns, long *pclnDel)
{
    AssertThis(0);
    AssertIn(ichMinEdit, 0, kcbMax);
    AssertIn(cchIns, 0, _pbsf->IbMac() - ichMinEdit + 1);
    AssertIn(cchDel, 0, kcbMax);
    AssertNilOrVarMem(plnNew);
    AssertNilOrVarMem(pclnIns);
    AssertNilOrVarMem(pclnDel);
    long ich;
    long ln, clnDel;

    // if the first displayed character was affected, reset it
    // to a valid line start
    if (FIn(*_QichLn(0) - 1, ichMinEdit, ichMinEdit + cchDel))
    {
        AssertDo(_FFindLineStart(ichMinEdit, &ich), 0);
        *_QichLn(0) = ich;
    }

    // skip unaffected lines
    for (ln = 0; ln < _pglichStarts->IvMac() && *_QichLn(ln) <= ichMinEdit; ln++)
        ;

    clnDel = 0;
    if (cchDel > 0)
    {
        // remove any deleted lines
        while (ln < _pglichStarts->IvMac() && *_QichLn(ln) <= ichMinEdit + cchDel)
        {
            _pglichStarts->Delete(ln);
            clnDel++;
        }
    }

    if (cchIns != cchDel)
    {
        long lnT;

        for (lnT = ln; lnT < _pglichStarts->IvMac(); lnT++)
            *_QichLn(lnT) += cchIns - cchDel;
    }

    _Reformat(LwMax(0, ln - 1), pclnIns);
    _Reformat(_clnDisp);
    if (pvNil != plnNew)
        *plnNew = ln;
    if (pvNil != pclnDel)
        *pclnDel = clnDel;
}

/***************************************************************************
    Fetch a character of the stream through the cache.
***************************************************************************/
bool TextDocumentByteStreamDisplay::_FFetchCh(long ich, achar *pch)
{
    AssertThis(0);
    AssertIn(_ichMinCache, 0, _pbsf->IbMac() + 1);
    AssertIn(_ichLimCache, _ichMinCache, _pbsf->IbMac() + 1);

    if (!FIn(ich, _ichMinCache, _ichLimCache))
    {
        // not a cache hit
        long ichMinCache, ichLimCache;
        long ichLim = _pbsf->IbMac();

        if (!FIn(ich, 0, ichLim))
        {
            TrashVar(pch);
            return fFalse;
        }

        // need to fetch some characters - try to center ich in the new cached data
        ichMinCache = LwMax(0, LwMin(ich - size(_rgchCache) / 2, ichLim - size(_rgchCache)));
        ichLimCache = LwMin(ichLim, ichMinCache + size(_rgchCache));
        AssertIn(ich, ichMinCache, ichLimCache);

        // see if we can use some of the currently cached characters
        if (_ichMinCache >= _ichLimCache)
            goto LFetchAll;
        if (FIn(_ichMinCache, ich, ichLimCache))
        {
            if (ichLimCache > _ichLimCache)
            {
                ichLimCache = _ichLimCache;
                ichMinCache = LwMax(0, ichLimCache - size(_rgchCache));
            }
            BltPb(_rgchCache, _rgchCache + (_ichMinCache - ichMinCache), ichLimCache - _ichMinCache);
            _pbsf->FetchRgb(ichMinCache, _ichMinCache - ichMinCache, _rgchCache);
        }
        else if (FIn(_ichLimCache, ichMinCache, ich + 1))
        {
            if (ichMinCache < _ichMinCache)
            {
                ichMinCache = _ichMinCache;
                ichLimCache = LwMin(ichLim, ichMinCache + size(_rgchCache));
            }
            BltPb(_rgchCache + (ichMinCache - _ichMinCache), _rgchCache, _ichLimCache - ichMinCache);
            _pbsf->FetchRgb(_ichLimCache, ichLimCache - _ichLimCache, _rgchCache + (_ichLimCache - ichMinCache));
        }
        else
        {
        LFetchAll:
            _pbsf->FetchRgb(ichMinCache, ichLimCache - ichMinCache, _rgchCache);
        }
        _ichMinCache = ichMinCache;
        _ichLimCache = ichLimCache;
        AssertIn(ich, _ichMinCache, _ichLimCache);
    }

    *pch = _rgchCache[ich - _ichMinCache];
    return fTrue;
}

/***************************************************************************
    Find the start of the line that ich is on.
***************************************************************************/
bool TextDocumentByteStreamDisplay::_FFindLineStart(long ich, long *pich)
{
    AssertThis(0);
    AssertVarMem(pich);
    achar ch;
    long dichLine = 0;
    long ichOrig = ich;

    if (ich > 0 && _FFetchCh(ich, &ch) && ch == kchLineFeed)
        dichLine++;

    for (;;)
    {
        if (!_FFetchCh(--ich, &ch))
        {
            if (ich == -1)
            {
                *pich = 0;
                return fTrue;
            }
            TrashVar(pich);
            return fFalse;
        }

        switch (ch)
        {
        case kchLineFeed:
            dichLine++;
            break;
        case kchReturn:
            if ((*pich = ich + 1 + dichLine) <= ichOrig)
                return fTrue;
            // fall through
        default:
            dichLine = 0;
            break;
        }
    }
    Bug("How did we get here?");
}

/***************************************************************************
    Find the next line start after ich.  If prgch is not nil, fills it
    with the characters between ich and the line start (but not more
    than cchMax characters).
***************************************************************************/
bool TextDocumentByteStreamDisplay::_FFindNextLineStart(long ich, long *pich, achar *prgch, long cchMax)
{
    AssertThis(0);
    AssertVarMem(pich);
    AssertIn(cchMax, 0, kcbMax);
    achar ch;
    bool fCr = fFalse;

    if (pvNil != prgch)
        AssertPvCb(prgch, cchMax);
    else
        cchMax = 0;

    for (;; ich++)
    {
        if (!_FFetchCh(ich, &ch))
        {
            if (fCr && ich == _pbsf->IbMac())
            {
                *pich = ich;
                return fTrue;
            }
            TrashVar(pich);
            return fFalse;
        }

        switch (ch)
        {
        case kchLineFeed:
            break;
        case kchReturn:
            if (!fCr)
            {
                fCr = fTrue;
                break;
            }
            // fall through
        default:
            if (fCr)
            {
                *pich = ich;
                return fTrue;
            }
            break;
        }

        if (cchMax > 0)
        {
            *prgch++ = ch;
            cchMax--;
        }
    }
    Bug("how did we get here?");
}

/***************************************************************************
    Find the start of the line that ich is on.  This routine assumes
    that _pglichStarts is valid and tries to use it.
***************************************************************************/
bool TextDocumentByteStreamDisplay::_FFindLineStartCached(long ich, long *pich)
{
    AssertThis(0);
    long ln = _LnFromIch(ich);

    if (FIn(ln, 0, _clnDisp))
    {
        *pich = *_QichLn(ln);
        return fTrue;
    }
    return _FFindLineStart(ich, pich);
}

/***************************************************************************
    Find the next line start after ich.  If prgch is not nil, fills it
    with the characters between ich and the line start (but not more
    than cchMax characters).
***************************************************************************/
bool TextDocumentByteStreamDisplay::_FFindNextLineStartCached(long ich, long *pich, achar *prgch, long cchMax)
{
    AssertThis(0);
    if (pvNil == prgch || cchMax == 0)
    {
        long ln = _LnFromIch(ich);

        if (FIn(ln, 0, _pglichStarts->IvMac() - 1))
        {
            *pich = *_QichLn(ln + 1);
            return fTrue;
        }
    }

    return _FFindNextLineStart(ich, pich, prgch, cchMax);
}

/***************************************************************************
    Draw the contents of the gob.
***************************************************************************/
void TextDocumentByteStreamDisplay::Draw(PGraphicsEnvironment pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);
    RC rc;
    long yp;
    long ln, lnLim;
    long cchDraw;
    achar rgch[kcchMaxLine];

    GetRc(&rc, cooLocal);
    if (!rc.FIntersect(prcClip))
        return;

    pgnv->SetFont(_onn, _grfont, _dypFont);
    ln = rc.ypTop / _dypLine;
    yp = LwMul(ln, _dypLine);
    lnLim = LwMin(_pglichStarts->IvMac(), LwDivAway(rc.ypBottom, _dypLine));
    for (; ln < lnLim; yp += _dypLine)
    {
        _FetchLineLn(ln++, rgch, size(rgch), &cchDraw);
        _DrawLine(pgnv, prcClip, yp, rgch, cchDraw);
    }
    rc.ypTop = yp;
    pgnv->FillRc(&rc, kacrWhite);
    if (_fSelOn)
        _InvertSel(pgnv, fTrue);
}

/***************************************************************************
    Fetch the characters for the given line.
***************************************************************************/
void TextDocumentByteStreamDisplay::_FetchLineLn(long ln, achar *prgch, long cchMax, long *pcch, long *pichMin)
{
    AssertThis(0);
    long ichMin, ichLim;

    ichMin = _IchMinLn(ln);
    ichLim = _IchMinLn(ln + 1);
    *pcch = LwMin(cchMax, ichLim - ichMin);
    _pbsf->FetchRgb(ichMin, *pcch, prgch);
    if (pvNil != pichMin)
        *pichMin = ichMin;
}

/***************************************************************************
    Fetch the characters for the given line.
***************************************************************************/
void TextDocumentByteStreamDisplay::_FetchLineIch(long ich, achar *prgch, long cchMax, long *pcch, long *pichMin)
{
    AssertThis(0);
    long ichMin, ichLim;

    AssertDo(_FFindLineStartCached(ich, &ichMin), 0);
    if (!_FFindNextLineStartCached(ichMin, &ichLim, prgch, cchMax))
        ichLim = _pbsf->IbMac();
    *pcch = LwMin(cchMax, ichLim - ichMin);
    if (pvNil != pichMin)
        *pichMin = ichMin;
}

/***************************************************************************
    Draw the line in the given GraphicsEnvironment.
***************************************************************************/
void TextDocumentByteStreamDisplay::_DrawLine(PGraphicsEnvironment pgnv, RC *prcClip, long yp, achar *prgch, long cch)
{
    AssertThis(0);
    long xp, xpOrigin, xpPrev;
    long ich, ichMin;
    RC rc;

    xpPrev = 0;
    xp = xpOrigin = kdxpIndentTxdd - _scvHorz;
    ich = 0;
    while (ich < cch)
    {
        while (ich < cch)
        {
            switch (prgch[ich])
            {
            case kchTab:
                xp = xpOrigin + LwRoundAway(xp - xpOrigin + 1, _dxpTab);
                break;
            case kchReturn:
            case kchLineFeed:
                break;
            default:
                goto LNonWhite;
            }
            ich++;
        }

    LNonWhite:
        // erase any blank portion of the line
        if (xp > xpPrev && xp > prcClip->xpLeft && xpPrev < prcClip->xpRight)
        {
            rc.Set(xpPrev, yp, xp, yp + _dypLine);
            pgnv->FillRc(&rc, kacrWhite);
        }

        ichMin = ich;
        while (ich < cch)
        {
            switch (prgch[ich])
            {
            case kchTab:
            case kchReturn:
            case kchLineFeed:
                goto LEndRun;
            }
            ich++;
        }

    LEndRun:
        if (ich > ichMin)
        {
            pgnv->GetRcFromRgch(&rc, prgch + ichMin, ich - ichMin);
            if (xp + rc.Dxp() > prcClip->xpLeft && xp < prcClip->xpRight)
                pgnv->DrawRgch(prgch + ichMin, ich - ichMin, xp, yp, kacrBlack, kacrWhite);
            xp += rc.Dxp();
        }
        xpPrev = xp;
    }

    // erase any remaining portion of the line
    if (xpPrev < prcClip->xpRight)
    {
        rc.Set(xpPrev, yp, prcClip->xpRight, yp + _dypLine);
        pgnv->FillRc(&rc, kacrWhite);
    }
}

/***************************************************************************
    Return the maximum scroll value for this view of the doc.
***************************************************************************/
long TextDocumentByteStreamDisplay::_ScvMax(bool fVert)
{
    if (fVert)
    {
        long ich;

        if (!_FFindLineStartCached(_pbsf->IbMac() - 1, &ich))
            ich = 0;
        return ich;
    }

    return LwMul(_dxpTab, 100);
}

/***************************************************************************
    Perform a scroll according to scaHorz and scaVert.
***************************************************************************/
void TextDocumentByteStreamDisplay::_Scroll(long scaHorz, long scaVert, long scvHorz, long scvVert)
{
    AssertThis(0);
    RC rc;
    long dxp, dyp;

    GetRc(&rc, cooLocal);
    dxp = 0;
    switch (scaHorz)
    {
    default:
        Assert(scaHorz == scaNil, "bad scaHorz");
        break;
    case scaPageUp:
        dxp = -LwMulDiv(rc.Dxp(), 9, 10);
        goto LHorz;
    case scaPageDown:
        dxp = LwMulDiv(rc.Dxp(), 9, 10);
        goto LHorz;
    case scaLineUp:
        dxp = -rc.Dxp() / 10;
        goto LHorz;
    case scaLineDown:
        dxp = rc.Dxp() / 10;
        goto LHorz;
    case scaToVal:
        dxp = scvHorz - _scvHorz;
    LHorz:
        dxp = LwBound(_scvHorz + dxp, 0, _ScvMax(fFalse) + 1) - _scvHorz;
        _scvHorz += dxp;
        break;
    }

    dyp = 0;
    if (scaVert != scaNil)
    {
        long ichMin;
        long ichMinNew;
        long cln;
        long ichT;

        ichMin = *_QichLn(0);
        switch (scaVert)
        {
        default:
            Bug("bad scaVert");
            ichMinNew = ichMin;
            break;
        case scaToVal:
            scvVert = LwBound(scvVert, 0, _ScvMax(fTrue) + 1);
            AssertDo(_FFindLineStartCached(scvVert, &ichMinNew), 0);

            for (cln = 0, ichT = LwMin(ichMin, ichMinNew);
                 cln < _clnDisp && _FFindNextLineStartCached(ichT, &ichT) && (ichT <= ichMin || ichT <= ichMinNew);)
            {
                cln++;
            }

            if (ichMin > ichMinNew)
                cln = -cln;
            dyp = LwMul(cln, _dypLine);
            break;

        case scaPageDown:
            cln = LwMax(1, _clnDispWhole - 1);
            goto LDown;
        case scaLineDown:
            cln = 1;
        LDown:
            cln = LwMin(cln, _pglichStarts->IvMac() - 1);
            dyp = LwMul(cln, _dypLine);
            ichMinNew = *_QichLn(cln);
            break;

        case scaPageUp:
            cln = LwMax(1, _clnDispWhole - 1);
            goto LUp;
        case scaLineUp:
            cln = 1;
        LUp:
            ichMinNew = ichMin;
            while (cln-- > 0 && _FFindLineStart(ichMinNew - 1, &ichT))
            {
                dyp -= _dypLine;
                ichMinNew = ichT;
            }
            break;
        }
        _scvVert = ichMinNew;
        if (ichMinNew != ichMin)
        {
            *_QichLn(0) = ichMinNew;
            _Reformat(0);
            _Reformat(_clnDisp);
        }
    }

    _SetScrollValues();
    if (dxp != 0 || dyp != 0)
        _ScrollDxpDyp(dxp, dyp);
}

/***************************************************************************
    Do idle processing.  If this handler has the active selection, make sure
    the selection is on or off according to rglw[0] (non-zero means on)
    and set rglw[0] to false.  Always return false.
***************************************************************************/
bool TextDocumentByteStreamDisplay::FCmdSelIdle(PCommand pcmd)
{
    AssertThis(0);

    // if rglw[1] is this one's hid, don't change the sel state.
    if (pcmd->rglw[1] != Hid())
    {
        if (!pcmd->rglw[0])
            _SwitchSel(fFalse, fFalse);
        else if (_ichAnchor != _ichOther || _tsSel == 0)
            _SwitchSel(fTrue, fTrue);
        else if (DtsCaret() < TsCurrent() - _tsSel)
            _SwitchSel(!_fSelOn, fTrue);
    }
    pcmd->rglw[0] = fFalse;
    return fFalse;
}

/***************************************************************************
    Set the selection.
***************************************************************************/
void TextDocumentByteStreamDisplay::SetSel(long ichAnchor, long ichOther, bool fDraw)
{
    AssertThis(0);
    long ichMac = _pbsf->IbMac();

    ichAnchor = LwBound(ichAnchor, 0, ichMac + 1);
    ichOther = LwBound(ichOther, 0, ichMac + 1);

    if (ichAnchor == _ichAnchor && ichOther == _ichOther)
        return;

    if (_fSelOn)
    {
        GraphicsEnvironment gnv(this);

        if (_ichAnchor != ichAnchor || _ichAnchor == _ichOther || ichAnchor == ichOther)
        {
            _InvertSel(&gnv, fDraw);
            _ichAnchor = ichAnchor;
            _ichOther = ichOther;
            _InvertSel(&gnv, fDraw);
            _tsSel = TsCurrent();
        }
        else
        {
            // they have the same anchor and neither is an insertion
            _InvertIchRange(&gnv, _ichOther, ichOther, fDraw);
            _ichOther = ichOther;
        }
    }
    else
    {
        _ichAnchor = ichAnchor;
        _ichOther = ichOther;
        _tsSel = 0L;
    }
}

/***************************************************************************
    Turn the sel on or off according to fOn.
***************************************************************************/
void TextDocumentByteStreamDisplay::_SwitchSel(bool fOn, bool fDraw)
{
    AssertThis(0);
    if (FPure(fOn) != FPure(_fSelOn))
    {
        GraphicsEnvironment gnv(this);

        _InvertSel(&gnv, fDraw);
        _fSelOn = FPure(fOn);
        _tsSel = TsCurrent();
    }
}

/***************************************************************************
    Invert the current selection.
***************************************************************************/
void TextDocumentByteStreamDisplay::_InvertSel(PGraphicsEnvironment pgnv, bool fDraw)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    RC rc, rcT;
    long ln;

    if (_ichAnchor == _ichOther)
    {
        // insertion bar
        ln = _LnFromIch(_ichAnchor);
        if (!FIn(ln, 0, _clnDisp))
            return;

        rc.xpLeft = _XpFromLnIch(pgnv, ln, _ichAnchor) - 1;
        rc.xpRight = rc.xpLeft + 2;
        rc.ypTop = LwMul(ln, _dypLine);
        rc.ypBottom = rc.ypTop + _dypLine;
        GetRc(&rcT, cooLocal);
        if (rcT.FIntersect(&rc))
        {
            if (fDraw)
                pgnv->FillRc(&rcT, kacrInvert);
            else
                InvalRc(&rcT, kginMark);
        }
    }
    else
        _InvertIchRange(pgnv, _ichAnchor, _ichOther, fDraw);
}

/***************************************************************************
    Invert a range.
***************************************************************************/
void TextDocumentByteStreamDisplay::_InvertIchRange(PGraphicsEnvironment pgnv, long ich1, long ich2, bool fDraw)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertIn(ich1, 0, _pbsf->IbMac() + 1);
    AssertIn(ich2, 0, _pbsf->IbMac() + 1);
    RC rc, rcClip, rcT;
    long ln1, ln2, xp2;

    if (ich1 == ich2)
        return;
    SortLw(&ich1, &ich2);
    ln1 = _LnFromIch(ich1);
    ln2 = _LnFromIch(ich2);
    if (ln1 >= _clnDisp || ln2 < 0)
        return;

    GetRc(&rcClip, cooLocal);
    rc.xpLeft = _XpFromLnIch(pgnv, ln1, ich1);
    rc.ypTop = LwMul(ln1, _dypLine);
    rc.ypBottom = LwMul(ln1 + 1, _dypLine);
    xp2 = _XpFromLnIch(pgnv, ln2, ich2);

    if (ln2 == ln1)
    {
        // only one line involved
        rc.xpRight = xp2;
        if (rcT.FIntersect(&rc, &rcClip))
        {
            if (fDraw)
                pgnv->HiliteRc(&rcT, kacrWhite);
            else
                InvalRc(&rcT);
        }
        return;
    }

    // invert the sel on the first line
    rc.xpRight = rcClip.xpRight;
    if (rcT.FIntersect(&rc, &rcClip))
    {
        if (fDraw)
            pgnv->HiliteRc(&rcT, kacrWhite);
        else
            InvalRc(&rcT);
    }

    // invert the main rectangular block
    rc.xpLeft = kdxpIndentTxdd - _scvHorz;
    rc.ypTop = rc.ypBottom;
    rc.ypBottom = LwMul(ln2, _dypLine);
    if (rcT.FIntersect(&rc, &rcClip))
    {
        if (fDraw)
            pgnv->HiliteRc(&rcT, kacrWhite);
        else
            InvalRc(&rcT);
    }

    // invert the last line
    rc.ypTop = rc.ypBottom;
    rc.ypBottom = LwMul(ln2 + 1, _dypLine);
    rc.xpRight = xp2;
    if (rcT.FIntersect(&rc, &rcClip))
    {
        if (fDraw)
            pgnv->HiliteRc(&rcT, kacrWhite);
        else
            InvalRc(&rcT);
    }
}

/***************************************************************************
    Find the line in the TextDocumentByteStreamDisplay that is displaying the given ich.  Returns -1
    if the ich is before the first displayed ich and returns _clnDisp if
    ich is after the last displayed ich.
***************************************************************************/
long TextDocumentByteStreamDisplay::_LnFromIch(long ich)
{
    AssertThis(0);
    long lnMin, lnLim, ln;
    long ichT;

    lnMin = 0;
    lnLim = LwMin(_clnDisp + 1, _pglichStarts->IvMac());
    while (lnMin < lnLim)
    {
        ln = (lnMin + lnLim) / 2;
        ichT = *_QichLn(ln);
        if (ichT < ich)
            lnMin = ln + 1;
        else if (ichT > ich)
            lnLim = ln;
        else
            return ln;
    }

    return lnMin - 1;
}

/***************************************************************************
    Return the ich of the first character on the given line.  If ln < 0,
    returns 0; if ln >= _clnDisp, returns IbMac().
***************************************************************************/
long TextDocumentByteStreamDisplay::_IchMinLn(long ln)
{
    AssertThis(0);
    long ich;

    if (ln < 0)
        return 0;
    if (ln >= _pglichStarts->IvMac())
        return _pbsf->IbMac();
    ich = *_QichLn(ln);
    return ich;
}

/***************************************************************************
    Return the xp location of the given ich on the given line.
***************************************************************************/
long TextDocumentByteStreamDisplay::_XpFromLnIch(PGraphicsEnvironment pgnv, long ln, long ich)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    RC rc;
    long cch, ichT;
    achar rgch[kcchMaxLine];

    if (!FIn(ln, 0, LwMin(_clnDisp, _pglichStarts->IvMac())))
        return 0;

    ichT = *_QichLn(ln);
    _FetchLineLn(ln, rgch, LwMin(ich - ichT, size(rgch)), &cch);
    return _XpFromRgch(pgnv, rgch, cch);
}

/***************************************************************************
    Return the xp location of the given ich.
***************************************************************************/
long TextDocumentByteStreamDisplay::_XpFromIch(long ich)
{
    AssertThis(0);
    long ichMin, cch;
    achar rgch[kcchMaxLine];

    if (!_FFindLineStartCached(ich, &ichMin))
        return 0;

    GraphicsEnvironment gnv(this);

    cch = LwMin(size(rgch), ich - ichMin);
    _pbsf->FetchRgb(ichMin, cch, rgch);
    return _XpFromRgch(&gnv, rgch, cch);
}

/***************************************************************************
    Return the xp location of the end of the given (rgch, cch), assuming
    it starts at the beginning of a line.
***************************************************************************/
long TextDocumentByteStreamDisplay::_XpFromRgch(PGraphicsEnvironment pgnv, achar *prgch, long cch)
{
    AssertThis(0);
    long xp, xpOrigin;
    long ich, ichMin;
    RC rc;

    pgnv->SetFont(_onn, _grfont, _dypFont);
    xp = xpOrigin = kdxpIndentTxdd - _scvHorz;
    ich = 0;
    while (ich < cch)
    {
        while (ich < cch)
        {
            switch (prgch[ich])
            {
            case kchTab:
                xp = xpOrigin + LwRoundAway(xp - xpOrigin + 1, _dxpTab);
                break;
            case kchReturn:
            case kchLineFeed:
                break;
            default:
                goto LNonWhite;
            }
            ich++;
        }

    LNonWhite:
        ichMin = ich;
        while (ich < cch)
        {
            switch (prgch[ich])
            {
            case kchTab:
            case kchReturn:
            case kchLineFeed:
                goto LEndRun;
            }
            ich++;
        }

    LEndRun:
        if (ich > ichMin)
        {
            pgnv->GetRcFromRgch(&rc, prgch + ichMin, ich - ichMin);
            xp += rc.Dxp();
        }
    }

    return xp;
}

/***************************************************************************
    Find the character that is closest to xp on the given line.
***************************************************************************/
long TextDocumentByteStreamDisplay::_IchFromLnXp(long ln, long xp)
{
    AssertThis(0);
    long ichMin, cch;
    achar rgch[kcchMaxLine];

    if (ln < 0)
        return 0;

    _FetchLineLn(ln, rgch, size(rgch), &cch, &ichMin);
    return _IchFromRgchXp(rgch, cch, ichMin, xp);
}

/***************************************************************************
    Find the character that is closest to xp on the same line as the given
    character.
***************************************************************************/
long TextDocumentByteStreamDisplay::_IchFromIchXp(long ich, long xp)
{
    AssertThis(0);
    long ichMin, cch;
    achar rgch[kcchMaxLine];

    _FetchLineIch(ich, rgch, size(rgch), &cch, &ichMin);
    return _IchFromRgchXp(rgch, cch, ichMin, xp);
}

/***************************************************************************
    Find the character that is closest to xp on the given line.
***************************************************************************/
long TextDocumentByteStreamDisplay::_IchFromRgchXp(achar *prgch, long cch, long ichMinLine, long xp)
{
    AssertThis(0);
    long xpT;
    long ich, ichMin, ichLim;
    GraphicsEnvironment gnv(this);

    while (cch > 0 && (prgch[cch - 1] == kchReturn || prgch[cch - 1] == kchLineFeed))
        cch--;
    ichLim = ichMinLine + cch;
    ichMin = ichMinLine;
    while (ichMin < ichLim)
    {
        ich = (ichMin + ichLim) / 2;
        xpT = _XpFromRgch(&gnv, prgch, ich - ichMinLine);
        if (xpT < xp)
            ichMin = ich + 1;
        else
            ichLim = ich;
    }
    if (ichMin > ichMinLine && LwAbs(xp - _XpFromRgch(&gnv, prgch, ichMin - 1 - ichMinLine)) <
                                   LwAbs(xp - _XpFromRgch(&gnv, prgch, ichMin - ichMinLine)))
    {
        ichMin--;
    }
    return ichMin;
}

/***************************************************************************
    Make sure the selection is visible (or at least _ichOther is).
***************************************************************************/
void TextDocumentByteStreamDisplay::ShowSel(bool fDraw)
{
    AssertThis(0);
    long ln, lnHope;
    long dxpScroll, dichScroll;
    long xpMin, xpLim;
    RC rc;
    long ichAnchor = _ichAnchor;

    // find the lines we want to show
    ln = _LnFromIch(_ichOther);
    lnHope = _LnFromIch(ichAnchor);
    GetRc(&rc, cooLocal);

    dichScroll = 0;
    if (!FIn(ln, 0, _clnDispWhole) || !FIn(lnHope, 0, _clnDispWhole))
    {
        // count the number of lines between _ichOther and ichAnchor
        long ichMinLine, ich;
        long ichMin = LwMin(ichAnchor, _ichOther);
        long ichLim = LwMax(ichAnchor, _ichOther);
        long cln = 0;

        AssertDo(_FFindLineStartCached(ichMin, &ichMinLine), 0);
        for (ich = ichMin; cln < _clnDispWhole && _FFindNextLineStartCached(ich, &ich) && ich < ichLim; cln++)
        {
        }

        if (cln >= _clnDispWhole)
        {
            // just show _ichOther
            AssertDo(_FFindLineStartCached(_ichOther, &ichMinLine), 0);
            ichAnchor = _ichOther;
            lnHope = ln;
            cln = 0;
        }

        if (ln < 0 || lnHope < 0)
        {
            // scroll up
            dichScroll = ichMinLine - _scvVert;
        }
        else if (ln >= _clnDispWhole || lnHope >= _clnDispWhole)
        {
            // scroll down
            cln = LwMax(0, _clnDispWhole - cln - 1);

            // move cln lines back from ichMinLine
            while (cln-- > 0 && _FFindLineStartCached(ichMinLine - 1, &ichMin))
                ichMinLine = ichMin;
            dichScroll = ichMinLine - _scvVert;
        }
    }

    // now do the horizontal stuff
    xpMin = _XpFromIch(_ichOther);
    xpLim = _XpFromIch(ichAnchor);
    if (LwAbs(xpLim - xpMin) > rc.Dxp())
    {
        // can't show both
        if (xpMin > xpLim)
        {
            xpLim = xpMin;
            xpMin = xpLim - rc.Dxp();
        }
        else
            xpLim = xpMin + rc.Dxp();
    }
    else
        SortLw(&xpMin, &xpLim);
    dxpScroll = LwMax(LwMin(0, rc.xpRight - xpLim), rc.xpLeft - xpMin);

    if (dxpScroll != 0 || dichScroll != 0)
    {
        _Scroll(scaToVal, scaToVal, _scvHorz - dxpScroll, _scvVert + dichScroll);
    }
}

/***************************************************************************
    Handle a mousedown in the TextDocumentByteStreamDisplay.
***************************************************************************/
bool TextDocumentByteStreamDisplay::FCmdTrackMouse(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    RC rc;
    long ich;
    long scaHorz, scaVert;
    long xp = pcmd->xp;
    long yp = pcmd->yp;

    if (pcmd->cid == cidMouseDown)
    {
        Assert(vpcex->PgobTracking() == pvNil, "mouse already being tracked!");
        vpcex->TrackMouse(this);
    }
    else
    {
        Assert(vpcex->PgobTracking() == this, "not tracking mouse!");
        Assert(pcmd->cid == cidTrackMouse, 0);
    }

    // do autoscrolling
    GetRc(&rc, cooLocal);
    if (!FIn(xp, rc.xpLeft, rc.xpRight))
    {
        scaHorz = (xp < rc.xpLeft) ? scaLineUp : scaLineDown;
        xp = LwBound(xp, rc.xpLeft, rc.xpRight);
    }
    else
        scaHorz = scaNil;
    if (!FIn(yp, rc.ypTop, rc.ypBottom))
    {
        scaVert = (yp < rc.ypTop) ? scaLineUp : scaLineDown;
        yp = LwBound(yp, rc.ypTop, rc.ypBottom);
    }
    else
        scaVert = scaNil;
    if (scaHorz != scaNil || scaVert != scaNil)
        _Scroll(scaHorz, scaVert);

    // set the selection
    ich = _IchFromLnXp(yp / _dypLine, xp);
    if (pcmd->cid != cidMouseDown || (pcmd->grfcust & fcustShift))
        SetSel(_ichAnchor, ich, fTrue);
    else
        SetSel(ich, ich, fTrue);
    _SwitchSel(fTrue, fTrue); // make sure the selection is on
    ShowSel(fTrue);

    if (!(pcmd->grfcust & fcustMouse))
        vpcex->EndMouseTracking();

    return fTrue;
}

/***************************************************************************
    Handle a key down.
***************************************************************************/
bool TextDocumentByteStreamDisplay::FCmdKey(PCMD_KEY pcmd)
{
    AssertThis(0);
    const long kcchInsBuf = 64;
    AssertThis(0);
    AssertVarMem(pcmd);
    ulong grfcust;
    long vkDone;
    long dich, dln, ichLim, ichT, ichMin;
    achar ch;
    long cact;
    Command cmd;
    achar rgch[kcchInsBuf + 1];

    // keep fetching characters until we get a cursor key, delete key or
    // until the buffer is full.
    vkDone = vkNil;
    ichLim = 0;
    do
    {
        grfcust = pcmd->grfcust;
        switch (pcmd->vk)
        {
        // these keys all terminate the key fetching loop
        case kvkHome:
        case kvkEnd:
        case kvkLeft:
        case kvkRight:
        case kvkUp:
        case kvkDown:
        case kvkPageUp:
        case kvkPageDown:
        case kvkDelete:
        case kvkBack:
            vkDone = pcmd->vk;
            goto LInsert;

        default:
            if (chNil == pcmd->ch)
                break;
            for (cact = 0; cact < pcmd->cact && ichLim < kcchInsBuf; cact++)
            {
                rgch[ichLim++] = (achar)pcmd->ch;
#ifdef WIN
                if ((achar)pcmd->ch == kchReturn)
                    rgch[ichLim++] = kchLineFeed;
#endif // WIN
            }
            break;
        }

        pcmd = (PCMD_KEY)&cmd;
    } while (ichLim < kcchInsBuf && vpcex->FGetNextKey(&cmd));

LInsert:
    if (ichLim > 0)
    {
        // have some characters to insert
        FReplace(rgch, ichLim, _ichAnchor, _ichOther, fTrue);
    }

    dich = 0;
    dln = 0;
    switch (vkDone)
    {
    case kvkHome:
        if (grfcust & fcustCmd)
            dich = -_pbsf->IbMac() - ichLim - 1;
        else if (_FFindLineStartCached(_ichOther, &ichT))
            dich = ichT - _ichOther;
        _fXpValid = fFalse;
        goto LSetSel;
    case kvkEnd:
        if (grfcust & fcustCmd)
            dich = _pbsf->IbMac() + ichLim + 1;
        else
        {
            if (!_FFindNextLineStartCached(_ichOther, &ichT))
                ichT = _pbsf->IbMac();

            // don't advance past trailing line feed and return characters
            while (ichT > _ichOther && _FFetchCh(ichT - 1, &ch) && (ch == kchReturn || ch == kchLineFeed))
            {
                ichT--;
            }
            dich = ichT - _ichOther;
        }
        _fXpValid = fFalse;
        goto LSetSel;
    case kvkLeft:
        dich = -1;
        while (_FFetchCh(_ichOther + dich, &ch) && ch == kchLineFeed)
            dich--;
        _fXpValid = fFalse;
        goto LSetSel;
    case kvkRight:
        dich = 1;
        while (_FFetchCh(_ichOther + dich, &ch) && ch == kchLineFeed)
            dich++;
        _fXpValid = fFalse;
        goto LSetSel;

    case kvkUp:
        dln = -1;
        goto LLineSel;
    case kvkDown:
        dln = 1;
        goto LLineSel;
    case kvkPageUp:
        dln = -LwMax(1, _clnDispWhole - 1);
        goto LLineSel;
    case kvkPageDown:
        dln = LwMax(1, _clnDispWhole - 1);
    LLineSel:
        if (!_fXpValid)
        {
            // get the xp of _ichOther
            _xpSel = _XpFromIch(_ichOther) + _scvHorz;
            _fXpValid = fTrue;
        }
        if (dln > 0)
        {
            ichMin = _ichOther;
            while (dln-- > 0 && _FFindNextLineStartCached(ichMin, &ichT))
                ichMin = ichT;
            if (dln >= 0)
            {
                // goto end of doc
                dich = _pbsf->IbMac() - _ichOther;
                _fXpValid = fFalse;
            }
            else
                goto LFindIch;
        }
        else
        {
            AssertDo(_FFindLineStartCached(_ichOther, &ichT), 0);
            ichMin = ichT;
            while (dln++ < 0 && _FFindLineStartCached(ichMin - 1, &ichT))
                ichMin = ichT;
            if (dln <= 0)
            {
                // goto top of doc
                dich = -_ichOther;
                _fXpValid = fFalse;
            }
            else
            {
            LFindIch:
                // ichMin is the start of the line to move the selection to
                dich = _IchFromIchXp(ichMin, _xpSel - _scvHorz) - _ichOther;
            }
        }
    LSetSel:
        // move the selection
        if (grfcust & fcustShift)
        {
            // extend selection
            SetSel(_ichAnchor, _ichOther + dich, fTrue);
            ShowSel(fTrue);
        }
        else
        {
            long ichAnchor = _ichAnchor;

            if (ichAnchor == _ichOther)
                ichAnchor += dich;
            else if ((dich > 0) != (ichAnchor > _ichOther))
                ichAnchor = _ichOther;
            SetSel(ichAnchor, ichAnchor, fTrue);
            ShowSel(fTrue);
        }
        break;

    case kvkDelete:
        dich = 1;
        goto LDelete;
    case kvkBack:
        dich = -1;
    LDelete:
        if (_ichAnchor != _ichOther)
            dich = _ichOther - _ichAnchor;
        else
            dich = LwBound(_ichAnchor + dich, 0, _pbsf->IbMac() + 1) - _ichAnchor;
        if (dich != 0)
            FReplace(pvNil, 0, _ichAnchor, _ichAnchor + dich, fTrue);
        break;
    }

    return fTrue;
}

/***************************************************************************
    Replaces the characters between ich1 and ich2 with the given ones.
***************************************************************************/
bool TextDocumentByteStreamDisplay::FReplace(achar *prgch, long cch, long ich1, long ich2, bool fDraw)
{
    AssertThis(0);
    _SwitchSel(fFalse, fTrue);
    SortLw(&ich1, &ich2);
    if (!_pbsf->FReplace(prgch, cch, ich1, ich2 - ich1))
        return fFalse;

    _InvalAllTxdd(ich1, cch, ich2 - ich1);
    ich1 += cch;
    SetSel(ich1, ich1, fTrue);
    ShowSel(fTrue);
    return fTrue;
}

/***************************************************************************
    Invalidate all TextDocumentByteStreamDisplays on this text doc.  Also dirties the document.
    Should be called by any code that edits the document.
***************************************************************************/
void TextDocumentByteStreamDisplay::_InvalAllTxdd(long ich, long cchIns, long cchDel)
{
    AssertThis(0);
    long ipddg;
    PDocumentDisplayGraphicsObject pddg;

    // mark the document dirty
    _pdocb->SetDirty();

    // inform the TextDocumentByteStreamDisplays
    for (ipddg = 0; pvNil != (pddg = _pdocb->PddgGet(ipddg)); ipddg++)
    {
        if (pddg->FIs(kclsTextDocumentByteStreamDisplay))
            ((PTextDocumentByteStreamDisplay)pddg)->_InvalIch(ich, cchIns, cchDel);
    }
}

/***************************************************************************
    Invalidate the display from ich.  If we're the active TextDocumentByteStreamDisplay, also redraw.
***************************************************************************/
void TextDocumentByteStreamDisplay::_InvalIch(long ich, long cchIns, long cchDel)
{
    AssertThis(0);
    Assert(!_fSelOn, "why is the sel on during an invalidation?");
    RC rcLoc, rc;
    LegacyLong ichAnchor, ichOther;
    long lnNew, clnIns, clnDel;
    long yp, dypIns, dypDel;

    // adjust the sel
    ichAnchor = _ichAnchor;
    ichOther = _ichOther;
    FAdjustIv(&ichAnchor, ich, cchIns, cchDel);
    FAdjustIv(&ichOther, ich, cchIns, cchDel);
    if (ichAnchor != _ichAnchor || ichOther != _ichOther)
        SetSel(ichAnchor, ichOther, fFalse);

    // adjust the cache
    if (_ichLimCache > _ichMinCache)
    {
        if (FPure(_ichLimCache <= ich) != FPure(_ichMinCache <= ich) ||
            FPure(_ichLimCache >= ich + cchDel) != FPure(_ichMinCache >= ich + cchDel) ||
            !FAdjustIv(&_ichLimCache, ich, cchIns, cchDel) || !FAdjustIv(&_ichMinCache, ich, cchIns, cchDel))
        {
            _ichMinCache = _ichLimCache = 0;
        }
    }

    // reformat
    _ReformatEdit(ich, cchIns, cchDel, &lnNew, &clnIns, &clnDel);
    if (lnNew > 0)
    {
        lnNew--;
        clnIns++;
        clnDel++;
    }

    // determine the dirty rectangles and if we're active, update them
    GetRc(&rcLoc, cooLocal);
    if (!_fActive)
    {
        rc = rcLoc;
        rc.ypTop = LwMul(lnNew, _dypLine);
        if (clnIns == clnDel)
            rc.ypBottom = LwMul(lnNew + clnIns, _dypLine);
        InvalRc(&rc);
        return;
    }

    dypIns = LwMul(clnIns, _dypLine);
    dypDel = LwMul(clnDel, _dypLine);
    yp = LwMul(lnNew, _dypLine);
    rc = rcLoc;
    rc.ypTop = yp;
    rc.ypBottom = yp + LwMin(dypIns, dypDel);
    if (_clnDisp > lnNew + clnIns - clnDel && clnIns != clnDel)
    {
        // have some bits to blt vertically
        rc = rcLoc;
        rc.ypTop = yp + LwMin(dypIns, dypDel);
        Scroll(&rc, 0, dypIns - dypDel, kginDraw);
        rc.ypBottom = rc.ypTop;
        rc.ypTop = yp;
    }
    if (!rc.FEmpty())
        InvalRc(&rc, kginDraw);

    _fXpValid = fFalse;
}

struct TextDocumentByteStreamDisplay::ClipboardHost : ClipboardViewHost
{
    PTextDocumentByteStreamDisplay view;
    PClipboardObject clipboard = nullptr;
    PDocumentBase *output = nullptr;
    PFileByteStream source = nullptr;

    explicit ClipboardHost(PTextDocumentByteStreamDisplay view) : view(view) {}

    MMTextClipboardServices Services() { return {this, MM_TEXT_CLIPBOARD_STREAM, Call}; }

    static int Call(void *context, MMTextClipboardOperation operation, MMTextClipboardRequest *q)
    {
        auto &host = *static_cast<ClipboardHost *>(context);
        auto view = host.view;
        auto document = static_cast<PDocumentBase>(q->document);
        return host.Guard([&]() -> int {
            switch (operation)
            {
            case MM_TEXT_CLIPBOARD_SELECTION:
                q->anchor = view->_ichAnchor;
                q->focus = view->_ichOther;
                break;
            case MM_TEXT_CLIPBOARD_ALLOCATE:
                q->document = static_cast<PDocumentBase>(TextDocumentByteStream::PtxdcNew());
                return q->document != nullptr;
            case MM_TEXT_CLIPBOARD_COPY:
                return static_cast<PTextDocumentByteStream>(document)->Pbsf()->FReplaceBsf(
                    view->_pbsf, q->first, q->inserted, 0, 0);
            case MM_TEXT_CLIPBOARD_PUBLISH: *host.output = document; break;
            case MM_TEXT_CLIPBOARD_RELEASE: document->Release(); break;
            case MM_TEXT_CLIPBOARD_CLEAR: view->FReplace(pvNil, 0, q->anchor, q->focus, fTrue); break;
            case MM_TEXT_CLIPBOARD_HAS_FORMAT: return Format(host.clipboard, kclsTextDocumentByteStream, nullptr);
            case MM_TEXT_CLIPBOARD_ACQUIRE: return Format(host.clipboard, kclsTextDocumentByteStream, &q->document);
            case MM_TEXT_CLIPBOARD_SIZE:
                host.source = static_cast<PTextDocumentByteStream>(document)->Pbsf();
                q->inserted = host.source ? host.source->IbMac() : 0;
                break;
            case MM_TEXT_CLIPBOARD_HIDE: view->_SwitchSel(fFalse, fTrue); break;
            case MM_TEXT_CLIPBOARD_PASTE_PLAIN:
                return view->_pbsf->FReplaceBsf(host.source, 0, q->inserted, q->first, q->deleted);
            case MM_TEXT_CLIPBOARD_INVALIDATE: view->_InvalAllTxdd(q->first, q->inserted, q->deleted); break;
            case MM_TEXT_CLIPBOARD_CARET: view->SetSel(q->first, q->first, fTrue); break;
            case MM_TEXT_CLIPBOARD_SHOW: view->ShowSel(fTrue); break;
            default: Bug("Unknown byte-stream clipboard operation"); return 0;
            }
            return 1;
        });
    }
};

bool TextDocumentByteStreamDisplay::_FCopySel(PDocumentBase *ppdocb)
{
    AssertThis(0);
    ClipboardHost host(this);
    host.output = ppdocb;
    const auto services = host.Services();
    return host.Check(mm_text_clipboard_copy(ppdocb != nullptr, &services)) > 0;
}

void TextDocumentByteStreamDisplay::_ClearSel()
{
    AssertThis(0);
    ClipboardHost host(this);
    const auto services = host.Services();
    host.Check(mm_text_clipboard_clear(&services));
}

bool TextDocumentByteStreamDisplay::_FPaste(PClipboardObject pclip, bool fDoIt, long cid)
{
    AssertThis(0);
    AssertPo(pclip, 0);
    ClipboardHost host(this);
    host.clipboard = pclip;
    const auto services = host.Services();
    return host.Check(mm_text_clipboard_paste(cid == cidPaste, fDoIt, &services)) > 0;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a TextDocumentByteStreamDisplay.
***************************************************************************/
void TextDocumentByteStreamDisplay::AssertValid(LegacyUlong grf)
{
    // REVIEW shonk: fill in more
    TextDocumentByteStreamDisplay_PAR::AssertValid(0);
    AssertPo(_pbsf, 0);
    AssertPo(_pglichStarts, 0);
}

/***************************************************************************
    Mark memory for the TextDocumentByteStreamDisplay.
***************************************************************************/
void TextDocumentByteStreamDisplay::MarkMem(void)
{
    AssertValid(0);
    TextDocumentByteStreamDisplay_PAR::MarkMem();
    MarkMemObj(_pbsf);
    MarkMemObj(_pglichStarts);
}
#endif // DEBUG

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Rich text document and associated DocumentDisplayGraphicsObject.

***************************************************************************/
#include <exception>
#include <new>
#include "frame.h"
ASSERTNAME
#include "rich_text_live_store_host.h"
#include "text_document_host.h"
#include "text_materialize_host.h"
#include "text_properties_host.h"
#include "text_format_host.h"

RTCLASS(TextDocumentBase)
RTCLASS(PlainTextDocument)
RTCLASS(RichTextDocument)
RTCLASS(TextDocumentGraphicsObject)
RTCLASS(LineTextGraphicsDocument)
RTCLASS(RichTextDocumentGraphicsObject)
RTCLASS(RichTextUndo)

BEGIN_CMD_MAP(RichTextDocumentGraphicsObject, DocumentDisplayGraphicsObject)
ON_CID_GEN(cidPlain, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidBold, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidItalic, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidUnderline, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidJustifyLeft, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidJustifyCenter, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidJustifyRight, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidIndentNone, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidIndentFirst, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidIndentRest, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidIndentAll, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidChooseFont, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidChooseFontSize, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
ON_CID_GEN(cidChooseSubSuper, &RichTextDocumentGraphicsObject::FCmdApplyProperty, &RichTextDocumentGraphicsObject::FEnablePropCmd)
END_CMD_MAP_NIL()

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the rich text doc.
***************************************************************************/
void TextDocumentBase::AssertValid(LegacyUlong grfobj)
{
    achar ch;
    long ibMac;
    long cpMac;

    TextDocumentBase_PAR::AssertValid(grfobj);
    AssertPo(_pbsf, 0);
    ibMac = _pbsf->IbMac();
    cpMac = ibMac / size(achar);
    Assert(ibMac % size(achar) == 0, "ibMac not divisible by size(achar)");
    AssertIn(ibMac, size(achar), kcbMax);
    AssertIn(_cpMinCache, 0, cpMac + 1);
    AssertIn(_cpLimCache, 0, cpMac + 1);
    AssertIn(_cpLimCache, _cpMinCache, _cpMinCache + size(_rgchCache) + 1);
    AssertNilOrPo(_pfil, 0);
    if (grfobj & fobjAssertFull)
    {
        _pbsf->FetchRgb(ibMac - size(achar), size(achar), &ch);
        Assert(ch == kchReturn, "stream doesn't end with a carriage return");
    }
}

/***************************************************************************
    Mark memory for the TextDocumentBase.
***************************************************************************/
void TextDocumentBase::MarkMem(void)
{
    AssertThis(fobjAssertFull);
    TextDocumentBase_PAR::MarkMem();
    MarkMemObj(_pbsf);
}
#endif // DEBUG

/***************************************************************************
    Constructor for the base text document class
***************************************************************************/
TextDocumentBase::TextDocumentBase(PDocumentBase pdocb, ulong grfdoc) : TextDocumentBase_PAR(pdocb, grfdoc)
{
    _acrBack = kacrWhite;
    _dxpDef = kdxpDocDef;
}

/***************************************************************************
    Destructor for the base text document class
***************************************************************************/
TextDocumentBase::~TextDocumentBase(void)
{
    DocumentHost host(this);
    auto objects = host.Objects();
    mm_text_document_dispose(0, &objects);
}

/***************************************************************************
    Initializer for the base text document class.
***************************************************************************/
bool TextDocumentBase::_FInit(PFilename pfni, PFileByteStream pbsf, short osk)
{
    AssertNilOrPo(pfni, ffniFile);
    AssertNilOrPo(pbsf, 0);
    DocumentHost host(this);
    auto services = host.Base();
    auto config = host.Config();
    int result = mm_text_base_init(&config, pfni, pbsf, osk, &services);
    host.Rethrow();
    if (result > 0)
        AssertThis(fobjAssertFull);
    return result > 0;
}

/***************************************************************************
    Load the document from its file
***************************************************************************/
bool TextDocumentBase::_FLoad(short osk)
{
    DocumentHost host(this);
    auto services = host.Base();
    int result = mm_text_base_load(osk, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Return the length of the text in the text document.
***************************************************************************/
long TextDocumentBase::CpMac(void)
{
    // Note: we don't do an AssertThis(0) here for debug performance
    AssertThisMem();
    AssertVarMem(_pbsf);
    Assert(_pbsf->IbMac() % size(achar) == 0, "IbMac() not divisible by size(achar)");

    return _pbsf->IbMac() / size(achar);
}

/***************************************************************************
    Suspend undo. This increments a count.
***************************************************************************/
void TextDocumentBase::SuspendUndo(void)
{
    AssertThis(0);
    _cactSuspendUndo++;
}

/***************************************************************************
    Resume undo. This decrements a count.
***************************************************************************/
void TextDocumentBase::ResumeUndo(void)
{
    AssertThis(0);
    Assert(_cactSuspendUndo > 0, "bad _cactSuspendUndo");
    _cactSuspendUndo--;
}

/***************************************************************************
    Set up undo for an action. If this succeeds, you must call either
    CancelUndo or CommitUndo. Default TextDocumentBase doesn't create an undo record.
***************************************************************************/
bool TextDocumentBase::FSetUndo(long cp1, long cp2, long ccpIns)
{
    AssertThis(0);
    AssertIn(cp1, 0, CpMac() + 1);
    AssertIn(cp2, 0, CpMac() + 1);
    AssertIn(ccpIns, 0, kcbMax);

    _cactSuspendUndo++;
    return fTrue;
}

/***************************************************************************
    Cancel undo.
***************************************************************************/
void TextDocumentBase::CancelUndo(void)
{
    ResumeUndo();
}

/***************************************************************************
    Commit the setup undo.
***************************************************************************/
void TextDocumentBase::CommitUndo(void)
{
    ResumeUndo();
}

/***************************************************************************
    Increments the _cactCombineUndo counter. This is used to determine
    whether the new undo record can be combined with the previous one
    (during typing etc).
***************************************************************************/
void TextDocumentBase::BumpCombineUndo(void)
{
    AssertThis(0);
    if (_cactSuspendUndo <= 0)
        _cactCombineUndo++;
}

/***************************************************************************
    Search for some text. If fCaseSensitive is false, we map prgch to all
    lower case. The pcpLim parameter is in case we support regular
    expressions in the future.
***************************************************************************/
bool TextDocumentBase::FFind(achar *prgch, long cch, long cpMin, long *pcpMin, long *pcpLim, bool fCaseSensitive)
{
    AssertThis(fobjAssertFull);
    AssertIn(cch, 1, kcbMax);
    AssertPvCb(prgch, cch * size(achar));
    AssertIn(cpMin, 0, CpMac() + 1);
    AssertVarMem(pcpMin);
    AssertVarMem(pcpLim);
    const long kcbCharSet = 256 / 8;
    byte grfbitUsed[kcbCharSet];
    long ibit;
    achar *pch, *pchLast;
    achar ch;
    long cpT, cpMac;

    if (!fCaseSensitive)
        LowerRgch(prgch, cch);

    cch--;
    pchLast = prgch + cch;

    // calculate the grfbitUsed bit field - indicating, which characters
    // appear in the search string.
    ClearPb(grfbitUsed, size(grfbitUsed));
    for (pch = prgch; pch <= pchLast; pch++)
    {
        ibit = (long)(byte)*pch;
        AssertIn(ibit, 0, 256);
        grfbitUsed[IbFromIbit(ibit)] |= Fbit(ibit);
    }

    for (cpMac = CpMac(); (cpT = cpMin + cch) < cpMac;)
    {
        for (pch = pchLast;; cpT--, pch--)
        {
            // get the character and map it to lower case if we're doing
            // a case insensitive search
            ch = _ChFetch(cpT);
            if (!fCaseSensitive)
                ch = ChLower(ch);

            // see if the character is used anywhere within the search string
            ibit = (long)(byte)ch;
            if (!(grfbitUsed[IbFromIbit(ibit)] & Fbit(ibit)))
            {
                // this character isn't anywhere in the search string,
                // so we can jump by a lot.
                cpMin = cpT + 1;
                break;
            }

            // see if the character matches the corresponding character in
            // the search string
            if (ch != *pch)
            {
                // character doesn't match, just jump by one
                cpMin++;
                break;
            }

            // if cpT is back to cpMin, we've got a match
            if (cpT == cpMin)
            {
                // we matched
                *pcpMin = cpMin;
                *pcpLim = cpMin + cch + 1;
                return fTrue;
            }
        }
    }

    TrashVar(pcpMin);
    TrashVar(pcpLim);
    return fFalse;
}

/***************************************************************************
    Turn the selection of this TextDocumentBase's active DocumentDisplayGraphicsObject off.
***************************************************************************/
void TextDocumentBase::HideSel(void)
{
    AssertThis(0);
    PDocumentDisplayGraphicsObject pddg;

    if (pvNil != (pddg = PddgActive()) && pddg->FIs(kclsTextDocumentGraphicsObject))
        ((PTextDocumentGraphicsObject)pddg)->HideSel();
}

/***************************************************************************
    Set the selection of this TextDocumentBase's active DocumentDisplayGraphicsObject to the given range.
***************************************************************************/
void TextDocumentBase::SetSel(long cpAnchor, long cpOther, long gin)
{
    AssertThis(0);
    PDocumentDisplayGraphicsObject pddg;

    if (pvNil != (pddg = PddgActive()) && pddg->FIs(kclsTextDocumentGraphicsObject))
        ((PTextDocumentGraphicsObject)pddg)->SetSel(cpAnchor, cpOther, gin);
}

/***************************************************************************
    Make sure the selection of this TextDocumentBase's acttive DocumentDisplayGraphicsObject is visible (at least
    the _cpOther end of it).
***************************************************************************/
void TextDocumentBase::ShowSel(void)
{
    AssertThis(0);
    PDocumentDisplayGraphicsObject pddg;

    if (pvNil != (pddg = PddgActive()) && pddg->FIs(kclsTextDocumentGraphicsObject))
        ((PTextDocumentGraphicsObject)pddg)->ShowSel();
}

/***************************************************************************
    Fetch a character of the stream through the cache.
***************************************************************************/
void TextDocumentBase::_CacheRange(long cpMin, long cpLim)
{
    AssertThis(0);
    AssertIn(cpMin, 0, CpMac() + 1);
    AssertIn(cpLim, cpMin, CpMac() + 1);
    AssertIn(cpLim - cpMin, 0, kcchMaxTxtbCache + 1);
    long cpMac;

    if (cpMin >= _cpMinCache && cpLim <= _cpLimCache)
        return;

    cpMac = CpMac();
    if (_cpLimCache <= _cpMinCache)
    {
        // nothing currently cached
        goto LNewCache;
    }

    if (cpMin < _cpMinCache)
    {
        long cpT;

        if (_cpMinCache >= cpMin + kcchMaxTxtbCache)
            goto LNewCache;

        // keep front end of old stuff
        cpT = LwMax(0, LwMin(cpMin, _cpLimCache - kcchMaxTxtbCache));
        _cpLimCache = LwMin(cpT + kcchMaxTxtbCache, _cpLimCache);
        BltPb(_rgchCache, _rgchCache + (_cpMinCache - cpT), (_cpLimCache - _cpMinCache) * size(achar));
        _pbsf->FetchRgb(cpT * size(achar), (_cpMinCache - cpT) * size(achar), _rgchCache);
        _cpMinCache = cpT;
    }
    else
    {
        Assert(cpLim > _cpLimCache, 0);
        if (cpLim >= _cpLimCache + kcchMaxTxtbCache)
        {
        LNewCache:
            // just cache around [cpMin, cpLim)
            _cpMinCache = LwMax(0, LwMin((cpMin + cpLim - kcchMaxTxtbCache) / 2, cpMac - kcchMaxTxtbCache));
            _cpLimCache = LwMin(_cpMinCache + kcchMaxTxtbCache, cpMac);
            _pbsf->FetchRgb(_cpMinCache * size(achar), (_cpLimCache - _cpMinCache) * size(achar), _rgchCache);
        }
        else
        {
            // keep back end old stuff
            long cpLimCache, cpMinCache;

            cpLimCache = LwMin(cpMac, LwMax(cpLim, _cpMinCache + kcchMaxTxtbCache));
            cpMinCache = LwMax(cpLimCache - kcchMaxTxtbCache, _cpMinCache);
            if (cpMinCache != _cpMinCache)
            {
                BltPb(_rgchCache + (cpMinCache - _cpMinCache), _rgchCache, (_cpLimCache - cpMinCache) * size(achar));
            }
            _pbsf->FetchRgb(_cpLimCache * size(achar), (cpLimCache - _cpLimCache) * size(achar),
                            _rgchCache + _cpLimCache - cpMinCache);
            _cpMinCache = cpMinCache;
            _cpLimCache = cpLimCache;
        }
    }
}

/***************************************************************************
    Characters have changed, so fix the cache.
***************************************************************************/
void TextDocumentBase::_InvalCache(long cp, long ccpIns, long ccpDel)
{
    long dcpFront, dcpBack;

    if (_cpLimCache <= cp || _cpLimCache <= _cpMinCache || ccpIns == 0 && ccpDel == 0)
    {
        // cache is before the edit or cache is already empty
        return;
    }

    if (_cpMinCache >= cp + ccpDel)
    {
        // cache is after the edit
        _cpMinCache += ccpIns - ccpDel;
        _cpLimCache += ccpIns - ccpDel;
        return;
    }

    dcpFront = cp - _cpMinCache;
    dcpBack = _cpLimCache - cp - ccpDel;
    if (dcpFront <= 0 && dcpBack <= 0)
        _cpMinCache = _cpLimCache = 0;
    else if (dcpFront >= dcpBack)
    {
        // keep the front end of the cache
        _cpLimCache = cp;
    }
    else
    {
        // keep the tail end of the cache
        BltPb(_rgchCache + cp + ccpDel - _cpMinCache, _rgchCache, dcpBack * size(achar));
        _cpMinCache = cp + ccpIns;
        _cpLimCache = _cpMinCache + dcpBack;
    }
}

/***************************************************************************
    Fetch a character of the stream through the cache.
***************************************************************************/
achar TextDocumentBase::_ChFetch(long cp)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac() + 1);

    if (!FIn(cp, _cpMinCache, _cpLimCache))
    {
        // not a cache hit
        _CacheRange(cp, cp + 1);
    }

    return _rgchCache[cp - _cpMinCache];
}

/***************************************************************************
    Fetch some characters from the text document.
***************************************************************************/
void TextDocumentBase::FetchRgch(long cp, long ccp, achar *prgch)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac() + 1);
    AssertIn(ccp, 0, CpMac() - cp + 1);
    AssertPvCb(prgch, ccp * size(achar));
    long ccpT;

    while (ccp > 0)
    {
        ccpT = LwMin(ccp, kcchMaxTxtbCache);
        _CacheRange(cp, cp + ccpT);
        CopyPb(_rgchCache + cp - _cpMinCache, prgch, ccpT * size(achar));
        prgch += ccpT;
        cp += ccpT;
        ccp -= ccpT;
    }
}

/***************************************************************************
    Returns non-zero iff cp is the beginning of a paragraph.
***************************************************************************/
bool TextDocumentBase::FMinPara(long cp)
{
    AssertThis(0);
    ulong grfch;

    if (cp <= 0)
        return cp == 0;
    if (cp >= CpMac())
        return fFalse;
    if (GrfchFromCh(_ChFetch(cp)) & fchIgnore)
        return fFalse;

    // return true iff the first non-ignore character we see
    // is a break character
    AssertIn(cp, 1, CpMac());
    while (cp-- > 0)
    {
        grfch = GrfchFromCh(_ChFetch(cp));
        if (fchBreak & grfch)
            return fTrue;
        if (!(fchIgnore & grfch))
            return fFalse;
    }

    // just line feeds!
    Warn("isolated line feeds");
    return fFalse;
}

/***************************************************************************
    Find the beginning of the paragraph that cp is in. If cp <= 0,
    returns 0. If cp >= CpMac(), returns the beginning of the last
    paragraph.
***************************************************************************/
long TextDocumentBase::CpMinPara(long cp)
{
    AssertThis(0);
    ulong grfch;
    long cpOrig;
    long dcpLine = 1;

    if (cp <= 0)
        return 0;

    if (cp >= CpMac())
        cp = CpMac() - 1;
    else if (GrfchFromCh(_ChFetch(cp)) & fchIgnore)
        dcpLine++;
    cpOrig = cp;

    AssertIn(cp, 1, CpMac());
    while (cp-- > 0)
    {
        grfch = GrfchFromCh(_ChFetch(cp));
        if (fchBreak & grfch)
        {
            if (cp + dcpLine <= cpOrig)
                return cp + dcpLine;
            dcpLine = 1;
        }
        else if (fchIgnore & grfch)
            dcpLine++;
        else
            dcpLine = 1;
    }

    return 0;
}

/***************************************************************************
    Find the end of the paragraph that cp is in. If cp < 0, returns 0.
    If cp >= CpMac(), returns CpMac().
***************************************************************************/
long TextDocumentBase::CpLimPara(long cp)
{
    AssertThis(0);
    ulong grfch;
    bool fCr = fFalse;
    long cpLim = CpMac();

    if (cp < 0)
        return 0;
    if (cp >= cpLim)
        return cpLim;

    // cp should now be a legal index into the character stream
    AssertIn(cp, 0, cpLim);
    while (cp > 0 && (GrfchFromCh(_ChFetch(cp)) & fchIgnore))
        cp--;

    for (; cp < cpLim; cp++)
    {
        grfch = GrfchFromCh(_ChFetch(cp));
        if (fchBreak & grfch)
        {
            if (fCr)
                return cp;
            fCr = fTrue;
        }
        else if (!(fchIgnore & grfch) && fCr)
            return cp;
    }
    Assert(fCr, "last character is not a return character");
    return cpLim;
}

/***************************************************************************
    Return cp of the previous character, skipping line feed characters. If
    fWord is true, skip to the beginning of a word.
***************************************************************************/
long TextDocumentBase::CpPrev(long cp, bool fWord)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac() + 1);

    if (!fWord)
    {
        while (cp > 0 && (fchIgnore & GrfchFromCh(_ChFetch(--cp))))
            ;
    }
    else
    {
        while (cp > 0 && ((fchIgnore | fchMayBreak) & GrfchFromCh(_ChFetch(cp - 1))))
        {
            cp--;
        }
        while (cp > 0 && !((fchIgnore | fchMayBreak) & GrfchFromCh(_ChFetch(cp - 1))))
        {
            cp--;
        }
    }

    return cp;
}

/***************************************************************************
    Return cp of the next character, skipping line feed characters. If
    fWord is true, skip to the beginning of the next word.
***************************************************************************/
long TextDocumentBase::CpNext(long cp, bool fWord)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac() + 1);
    long cpMac = CpMac();

    if (cp >= cpMac)
        return cpMac;

    if (!fWord)
    {
        while (++cp < cpMac && (fchIgnore & GrfchFromCh(_ChFetch(cp))))
            ;
    }
    else
    {
        while (cp < cpMac && !((fchIgnore | fchMayBreak) & GrfchFromCh(_ChFetch(cp))))
        {
            cp++;
        }
        while (cp < cpMac && ((fchIgnore | fchMayBreak) & GrfchFromCh(_ChFetch(cp))))
        {
            cp++;
        }
    }
    return cp;
}

/***************************************************************************
    Invalidate all DDGs on this text doc. Also dirties the document.
    Should be called by any code that edits the document.
***************************************************************************/
void TextDocumentBase::InvalAllDdg(long cp, long ccpIns, long ccpDel, ulong grfdoc)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac() + 1);
    AssertIn(ccpIns, 0, CpMac() + 1 - cp);
    AssertIn(ccpDel, 0, kcbMax);
    long ipddg;
    PDocumentDisplayGraphicsObject pddg;

    // mark the document dirty
    SetDirty();
    if (fdocNil == grfdoc)
        return;

    // inform the DDGs
    for (ipddg = 0; pvNil != (pddg = PddgGet(ipddg)); ipddg++)
    {
        if ((grfdoc & fdocUpdate) && pddg->FIs(kclsTextDocumentGraphicsObject))
            ((PTextDocumentGraphicsObject)pddg)->InvalCp(cp, ccpIns, ccpDel);
        else
            pddg->InvalRc(pvNil);
    }
}

/***************************************************************************
    Set the background color of the document.
***************************************************************************/
void TextDocumentBase::SetAcrBack(AbstractColor acr, ulong grfdoc)
{
    AssertThis(0);
    AssertPo(&acr, 0);
    Assert(acr != kacrInvert, "can't use invert as a background color");

    if (_acrBack != acr)
    {
        _acrBack = acr;
        SetDirty();
        if (grfdoc & (fdocUpdate | fdocInval))
            InvalAllDdg(0, 0, 0, fdocInval);
    }
}

/***************************************************************************
    Set the default width of the document.
***************************************************************************/
void TextDocumentBase::SetDxpDef(long dxp)
{
    AssertThis(0);
    AssertIn(dxp, 1, kcbMax);
    long cpMac;

    if (_dxpDef == dxp)
        return;

    _dxpDef = dxp;
    SetDirty();
    HideSel();
    cpMac = CpMac();
    InvalAllDdg(0, cpMac, cpMac);
}

/***************************************************************************
    Replace cp to cp + ccpDel with ccpIns characters from prgch. If ccpIns
    is zero, prgch can be nil. The last character should never be replaced.
***************************************************************************/
bool TextDocumentBase::FReplaceRgch(void *prgch, long ccpIns, long cp, long ccpDel, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertIn(ccpIns, 0, kcbMax);
    AssertPvCb(prgch, ccpIns * size(achar));
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);

    if (!_pbsf->FReplace(prgch, ccpIns * size(achar), cp * size(achar), ccpDel * size(achar)))
    {
        return fFalse;
    }
    _InvalCache(cp, ccpIns, ccpDel);
    AssertThis(fobjAssertFull);
    InvalAllDdg(cp, ccpIns, ccpDel, grfdoc);
    return fTrue;
}

/***************************************************************************
    Replace cp to cp + ccpDel with the characters in the given FileLocation.
***************************************************************************/
bool TextDocumentBase::FReplaceFlo(PFileLocation pflo, bool fCopy, long cp, long ccpDel, short osk, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPo(pflo, 0);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    bool fRet = fFalse;
    FileLocation flo = *pflo;

    flo.pfil->AddRef();
    if (flo.FTranslate(osk) &&
        _pbsf->FReplaceFlo(&flo, fCopy && flo.pfil == pflo->pfil, cp * size(achar), ccpDel * size(achar)))
    {
        _InvalCache(cp, flo.cb / size(achar), ccpDel);
        AssertThis(fobjAssertFull);
        InvalAllDdg(cp, flo.cb / size(achar), ccpDel, grfdoc);
        fRet = fTrue;
    }

    ReleasePpo(&flo.pfil);
    return fRet;
}

/***************************************************************************
    Replace cp to cpDst + ccpDel with ccpSrc characters from pbsfSrc starting
    at cpSrc.
***************************************************************************/
bool TextDocumentBase::FReplaceBsf(PFileByteStream pbsfSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPo(pbsfSrc, 0);
    AssertIn(cpSrc, 0, pbsfSrc->IbMac() / size(achar) + 1);
    AssertIn(ccpSrc, 0, pbsfSrc->IbMac() / size(achar) + 1 - cpSrc);
    AssertIn(cpDst, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cpDst);

    if (!_pbsf->FReplaceBsf(pbsfSrc, cpSrc * size(achar), ccpSrc * size(achar), cpDst * size(achar),
                            ccpDel * size(achar)))
    {
        return fFalse;
    }
    _InvalCache(cpDst, ccpSrc, ccpDel);
    AssertThis(fobjAssertFull);
    InvalAllDdg(cpDst, ccpSrc, ccpDel, grfdoc);
    return fTrue;
}

/***************************************************************************
    Replace cp to cpDst + ccpDel with ccpSrc characters from ptxtbSrc starting
    at cpSrc.
***************************************************************************/
bool TextDocumentBase::FReplaceTxtb(PTextDocumentBase ptxtbSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, ulong grfdoc)
{
    AssertPo(ptxtbSrc, 0);
    AssertIn(cpSrc, 0, ptxtbSrc->CpMac() + 1);
    AssertIn(ccpSrc, 0, ptxtbSrc->CpMac() + 1 - cpSrc);

    return FReplaceBsf(ptxtbSrc->_pbsf, cpSrc, ccpSrc, cpDst, ccpDel, grfdoc);
}

/***************************************************************************
    Get the bounds of an object - since plain text doesn't have objects,
    just return false.
***************************************************************************/
bool TextDocumentBase::FGetObjectRc(long cp, PGraphicsEnvironment pgnv, PCHP pchp, RC *prc)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac());
    AssertPo(pgnv, 0);
    AssertVarMem(pchp);
    AssertVarMem(prc);

    TrashVar(prc);
    return fFalse;
}

/***************************************************************************
    Draw an object - since plain text doesn't have objects, just return
    false.
***************************************************************************/
bool TextDocumentBase::FDrawObject(long cp, PGraphicsEnvironment pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac());
    AssertPo(pgnv, 0);
    AssertVarMem(pxp);
    AssertVarMem(pchp);
    AssertVarMem(prcClip);

    return fFalse;
}

/***************************************************************************
    Get the current Filename for the doc. Return false if the doc is not
    currently based on an Filename (it's a new doc or an internal one).
***************************************************************************/
bool TextDocumentBase::FGetFni(Filename *pfni)
{
    AssertThis(0);
    AssertBasePo(pfni, 0);
    DocumentHost host(this);
    auto services = host.File();
    int result = mm_text_document_name(MM_TEXT_PLAIN, pfni, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Export the text.
***************************************************************************/
void TextDocumentBase::ExportFormats(PClipboardObject pclip)
{
    AssertThis(0);
    AssertPo(pclip, 0);
    void *pv;
    long ccp = CpMac() - 1;

    if (ccp <= 0)
        return;

    if (!pclip->FInitExport())
        return;

    if (pvNil != (pv = pclip->PvExport(ccp * size(achar), kclfmText)))
        _pbsf->FetchRgb(0, ccp * size(achar), pv);

    pclip->EndExport();
}

/***************************************************************************
    Constructor for plain text doc.
***************************************************************************/
PlainTextDocument::PlainTextDocument(PDocumentBase pdocb, ulong grfdoc) : PlainTextDocument_PAR(pdocb, grfdoc)
{
}

/***************************************************************************
    Static method to create a new plain text document.
***************************************************************************/
PPlainTextDocument PlainTextDocument::PtxpdNew(PFilename pfni, PFileByteStream pbsf, short osk, PDocumentBase pdocb,
                                               ulong grfdoc)
{
    AssertNilOrPo(pfni, ffniFile);
    AssertNilOrPo(pbsf, 0);
    AssertNilOrPo(pdocb, 0);
    struct Factory : TextDocumentFactoryHost
    {
        PFilename name;
        PFileByteStream stream;
        short system;
        PDocumentBase parent;
        ulong flags;
    };
    Factory host;
    host.name = pfni;
    host.stream = pbsf;
    host.system = osk;
    host.parent = pdocb;
    host.flags = grfdoc;
    MMTextFactoryServices services = {
        &host,
        [](void *v, void **out) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard([&] {
                *out = NewObj PlainTextDocument(h.parent, h.flags);
                return *out != nullptr;
            });
        },
        [](void *v, void *p) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard([&] { return static_cast<PPlainTextDocument>(p)->_FInit(h.name, h.stream, h.system); });
        },
        [](void *, void *p) { static_cast<BASE *>(p)->Release(); }, [](void *) { PushErc(ercRtxdReadFailed); }};
    void *object;
    mm_text_document_create(&object, 0, &services);
    host.Rethrow();
    return static_cast<PPlainTextDocument>(object);
}

/***************************************************************************
    Create a new LineTextGraphicsDocument to display the PlainTextDocument.
***************************************************************************/
PDocumentDisplayGraphicsObject PlainTextDocument::PddgNew(PGraphicsObjectBlock pgcb)
{
    AssertThis(fobjAssertFull);
    long onn = vpappb->OnnDefFixed();
    long dypFont = vpappb->DypTextDef();

    return LineTextGraphicsDocument::PtxlgNew(this, pgcb, onn, fontNil, dypFont, 4);
}

/***************************************************************************
    Save the document and optionally set this fni as the current one.
    If the doc is currently based on an Filename, pfni may be nil, indicating
    that this is a normal save (not save as). If pfni is not nil and
    fSetFni is false, this just writes a copy of the doc but doesn't change
    the doc one bit.
***************************************************************************/
bool PlainTextDocument::FSaveToFni(Filename *pfni, bool fSetFni)
{
    AssertThis(fobjAssertFull);
    AssertNilOrPo(pfni, ffniFile);
    DocumentHost host(this);
    auto services = host.File();
    auto config = host.Config();
    Filename fallback;
    int result = mm_text_document_save(MM_TEXT_PLAIN, &config, pfni, &fallback, fSetFni, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Constructor for a rich text document.
***************************************************************************/
RichTextDocument::RichTextDocument(PDocumentBase pdocb, ulong grfdoc) : RichTextDocument_PAR(pdocb, grfdoc)
{
}

/***************************************************************************
    Destructor for a rich text document.
***************************************************************************/
RichTextDocument::~RichTextDocument(void)
{
    DocumentHost host(this);
    auto objects = host.Objects();
    mm_text_document_dispose(1, &objects);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a RichTextDocument.
***************************************************************************/
void RichTextDocument::AssertValid(LegacyUlong grfobj)
{
    RichTextDocument_PAR::AssertValid(grfobj);
    AssertPo(_pglmpe, 0);
    AssertNilOrPo(_pagcact, 0);
    AssertIn(_dypFontDef, 1, kswMax);
    AssertIn(_pbsf->IbMac() / size(achar), 1, kcpMaxTxrd);

    if (grfobj & fobjAssertFull)
    {
        MPE mpePrev, mpe;
        long impe;

        mpePrev.spcp = (ulong)-1;
        for (impe = _pglmpe->IvMac(); impe-- > 0;)
        {
            _pglmpe->Get(impe, &mpe);
            Assert(mpe.spcp < mpePrev.spcp, "non-increasing mpe's");
            mpePrev = mpe;
        }
        // REVIEW shonk: RichTextDocument::AssertValid: fill out
    }
}

/***************************************************************************
    Mark memory for the RichTextDocument.
***************************************************************************/
void RichTextDocument::MarkMem(void)
{
    AssertThis(fobjAssertFull);
    RichTextDocument_PAR::MarkMem();
    MarkMemObj(_pglmpe);
    MarkMemObj(_pagcact);
}
#endif // DEBUG

/***************************************************************************
    Static method to create or open a rich text document.
***************************************************************************/
PRichTextDocument RichTextDocument::PtxrdNew(PFilename pfni)
{
    AssertNilOrPo(pfni, ffniFile);
    struct Factory : TextDocumentFactoryHost
    {
        PFilename name;
    };
    Factory host;
    host.name = pfni;
    MMTextFactoryServices services = {
        &host,
        [](void *v, void **out) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard([&] {
                *out = NewObj RichTextDocument;
                return *out != nullptr;
            });
        },
        [](void *v, void *p) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard([&] { return static_cast<PRichTextDocument>(p)->_FInit(h.name); });
        },
        [](void *, void *p) { static_cast<BASE *>(p)->Release(); }, [](void *) { PushErc(ercRtxdReadFailed); }};
    void *object;
    mm_text_document_create(&object, 0, &services);
    host.Rethrow();
    return static_cast<PRichTextDocument>(object);
}

/***************************************************************************
    Initialize the rich text document.
***************************************************************************/
bool RichTextDocument::_FInit(PFilename pfni, ChunkTagOrType ctg)
{
    AssertBaseThis(0);
    AssertNilOrPo(pfni, 0);
    DocumentHost host(this);
    auto services = host.Init();
    auto config = host.Config();
    int result = mm_text_rich_init(&config, pfni, ctg, &services);
    host.Rethrow();
    if (result > 0)
        AssertThis(fobjAssertFull);
    return result > 0;
}

/***************************************************************************
    Static method to read a rich text document from a chunk.
***************************************************************************/
PRichTextDocument RichTextDocument::PtxrdReadChunk(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno,
                                                   bool fCopyText)
{
    AssertPo(pcfl, 0);
    struct Factory : TextDocumentFactoryHost
    {
        PChunkyFile file;
        ChunkTagOrType type;
        ChunkNumber number;
        bool copy;
    };
    Factory host;
    host.file = pcfl;
    host.type = ctg;
    host.number = cno;
    host.copy = fCopyText;
    MMTextFactoryServices services = {
        &host,
        [](void *v, void **out) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard([&] {
                *out = NewObj RichTextDocument;
                return *out != nullptr;
            });
        },
        [](void *v, void *p) -> int {
            auto &h = *static_cast<Factory *>(v);
            return h.Guard(
                [&] { return static_cast<PRichTextDocument>(p)->_FReadChunk(h.file, h.type, h.number, h.copy); });
        },
        [](void *, void *p) { static_cast<BASE *>(p)->Release(); }, [](void *) { PushErc(ercRtxdReadFailed); }};
    void *object;
    mm_text_document_create(&object, 1, &services);
    host.Rethrow();
    return static_cast<PRichTextDocument>(object);
}

/***************************************************************************
    Read the given chunk into this RichTextDocument.
***************************************************************************/
bool RichTextDocument::_FReadChunk(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool fCopyText)
{
    AssertPo(pcfl, 0);
    MaterializeHost host(this, pcfl);
    auto config = host.Config();
    auto services = host.Read();
    int result = mm_text_materialize(&config, ctg, cno, fCopyText, &services);
    host.Rethrow();
    if (result > 0)
        AssertThis(fobjAssertFull);
    return result > 0;
}

/***************************************************************************
    Do any necessary munging of the AllocatedGroup entry on open. Return false if
    we don't recognize this argument type.
***************************************************************************/
bool RichTextDocument::_FOpenArg(long icact, byte sprm, short bo, short osk)
{
    AssertBaseThis(0);
    MaterializeHost host(this);
    auto services = host.Argument();
    int result = mm_text_open_font_argument(sizeof(achar), icact, sprm, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Get the current Filename for the doc. Return false if the doc is not
    currently based on an Filename (it's a new doc or an internal one).
***************************************************************************/
bool RichTextDocument::FGetFni(Filename *pfni)
{
    AssertThis(0);
    AssertBasePo(pfni, 0);
    DocumentHost host(this);
    auto services = host.File();
    int result = mm_text_document_name(MM_TEXT_RICH, pfni, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Ask the user what file they want to save to.
***************************************************************************/
bool RichTextDocument::FGetFniSave(Filename *pfni)
{
    // REVIEW shonk: what file type to use on Mac?
    AssertThis(0);
    return FGetFniSaveMacro(pfni, 'CHN2',
                            "\x9"
                            "Save As: ",
                            "", PszLit("All files\0*.*\0"), (GraphicsObject::PgobScreen() ? GraphicsObject::PgobScreen()->Hwnd() : hNil));
}

/***************************************************************************
    Save the rich text document and optionally set this fni as the current
    one. If the doc is currently based on an Filename, pfni may be nil, indicating
    that this is a normal save (not save as). If pfni is not nil and
    fSetFni is false, this just writes a copy of the doc but doesn't change
    the doc one bit.
***************************************************************************/
bool RichTextDocument::FSaveToFni(Filename *pfni, bool fSetFni)
{
    AssertThis(fobjAssertFull);
    AssertNilOrPo(pfni, ffniFile);
    DocumentHost host(this);
    auto services = host.File();
    auto config = host.Config();
    Filename fallback;
    int result = mm_text_document_save(MM_TEXT_RICH, &config, pfni, &fallback, fSetFni, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Save a rich text document to the given chunky file. Fill in *pcki with
    where we put the root chunk.
***************************************************************************/
bool RichTextDocument::FSaveToChunk(PChunkyFile pcfl, ChunkIdentification *pcki, bool fRedirectText)
{
    AssertThis(fobjAssertFull);
    AssertPo(pcfl, 0);
    AssertVarMem(pcki);
    RichTextStoreHost::LiveHost host(pcfl, _pbsf);
    auto services = host.Services();
    MMRichTextState state;
    mm_rich_text_init(&state, sizeof(achar), koskCur);
    state.defaults = {kboCur, _oskFont, static_cast<MMI32>(_dxpDef), static_cast<MMI32>(_dypFontDef),
                      static_cast<MMU32>(_acrBack.LwGet())};
    state.font = RichTextStoreHost::Font(_stnFontDef);
    state.text.bytes = _pbsf->IbMac() - static_cast<LegacyLong>(sizeof(achar));
    state.properties = _pglmpe;
    state.arguments = _pagcact;
    MMTextLiveOutput output = {};
    const int result = mm_rich_text_save(&state, &output, RichTextStoreHost::NativeBig(), fRedirectText, &services);
    pcki->ctg = output.chunk.type;
    if (output.number_changed)
        pcki->cno = output.chunk.number;
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Create a new RichTextDocumentGraphicsObject to display the RichTextDocument.
***************************************************************************/
PDocumentDisplayGraphicsObject RichTextDocument::PddgNew(PGraphicsObjectBlock pgcb)
{
    AssertThis(fobjAssertFull);
    return RichTextDocumentGraphicsObject::PtxrgNew(this, pgcb);
}

/***************************************************************************
    Look for an MPE for the given spcp. Return false iff there isn't one.
    In either event, fill pimpe with where one would go if it did exist.
***************************************************************************/
bool RichTextDocument::_FFindMpe(ulong spcp, MPE *pmpe, long *pcpLim, long *pimpe)
{
    AssertThis(0);
    AssertNilOrVarMem(pmpe);
    AssertNilOrVarMem(pcpLim);
    AssertNilOrVarMem(pimpe);
    PropertyHost host(this);
    auto services = host.Properties();
    MMTextPropertyEntry entry;
    MMI32 end, index;
    bool found = mm_text_property_find(spcp, CpMac(), pmpe ? &entry : nullptr, pcpLim ? &end : nullptr,
                                       pimpe ? &index : nullptr, &services) != 0;
    if (pmpe)
    {
        if (found)
            *pmpe = {entry.key, entry.value};
        else
            TrashVar(pmpe);
    }
    if (pcpLim)
        *pcpLim = end;
    if (pimpe)
        *pimpe = index;
    return found;
}

/***************************************************************************
    Fetch the impe'th property, returning all the relevant info about it.
***************************************************************************/
bool RichTextDocument::_FFetchProp(long impe, byte *psprm, long *plw, long *pcpMin, long *pcpLim)
{
    PropertyHost host(this);
    auto services = host.Properties();
    MMTextPropertyEntry entry;
    MMI32 end;
    if (!mm_text_property_fetch(impe, CpMac(), &entry, pcpLim ? &end : nullptr, &services))
        return false;
    if (psprm)
        *psprm = static_cast<byte>(entry.key >> 24);
    if (plw)
        *plw = entry.value;
    if (pcpMin)
        *pcpMin = entry.key & 0x00ffffff;
    if (pcpLim)
        *pcpLim = end;
    return true;
}

/***************************************************************************
    Adjust the MPE's after an edit. This may involve deleting some MPE's
    and/or updating the cp's.
***************************************************************************/
void RichTextDocument::_AdjustMpe(long cp, long ccpIns, long ccpDel)
{
    AssertBaseThis(0);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpIns, 0, CpMac() - cp);
    AssertIn(ccpDel, 0, kcbMax);
    PropertyHost host(this);
    auto services = host.Properties();
    mm_text_properties_adjust(cp, ccpIns, ccpDel, CpMac(), &services);
    host.Rethrow();
    AssertThis(0);
}

/***************************************************************************
    Make sure there is an entry in the AllocatedGroup with the given data. If it's
    already there, increment its reference count. Otherwise, set its
    reference count to 1.
***************************************************************************/
bool RichTextDocument::_FEnsureInAg(byte sprm, void *pv, long cb, LegacyLong *pjv)
{
    AssertIn(cb, 1, kcbMax);
    AssertPvCb(pv, cb);
    AssertVarMem(pjv);
    PropertyHost host(this);
    auto services = host.Arguments();
    MMI32 reference;
    int result = mm_text_argument_ensure(sprm, pv, cb, &reference, &services);
    host.Rethrow();
    if (result > 0)
        *pjv = reference;
    else
        TrashVar(pjv);
    return result > 0;
}

/***************************************************************************
    Decrement the reference count on the given element of the AllocatedGroup and if
    the reference count becomes zero, delete the element.
***************************************************************************/
void RichTextDocument::_ReleaseInAg(long jv)
{
    PropertyHost host(this);
    auto services = host.Arguments();
    mm_text_argument_ref(jv, 0, &services);
}

/***************************************************************************
    Increment the reference count on the given element of the AllocatedGroup.
***************************************************************************/
void RichTextDocument::_AddRefInAg(long jv)
{
    PropertyHost host(this);
    auto services = host.Arguments();
    mm_text_argument_ref(jv, 1, &services);
}

/***************************************************************************
    Static method: return true iff the given sprm has its argument in
    the _pagcact.
***************************************************************************/
bool RichTextDocument::_FSprmInAg(byte sprm)
{
    return mm_text_property_has_argument(sprm) != 0;
}

/***************************************************************************
    If the sprm allocates stuff in the ag, release it. The inverse operation
    of _AddRefSprmLw.
***************************************************************************/
void RichTextDocument::_ReleaseSprmLw(byte sprm, long lw)
{
    PropertyHost host(this);
    auto services = host.Arguments();
    mm_text_property_ref(sprm, lw, 0, &services);
}

/***************************************************************************
    If the sprm allocates stuff in the ag, addref it. The inverse operation
    of _ReleaseSprmLw.
***************************************************************************/
void RichTextDocument::_AddRefSprmLw(byte sprm, long lw)
{
    PropertyHost host(this);
    auto services = host.Arguments();
    mm_text_property_ref(sprm, lw, 1, &services);
}

/***************************************************************************
    Get an array of SPVM's that describe the differences between pchp and
    pchpDiff. If pchpDiff is nil, pchp is just described. If this
    succeeds, either _ApplyRgspvm or _ReleaseRgspvm should be called.
***************************************************************************/
bool RichTextDocument::_FGetRgspvmFromChp(PCHP pchp, PCHP pchpDiff, SPVM *prgspvm, long *pcspvm)
{
    AssertVarMem(pchp);
    AssertNilOrVarMem(pchpDiff);
    AssertPvCb(prgspvm, LwMul(size(SPVM), 4));
    AssertVarMem(pcspvm);
    FormatHost host(this);
    auto config = host.Configuration();
    auto services = host.Values();
    auto value = host.Character(*pchp);
    auto old = pchpDiff ? host.Character(*pchpDiff) : decltype(value){};
    MMTextPropertyChange changes[4];
    MMI32 count;
    int result = mm_text_format_changes(0, &config, &value, pchpDiff ? &old : nullptr, changes, &count, &services);
    for (MMI32 i = 0; i < count; ++i)
        prgspvm[i] = {static_cast<byte>(changes[i].kind), static_cast<LegacyLong>(changes[i].value),
                      static_cast<LegacyLong>(changes[i].mask)};
    host.Rethrow();
    if (result > 0)
        *pcspvm = count;
    else
        TrashVar(pcspvm);
    return result > 0;
}

/***************************************************************************
    Get an array of SPVM's that describe the differences between ppap and
    ppapDiff. If ppapDiff is nil, ppap is just described. If this
    succeeds, either _ApplyRgspvm or _ReleaseRgspvm should be called.
***************************************************************************/
bool RichTextDocument::_FGetRgspvmFromPap(PPAP ppap, PPAP ppapDiff, SPVM *prgspvm, long *pcspvm)
{
    AssertVarMem(ppap);
    AssertNilOrVarMem(ppapDiff);
    AssertPvCb(prgspvm, LwMul(size(SPVM), 3));
    AssertVarMem(pcspvm);
    FormatHost host(this);
    auto config = host.Configuration();
    auto services = host.Values();
    auto value = host.Paragraph(*ppap);
    auto old = ppapDiff ? host.Paragraph(*ppapDiff) : decltype(value){};
    MMTextPropertyChange changes[3];
    MMI32 count;
    int result = mm_text_format_changes(1, &config, &value, ppapDiff ? &old : nullptr, changes, &count, &services);
    for (MMI32 i = 0; i < count; ++i)
        prgspvm[i] = {static_cast<byte>(changes[i].kind), static_cast<LegacyLong>(changes[i].value),
                      static_cast<LegacyLong>(changes[i].mask)};
    host.Rethrow();
    if (result > 0)
        *pcspvm = count;
    else
        TrashVar(pcspvm);
    return result > 0;
}

/***************************************************************************
    Release the SPVM's.
***************************************************************************/
void RichTextDocument::_ReleaseRgspvm(SPVM *prgspvm, long cspvm)
{
    AssertIn(cspvm, 0, kcbMax);
    AssertPvCb(prgspvm, LwMul(size(SPVM), cspvm));
    PropertyHost host(this);
    host.changes = prgspvm;
    auto services = host.Properties();
    mm_text_properties_release(cspvm, &services);
}

/***************************************************************************
    Apply the SPVM's to the given range of characters. Assumes that the
    inserts can't fail. If you call this, don't also call _ReleaseRgspvm.
    The caller should have ensured that _pglmpe has room for 2 * cspvm
    additional entries.
***************************************************************************/
void RichTextDocument::_ApplyRgspvm(long cp, long ccp, SPVM *prgspvm, long cspvm)
{
    AssertIn(cp, 0, CpMac());
    AssertIn(ccp, 1, CpMac() + 1 - cp);
    PropertyHost host(this);
    host.changes = prgspvm;
    auto services = host.Properties();
    mm_text_properties_apply(cp, ccp, CpMac(), cspvm, &services);
    host.Rethrow();
}

/***************************************************************************
    Get the long associated with the sprm and the difference between the
    two CHPs. pchpOld may be nil. *plwMask indicates which portion of
    the lw is significant. Returns tYes or tNo depending on whether the
    values of pchpOld and pchpNew differ for the given sprm. Returns
    tMaybe on error.
***************************************************************************/
tribool RichTextDocument::_TGetLwFromChp(byte sprm, PCHP pchpNew, PCHP pchpOld, LegacyLong *plw, LegacyLong *plwMask)
{
    AssertIn(sprm, sprmMinChp, sprmLimChp);
    AssertVarMem(pchpNew);
    AssertNilOrVarMem(pchpOld);
    AssertVarMem(plw);
    AssertVarMem(plwMask);
    FormatHost host(this);
    auto config = host.Configuration();
    auto value = host.Character(*pchpNew);
    auto old = pchpOld ? host.Character(*pchpOld) : decltype(value){};
    MMU32 word, mask;
    auto services = host.Values();
    int result = mm_text_character_delta(sprm, &config, &value, pchpOld ? &old : nullptr, &word, &mask, &services);
    *plwMask = static_cast<LegacyLong>(mask);
    host.Rethrow();
    if (result > 0)
        *plw = static_cast<LegacyLong>(word);
    else
        TrashVar(plw);
    return result > 0 ? tYes : result == 0 ? tNo : tMaybe;
}

/***************************************************************************
    Get the character properties for the given character.
***************************************************************************/
void RichTextDocument::FetchChp(long cp, PCHP pchp, long *pcpMin, long *pcpLim)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac());
    AssertVarMem(pchp);
    AssertNilOrVarMem(pcpMin);
    AssertNilOrVarMem(pcpLim);
    FormatHost host(this);
    auto config = host.Configuration();
    auto services = host.Values();
    auto cache = host.CharacterCache();
    mm_text_fetch_character(&config, &cache, cp, CpMac(), &services);
    host.Publish(cache);
    host.Rethrow();
    *pchp = _chp;
    if (pcpMin)
        *pcpMin = _cpMinChp;
    if (pcpLim)
        *pcpLim = _cpLimChp;
}

/***************************************************************************
    Apply the given character properties to the given range of characters.
***************************************************************************/
bool RichTextDocument::FApplyChp(long cp, long ccp, PCHP pchp, PCHP pchpDiff, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertIn(cp, 0, CpMac() - 1);
    AssertIn(ccp, 1, CpMac() - cp);
    FormatHost host(this);
    host.chp = pchp;
    host.oldChp = pchpDiff;
    auto services = host.Edit();
    int result = mm_text_format_apply(0, nullptr, cp, ccp, CpMac(), 0, grfdoc, nullptr, nullptr, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Find the bounds of the whole paragraphs overlapping the range
    [*pcpMin, *pcpMin + *pccp). If fExpand is false, the bounds are of
    whole paragraphs totally contained in the range, with the possible
    exception of the trailing end of paragraph marker. If fExpand is
    true, *pcpMin will only decrease and *pcpLim will only increase;
    while if fExpand is false, *pcpMin will only increase and
    *pcpLim may decrease or at most increase past an end of
    paragraph marker and its associated line feed characters.
***************************************************************************/
void RichTextDocument::_GetParaBounds(long *pcpMin, long *pcpLim, bool fExpand)
{
    AssertVarMem(pcpMin);
    AssertVarMem(pcpLim);
    AssertIn(*pcpMin, 0, CpMac());
    AssertIn(*pcpLim, *pcpMin, CpMac() + 1);
    FormatHost host(this);
    auto services = host.Paragraphs();
    MMI32 first = *pcpMin, end = *pcpLim;
    MMI32 *limit = pcpMin == pcpLim ? &first : &end;
    mm_text_paragraph_bounds(&first, limit, CpMac(), fExpand, &services);
    *pcpMin = first;
    *pcpLim = *limit;
    host.Rethrow();
}

/***************************************************************************
    Get the long associated with the sprm and the difference between the
    two PAPs. ppapOld may be nil. *plwMask indicates which portion of
    the lw is significant. Returns tYes or tNo depending on whether the
    values of pchpOld and pchpNew differ for the given sprm. Returns
    tMaybe on error.
***************************************************************************/
tribool RichTextDocument::_TGetLwFromPap(byte sprm, PPAP ppapNew, PPAP ppapOld, LegacyLong *plw, LegacyLong *plwMask)
{
    AssertIn(sprm, sprmMinPap, sprmLimPap);
    AssertVarMem(ppapNew);
    AssertNilOrVarMem(ppapOld);
    AssertVarMem(plw);
    AssertVarMem(plwMask);
    FormatHost host(this);
    auto config = host.Configuration();
    auto value = host.Paragraph(*ppapNew);
    auto old = ppapOld ? host.Paragraph(*ppapOld) : decltype(value){};
    MMU32 word, mask;
    int result = mm_text_paragraph_delta(sprm, &config, &value, ppapOld ? &old : nullptr, &word, &mask);
    *plwMask = static_cast<LegacyLong>(mask);
    host.Rethrow();
    if (result > 0)
        *plw = static_cast<LegacyLong>(word);
    else
        TrashVar(plw);
    return result > 0 ? tYes : result == 0 ? tNo : tMaybe;
}

/***************************************************************************
    Get the paragraph properties for the paragraph containing the given
    character.
***************************************************************************/
void RichTextDocument::FetchPap(long cp, PPAP ppap, long *pcpMin, long *pcpLim)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac());
    AssertVarMem(ppap);
    AssertNilOrVarMem(pcpMin);
    AssertNilOrVarMem(pcpLim);
    FormatHost host(this);
    auto config = host.Configuration();
    auto services = host.Values();
    auto cache = host.ParagraphCache();
    mm_text_fetch_paragraph(&config, &cache, cp, CpMac(), &services);
    host.Publish(cache);
    host.Rethrow();
    *ppap = _pap;
    if (pcpMin)
        *pcpMin = _cpMinPap;
    if (pcpLim)
        *pcpLim = _cpLimPap;
}

/***************************************************************************
    Apply the given paragraph properties to the given range of characters.
    If fExpand is true, the properties of the current paragraph containing
    cp are set regardless of whether cp is at the beginning of the
    paragraph.
***************************************************************************/
bool RichTextDocument::FApplyPap(long cp, long ccp, PPAP ppap, PPAP ppapDiff, long *pcpMin, long *pcpLim, bool fExpand,
                                 ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccp, 0, CpMac() + 1 - cp);
    AssertVarMem(ppap);
    AssertNilOrVarMem(ppapDiff);
    AssertNilOrVarMem(pcpMin);
    AssertNilOrVarMem(pcpLim);
    FormatHost host(this);
    host.first = pcpMin;
    host.end = pcpLim;
    host.pap = ppap;
    host.oldPap = ppapDiff;
    auto services = host.Edit();
    int result = mm_text_format_apply(1, nullptr, cp, ccp, CpMac(), fExpand, grfdoc, nullptr, nullptr, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Set up undo for an action. If this succeeds, you must call either
    CancelUndo or CommitUndo.
***************************************************************************/
bool RichTextDocument::FSetUndo(long cp1, long cp2, long ccpIns)
{
    if (_cactSuspendUndo++ > 0 || _cundbMax == 0)
        return fTrue;

    Assert(_prtun == pvNil, "why is there an active undo record?");
    if (cp1 == cp2 && ccpIns == 0)
        return fTrue;

    if (pvNil == (_prtun = RichTextUndo::PrtunNew(_cactCombineUndo, this, cp1, cp2, ccpIns)))
    {
        ResumeUndo();
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Cancel undo.
***************************************************************************/
void RichTextDocument::CancelUndo(void)
{
    ResumeUndo();
    if (_cactSuspendUndo == 0)
        ReleasePpo(&_prtun);
}

/***************************************************************************
    Commit the setup undo.
***************************************************************************/
void RichTextDocument::CommitUndo(void)
{
    PRichTextUndo prtunPrev;

    ResumeUndo();
    if (_cactSuspendUndo != 0 || _prtun == pvNil)
        return;

    AssertPo(_prtun, 0);
    // see if this one can be combined with the last one
    if (_ipundbLimDone > 0)
    {
        _pglpundb->Get(_ipundbLimDone - 1, &prtunPrev);
        if (prtunPrev->FIs(kclsRichTextUndo) && prtunPrev->FCombine(_prtun))
        {
            ClearRedo();
            goto LDone;
        }
    }
    FAddUndo(_prtun);

LDone:
    ReleasePpo(&_prtun);
}

/***************************************************************************
    Replace cp to cp + ccpDel with ccpIns characters from prgch. If ccpIns
    is zero, prgch can be nil.
***************************************************************************/
bool RichTextDocument::FReplaceRgch(void *prgch, long ccpIns, long cp, long ccpDel, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertIn(ccpIns, 0, kcbMax);
    AssertPvCb(prgch, ccpIns * size(achar));
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);

    if (CpMac() + ccpIns >= kcpMaxTxrd)
    {
        PushErc(ercRtxdTooMuchText);
        return fFalse;
    }

    if (!FSetUndo(cp, cp + ccpDel, ccpIns))
        return fFalse;

    if (!RichTextDocument_PAR::FReplaceRgch(prgch, ccpIns, cp, ccpDel, fdocNil))
    {
        CancelUndo();
        return fFalse;
    }
    _AdjustMpe(cp, ccpIns, ccpDel);
    CommitUndo();
    AssertThis(fobjAssertFull);
    InvalAllDdg(cp, ccpIns, ccpDel, grfdoc);
    return fTrue;
}

/***************************************************************************
    Replace cp to cp + ccpDel with ccpIns characters from prgch, using
    the given chp and pap. If ccpIns is zero, prgch can be nil. pchp
    and/or ppap can be nil.
***************************************************************************/
bool RichTextDocument::FReplaceRgch(void *prgch, long ccpIns, long cp, long ccpDel, PCHP pchp, PPAP ppap, ulong grfdoc)
{
    AssertThis(0);
    AssertIn(ccpIns, 0, kcbMax);
    AssertPvCb(prgch, ccpIns * size(achar));
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    AssertNilOrVarMem(pchp);
    AssertNilOrVarMem(ppap);

    return _FReplaceCore(prgch, pvNil, fFalse, pvNil, 0, ccpIns, cp, ccpDel, pchp, ppap, grfdoc);
}

/***************************************************************************
    Replace cp to cp + ccpDel with ccpIns characters from prgch, pflo or pbsf,
    using the given chp and pap. If ccpIns is zero, prgch can be nil. pchp
    and/or ppap can be nil.
***************************************************************************/
bool RichTextDocument::_FReplaceCore(void *prgch, PFileLocation pflo, bool fCopy, PFileByteStream pbsf, long cpSrc, long ccpIns, long cp, long ccpDel,
                         PCHP pchp, PPAP ppap, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertIn(ccpIns, 0, kcbMax);
    AssertNilOrPvCb(prgch, ccpIns * size(achar));
    AssertNilOrPo(pflo, 0);
    AssertNilOrPo(pbsf, 0);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    AssertNilOrVarMem(pchp);
    AssertNilOrVarMem(ppap);
    SPVM rgspvm[sprmLimChp - sprmMinChp + sprmLimPap - sprmMinPap];
    long cspvmChp, cspvmPap;
    long cpMinUndo, cpLimUndo;

    if (CpMac() + ccpIns >= kcpMaxTxrd)
    {
        PushErc(ercRtxdTooMuchText);
        return fFalse;
    }

    cpMinUndo = cp;
    cpLimUndo = cp + ccpDel;

    cspvmChp = cspvmPap = 0;
    if (pvNil != pchp && ccpIns > 0 && !_FGetRgspvmFromChp(pchp, pvNil, rgspvm, &cspvmChp))
    {
        return fFalse;
    }

    if (pvNil != ppap && ccpIns > 0)
    {
        _GetParaBounds(&cpMinUndo, &cpLimUndo, fFalse);
        if (cpMinUndo < cpLimUndo && !_FGetRgspvmFromPap(ppap, pvNil, rgspvm + cspvmChp, &cspvmPap))
        {
            goto LFail;
        }
        cpMinUndo = LwMin(cp, cpMinUndo);
        cpLimUndo = LwMax(cp + ccpDel, cpLimUndo);
    }

    // now make sure that _pglmpe has enough room - we need at most 2
    // entries per sprm.
    if (!_pglmpe->FEnsureSpace(2 * (cspvmChp + cspvmPap)))
        goto LFail;

    if (!FSetUndo(cpMinUndo, cpLimUndo, cpLimUndo - cpMinUndo - ccpDel + ccpIns))
        goto LFail;

    if (pvNil != pflo)
    {
        Assert(cpSrc == 0 && ccpIns * size(achar) == pflo->cb, "bad flo");
        if (!FReplaceFlo(pflo, fCopy, cp, ccpDel, koskCur, fdocNil))
            goto LCancel;
    }
    else if (pvNil != pbsf)
    {
        if (!FReplaceBsf(pbsf, cpSrc, ccpIns, cp, ccpDel, fdocNil))
            goto LCancel;
    }
    else
    {
        Assert(cpSrc == 0, "bad cpSrc");
        AssertPvCb(prgch, ccpIns * size(achar));
        if (!FReplaceRgch(prgch, ccpIns, cp, ccpDel, fdocNil))
        {
        LCancel:
            CancelUndo();
        LFail:
            _ReleaseRgspvm(rgspvm, cspvmChp + cspvmPap);
            return fFalse;
        }
    }

    if (cspvmChp > 0)
        _ApplyRgspvm(cp, ccpIns, rgspvm, cspvmChp);
    if (cspvmPap > 0)
    {
        long cpMin = cp;
        long cpLim = cp + ccpIns;

        _GetParaBounds(&cpMin, &cpLim, fFalse);
        Assert(cpMin >= cpMinUndo, "new para before old one");
        Assert(cpLim <= cpLimUndo - ccpDel + ccpIns, "new para after old one");
        _ApplyRgspvm(cpMin, cpLim - cpMin, rgspvm + cspvmChp, cspvmPap);
    }
    CommitUndo();

    AssertThis(fobjAssertFull);
    InvalAllDdg(cpMinUndo, cpLimUndo - cpMinUndo - ccpDel + ccpIns, cpLimUndo - cpMinUndo, grfdoc);
    return fTrue;
}

/***************************************************************************
    Replace cp to cp + ccpDel with the characters in the given FileLocation.
***************************************************************************/
bool RichTextDocument::FReplaceFlo(PFileLocation pflo, bool fCopy, long cp, long ccpDel, short osk, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPo(pflo, 0);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    long ccpIns;
    FileLocation flo = *pflo;

    flo.pfil->AddRef();
    if (!flo.FTranslate(osk))
        goto LFail;

    ccpIns = flo.cb / size(achar);
    if (CpMac() + ccpIns >= kcpMaxTxrd)
    {
        PushErc(ercRtxdTooMuchText);
        goto LFail;
    }

    if (!FSetUndo(cp, cp + ccpDel, ccpIns))
        goto LFail;

    if (!RichTextDocument_PAR::FReplaceFlo(&flo, fCopy, cp, ccpDel, koskCur, fdocNil))
    {
        CancelUndo();
    LFail:
        ReleasePpo(&flo.pfil);
        return fFalse;
    }
    ReleasePpo(&flo.pfil);

    _AdjustMpe(cp, ccpIns, ccpDel);
    CommitUndo();
    AssertThis(fobjAssertFull);
    InvalAllDdg(cp, ccpIns, ccpDel, grfdoc);
    return fTrue;
}

/***************************************************************************
    Replace cp to cp + ccpDel with the characters in the given FileLocation, using
    the given chp and pap. pchp and/or ppap can be nil.
***************************************************************************/
bool RichTextDocument::FReplaceFlo(PFileLocation pflo, bool fCopy, long cp, long ccpDel, PCHP pchp, PPAP ppap, short osk, ulong grfdoc)
{
    AssertThis(0);
    AssertPo(pflo, 0);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    AssertNilOrVarMem(pchp);
    AssertNilOrVarMem(ppap);
    bool fRet;
    FileLocation flo = *pflo;

    flo.pfil->AddRef();
    fRet = flo.FTranslate(osk) && _FReplaceCore(pvNil, &flo, fCopy && pflo->pfil == flo.pfil, pvNil, 0, pflo->cb, cp,
                                                ccpDel, pchp, ppap, grfdoc);
    ReleasePpo(&flo.pfil);
    return fRet;
}

/***************************************************************************
    Replace cp to cpDst + ccpDel with ccpSrc characters from pbsfSrc starting
    at cpSrc.
***************************************************************************/
bool RichTextDocument::FReplaceBsf(PFileByteStream pbsfSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPo(pbsfSrc, 0);
    AssertIn(cpSrc, 0, pbsfSrc->IbMac() / size(achar) + 1);
    AssertIn(ccpSrc, 0, pbsfSrc->IbMac() / size(achar) + 1 - cpSrc);
    AssertIn(cpDst, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cpDst);

    if (CpMac() + ccpSrc >= kcpMaxTxrd)
    {
        PushErc(ercRtxdTooMuchText);
        return fFalse;
    }

    if (!FSetUndo(cpDst, cpDst + ccpDel, ccpSrc))
        return fFalse;

    if (!RichTextDocument_PAR::FReplaceBsf(pbsfSrc, cpSrc, ccpSrc, cpDst, ccpDel, fdocNil))
    {
        CancelUndo();
        return fFalse;
    }

    _AdjustMpe(cpDst, ccpSrc, ccpDel);
    CommitUndo();
    AssertThis(fobjAssertFull);
    InvalAllDdg(cpDst, ccpSrc, ccpDel, grfdoc);
    return fTrue;
}

/***************************************************************************
    Replace cp to cpDst + ccpDel with ccpSrc characters from pbsfSrc starting
    at cpSrc, using the given chp and pap. pchp and/or ppap can be nil.
***************************************************************************/
bool RichTextDocument::FReplaceBsf(PFileByteStream pbsfSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, PCHP pchp, PPAP ppap,
                       ulong grfdoc)
{
    AssertThis(0);
    AssertPo(pbsfSrc, 0);
    AssertIn(cpSrc, 0, pbsfSrc->IbMac() + 1);
    AssertIn(ccpSrc, 0, pbsfSrc->IbMac() + 1 - cpSrc);
    AssertIn(cpDst, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cpDst);
    AssertNilOrVarMem(pchp);
    AssertNilOrVarMem(ppap);

    return _FReplaceCore(pvNil, pvNil, fFalse, pbsfSrc, cpSrc, ccpSrc, cpDst, ccpDel, pchp, ppap, grfdoc);
}

/***************************************************************************
    Replace cp to cpDst + ccpDel with ccpSrc characters from pbsfSrc starting
    at cpSrc, using the given chp and pap. pchp and/or ppap can be nil.
***************************************************************************/
bool RichTextDocument::FReplaceTxtb(PTextDocumentBase ptxtbSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, ulong grfdoc)
{
    AssertThis(0);
    AssertPo(ptxtbSrc, 0);
    AssertIn(cpSrc, 0, ptxtbSrc->CpMac() + 1);
    AssertIn(ccpSrc, 0, ptxtbSrc->CpMac() + 1 - cpSrc);
    AssertIn(cpDst, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cpDst);

    // NOTE: this cast to PRichTextDocument is just a rude hack to get around
    // an oddity of C++. RichTextDocument cannot access _pbsf in a TextDocumentBase, only in a RichTextDocument.
    // ptxtbSrc is probably not a RichTextDocument, but the cast still works.
    return FReplaceBsf(((PRichTextDocument)ptxtbSrc)->_pbsf, cpSrc, ccpSrc, cpDst, ccpDel, grfdoc);
}

/***************************************************************************
    Replace cp to cpDst + ccpDel with ccpSrc characters from pbsfSrc starting
    at cpSrc, using the given chp and pap. pchp and/or ppap can be nil.
***************************************************************************/
bool RichTextDocument::FReplaceTxtb(PTextDocumentBase ptxtbSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, PCHP pchp, PPAP ppap,
                        ulong grfdoc)
{
    AssertThis(0);
    AssertPo(ptxtbSrc, 0);
    AssertIn(cpSrc, 0, ptxtbSrc->CpMac() + 1);
    AssertIn(ccpSrc, 0, ptxtbSrc->CpMac() + 1 - cpSrc);
    AssertIn(cpDst, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cpDst);
    AssertNilOrVarMem(pchp);
    AssertNilOrVarMem(ppap);

    // NOTE: this cast to PRichTextDocument is just a rude hack to get around
    // an oddity of C++. RichTextDocument cannot access _pbsf in a TextDocumentBase, only in a RichTextDocument.
    // ptxtbSrc is probably not a RichTextDocument, but the cast still works.
    return _FReplaceCore(pvNil, pvNil, fFalse, ((PRichTextDocument)ptxtbSrc)->_pbsf, cpSrc, ccpSrc, cpDst, ccpDel, pchp, ppap,
                         grfdoc);
}

/***************************************************************************
    Replace stuff in this document with stuff in the given document.
    REVIEW shonk: this doesn't preserve font size if the two docs have
    different default font sizes!
***************************************************************************/
bool RichTextDocument::FReplaceTxrd(PRichTextDocument ptxrd, long cpSrc, long ccpSrc, long cpDst, long ccpDel, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPo(ptxrd, 0);
    AssertIn(cpSrc, 0, ptxrd->CpMac());
    AssertIn(ccpSrc, 0, ptxrd->CpMac() + 1 - cpSrc);
    AssertIn(cpDst, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() + 1 - cpDst);
    Assert(ptxrd != this, "can't copy from a rich text doc to itself!");
    long dcp = 0;

    // REVIEW shonk: is there an easy way to make this atomic?

    if (CpMac() + ccpSrc >= kcpMaxTxrd)
    {
        PushErc(ercRtxdTooMuchText);
        return fFalse;
    }

    if (!FSetUndo(cpDst, cpDst + ccpDel, ccpSrc))
        return fFalse;

    // protect the trailing EOP
    if (cpDst + ccpDel == CpMac())
    {
#ifdef DEBUG
        achar ch;
        ptxrd->FetchRgch(cpSrc + ccpSrc - 1, 1, &ch);
        Assert(ch == kchReturn, "trying to replace trailing EOP");
#endif // DEBUG
        Assert(ccpDel > 0 && ccpSrc > 0, "bad parameters to FReplaceTxrd");
        ccpDel--;
        ccpSrc--;
        dcp = 1;
    }

    // insert the text
    if ((ccpSrc > 0 || ccpDel > 0) && !FReplaceBsf(ptxrd->_pbsf, cpSrc, ccpSrc, cpDst, ccpDel, fdocNil))
    {
        CancelUndo();
        return fFalse;
    }

    if (ccpSrc > 0)
        _CopyProps(ptxrd, cpSrc, cpDst, ccpSrc, ccpSrc, 0, sprmMinPap);

    if (ccpSrc + dcp > 0)
    {
        long ccpSrcPap;
        long cpMinPap = cpDst;
        long cpLimPap = cpDst + ccpSrc;

        _GetParaBounds(&cpMinPap, &cpLimPap, fFalse);
        ccpSrcPap = ccpSrc - cpMinPap + cpDst;
        if (ccpSrcPap > 0 && cpMinPap < cpLimPap)
        {
            _CopyProps(ptxrd, cpSrc + cpMinPap - cpDst, cpMinPap, ccpSrcPap, cpLimPap - cpMinPap, sprmMinPap,
                       sprmMinObj);
        }
    }

    if (ccpSrc > 0)
    {
        // object properties
        long cp;
        byte sprm;
        long impe, impeNew;
        MPE mpe, mpeNew;
        void *pv;
        bool fRet;

        ptxrd->_FFindMpe(_SpcpFromSprmCp(sprmMinObj, cpSrc), pvNil, pvNil, &impe);
        while (impe < ptxrd->_pglmpe->IvMac())
        {
            ptxrd->_pglmpe->Get(impe++, &mpe);
            cp = _CpFromSpcp(mpe.spcp);
            if (!FIn(cp, cpSrc, cpSrc + ccpSrc))
                continue;
            sprm = _SprmFromSpcp(mpe.spcp);
            mpeNew.spcp = _SpcpFromSprmCp(sprm, cpDst + cp - cpSrc);
            pv = ptxrd->_pagcact->PvLock(mpe.lw - 1);
            fRet = _FEnsureInAg(sprm, pv, ptxrd->_pagcact->Cb(mpe.lw - 1), &mpeNew.lw);
            ptxrd->_pagcact->Unlock();
            if (!fRet)
                continue;

            // insert the object sprm
            if (_FFindMpe(mpeNew.spcp, pvNil, pvNil, &impeNew))
                impeNew++;
            if (!_pglmpe->FInsert(impeNew, &mpeNew))
                _ReleaseInAg(mpeNew.lw);
        }
    }

    CommitUndo();
    AssertThis(fobjAssertFull);
    InvalAllDdg(cpDst, ccpSrc + dcp, ccpDel + dcp, grfdoc);
    return fTrue;
}

/***************************************************************************
    Copy properties from a RichTextDocument to this one. Properties from sprmMin to
    sprmLim are copied.
***************************************************************************/
void RichTextDocument::_CopyProps(PRichTextDocument ptxrd, long cpSrc, long cpDst, long ccpSrc, long ccpDst, byte sprmMin, byte sprmLim)
{
    AssertThis(0);
    AssertPo(ptxrd, 0);
    AssertIn(cpSrc, 0, ptxrd->CpMac());
    AssertIn(cpDst, 0, CpMac());
    AssertIn(ccpSrc, 1, ptxrd->CpMac() + 1 - cpSrc);
    AssertIn(ccpDst, 1, CpMac() + 1 - cpDst);
    SPVM spvm;
    byte sprm;
    long impe;
    long cpMin, cpLim;
    bool fRet;
    long lw;
    LegacyLong cb;
    void *pv;

    // zero the character properties over the inserted text
    spvm.sprm = sprmNil;
    spvm.lw = 0;
    spvm.lwMask = -1;
    _FFindMpe(_SpcpFromSprmCp(sprmMin, 0), pvNil, pvNil, &impe);
    while (_FFetchProp(impe++, &sprm) && sprm < sprmLim)
    {
        if (spvm.sprm == sprm)
            continue;
        spvm.sprm = sprm;
        if (_pglmpe->FEnsureSpace(2))
            _ApplyRgspvm(cpDst, ccpDst, &spvm, 1);
        else
            Warn("growing _pglmpe failed");
    }

    // apply the source properties
    ptxrd->_FFindMpe(_SpcpFromSprmCp(sprmMin, 0), pvNil, pvNil, &impe);
    while (ptxrd->_FFetchProp(impe++, &sprm, &lw, &cpMin, &cpLim) && sprm < sprmLim)
    {
        // if this MPE doesn't overlap our source range, ignore it.
        if (cpLim <= cpSrc || cpMin >= cpSrc + ccpSrc)
            continue;

        // if this MPE goes to the end of the source range, also carry it
        // to the end of the destination
        if (cpLim >= cpSrc + ccpSrc)
            cpLim = cpSrc + ccpDst;

        // adjust the min and lim to destination cp values and restrict
        // them to [cpDst, cpDst + ccpDst).
        if ((cpMin += cpDst - cpSrc) < cpDst)
            cpMin = cpDst;
        if ((cpLim += cpDst - cpSrc) > cpDst + ccpDst)
            cpLim = cpDst + ccpDst;

        // if the range is empty, ignore it
        if (cpMin >= cpLim)
            continue;

        spvm.sprm = sprm;
        if (_FSprmInAg(sprm) && lw > 0)
        {
            cb = ptxrd->_pagcact->Cb(lw - 1);
            pv = ptxrd->_pagcact->PvLock(lw - 1);
            fRet = _FEnsureInAg(sprm, pv, cb, &spvm.lw);
            ptxrd->_pagcact->Unlock();
            if (!fRet)
            {
                Warn("ensure in ag failed");
                continue;
            }
        }
        else
            spvm.lw = lw;

        if (_pglmpe->FEnsureSpace(2))
            _ApplyRgspvm(cpMin, cpLim - cpMin, &spvm, 1);
        else
        {
            Warn("growing _pglmpe failed");
            _ReleaseRgspvm(&spvm, 1);
        }
    }
}

/***************************************************************************
    Find the first object at or after cpMin. Put its location in *pcp and
    allocate a buffer to hold its extra data and put it in *ppv (if ppv is
    not nil). If an object was found, but the buffer couldn't be allocated,
    *ppv is set to nil.
***************************************************************************/
bool RichTextDocument::FFetchObject(long cpMin, long *pcp, void **ppv, long *pcb)
{
    AssertThis(0);
    AssertIn(cpMin, 0, CpMac());
    AssertVarMem(pcp);
    AssertNilOrVarMem(ppv);
    AssertNilOrVarMem(pcb);

    MPE mpe;
    long impe;
    LegacyLong cb;
    bool fRet;

    fRet = _FFindMpe(_SpcpFromSprmCp(sprmObject, cpMin), &mpe, pcp, &impe);
    if (fRet && cpMin == _CpFromSpcp(mpe.spcp))
        *pcp = cpMin;
    else if (*pcp >= CpMac())
        return fFalse;
    else
    {
        if (fRet)
            impe++;
        Assert(impe < _pglmpe->IvMac(), "bad cpLim from _FFindMpe");
        _pglmpe->Get(impe, &mpe);
        Assert(_CpFromSpcp(mpe.spcp) == *pcp, "_FFindMpe messed up");
    }

    if (pvNil != pcb)
        *pcb = _pagcact->Cb(mpe.lw - 1);
    if (pvNil != ppv)
    {
        mpe.lw--;
        AssertIn(mpe.lw, 0, _pagcact->IvMac());
        cb = _pagcact->Cb(mpe.lw);
        if (FAllocPv(ppv, cb, fmemNil, mprNormal))
            CopyPb((byte *)_pagcact->QvGet(mpe.lw), *ppv, cb);
    }

    return fTrue;
}

/***************************************************************************
    Insert a picture into the rich text document.
***************************************************************************/
bool RichTextDocument::FInsertObject(void *pv, long cb, long cp, long ccpDel, PCHP pchp, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPvCb(pv, cb);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    AssertNilOrVarMem(pchp);
    SPVM rgspvm[sprmLimChp - sprmMinChp];
    MPE mpe;
    long impe;
    achar ch = kchObject;
    long cspvmChp = 0;

    BumpCombineUndo();
    if (!FSetUndo(cp, cp + ccpDel, 1))
        return fFalse;

    mpe.spcp = _SpcpFromSprmCp(sprmObject, cp);
    if (!_FEnsureInAg(sprmObject, pv, cb, &mpe.lw))
        goto LFail;

    if (pvNil != pchp && !_FGetRgspvmFromChp(pchp, pvNil, rgspvm, &cspvmChp))
        goto LFail;

    // now make sure that _pglmpe has enough room - we need at most 2
    // entries per sprm, plus one for the object
    if (!_pglmpe->FEnsureSpace(2 * cspvmChp + 1))
        goto LFail;

    if (!FReplaceRgch(&ch, 1, cp, ccpDel, fdocNil))
    {
        _ReleaseRgspvm(rgspvm, cspvmChp);
    LFail:
        _ReleaseSprmLw(sprmObject, mpe.lw);
        CancelUndo();
        return fFalse;
    }

    if (cspvmChp > 0)
        _ApplyRgspvm(cp, 1, rgspvm, cspvmChp);

    // insert the object sprm
    if (_FFindMpe(mpe.spcp, pvNil, pvNil, &impe))
        impe++;
    AssertDo(_pglmpe->FInsert(impe, &mpe), "should have been ensured");
    CommitUndo();
    BumpCombineUndo();

    AssertThis(fobjAssertFull);
    InvalAllDdg(cp, 1, ccpDel, grfdoc);
    return fTrue;
}

/***************************************************************************
    Insert a picture into the rich text document.
***************************************************************************/
bool RichTextDocument::FApplyObjectProps(void *pv, long cb, long cp, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPvCb(pv, cb);
    AssertIn(cp, 0, CpMac());
    MPE mpe;
    long impe;
    long lwOld;

    if (!_FFindMpe(_SpcpFromSprmCp(sprmObject, cp), &mpe, pvNil, &impe) || cp != _CpFromSpcp(mpe.spcp))
    {
        Bug("cp not an object");
        return fFalse;
    }

    BumpCombineUndo();
    if (!FSetUndo(cp, cp + 1, 1))
        return fFalse;

    lwOld = mpe.lw;
    if (!_FEnsureInAg(sprmObject, pv, cb, &mpe.lw))
    {
        CancelUndo();
        return fFalse;
    }

    _pglmpe->Put(impe, &mpe);
    _ReleaseSprmLw(sprmObject, lwOld);
    CommitUndo();
    BumpCombineUndo();

    AssertThis(fobjAssertFull);
    InvalAllDdg(cp, 1, 1, grfdoc);
    return fTrue;
}

/***************************************************************************
    Get the bounds of an object.
***************************************************************************/
bool RichTextDocument::FGetObjectRc(long cp, PGraphicsEnvironment pgnv, PCHP pchp, RC *prc)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac());
    AssertPo(pgnv, 0);
    AssertVarMem(pchp);
    AssertVarMem(prc);
    MPE mpe;
    ulong spcp = _SpcpFromSprmCp(sprmObject, cp);

    if (!_FFindMpe(spcp, &mpe) || _CpFromSpcp(mpe.spcp) != cp)
        return fFalse;

    return _FGetObjectRc(mpe.lw - 1, _SprmFromSpcp(mpe.spcp), pgnv, pchp, prc);
}

/***************************************************************************
    Get the object bounds from the AllocatedGroup entry.
***************************************************************************/
bool RichTextDocument::_FGetObjectRc(long icact, byte sprm, PGraphicsEnvironment pgnv, PCHP pchp, RC *prc)
{
    AssertIn(icact, 0, _pagcact->IvMac());
    Assert(sprm >= sprmObject, 0);
    AssertPo(pgnv, 0);
    AssertVarMem(pchp);
    AssertVarMem(prc);

    // RichTextDocument has no acceptable object types
    TrashVar(prc);
    return fFalse;
}

/***************************************************************************
    Draw an object.
***************************************************************************/
bool RichTextDocument::FDrawObject(long cp, PGraphicsEnvironment pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac());
    AssertPo(pgnv, 0);
    AssertVarMem(pxp);
    AssertVarMem(pchp);
    AssertVarMem(prcClip);
    MPE mpe;
    ulong spcp = _SpcpFromSprmCp(sprmObject, cp);

    if (!_FFindMpe(spcp, &mpe) || _CpFromSpcp(mpe.spcp) != cp)
        return fFalse;

    return _FDrawObject(mpe.lw - 1, _SprmFromSpcp(mpe.spcp), pgnv, pxp, yp, pchp, prcClip);
}

/***************************************************************************
    Draw the object.
***************************************************************************/
bool RichTextDocument::_FDrawObject(long icact, byte sprm, PGraphicsEnvironment pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip)
{
    AssertIn(icact, 0, _pagcact->IvMac());
    Assert(sprm >= sprmObject, 0);
    AssertPo(pgnv, 0);
    AssertVarMem(pxp);
    AssertVarMem(pchp);
    AssertVarMem(prcClip);

    // RichTextDocument has no acceptable object types
    return fFalse;
}

/***************************************************************************
    Create a new rich text undo object for the given rich text document.
***************************************************************************/
PRichTextUndo RichTextUndo::PrtunNew(long cactCombine, PRichTextDocument ptxrd, long cp1, long cp2, long ccpIns)
{
    AssertPo(ptxrd, 0);
    AssertIn(cp1, 0, ptxrd->CpMac() + 1);
    AssertIn(cp2, 0, ptxrd->CpMac() + 1);
    AssertIn(ccpIns, 0, kcbMax);
    PRichTextUndo prtun;

    SortLw(&cp1, &cp2);
    AssertIn(cp1, 0, ptxrd->CpMac());
    if (pvNil == (prtun = NewObj RichTextUndo))
        return pvNil;

    prtun->_cactCombine = cactCombine;
    prtun->_cpMin = cp1;
    prtun->_ccpIns = ccpIns;
    if (cp1 < cp2)
    {
        // copy the piece of the txrd.
        if (pvNil == (prtun->_ptxrd = RichTextDocument::PtxrdNew(pvNil)))
            goto LFail;
        prtun->_ptxrd->SetInternal();
        prtun->_ptxrd->SuspendUndo();
        if (!prtun->_ptxrd->FReplaceTxrd(ptxrd, cp1, cp2 - cp1, 0, 0))
        {
        LFail:
            ReleasePpo(&prtun);
        }
    }

    AssertNilOrPo(prtun, 0);
    return prtun;
}

/***************************************************************************
    Destructor for a rich text undo object.
***************************************************************************/
RichTextUndo::~RichTextUndo(void)
{
    ReleasePpo(&_ptxrd);
}

/***************************************************************************
    Undo this rich text undo object on the given document.
***************************************************************************/
bool RichTextUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    PRichTextDocument ptxrd;
    long ccpIns;
    PRichTextDocument ptxrdNew = pvNil;

    if (!pdocb->FIs(kclsRichTextDocument))
        goto LAssert;
    ptxrd = (PRichTextDocument)pdocb;
    AssertPo(ptxrd, 0);

    if (!FIn(_cpMin, 0, ptxrd->CpMac()) || !FIn(_ccpIns, 0, ptxrd->CpMac() + 1 - _cpMin))
    {
    LAssert:
        Bug("This rich text undo object cannot be applied to this document");
        return fFalse;
    }

    if (_ccpIns > 0)
    {
        // copy the piece of the txrd.
        if (pvNil == (ptxrdNew = RichTextDocument::PtxrdNew(pvNil)))
            return fFalse;
        ptxrdNew->SetInternal();
        ptxrdNew->SuspendUndo();
        if (!ptxrdNew->FReplaceTxrd(ptxrd, _cpMin, _ccpIns, 0, 0))
            goto LFail;
    }

    ptxrd->SuspendUndo();
    ptxrd->HideSel();
    if (pvNil == _ptxrd)
    {
        ccpIns = 0;
        if (!ptxrd->FReplaceRgch(pvNil, 0, _cpMin, _ccpIns))
            goto LFail;
    }
    else
    {
        ccpIns = _ptxrd->CpMac() - 1;
        if (!ptxrd->FReplaceTxrd(_ptxrd, 0, ccpIns, _cpMin, _ccpIns))
        {
        LFail:
            ReleasePpo(&ptxrdNew);
            ptxrd->ResumeUndo();
            return fFalse;
        }
    }
    ptxrd->ResumeUndo();
    ptxrd->SetSel(_cpMin, _cpMin + ccpIns, ginNil);
    ptxrd->ShowSel();
    ptxrd->SetSel(_cpMin + ccpIns, _cpMin + ccpIns);

    ReleasePpo(&_ptxrd);
    _ptxrd = ptxrdNew;
    _ccpIns = ccpIns;
    return fTrue;
}

/***************************************************************************
    Redo this rich text undo object on the given document.
***************************************************************************/
bool RichTextUndo::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    return FUndo(pdocb);
}

/***************************************************************************
    If possible, combine the given rtun with this one. Returns success.
***************************************************************************/
bool RichTextUndo::FCombine(PRichTextUndo prtun)
{
    AssertThis(0);
    AssertPo(prtun, 0);
    long ccp;

    // if the _cactCombine numbers are different, they can't be combined
    if (prtun->_cactCombine != _cactCombine)
        return fFalse;

    // if the new record doesn't delete anything and the new text is
    // at the end of the old text, just adjust _ccpIns.
    if (prtun->_ptxrd == pvNil && _cpMin + _ccpIns == prtun->_cpMin)
    {
        _ccpIns += prtun->_ccpIns;
        return fTrue;
    }

    // if either of the new records inserts anything, we can't combine the two
    if (prtun->_ccpIns != 0 || _ccpIns != 0)
        return fFalse;

    // handle repeated delete keys
    AssertPo(_ptxrd, 0);
    AssertPo(prtun->_ptxrd, 0);
    ccp = prtun->_ptxrd->CpMac() - 1;
    if (prtun->_cpMin == _cpMin)
    {
        return _ptxrd->FReplaceTxrd(prtun->_ptxrd, 0, ccp, _ptxrd->CpMac() - 1, 0);
    }

    // handle repeated backspace keys
    if (prtun->_cpMin + ccp == _cpMin)
    {
        if (!_ptxrd->FReplaceTxrd(prtun->_ptxrd, 0, ccp, 0, 0))
            return fFalse;
        _cpMin = prtun->_cpMin;
        return fTrue;
    }

    return fFalse;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a RichTextUndo.
***************************************************************************/
void RichTextUndo::AssertValid(LegacyUlong grf)
{
    RichTextUndo_PAR::AssertValid(grf);
    AssertNilOrPo(_ptxrd, 0);
    AssertIn(_cpMin, 0, kcbMax);
    AssertIn(_ccpIns, 0, kcbMax);
    Assert(_ccpIns > 0 || _ptxrd != pvNil, "empty RichTextUndo");
}

/***************************************************************************
    Mark memory for the RichTextUndo.
***************************************************************************/
void RichTextUndo::MarkMem(void)
{
    AssertThis(fobjAssertFull);
    RichTextUndo_PAR::MarkMem();
    MarkMemObj(_ptxrd);
}
#endif // DEBUG

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
#include "clipboard_view_host.h"
ASSERTNAME
#include "rich_text_store_host.h"
#include "mm/text_live.h"
#include "text_document_host.h"
#include "text_materialize_host.h"
#include "text_properties_host.h"
#include "text_format_host.h"
#include "text_access_host.h"
#include "text_replace_host.h"
#include "text_copy_host.h"
#include "text_undo_host.h"

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
    AssertIn(_textCache.first, 0, cpMac + 1);
    AssertIn(_textCache.end, 0, cpMac + 1);
    AssertIn(_textCache.end, _textCache.first, _textCache.first + MM_TEXT_CACHE_UNITS + 1);
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
TextDocumentBase::TextDocumentBase(PDocumentBase pdocb, ulong grfdoc)
    : TextDocumentBase_PAR(pdocb, grfdoc), _textCache{_rgchCache, 0, 0, sizeof(achar)}
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
    int result = mm_text_base_init(MM_TEXT_PLAIN, &config, pfni, pbsf, osk, &services);
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
    const MMU32 bytes = _pbsf->CorePieces()->length;
    Assert(bytes % size(achar) == 0, "IbMac() not divisible by size(achar)");

    return bytes / size(achar);
}

/***************************************************************************
    Suspend undo. This increments a count.
***************************************************************************/
void TextDocumentBase::SuspendUndo(void)
{
    AssertThis(0);
    mm_text_undo_suspend(&_textUndo);
}

/***************************************************************************
    Resume undo. This decrements a count.
***************************************************************************/
void TextDocumentBase::ResumeUndo(void)
{
    AssertThis(0);
    Assert(_textUndo.suspended > 0, "bad _cactSuspendUndo");
    mm_text_undo_resume(&_textUndo);
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

    mm_text_undo_suspend(&_textUndo);
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
    Increments the undo combine counter. This is used to determine
    whether the new undo record can be combined with the previous one
    (during typing etc).
***************************************************************************/
void TextDocumentBase::BumpCombineUndo(void)
{
    AssertThis(0);
    mm_text_undo_bump(&_textUndo);
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
    AccessHost host(this);
    auto services = host.Services();
    MMI32 first, end;
    int result = mm_text_find(prgch, cch, sizeof(achar), cpMin, CpMac(), fCaseSensitive, &first, &end, &services);
    host.Rethrow();
    if (result > 0)
    {
        *pcpMin = first;
        *pcpLim = end;
    }
    else
    {
        TrashVar(pcpMin);
        TrashVar(pcpLim);
    }
    return result > 0;
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
    AssertIn(cpLim - cpMin, 0, MM_TEXT_CACHE_UNITS + 1);
    const int result = mm_text_cache_fill(&_textCache, cpMin, cpLim, _pbsf->CorePieces());
    Assert(result == MM_DATA_OK || result == MM_DATA_IO, "invalid stream fetch");
}

/***************************************************************************
    Characters have changed, so fix the cache.
***************************************************************************/
void TextDocumentBase::_InvalCache(long cp, long ccpIns, long ccpDel)
{
    mm_text_cache_invalidate(&_textCache, cp, ccpIns, ccpDel);
}

/***************************************************************************
    Fetch a character of the stream through the cache.
***************************************************************************/
achar TextDocumentBase::_ChFetch(long cp)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac() + 1);
    AccessHost host(this);
    auto services = host.Services();
    MMU32 value = 0;
    mm_text_access_character(cp, &value, &services);
    host.Rethrow();
    return static_cast<achar>(value);
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
    AccessHost host(this);
    auto services = host.Services();
    mm_text_access_fetch(cp, ccp, prgch, &services);
    host.Rethrow();
}

/***************************************************************************
    Returns non-zero iff cp is the beginning of a paragraph.
***************************************************************************/
bool TextDocumentBase::FMinPara(long cp)
{
    AssertThis(0);
    AccessHost host(this);
    auto services = host.Services();
    MMI32 value = 0;
    mm_text_navigate(MM_TEXT_NAV_IS_START, cp, 0, CpMac(), &value, &services);
    host.Rethrow();
    return value != 0;
}

/***************************************************************************
    Find the beginning of the paragraph that cp is in. If cp <= 0,
    returns 0. If cp >= CpMac(), returns the beginning of the last
    paragraph.
***************************************************************************/
long TextDocumentBase::CpMinPara(long cp)
{
    AssertThis(0);
    AccessHost host(this);
    auto services = host.Services();
    MMI32 value = 0;
    mm_text_navigate(MM_TEXT_NAV_START, cp, 0, CpMac(), &value, &services);
    host.Rethrow();
    return value;
}

/***************************************************************************
    Find the end of the paragraph that cp is in. If cp < 0, returns 0.
    If cp >= CpMac(), returns CpMac().
***************************************************************************/
long TextDocumentBase::CpLimPara(long cp)
{
    AssertThis(0);
    AccessHost host(this);
    auto services = host.Services();
    MMI32 value = 0;
    mm_text_navigate(MM_TEXT_NAV_END, cp, 0, CpMac(), &value, &services);
    host.Rethrow();
    return value;
}

/***************************************************************************
    Return cp of the previous character, skipping line feed characters. If
    fWord is true, skip to the beginning of a word.
***************************************************************************/
long TextDocumentBase::CpPrev(long cp, bool fWord)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac() + 1);
    AccessHost host(this);
    auto services = host.Services();
    MMI32 value = 0;
    mm_text_navigate(MM_TEXT_NAV_PREVIOUS, cp, fWord, CpMac(), &value, &services);
    host.Rethrow();
    return value;
}

/***************************************************************************
    Return cp of the next character, skipping line feed characters. If
    fWord is true, skip to the beginning of the next word.
***************************************************************************/
long TextDocumentBase::CpNext(long cp, bool fWord)
{
    AssertThis(0);
    AssertIn(cp, 0, CpMac() + 1);
    AccessHost host(this);
    auto services = host.Services();
    MMI32 value = 0;
    mm_text_navigate(MM_TEXT_NAV_NEXT, cp, fWord, CpMac(), &value, &services);
    host.Rethrow();
    return value;
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

    ReplaceHost host(this);
    auto services = host.Services();
    auto request = host.Request(MM_TEXT_REPLACE_BYTES, prgch, ccpIns, cp, ccpDel, grfdoc);
    int result = mm_text_replace_base(sizeof(achar), &request, &services);
    host.Rethrow();
    return result > 0;
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
    ReplaceHost host(this);
    auto services = host.Services();
    auto request = host.Request(MM_TEXT_REPLACE_LOCATION, nullptr, 0, cp, ccpDel, grfdoc);
    request.location = RichTextStoreHost::Location(*pflo);
    request.copy = fCopy;
    request.system = osk;
    int result = mm_text_replace_base(sizeof(achar), &request, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Replace cp to cpDst + ccpDel with ccpSrc characters from pbsfSrc starting
    at cpSrc.
***************************************************************************/
bool TextDocumentBase::FReplaceBsf(PFileByteStream pbsfSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel,
                                   ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPo(pbsfSrc, 0);
    AssertIn(cpSrc, 0, pbsfSrc->IbMac() / size(achar) + 1);
    AssertIn(ccpSrc, 0, pbsfSrc->IbMac() / size(achar) + 1 - cpSrc);
    AssertIn(cpDst, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cpDst);

    ReplaceHost host(this);
    auto services = host.Services();
    auto request = host.Request(MM_TEXT_REPLACE_STREAM, nullptr, ccpSrc, cpDst, ccpDel, grfdoc);
    request.stream = pbsfSrc;
    request.source = cpSrc;
    int result = mm_text_replace_base(sizeof(achar), &request, &services);
    host.Rethrow();
    return result > 0;
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
    struct ExportHost : ClipboardViewHost
    {
        PTextDocumentBase document;
        PClipboardObject clipboard;
        static ExportHost &Get(void *p) { return *static_cast<ExportHost *>(p); }
    } host;
    host.document = this;
    host.clipboard = pclip;
    const MMClipboardTextExport services = {
        &host, kclfmText, sizeof(achar),
        [](void *p) {
            auto *pieces = ExportHost::Get(p).document->_pbsf->CorePieces();
            Assert(pieces->length % sizeof(achar) == 0, "stream has a partial character");
            return pieces;
        },
        [](void *p) {
            auto &h = ExportHost::Get(p);
            return h.Guard([&] { return h.clipboard->FInitExport(); });
        },
        [](void *p, MMI32 bytes, MMI32 format, void **out) {
            auto &h = ExportHost::Get(p);
            return h.Guard([&] { *out = h.clipboard->PvExport(bytes, format); return *out != nullptr; });
        },
        [](void *p) {
            auto &h = ExportHost::Get(p);
            return h.Guard([&] { h.clipboard->EndExport(); return 1; });
        }};
    host.Check(mm_clipboard_export_text(&services));
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
    Assert(_properties && mm_text_collection_width(_properties) == 8, "invalid property rows");
    Assert(!_arguments || mm_text_collection_width(_arguments) == 4, "invalid argument rows");
    AssertIn(_dypFontDef, 1, kswMax);
    AssertIn(_pbsf->IbMac() / size(achar), 1, kcpMaxTxrd);

    if (grfobj & fobjAssertFull)
    {
        MMU32 previous = 0xffffffffUL, words[2];
        for (MMU32 i = mm_text_collection_count(_properties); i-- > 0;)
        {
            AssertDo(mm_text_collection_property(_properties, i, words), "invalid property row");
            Assert(words[0] < previous, "non-increasing mpe's");
            previous = words[0];
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
    const auto properties = mm_text_collection_property_array(_properties, nullptr);
    const auto arguments = mm_text_collection_argument_group(_arguments);
    if (properties)
        MarkPv(properties->storage->buffers[0].data);
    if (arguments)
    {
        MarkPv(arguments->storage->buffers[0].data);
        MarkPv(arguments->storage->buffers[1].data);
    }
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
    MovieArchiveHost movie{pcfl};
    return _FReadArchive(movie.Editor(), pcfl->ReadOwner(), ctg, cno, fCopyText);
}

bool RichTextDocument::_FReadArchive(const MMMovieArchiveEditor &editor, MMChunkOwnerRead *reader,
                                     ChunkTagOrType ctg, ChunkNumber cno, bool fCopyText)
{
    MaterializeHost host(this, editor, reader);
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
    DocumentHost host(this);
    auto services = host.FontArgument();
    int result = mm_text_open_font_argument(_arguments, icact, sprm, &services);
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
    AssertPo(pcfl, 0);
    MovieArchiveHost archive{pcfl};
    return FSaveToArchive(archive.Editor(), archive.Writer(), pcki, fRedirectText);
}

bool RichTextDocument::FSaveToArchive(const MMMovieArchiveEditor &editor, const MMArchiveWriter &archive,
                                     ChunkIdentification *pcki, bool fRedirectText)
{
    AssertThis(fobjAssertFull);
    AssertVarMem(pcki);
    RichTextStoreHost::Host host{editor};
    host.UseLiveCollections();
    MMRichTextLiveServices services{};
    services.write = host.Write();
    services.text = _pbsf->CorePieces();
    services.archive = &archive;
    services.error = [](void *) { PushErc(ercRtxdSaveFailed); };
    MMRichTextState state;
    mm_rich_text_init(&state, sizeof(achar), koskCur);
    state.defaults = {kboCur, _oskFont, static_cast<MMI32>(_dxpDef), static_cast<MMI32>(_dypFontDef),
                      static_cast<MMU32>(_acrBack.LwGet())};
    state.font = RichTextStoreHost::Font(_stnFontDef);
    state.properties = _properties;
    state.arguments = _arguments;
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
    the argument table.
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
    The caller should have ensured that the property table has room for 2 * cspvm
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
    UndoHost host(this);
    auto services = host.Services();
    int result = mm_text_undo_setup(cp1, cp2, ccpIns, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Cancel undo.
***************************************************************************/
void RichTextDocument::CancelUndo(void)
{
    UndoHost host(this);
    auto services = host.Services();
    mm_text_undo_cancel(&services);
    host.Rethrow();
}

/***************************************************************************
    Commit the setup undo.
***************************************************************************/
void RichTextDocument::CommitUndo(void)
{
    UndoHost host(this);
    auto services = host.Services();
    mm_text_undo_commit(&services);
    host.Rethrow();
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

    ReplaceHost host(this);
    auto services = host.Services();
    auto request = host.Request(MM_TEXT_REPLACE_BYTES, prgch, ccpIns, cp, ccpDel, grfdoc);
    int result = mm_text_replace_rich(sizeof(achar), koskCur, &request, &services);
    host.Rethrow();
    return result > 0;
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
bool RichTextDocument::_FReplaceCore(void *prgch, PFileLocation pflo, bool fCopy, PFileByteStream pbsf, long cpSrc,
                                     long ccpIns, long cp, long ccpDel, PCHP pchp, PPAP ppap, ulong grfdoc)
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
    ReplaceHost host(this);
    host.chp = pchp;
    host.pap = ppap;
    auto services = host.Services();
    auto request = host.Request(pflo   ? MM_TEXT_REPLACE_LOCATION
                                : pbsf ? MM_TEXT_REPLACE_STREAM
                                       : MM_TEXT_REPLACE_BYTES,
                                prgch, ccpIns, cp, ccpDel, grfdoc);
    if (pflo)
        request.location = RichTextStoreHost::Location(*pflo);
    request.copy = fCopy;
    request.stream = pbsf;
    request.source = cpSrc;
    int result = mm_text_replace_formatted(sizeof(achar), koskCur, &request, &services);
    host.Rethrow();
    return result > 0;
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
    ReplaceHost host(this);
    auto services = host.Services();
    auto request = host.Request(MM_TEXT_REPLACE_LOCATION, nullptr, 0, cp, ccpDel, grfdoc);
    request.location = RichTextStoreHost::Location(*pflo);
    request.copy = fCopy;
    request.system = osk;
    int result = mm_text_replace_rich(sizeof(achar), koskCur, &request, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Replace cp to cp + ccpDel with the characters in the given FileLocation, using
    the given chp and pap. pchp and/or ppap can be nil.
***************************************************************************/
bool RichTextDocument::FReplaceFlo(PFileLocation pflo, bool fCopy, long cp, long ccpDel, PCHP pchp, PPAP ppap,
                                   short osk, ulong grfdoc)
{
    AssertThis(0);
    AssertPo(pflo, 0);
    AssertIn(cp, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cp);
    AssertNilOrVarMem(pchp);
    AssertNilOrVarMem(ppap);
    ReplaceHost host(this);
    host.chp = pchp;
    host.pap = ppap;
    auto services = host.Services();
    auto request = host.Request(MM_TEXT_REPLACE_LOCATION, nullptr, 0, cp, ccpDel, grfdoc);
    request.location = RichTextStoreHost::Location(*pflo);
    request.copy = fCopy;
    request.system = osk;
    int result = mm_text_replace_formatted_location(sizeof(achar), koskCur, &request, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Replace cp to cpDst + ccpDel with ccpSrc characters from pbsfSrc starting
    at cpSrc.
***************************************************************************/
bool RichTextDocument::FReplaceBsf(PFileByteStream pbsfSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel,
                                   ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPo(pbsfSrc, 0);
    AssertIn(cpSrc, 0, pbsfSrc->IbMac() / size(achar) + 1);
    AssertIn(ccpSrc, 0, pbsfSrc->IbMac() / size(achar) + 1 - cpSrc);
    AssertIn(cpDst, 0, CpMac());
    AssertIn(ccpDel, 0, CpMac() - cpDst);

    ReplaceHost host(this);
    auto services = host.Services();
    auto request = host.Request(MM_TEXT_REPLACE_STREAM, nullptr, ccpSrc, cpDst, ccpDel, grfdoc);
    request.stream = pbsfSrc;
    request.source = cpSrc;
    int result = mm_text_replace_rich(sizeof(achar), koskCur, &request, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Replace cp to cpDst + ccpDel with ccpSrc characters from pbsfSrc starting
    at cpSrc, using the given chp and pap. pchp and/or ppap can be nil.
***************************************************************************/
bool RichTextDocument::FReplaceBsf(PFileByteStream pbsfSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, PCHP pchp,
                                   PPAP ppap, ulong grfdoc)
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
    CopyHost host(this, ptxrd);
    auto services = host.Copy();
    int result = mm_text_copy_document(cpSrc, ccpSrc, cpDst, ccpDel, grfdoc, &services);
    host.Rethrow();
    return result > 0;
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
    CopyHost host(this, ptxrd);
    auto services = host.Copy();
    mm_text_copy_properties(cpSrc, cpDst, ccpSrc, ccpDst, sprmMin, sprmLim, &services);
    host.Rethrow();
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

    CopyHost host(this);
    host.position = pcp;
    host.byteCount = pcb;
    auto services = host.ObjectServices();
    MMI32 at, bytes;
    int result = mm_text_object_fetch(cpMin, &at, ppv, pcb ? &bytes : nullptr, &services);
    host.Rethrow();
    return result > 0;
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
    CopyHost host(this);
    host.chp = pchp;
    auto services = host.ObjectServices();
    int result = mm_text_object_insert(pv, cb, cp, ccpDel, grfdoc, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Insert a picture into the rich text document.
***************************************************************************/
bool RichTextDocument::FApplyObjectProps(void *pv, long cb, long cp, ulong grfdoc)
{
    AssertThis(fobjAssertFull);
    AssertPvCb(pv, cb);
    AssertIn(cp, 0, CpMac());
    CopyHost host(this);
    auto services = host.ObjectServices();
    int result = mm_text_object_apply(pv, cb, cp, grfdoc, &services);
    host.Rethrow();
    return result > 0;
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
    CopyHost host(this);
    host.graphics = pgnv;
    host.objectChp = pchp;
    host.rect = prc;
    auto services = host.ObjectServices();
    int result = mm_text_object_dispatch(cp, 0, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Get the object bounds from the AllocatedGroup entry.
***************************************************************************/
bool RichTextDocument::_FGetObjectRc(long icact, byte sprm, PGraphicsEnvironment pgnv, PCHP pchp, RC *prc)
{
    AssertIn(icact, 0, mm_text_collection_count(_arguments));
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
    CopyHost host(this);
    host.graphics = pgnv;
    host.objectChp = pchp;
    host.x = pxp;
    host.y = yp;
    host.clip = prcClip;
    auto services = host.ObjectServices();
    int result = mm_text_object_dispatch(cp, 1, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Draw the object.
***************************************************************************/
bool RichTextDocument::_FDrawObject(long icact, byte sprm, PGraphicsEnvironment pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip)
{
    AssertIn(icact, 0, mm_text_collection_count(_arguments));
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
    UndoHost host;
    auto services = host.Services();
    void *owned;
    mm_text_undo_record_create(&owned, cactCombine, ptxrd, cp1, cp2, ccpIns, &services);
    host.Rethrow();
    auto result = static_cast<PRichTextUndo>(owned);
    AssertNilOrPo(result, 0);
    return result;
}

/***************************************************************************
    Destructor for a rich text undo object.
***************************************************************************/
RichTextUndo::~RichTextUndo(void)
{
    UndoHost host;
    auto services = host.Services();
    mm_text_undo_record_dispose(&_undo, &services);
}

/***************************************************************************
    Undo this rich text undo object on the given document.
***************************************************************************/
bool RichTextUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    UndoHost host;
    auto services = host.Services();
    int result = mm_text_undo_record_apply(&_undo, pdocb, &services);
    host.Rethrow();
    return result > 0;
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
    UndoHost host;
    auto services = host.Services();
    int result = mm_text_undo_record_combine(&_undo, &prtun->_undo, &services);
    host.Rethrow();
    return result > 0;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a RichTextUndo.
***************************************************************************/
void RichTextUndo::AssertValid(LegacyUlong grf)
{
    RichTextUndo_PAR::AssertValid(grf);
    AssertNilOrPo(static_cast<PRichTextDocument>(_undo.saved), 0);
    AssertIn(_undo.at, 0, kcbMax);
    AssertIn(_undo.inserted, 0, kcbMax);
    Assert(_undo.inserted > 0 || static_cast<PRichTextDocument>(_undo.saved) != pvNil, "empty RichTextUndo");
}

/***************************************************************************
    Mark memory for the RichTextUndo.
***************************************************************************/
void RichTextUndo::MarkMem(void)
{
    AssertThis(fobjAssertFull);
    RichTextUndo_PAR::MarkMem();
    MarkMemObj(static_cast<PRichTextDocument>(_undo.saved));
}
#endif // DEBUG

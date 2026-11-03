/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Clipboard object implementation.

***************************************************************************/
#ifdef KAUAI_SDL3
#include "sdl_host.h"
#include "text_utf8.h"
#include "platform_text.h"
#include <algorithm>
#endif
#include "frame.h"
ASSERTNAME
#ifdef WIN
static_assert(kclfmUniText == CF_UNICODETEXT && kclfmSbText == CF_TEXT, "clipboard format ABI changed");
#endif

RTCLASS(ClipboardObject)

PClipboardObject vpclip;
ClipboardObject _clip;

/***************************************************************************
    Constructor for the clipboard.
***************************************************************************/
ClipboardObject::ClipboardObject(void)
{
#ifdef KAUAI_SDL3
    _hqExport = hqNil;
    _pvExport = pvNil;
#endif
    vpclip = this;
    AssertThis(0);
}

/***************************************************************************
    Return whether the given document is the clipboard document.  If pdocb
    is nil this returns true if the clipboard is empty and false if it's
    not empty.
***************************************************************************/
bool ClipboardObject::FDocIsClip(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertNilOrPo(pdocb, 0);

    if (pvNil == pdocb)
        _EnsureDoc();

    return pdocb == _pdocb;
}

/***************************************************************************
    Show the clipboard document.
***************************************************************************/
void ClipboardObject::Show(void)
{
    AssertThis(0);

    _EnsureDoc();
    _ImportCur();
    if (pvNil != _pdocb)
    {
        if (_pdocb->Cddg() > 0)
            _pdocb->ActivateDmd();
        else
            _pdocb->PdmdNew();
    }
}

/***************************************************************************
    Make the given document the clipboard document.
***************************************************************************/
void ClipboardObject::Set(PDocumentBase pdocb, bool fExport)
{
    AssertThis(0);
    AssertNilOrPo(pdocb, 0);

    if (_fExporting || _fImporting)
    {
        Bug("can't change the clipboard while exporting or importing");
        return;
    }

    if (pdocb == _pdocb)
        return;

    _fDocCurrent = fTrue;
    _fDelayImport = fFalse;
    _clfmImport = clfmNil;

    // throw away the old clip document and replace it with the new one
    SwapVars(&pdocb, &_pdocb);
    if (pvNil != pdocb)
    {
        if (pdocb->Cddg() > 0)
            pdocb->UpdateName();
        ReleasePpo(&pdocb);
    }

    if (pvNil != _pdocb)
    {
        _pdocb->AddRef();
        if (_pdocb->Cddg() > 0)
            _pdocb->UpdateName();

        if (fExport)
        {
            _pdocb->ExportFormats(this);
            EndExport();
        }
    }
}

/***************************************************************************
    See if the clipboard supports this format and if so, get it.
***************************************************************************/
bool ClipboardObject::FGetFormat(long cls, PDocumentBase *ppdocb)
{
    AssertThis(0);
    AssertNilOrVarMem(ppdocb);

    _EnsureDoc();
    if (pvNil != ppdocb)
        _ImportCur();

    if (pvNil == _pdocb)
        return fFalse;

    if (_pdocb->FIs(cls))
    {
        if (pvNil != ppdocb)
        {
            *ppdocb = _pdocb;
            (*ppdocb)->AddRef();
        }
        return fTrue;
    }

    return _pdocb->FGetFormat(cls, ppdocb);
}

/***************************************************************************
    Import stuff from the external clipboard.
***************************************************************************/
void ClipboardObject::Import(void)
{
    AssertThis(0);

    if (pvNil != _pdocb && _pdocb->Cddg() > 0)
        Set();
    _fDocCurrent = fFalse;
}

/***************************************************************************
    Make sure the _pdocb is current - import the current system clipboard
    if it isn't.
***************************************************************************/
void ClipboardObject::_EnsureDoc(void)
{
    if (_fExporting || _fImporting)
    {
        Bug("can't import the clipboard while exporting or importing");
        return;
    }

    if (_fDocCurrent)
        return;

    _fDocCurrent = fTrue;

#ifdef KAUAI_SDL3
    if (KauaiSDL::Main().OwnsClipboard())
        return;
    try
    {
        std::string utf8;
        std::vector<std::uint16_t> units;
        if (!KauaiSDL::Main().GetClipboardText(utf8) || !KauaiText::DecodeUtf8(utf8, units))
            return;
        // Kauai paragraphs end with CR. Accept LF and CRLF from SDL hosts.
        std::vector<achar> text;
        std::vector<std::uint16_t> paragraphs;
        for (std::size_t i = 0; i < (units.size)(); ++i)
            if (units[i] != '\n' || !i || units[i - 1] != '\r')
                paragraphs.push_back(units[i] == '\n' ? '\r' : units[i]);
#ifdef UNICODE
        text.assign(paragraphs.begin(), paragraphs.end());
#else
        std::vector<std::uint8_t> littleEndian;
        for (auto unit : paragraphs)
        {
            littleEndian.push_back(static_cast<std::uint8_t>(unit));
            littleEndian.push_back(static_cast<std::uint8_t>(unit >> 8));
        }
        const auto count =
            KauaiText::ToAnsi(littleEndian.data(), (littleEndian.size)(), KauaiText::Encoding::Utf16LE, nullptr, 0);
        text.resize(count);
        if (count && !KauaiText::ToAnsi(littleEndian.data(), (littleEndian.size)(), KauaiText::Encoding::Utf16LE,
                                        text.data(), count))
            return;
#endif
        PDocumentBase pdocb = pvNil;
        if (text.empty() || ((text.size)() <= kcbMax / size(achar) &&
                             _FImportFormat(kclfmText, text.data(), (text.size)() * size(achar), &pdocb)))
            Set(pdocb, fFalse);
        ReleasePpo(&pdocb);
    }
    catch (const std::bad_alloc &)
    {
        return;
    }
#elif defined(WIN)
    HN hn;
    PDocumentBase pdocb;
    bool fDelay;
    long clfm;

    if (GetClipboardOwner() == vwig.hwndApp || !OpenClipboard(vwig.hwndApp))
        return;

    for (clfm = 0; 0 != (clfm = EnumClipboardFormats(clfm));)
    {
        if (!_FImportFormat(clfm))
            continue;

        if (hNil == (hn = GetClipboardData(clfm)))
            continue;

        pdocb = pvNil;
        fDelay = fFalse;
        if (_FImportFormat(clfm, GlobalLock(hn), GlobalSize(hn), &pdocb, &fDelay))
        {
            GlobalUnlock(hn);
            Set(pdocb, fFalse);
            _fDelayImport = FPure(fDelay) && pdocb != pvNil;
            _clfmImport = clfm;
            ReleasePpo(&pdocb);
            break;
        }
        GlobalUnlock(hn);
    }
    CloseClipboard();
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement ClipboardObject::Import
#endif        // MAC
}

/***************************************************************************
    Import the actual data for the current clipboard (if importing was
    delayed).
***************************************************************************/
void ClipboardObject::_ImportCur(void)
{
    AssertThis(0);
    Assert(_fDocCurrent, 0);

    if (pvNil == _pdocb || !_fDelayImport || clfmNil == _clfmImport)
        return;

#ifdef KAUAI_SDL3
    // SDL imports a complete owned text snapshot, never delayed native data.
    return;
#elif defined(WIN)
    HN hn;
    bool fRet;
    PDocumentBase pdocb;

    if (GetClipboardOwner() == vwig.hwndApp || !OpenClipboard(vwig.hwndApp))
        return;

    if (hNil != (hn = GetClipboardData(_clfmImport)))
    {
        pdocb = _pdocb;
        if (pvNil != pdocb)
            pdocb->AddRef();
        fRet = _FImportFormat(_clfmImport, GlobalLock(hn), GlobalSize(hn), &pdocb);
        GlobalUnlock(hn);
        if (fRet)
            Set(pdocb, fFalse);
        _fDelayImport = fFalse;
        _clfmImport = clfmNil;
        ReleasePpo(&pdocb);
    }
    CloseClipboard();
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement ClipboardObject::_ImportCur
#endif        // MAC
}

/***************************************************************************
    Import a particular format.
***************************************************************************/
bool ClipboardObject::_FImportFormat(long clfm, void *pv, long cb, PDocumentBase *ppdocb, bool *pfDelay)
{
    AssertThis(0);
    AssertPvCb(pv, cb);
    AssertNilOrVarMem(ppdocb);
    AssertNilOrVarMem(pfDelay);
    Assert(_fDocCurrent, 0);
    bool fRet;

#ifdef WIN
    if (pvNil != pv && cb > 0)
    {
        // adjust cb for text - remove the stupid trailing null and compensate
        // for the fact that cb might be too big (GlobalSize(hn) might be bigger
        // than what was allocated).
        switch (clfm)
        {
        case kclfmUniText:
            wchar *pchw, *pchwLim;

            pchw = (wchar *)pv;
            pchwLim = (wchar *)pv + cb / size(wchar);
            while (pchw < pchwLim && *pchw != 0)
                pchw++;
            cb = BvSubPvs(pchw, pv);
            break;

        case kclfmSbText:
            schar *pchs, *pchsLim;

            pchs = (schar *)pv;
            pchsLim = (schar *)pv + cb;
            while (pchs < pchsLim && *pchs != 0)
                pchs++;
            cb = BvSubPvs(pchs, pv);
            break;
        }
    }
#endif // WIN

    _fImporting = fTrue;
    fRet = vpappb->FImportClip(clfm, pv, cb, ppdocb, pfDelay);
    _fImporting = fFalse;

    return fRet;
}

/***************************************************************************
    Start an export session.
***************************************************************************/
bool ClipboardObject::FInitExport(void)
{
    AssertThis(0);

    if (_fExporting || _fImporting)
    {
        Bug("Already exporting or importing");
        return fFalse;
    }

#ifdef KAUAI_SDL3
    _hqExport = hqNil;
    _pvExport = pvNil;
    _fExporting = fTrue;
#elif defined(WIN)
    _hnExport = hNil;
    if (!OpenClipboard(vwig.hwndApp))
        return fFalse;
    if (!EmptyClipboard())
    {
        CloseClipboard();
        return fFalse;
    }
    _fExporting = fTrue;
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement ClipboardObject::FInitExport
#endif        // MAC

    return _fExporting;
}

/***************************************************************************
    Allocate a buffer to export to.
***************************************************************************/
void *ClipboardObject::PvExport(long cb, long clfm)
{
    AssertThis(0);
    AssertIn(cb, 1, kcbMax);

    if (!_fExporting)
    {
        Bug("not exporting");
        return pvNil;
    }

    _ExportCur();

#ifdef KAUAI_SDL3
    if (clfm != kclfmText || cb > kcbMax - size(achar))
        return pvNil;
    if (!FAllocHq(&_hqExport, cb + size(achar), fmemClear, mprNormal))
        return pvNil;
    _clfmExport = clfm;
    return _pvExport = PvLockHq(_hqExport);
#elif defined(WIN)
    switch (clfm)
    {
    case kclfmText:
        // need to add size(achar) for the terminating zero character
        cb = LwRoundAway(cb + size(achar), size(achar));
        break;
    }

    if (hNil == (_hnExport = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE | GMEM_ZEROINIT, cb)))
    {
        return pvNil;
    }

    _clfmExport = clfm;
    return GlobalLock(_hnExport);
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement ClipboardObject::PvExport
    return pvNil;
#endif // MAC
}

/***************************************************************************
    End an exporting session.
***************************************************************************/
void ClipboardObject::EndExport(void)
{
    AssertThis(0);

    if (!_fExporting)
        return;

    _ExportCur();

#if defined(WIN) && !defined(KAUAI_SDL3)
    CloseClipboard();
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement ClipboardObject::EndExport
#endif        // MAC
    _fExporting = fFalse;
}

/***************************************************************************
    Export the current format.
***************************************************************************/
void ClipboardObject::_ExportCur(void)
{
    AssertThis(0);
    Assert(_fExporting, 0);

#ifdef KAUAI_SDL3
    if (_hqExport != hqNil)
    {
        try
        {
            const auto bytes = CbOfHq(_hqExport) - size(achar);
            const auto count = KauaiText::ToUtf16(_pvExport, bytes,
#ifdef UNICODE
                                                  KauaiText::Encoding::Utf16LE,
#else
                                                  KauaiText::Encoding::WindowsAnsi,
#endif
                                                  nullptr, 0);
            std::vector<std::uint16_t> units(count);
            if (!count || KauaiText::ToUtf16(_pvExport, bytes,
#ifdef UNICODE
                                             KauaiText::Encoding::Utf16LE,
#else
                                             KauaiText::Encoding::WindowsAnsi,
#endif
                                             units.data(), count))
            {
                // Clipboard text ends at the first NUL, as on the native path.
                units.erase(std::find(units.begin(), units.end(), 0), units.end());
                std::string utf8;
                if (KauaiText::EncodeUtf8(units, utf8))
                    KauaiSDL::Main().SetClipboardText(utf8);
            }
        }
        catch (const std::bad_alloc &)
        {
        }
        UnlockHq(_hqExport);
        FreePhq(&_hqExport);
        _pvExport = pvNil;
    }
#elif defined(WIN)
    if (hNil != _hnExport)
    {
        GlobalUnlock(_hnExport);
        SetClipboardData(_clfmExport, _hnExport);
        _hnExport = hNil;
    }
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement ClipboardObject::_ExportCur
#endif        // MAC
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a ClipboardObject.
***************************************************************************/
void ClipboardObject::AssertValid(LegacyUlong grf)
{
    ClipboardObject_PAR::AssertValid(0);
    AssertNilOrPo(_pdocb, 0);
    Assert(!_fExporting || !_fImporting, "both importing and exporting!");
}

/***************************************************************************
    Mark memory for the ClipboardObject.
***************************************************************************/
void ClipboardObject::MarkMem(void)
{
    AssertValid(0);
    ClipboardObject_PAR::MarkMem();
    MarkMemObj(_pdocb);
#ifdef KAUAI_SDL3
    MarkHq(_hqExport);
#endif
}
#endif // DEBUG

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
#endif
#include "frame.h"
ASSERTNAME
#include "clipboard_view_host.h"
#ifdef WIN
static_assert(kclfmUniText == CF_UNICODETEXT && kclfmSbText == CF_TEXT, "clipboard format ABI changed");
#endif

RTCLASS(ClipboardObject)

PClipboardObject vpclip;
ClipboardObject _clip;

struct ClipboardObject::Host : ClipboardViewHost
{
    ClipboardObject *clip;
    PDocumentBase *output = nullptr;
#ifdef KAUAI_SDL3
    std::vector<achar> text;
#endif

    explicit Host(ClipboardObject *clip) : clip(clip) {}

    int Check(int result)
    {
        return result < 0 ? ClipboardViewHost::Check(result) : result;
    }

    MMClipboardServices Services()
    {
        MMClipboardServices s = {};
        s.context = this;
#if defined(KAUAI_SDL3)
        s.transport = MM_CLIPBOARD_TEXT;
#elif defined(WIN)
        s.transport = MM_CLIPBOARD_FORMATS;
#else
        s.transport = MM_CLIPBOARD_UNAVAILABLE;
#endif
#ifdef WIN
        s.byte_format = kclfmSbText;
        s.wide_format = kclfmUniText;
#endif
        s.text_format = kclfmText;
        s.text_unit = sizeof(achar);
        s.max_bytes = kcbMax;
        s.call = Call;
        return s;
    }

#ifdef KAUAI_SDL3
    bool ReadText(MMClipboardRequest *q)
    {
        if (KauaiSDL::Main().OwnsClipboard())
            return false;
        try
        {
            std::string utf8;
            std::vector<std::uint16_t> units;
            if (!KauaiSDL::Main().GetClipboardText(utf8) || !KauaiText::DecodeUtf8(utf8, units))
                return false;
            units.resize(mm_clipboard_normalize_paragraphs(units.data(), (units.size)()));
#ifdef UNICODE
            text.assign(units.begin(), units.end());
#else
            std::vector<std::uint8_t> littleEndian;
            for (auto unit : units)
            {
                littleEndian.push_back(static_cast<std::uint8_t>(unit));
                littleEndian.push_back(static_cast<std::uint8_t>(unit >> 8));
            }
            const auto count = KauaiText::ToAnsi(littleEndian.data(), (littleEndian.size)(),
                                                KauaiText::Encoding::Utf16LE, nullptr, 0);
            text.resize(count);
            if (count && !KauaiText::ToAnsi(littleEndian.data(), (littleEndian.size)(), KauaiText::Encoding::Utf16LE,
                                           text.data(), count))
                return false;
#endif
            if ((text.size)() > 0x7fffffffUL / sizeof(achar))
                return false;
            q->buffer = text.data();
            q->bytes = static_cast<MMI32>((text.size)() * sizeof(achar));
            return true;
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
    }

    void FlushText()
    {
        if (clip->_hqExport == hqNil)
            return;
        try
        {
#ifdef UNICODE
            const auto encoding = KauaiText::Encoding::Utf16LE;
#else
            const auto encoding = KauaiText::Encoding::WindowsAnsi;
#endif
            const auto bytes = CbOfHq(clip->_hqExport) - sizeof(achar);
            const auto count = KauaiText::ToUtf16(clip->_pvExport, bytes, encoding, nullptr, 0);
            std::vector<std::uint16_t> units(count);
            if (!count || KauaiText::ToUtf16(clip->_pvExport, bytes, encoding, units.data(), count))
            {
                units.resize(mm_clipboard_text_bytes(units.data(), (units.size)() * 2, 2) / 2);
                std::string utf8;
                if (KauaiText::EncodeUtf8(units, utf8))
                    KauaiSDL::Main().SetClipboardText(utf8);
            }
        }
        catch (const std::bad_alloc &)
        {
        }
        UnlockHq(clip->_hqExport);
        FreePhq(&clip->_hqExport);
        clip->_pvExport = pvNil;
    }
#endif

    static int Call(void *context, MMClipboardOperation operation, MMClipboardRequest *q)
    {
        auto &host = *static_cast<Host *>(context);
        auto clip = host.clip;
        auto document = static_cast<PDocumentBase>(q->document);
        return host.Guard([&]() -> int {
            switch (operation)
            {
            case MM_CLIPBOARD_HAS_VIEWS: return document->Cddg() > 0;
            case MM_CLIPBOARD_RENAME: document->UpdateName(); break;
            case MM_CLIPBOARD_RETAIN: document->AddRef(); break;
            case MM_CLIPBOARD_RELEASE: document->Release(); break;
            case MM_CLIPBOARD_ACTIVATE: document->ActivateDmd(); break;
            case MM_CLIPBOARD_SHOW: document->PdmdNew(); break;
            case MM_CLIPBOARD_EXPORT_DOCUMENT: document->ExportFormats(clip); break;
            case MM_CLIPBOARD_IS_FORMAT: return document->FIs(q->format);
            case MM_CLIPBOARD_DOCUMENT_FORMAT: return document->FGetFormat(q->format, host.output);
            case MM_CLIPBOARD_PUBLISH_DOCUMENT: *host.output = document; break;
            case MM_CLIPBOARD_DIAGNOSTIC:
                switch (q->diagnostic)
                {
                case MM_CLIPBOARD_BUSY_SET: Bug("can't change the clipboard while exporting or importing"); break;
                case MM_CLIPBOARD_BUSY_READ: Bug("can't import the clipboard while exporting or importing"); break;
                case MM_CLIPBOARD_BUSY_WRITE: Bug("Already exporting or importing"); break;
                default: Bug("not exporting"); break;
                }
                break;
            case MM_CLIPBOARD_IMPORT_DOCUMENT:
            {
                AssertPvCb(q->buffer, q->bytes);
                Assert(clip->_clipboard.current, 0);
                bool delay = q->delay != 0;
                const bool imported = vpappb->FImportClip(q->format, q->buffer, q->bytes,
                                                         q->output ? &document : nullptr,
                                                         q->allow_delay ? &delay : nullptr);
                q->document = document;
                q->delay = delay;
                return imported;
            }

#if defined(KAUAI_SDL3)
            case MM_CLIPBOARD_READ_TEXT: return host.ReadText(q);
            case MM_CLIPBOARD_OPEN_WRITE:
                clip->_hqExport = hqNil;
                clip->_pvExport = pvNil;
                break;
            case MM_CLIPBOARD_ALLOCATE_EXPORT:
                if (!FAllocHq(&clip->_hqExport, q->bytes, fmemClear, mprNormal))
                    return 0;
                q->buffer = clip->_pvExport = PvLockHq(clip->_hqExport);
                break;
            case MM_CLIPBOARD_FLUSH_EXPORT: host.FlushText(); break;
            case MM_CLIPBOARD_CLOSE: break;
#elif defined(WIN)
            case MM_CLIPBOARD_OPEN_READ:
                return GetClipboardOwner() != vwig.hwndApp && OpenClipboard(vwig.hwndApp);
            case MM_CLIPBOARD_NEXT_FORMAT:
                q->format = static_cast<MMI32>(EnumClipboardFormats(q->format));
                return q->format != 0;
            case MM_CLIPBOARD_GET_DATA:
                q->handle = GetClipboardData(q->format);
                return q->handle != hNil;
            case MM_CLIPBOARD_LOCK_DATA:
                q->buffer = GlobalLock(q->handle);
                q->bytes = static_cast<MMI32>(GlobalSize(q->handle));
                break;
            case MM_CLIPBOARD_UNLOCK_DATA: GlobalUnlock(q->handle); break;
            case MM_CLIPBOARD_CLOSE: CloseClipboard(); break;
            case MM_CLIPBOARD_OPEN_WRITE:
                clip->_hnExport = hNil;
                if (!OpenClipboard(vwig.hwndApp))
                    return 0;
                if (!EmptyClipboard())
                {
                    CloseClipboard();
                    return 0;
                }
                break;
            case MM_CLIPBOARD_ALLOCATE_EXPORT:
                clip->_hnExport = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE | GMEM_ZEROINIT, q->bytes);
                if (clip->_hnExport == hNil)
                    return 0;
                q->buffer = GlobalLock(clip->_hnExport);
                break;
            case MM_CLIPBOARD_FLUSH_EXPORT:
                if (clip->_hnExport != hNil)
                {
                    GlobalUnlock(clip->_hnExport);
                    SetClipboardData(q->format, clip->_hnExport);
                    clip->_hnExport = hNil;
                }
                break;
#endif

            default: Bug("unsupported clipboard transport operation"); return 0;
            }
            return 1;
        });
    }
};

ClipboardObject::ClipboardObject()
{
#ifdef KAUAI_SDL3
    _hqExport = hqNil;
    _pvExport = pvNil;
#else
    _hnExport = hNil;
#endif
    vpclip = this;
    AssertThis(0);
}

bool ClipboardObject::FDocIsClip(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertNilOrPo(pdocb, 0);
    Host host(this);
    const auto services = host.Services();
    return host.Check(mm_clipboard_is_document(&_clipboard, pdocb, &services)) > 0;
}

void ClipboardObject::Show()
{
    AssertThis(0);
    Host host(this);
    const auto services = host.Services();
    host.Check(mm_clipboard_show(&_clipboard, &services));
}

void ClipboardObject::Set(PDocumentBase pdocb, bool fExport)
{
    AssertThis(0);
    AssertNilOrPo(pdocb, 0);
    Host host(this);
    const auto services = host.Services();
    host.Check(mm_clipboard_set(&_clipboard, pdocb, fExport, &services));
}

bool ClipboardObject::FGetFormat(long cls, PDocumentBase *ppdocb)
{
    AssertThis(0);
    AssertNilOrVarMem(ppdocb);
    Host host(this);
    host.output = ppdocb;
    const auto services = host.Services();
    return host.Check(mm_clipboard_get(&_clipboard, cls, ppdocb != nullptr, &services)) > 0;
}

void ClipboardObject::Import()
{
    AssertThis(0);
    Host host(this);
    const auto services = host.Services();
    host.Check(mm_clipboard_invalidate(&_clipboard, &services));
}

bool ClipboardObject::FInitExport()
{
    AssertThis(0);
    Host host(this);
    const auto services = host.Services();
    return host.Check(mm_clipboard_begin_export(&_clipboard, &services)) > 0;
}

void *ClipboardObject::PvExport(long cb, long clfm)
{
    AssertThis(0);
    AssertIn(cb, 1, kcbMax);
    Host host(this);
    const auto services = host.Services();
    void *buffer;
    host.Check(mm_clipboard_export_buffer(&_clipboard, cb, clfm, &buffer, &services));
    return buffer;
}

void ClipboardObject::EndExport()
{
    AssertThis(0);
    Host host(this);
    const auto services = host.Services();
    host.Check(mm_clipboard_end_export(&_clipboard, &services));
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a ClipboardObject.
***************************************************************************/
void ClipboardObject::AssertValid(LegacyUlong grf)
{
    ClipboardObject_PAR::AssertValid(0);
    AssertNilOrPo(static_cast<PDocumentBase>(_clipboard.document), 0);
    Assert(!_clipboard.exporting || !_clipboard.importing, "both importing and exporting!");
}

/***************************************************************************
    Mark memory for the ClipboardObject.
***************************************************************************/
void ClipboardObject::MarkMem(void)
{
    AssertValid(0);
    ClipboardObject_PAR::MarkMem();
    MarkMemObj(static_cast<PDocumentBase>(_clipboard.document));
#ifdef KAUAI_SDL3
    MarkHq(_hqExport);
#endif
}
#endif // DEBUG

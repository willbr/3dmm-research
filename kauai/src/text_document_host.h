#ifndef TEXT_DOCUMENT_HOST_H
#define TEXT_DOCUMENT_HOST_H
#include "mm/text_document.h"
#include "mm/text_materialize.h"
// Typed owner slots and primitive I/O. C owns sequencing and reference transfers.
struct TextDocumentFactoryHost
{
    std::exception_ptr exception;
    template <class F> int Guard(F f)
    {
        try
        {
            return f();
        }
        catch (const std::bad_alloc &)
        {
            exception = std::current_exception();
            return -1;
        }
        catch (...)
        {
            exception = std::current_exception();
            return -2;
        }
    }
    void Rethrow()
    {
        if (exception)
            std::rethrow_exception(exception);
    }
};
struct TextDocumentBase::DocumentHost : TextDocumentFactoryHost
{
    TextDocumentBase *doc;
    explicit DocumentHost(TextDocumentBase *p) : doc(p)
    {
    }
    virtual ~DocumentHost() = default;
    virtual void *Get(int slot)
    {
        return slot == MM_TEXT_OWNER_STREAM ? static_cast<void *>(doc->_pbsf)
               : slot == MM_TEXT_OWNER_FILE ? static_cast<void *>(doc->_pfil)
                                            : nullptr;
    }
    virtual void Set(int slot, void *p)
    {
        if (slot == MM_TEXT_OWNER_STREAM)
            doc->_pbsf = static_cast<PFileByteStream>(p);
        else if (slot == MM_TEXT_OWNER_FILE)
            doc->_pfil = static_cast<PFileObject>(p);
    }
    static MMTextDocumentConfig Config()
    {
        return {sizeof(achar), koskCur, RichTextStoreHost::NativeBig()};
    }
    MMTextDocumentObjects Objects()
    {
        return {this, [](void *v, int slot) -> void * { return static_cast<DocumentHost *>(v)->Get(slot); },
                [](void *v, int slot, void *p) { static_cast<DocumentHost *>(v)->Set(slot, p); },
                [](void *, int, void *p) { static_cast<BASE *>(p)->AddRef(); },
                [](void *, int, void *p) { static_cast<BASE *>(p)->Release(); }};
    }
    static void Diagnostic(void *, int kind)
    {
        if (kind == MM_TEXT_DOC_NO_FILE)
            Bug("Can't do a normal save - no file");
        else if (kind == MM_TEXT_DOC_ENCODING)
            Bug("Can't translate a FileByteStream");
        else
            Bug("FileByteStream has a partial character");
    }
    MMTextBaseServices Base()
    {
        MMTextBaseServices s = {};
        s.context = this;
        s.objects = Objects();
        s.diagnostic = Diagnostic;
        s.stream_bytes = [](void *, void *p) -> MMI32 { return static_cast<PFileByteStream>(p)->IbMac(); };
        s.file_bytes = [](void *, void *p) -> MMI32 { return static_cast<PFileObject>(p)->FpMac(); };
        s.open = [](void *v, void *name, void **out) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                *out = FileObject::PfilOpen(static_cast<PFilename>(name));
                return *out != nullptr;
            });
        };
        s.new_stream = [](void *v, void **out) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                *out = NewObj FileByteStream;
                return *out != nullptr;
            });
        };
        s.load = [](void *v, int system) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] { return h.doc->_FLoad(static_cast<short>(system)); });
        };
        s.translate = [](void *v, MMTextLocation *p, int system) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            auto location = RichTextStoreHost::Location(*p);
            int result = h.Guard([&] { return location.FTranslate(static_cast<short>(system)); });
            *p = RichTextStoreHost::Location(location);
            return result;
        };
        s.replace = [](void *v, void *stream, const MMTextLocation *p, MMI32 count) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                auto location = RichTextStoreHost::Location(*p);
                return static_cast<PFileByteStream>(stream)->FReplaceFlo(&location, false, 0, count);
            });
        };
        s.append = [](void *v, void *stream, const void *bytes, MMU32 n) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                auto p = static_cast<PFileByteStream>(stream);
                return p->FReplace(const_cast<void *>(bytes), n, p->IbMac(), 0);
            });
        };
        return s;
    }
    MMTextFileServices File()
    {
        MMTextFileServices s = {};
        s.context = this;
        s.objects = Objects();
        s.diagnostic = Diagnostic;
        s.name = [](void *v, int profile, void *owner, void *out) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                if (profile == MM_TEXT_RICH)
                    static_cast<PChunkyFile>(owner)->GetFni(static_cast<PFilename>(out));
                else
                    static_cast<PFileObject>(owner)->GetFni(static_cast<PFilename>(out));
                return 1;
            });
        };
        s.is_temp = [](void *, int profile, void *owner) -> int {
            return profile == MM_TEXT_RICH ? 0 : static_cast<PFileObject>(owner)->FTemp();
        };
        s.stream_bytes = [](void *, void *p) -> MMI32 { return static_cast<PFileByteStream>(p)->IbMac(); };
        s.create = [](void *v, int profile, void *name, void **out) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                *out = profile == MM_TEXT_RICH
                           ? static_cast<void *>(ChunkyFile::PcflCreateTemp(static_cast<PFilename>(name)))
                           : static_cast<void *>(FileObject::PfilCreateTemp(static_cast<PFilename>(name)));
                return *out != nullptr;
            });
        };
        s.put = [](void *v, const MMTextLocation *p, const void *bytes, MMU32 n) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                auto location = RichTextStoreHost::Location(*p);
                return location.FWriteRgb(const_cast<void *>(bytes), n, 0);
            });
        };
        s.write = [](void *v, void *stream, const MMTextLocation *p) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                auto location = RichTextStoreHost::Location(*p);
                return static_cast<PFileByteStream>(stream)->FWriteRgb(&location);
            });
        };
        s.redirect = [](void *v, void *stream, const MMTextLocation *p) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                auto location = RichTextStoreHost::Location(*p);
                return static_cast<PFileByteStream>(stream)->FReplaceFlo(&location, false, 0, p->bytes);
            });
        };
        s.chunk = [](void *v, void *archive, int redirect) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                ChunkIdentification chunk;
                return static_cast<RichTextDocument *>(h.doc)->FSaveToChunk(static_cast<PChunkyFile>(archive), &chunk,
                                                                            redirect != 0);
            });
        };
        s.save = [](void *v, void *archive) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] { return static_cast<PChunkyFile>(archive)->FSave(kctgFramework, nullptr); });
        };
        s.rename = [](void *v, int profile, void *owner, void *name) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                return profile == MM_TEXT_RICH ? static_cast<PChunkyFile>(owner)->FSetFni(static_cast<PFilename>(name))
                                               : static_cast<PFileObject>(owner)->FSetFni(static_cast<PFilename>(name));
            });
        };
        s.keep = [](void *, int profile, void *owner) {
            if (profile == MM_TEXT_RICH)
                static_cast<PChunkyFile>(owner)->SetTemp(false);
            else
                static_cast<PFileObject>(owner)->SetTemp(false);
        };
        s.clean = [](void *v) { static_cast<DocumentHost *>(v)->doc->_fDirty = false; };
        s.error = [](void *, int kind) { PushErc(kind == MM_TEXT_DOC_READ ? ercRtxdReadFailed : ercRtxdSaveFailed); };
        return s;
    }
};
struct RichTextDocument::DocumentHost : TextDocumentBase::DocumentHost
{
    RichTextDocument *rich;
    explicit DocumentHost(RichTextDocument *p) : TextDocumentBase::DocumentHost(p), rich(p)
    {
    }
    void *Get(int slot) override
    {
        if (slot == MM_TEXT_OWNER_ARCHIVE)
            return rich->_pcfl;
        if (slot == MM_TEXT_OWNER_PROPERTIES)
            return rich->_pglmpe;
        if (slot == MM_TEXT_OWNER_ARGUMENTS)
            return rich->_pagcact;
        return TextDocumentBase::DocumentHost::Get(slot);
    }
    void Set(int slot, void *p) override
    {
        if (slot == MM_TEXT_OWNER_ARCHIVE)
            rich->_pcfl = static_cast<PChunkyFile>(p);
        else if (slot == MM_TEXT_OWNER_PROPERTIES)
            rich->_pglmpe = static_cast<PDynamicArray>(p);
        else if (slot == MM_TEXT_OWNER_ARGUMENTS)
            rich->_pagcact = static_cast<PAllocatedGroup>(p);
        else
            TextDocumentBase::DocumentHost::Set(slot, p);
    }
    MMGroup ArgumentRows() const
    {
        return rich->_pagcact ? rich->_pagcact->CoreRows() : MMGroup{};
    }
    MMTextFontArgumentServices FontArgument()
    {
        MMTextFontArgumentServices s = {};
        s.context = this;
        s.arguments = [](void *v) { return static_cast<DocumentHost *>(v)->ArgumentRows(); };
        s.wire = {nullptr, sizeof(achar), koskCur, MacWin(koskUniWin, koskUniMac), RichTextStoreHost::NativeBig(),
                  nullptr, [](void *, const void *input, MMU32 bytes, int encoding, void *output, MMU32 capacity) -> MMI32 {
                      return CchTranslateRgb(const_cast<void *>(input), bytes, static_cast<short>(encoding),
                                             static_cast<achar *>(output), capacity);
                  }};
        s.map = [](void *v, const MMMovieTitle *p, int system, MMI32 *out) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                String name;
                RichTextStoreHost::Font(*p, &name);
                *out = vntl.OnnMapStn(&name, static_cast<short>(system));
                return 1;
            });
        };
        s.bad_string = [](void *) {
            PushErc(ercStnRead);
            Warn("bad String data");
        };
        s.bad_entry = [](void *) { Bug("bad font entry"); };
        return s;
    }
    MMTextRichInitServices Init()
    {
        MMTextRichInitServices s = {};
        s.context = this;
        s.objects = Objects();
        s.open = [](void *v, void *name, void **out) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                *out = ChunkyFile::PcflOpen(static_cast<PFilename>(name), fcflNil);
                return *out != nullptr;
            });
        };
        s.first = [](void *v, void *archive, MMU32 type, MMTextChunk *out) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                ChunkIdentification chunk;
                if (!static_cast<PChunkyFile>(archive)->FGetCkiCtg(type, 0, &chunk))
                    return false;
                *out = {chunk.ctg, chunk.cno};
                return true;
            });
        };
        s.read = [](void *v, void *archive, const MMTextChunk *p) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard(
                [&] { return h.rich->_FReadChunk(static_cast<PChunkyFile>(archive), p->type, p->number, false); });
        };
        s.base = [](void *v) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] { return h.rich->TextDocumentBase::_FInit(); });
        };
        s.properties = [](void *v, MMU32 n, void **out) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                *out = DynamicArray::PglNew(n);
                return *out != nullptr;
            });
        };
        s.min_grow = [](void *, void *p, MMU32 n) { static_cast<PDynamicArray>(p)->SetMinGrow(n); };
        s.font = [](void *v, int system, MMI32 height) -> int {
            auto &h = *static_cast<DocumentHost *>(v);
            return h.Guard([&] {
                h.rich->_onnDef = vpappb->OnnDefVariable();
                h.rich->_oskFont = static_cast<short>(system);
                vntl.GetStn(h.rich->_onnDef, &h.rich->_stnFontDef);
                h.rich->_dypFontDef = height;
                return 1;
            });
        };
        s.error = [](void *, int kind) { PushErc(kind == MM_TEXT_DOC_READ ? ercRtxdReadFailed : ercRtxdSaveFailed); };
        return s;
    }
};
#endif

#ifndef TEXT_DOCUMENT_HOST_H
#define TEXT_DOCUMENT_HOST_H
#include "text_file_host.h"
#include "mm/text_materialize.h"

struct TextDocumentBase::DocumentHost : TextFileDocumentHost
{
    TextDocumentBase *doc;
    explicit DocumentHost(TextDocumentBase *p) : TextFileDocumentHost(p, p->_pbsf, p->_pfil), doc(p)
    {
    }
    static MMTextDocumentConfig Config()
    {
        return {sizeof(achar), koskCur, RichTextStoreHost::NativeBig()};
    }
    MMTextBaseServices Base()
    {
        auto s = TextFileDocumentHost::Base();
        s.load = [](void *v, int system) -> int {
            auto &h = *static_cast<DocumentHost *>(static_cast<TextFileDocumentHost *>(v));
            return h.Guard([&] { return h.doc->_FLoad(static_cast<short>(system)); });
        };
        s.translate = [](void *v, MMTextLocation *p, int system) -> int {
            auto &h = *static_cast<DocumentHost *>(static_cast<TextFileDocumentHost *>(v));
            auto location = RichTextStoreHost::Location(*p);
            int result = h.Guard([&] { return location.FTranslate(static_cast<short>(system)); });
            *p = RichTextStoreHost::Location(location);
            return result;
        };
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
    MMTextFileServices File()
    {
        auto s = TextFileDocumentHost::File();
        s.chunk = [](void *v, void *archive, int redirect) -> int {
            auto &h = *static_cast<DocumentHost *>(static_cast<TextFileDocumentHost *>(v));
            return h.Guard([&] {
                ChunkIdentification chunk;
                return h.rich->FSaveToChunk(static_cast<PChunkyFile>(archive), &chunk, redirect != 0);
            });
        };
        return s;
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

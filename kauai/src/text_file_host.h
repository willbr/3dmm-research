#ifndef TEXT_FILE_HOST_H
#define TEXT_FILE_HOST_H
#include <exception>
#include <new>
#include "handle_services.h"
#include "mm/text_document.h"

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
// Shared access to the native owner slots and physical file services.
struct TextFileDocumentHost : TextDocumentFactoryHost
{
    PDocumentBase document;
    PFileByteStream &stream;
    PFileObject &file;
    TextFileDocumentHost(PDocumentBase p, PFileByteStream &byteStream, PFileObject &fileOwner)
        : document(p), stream(byteStream), file(fileOwner)
    {
    }
    virtual ~TextFileDocumentHost() = default;
    virtual void *Get(int slot)
    {
        return slot == MM_TEXT_OWNER_STREAM ? static_cast<void *>(stream)
               : slot == MM_TEXT_OWNER_FILE ? static_cast<void *>(file)
                                            : nullptr;
    }
    virtual void Set(int slot, void *p)
    {
        if (slot == MM_TEXT_OWNER_STREAM)
            stream = static_cast<PFileByteStream>(p);
        else if (slot == MM_TEXT_OWNER_FILE)
            file = static_cast<PFileObject>(p);
    }
    virtual void Release(int, void *p)
    {
        static_cast<BASE *>(p)->Release();
    }
    static MMPieceTable *Pieces(void *p)
    {
        return static_cast<PFileByteStream>(p)->CorePieces();
    }
    MMTextDocumentObjects Objects()
    {
        return {this, [](void *v, int slot) -> void * { return static_cast<TextFileDocumentHost *>(v)->Get(slot); },
                [](void *v, int slot, void *p) { static_cast<TextFileDocumentHost *>(v)->Set(slot, p); },
                [](void *, int, void *p) { static_cast<BASE *>(p)->AddRef(); },
                [](void *v, int slot, void *p) { static_cast<TextFileDocumentHost *>(v)->Release(slot, p); }};
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
        s.pieces = Pieces;
        s.file_bytes = [](void *, void *p) -> MMI32 { return static_cast<PFileObject>(p)->FpMac(); };
        s.open = [](void *v, void *name, void **out) -> int {
            auto &h = *static_cast<TextFileDocumentHost *>(v);
            return h.Guard([&] {
                *out = FileObject::PfilOpen(static_cast<PFilename>(name));
                return *out != nullptr;
            });
        };
        s.new_stream = [](void *v, void **out) -> int {
            auto &h = *static_cast<TextFileDocumentHost *>(v);
            return h.Guard([&] {
                *out = NewObj FileByteStream;
                return *out != nullptr;
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
            auto &h = *static_cast<TextFileDocumentHost *>(v);
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
        s.pieces = Pieces;
        s.handles = KauaiHandleServices();
        s.files = KauaiBlockFileServices();
        s.copy_allocator = KauaiCopyAllocator();
        s.create = [](void *v, int profile, void *name, void **out) -> int {
            auto &h = *static_cast<TextFileDocumentHost *>(v);
            return h.Guard([&] {
                *out = profile == MM_TEXT_RICH
                           ? static_cast<void *>(ChunkyFile::PcflCreateTemp(static_cast<PFilename>(name)))
                           : static_cast<void *>(FileObject::PfilCreateTemp(static_cast<PFilename>(name)));
                return *out != nullptr;
            });
        };
        s.save = [](void *v, void *archive) -> int {
            auto &h = *static_cast<TextFileDocumentHost *>(v);
            return h.Guard([&] { return static_cast<PChunkyFile>(archive)->FSave(kctgFramework, nullptr); });
        };
        s.rename = [](void *v, int profile, void *owner, void *name) -> int {
            auto &h = *static_cast<TextFileDocumentHost *>(v);
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
        s.clean = [](void *v) { static_cast<TextFileDocumentHost *>(v)->document->DocumentBase::SetDirty(false); };
        s.error = [](void *, int kind) { PushErc(kind == MM_TEXT_DOC_READ ? ercRtxdReadFailed : ercRtxdSaveFailed); };
        return s;
    }
};
#endif

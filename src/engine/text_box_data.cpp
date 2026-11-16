/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include <exception>
#include <new>
#include "soc_core.h"
#include "mm/text_store.h"
ASSERTNAME
namespace
{
struct Host
{
    PChunkyFile file;
    DataBlock block;
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
    MMTextBoxServices Services()
    {
        MMTextBoxServices s = {};
        s.context = this;
        s.read = [](void *v, MMU32 number, MMReadStream *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                if (!h.file->FFind(kctgTbox, number, &h.block) || !h.block.FUnpackData())
                    return false;
                *out = {&h, static_cast<MMU32>(h.block.Cb()), [](void *v, MMU32 offset, void *p, size_t n) -> int {
                            auto &h = *static_cast<Host *>(v);
                            return h.Guard([&] { return h.block.FReadRgb(p, static_cast<LegacyLong>(n), offset); }) > 0;
                        }};
                return true;
            });
        };
        s.child = [](void *v, MMU32 number, MMU32 slot, MMTextChunk *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                ChildChunkIdentification child;
                if (!h.file->FGetKidChidCtg(kctgTbox, number, slot, kctgRichText, &child))
                    return false;
                *out = {child.cki.ctg, child.cki.cno};
                return true;
            });
        };
        s.put = [](void *v, MMU32 number, const unsigned char *p) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] { return h.file->FPutPv(const_cast<unsigned char *>(p), 36, kctgTbox, number); });
        };
        return s;
    }
};
bool NativeBig()
{
    const unsigned short one = 1;
    return *reinterpret_cast<const byte *>(&one) == 0;
}
} // namespace
bool TextBoxData::FRead(PChunkyFile file, ChunkNumber number, ChunkIdentification *richText)
{
    Host host = {file};
    auto services = host.Services();
    MMTextChunk child;
    int result = mm_text_box_read(&state, number, richText ? &child : nullptr, &services);
    if (result > 0)
    {
        if (richText)
            *richText = {child.type, child.number};
    }
    host.Rethrow();
    return result > 0;
}
bool TextBoxData::FWrite(PChunkyFile file, ChunkNumber number)
{
    Host host = {file};
    auto services = host.Services();
    int result = mm_text_box_write(&state, number, koskCur, NativeBig(), &services);
    host.Rethrow();
    return result > 0;
}

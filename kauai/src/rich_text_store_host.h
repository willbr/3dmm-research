#ifndef RICH_TEXT_STORE_HOST_H
#define RICH_TEXT_STORE_HOST_H
#include "mm/text_store.h"
// Native representations and primitive file/collection operations for C persistence.
namespace RichTextStoreHost
{
inline bool NativeBig()
{
    const unsigned short one = 1;
    return *reinterpret_cast<const byte *>(&one) == 0;
}
inline MMRichTextDefaults Defaults(const RichTextProperties &d)
{
    return {d.bo, d.oskFont, d.dxpDef, d.dypFont, static_cast<MMU32>(d.lwAcrBack)};
}
inline MMMovieTitle Font(String &name)
{
    MMMovieTitle value;
    mm_movie_title_init(&value, sizeof(achar));
    mm_movie_title_set(&value, name.Prgch(), name.Cch());
    return value;
}
inline void Font(const MMMovieTitle &value, PString name)
{
    name->SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(&value))), value.count);
}
inline MMTextLocation Location(const FileLocation &f)
{
    return {f.pfil, static_cast<MMU32>(f.fp), f.cb};
}
inline FileLocation Location(const MMTextLocation &f)
{
    return {static_cast<PFileObject>(f.file), static_cast<FilePosition>(f.offset), f.bytes};
}
inline MMRichTextState State(RichTextData &v)
{
    MMRichTextState s;
    mm_rich_text_init(&s, sizeof(achar), koskCur);
    s.defaults = Defaults(v.defaults);
    s.font = Font(v.fontName);
    s.text = Location(v.text);
    s.properties = v.properties;
    s.arguments = v.arguments;
    s.text_system = v.textOsk;
    s.property_order = v.propertyBo;
    s.property_system = v.propertyOsk;
    s.argument_order = v.argumentBo;
    s.argument_system = v.argumentOsk;
    return s;
}
inline void Publish(RichTextData &v, const MMRichTextState &s)
{
    v.defaults = {static_cast<int16_t>(s.defaults.order), static_cast<int16_t>(s.defaults.font_system),
                  s.defaults.width, s.defaults.height, static_cast<int32_t>(s.defaults.background)};
    Font(s.font, &v.fontName);
    v.text = Location(s.text);
    v.properties = static_cast<PDynamicArray>(s.properties);
    v.arguments = static_cast<PAllocatedGroup>(s.arguments);
    v.textOsk = static_cast<short>(s.text_system);
    v.propertyBo = static_cast<short>(s.property_order);
    v.propertyOsk = static_cast<short>(s.property_system);
    v.argumentBo = static_cast<short>(s.argument_order);
    v.argumentOsk = static_cast<short>(s.argument_system);
}
inline MMRichTextObjects Objects()
{
    return {nullptr, [](void *, void *p) { static_cast<PFileObject>(p)->AddRef(); },
            [](void *, MMU32, void *p) { static_cast<BASE *>(p)->Release(); },
            [](void *, MMU32 kind, void *p) -> MMI32 {
                return kind == MM_TEXT_PROPERTIES ? static_cast<PDynamicArray>(p)->CbEntry()
                                                  : static_cast<PAllocatedGroup>(p)->CbFixed();
            }};
}
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
    MMRichTextReadServices Read()
    {
        MMRichTextReadServices s = {};
        s.context = this;
        s.objects = Objects();
        s.root = [](void *v, MMU32 type, MMU32 number, MMReadStream *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                if (!h.file->FFind(type, number, &h.block) || !h.block.FUnpackData())
                    return false;
                *out = {&h, static_cast<MMU32>(h.block.Cb()), [](void *v, MMU32 offset, void *bytes, size_t n) -> int {
                            auto &h = *static_cast<Host *>(v);
                            return h.Guard(
                                       [&] { return h.block.FReadRgb(bytes, static_cast<LegacyLong>(n), offset); }) > 0;
                        }};
                return true;
            });
        };
        s.font = [](void *v, MMMovieTitle *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                String name;
                if (!name.FRead(&h.block, 16))
                    return false;
                *out = Font(name);
                return true;
            });
        };
        s.child = [](void *v, MMU32 type, MMU32 number, MMU32 kind, MMU32 *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                ChildChunkIdentification child;
                if (!h.file->FGetKidChidCtg(type, number, 0, kind, &child))
                    return false;
                *out = child.cki.cno;
                return true;
            });
        };
        s.location = [](void *v, MMU32 type, MMU32 number, MMTextLocation *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                FileLocation location;
                if (!h.file->FFindFlo(type, number, &location))
                    return false;
                *out = Location(location);
                return true;
            });
        };
        s.read_text = [](void *v, const MMTextLocation *p, void *out, MMU32 n) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                auto location = Location(*p);
                return location.FReadRgb(out, n, 0);
            });
        };
        s.group = [](void *v, MMU32 kind, MMU32 number, void **out, int *order, int *system) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                if (!h.file->FFind(kind, number, &h.block))
                    return false;
                short bo, osk;
                *out = kind == MM_TEXT_PROPERTIES ? static_cast<void *>(DynamicArray::PglRead(&h.block, &bo, &osk))
                                                  : static_cast<void *>(AllocatedGroup::PagRead(&h.block, &bo, &osk));
                if (!*out)
                    return false;
                *order = bo;
                *system = osk;
                return true;
            });
        };
        return s;
    }
    MMRichTextWriteServices Write()
    {
        MMRichTextWriteServices s = {};
        s.context = this;
        s.objects = Objects();
        s.font_bytes = [](void *, const MMMovieTitle *p) -> MMU32 {
            String font;
            Font(*p, &font);
            return font.CbData();
        };
        s.add_root = [](void *v, MMU32 bytes, MMU32 *number) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] { return h.file->FAdd(bytes, kctgRichText, number, &h.block); });
        };
        s.write = [](void *v, MMU32 offset, const void *p, MMU32 n) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] { return h.block.FWriteRgb(const_cast<void *>(p), n, offset); });
        };
        s.font = [](void *v, const MMMovieTitle *p) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                String name;
                Font(*p, &name);
                return name.FWrite(&h.block, 16);
            });
        };
        s.remove = [](void *v, MMU32 number) { static_cast<Host *>(v)->file->Delete(kctgRichText, number); };
        s.add_child = [](void *v, MMU32 root, MMU32 kind, MMU32 bytes) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                ChunkNumber child;
                return h.file->FAddChild(kctgRichText, root, 0, bytes, kind, &child, &h.block);
            });
        };
        s.destination = [](void *v, MMTextLocation *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                FileLocation location;
                if (!h.block.FGetFlo(&location))
                    return false;
                *out = Location(location);
                return true;
            });
        };
        s.copy = [](void *v, const MMTextLocation *source, const MMTextLocation *destination) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                auto from = Location(*source), to = Location(*destination);
                return from.FCopy(&to);
            });
        };
        s.group_bytes = [](void *, MMU32 kind, void *p) -> MMU32 {
            return kind == MM_TEXT_PROPERTIES ? static_cast<PDynamicArray>(p)->CbOnFile()
                                              : static_cast<PAllocatedGroup>(p)->CbOnFile();
        };
        s.count = [](void *, void *p) -> MMI32 { return static_cast<PAllocatedGroup>(p)->IvMac(); };
        s.group = [](void *v, MMU32 kind, void *p, int order, int system) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                return kind == MM_TEXT_PROPERTIES ? static_cast<PDynamicArray>(p)->FWrite(&h.block, order, system)
                                                  : static_cast<PAllocatedGroup>(p)->FWrite(&h.block, order, system);
            });
        };
        return s;
    }
};
} // namespace RichTextStoreHost
#endif

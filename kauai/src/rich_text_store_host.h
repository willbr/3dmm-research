#ifndef RICH_TEXT_STORE_HOST_H
#define RICH_TEXT_STORE_HOST_H
#include <exception>
#include <new>
#include "movie_archive_host.h"
#include "handle_services.h"
#include "mm/text_store.h"
// Native representations and primitive file/collection operations for C persistence.
namespace RichTextStoreHost
{
inline bool NativeBig()
{
    const unsigned short one = 1;
    return *reinterpret_cast<const byte *>(&one) == 0;
}
inline MMTextCollectionConfig LiveCollectionConfig()
{
    MMTextCollectionConfig config = {};
    config.allocator = Group::CollectionAllocator();
#ifdef DEBUG
    config.trash_unused = 1;
#endif
    return config;
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
    MMRichTextArchive archive;
    MMRichTextArchiveReader reader;
    MMRichTextArchiveWriter writer;
    std::exception_ptr exception;

    explicit Host(const MMMovieArchiveEditor &editor, MMChunkOwnerRead *owner = nullptr)
        : archive(mm_rich_text_file_archive(owner, KauaiBlockFileServices(), &editor,
                                             MacWin(koskUniWin, koskUniMac)))
    {
        archive.context = this;
        archive.collections = &Collections();
        archive.location = [](const MMRichTextArchive *source, MMU32 number, MMTextLocation *out) {
            auto &host = *static_cast<Host *>(source->context);
            return host.Guard([&] { return mm_rich_text_file_location(source, number, out); });
        };
        archive.read_at = [](const MMRichTextArchive *source, const MMTextLocation *text,
                             MMU32 offset, void *out, size_t bytes) {
            auto &host = *static_cast<Host *>(source->context);
            return host.Guard([&] { return mm_rich_text_file_read(source, text, offset, out, bytes); });
        };
    }

    template <class F> int Guard(F f)
    {
        try { return f(); }
        catch (const std::bad_alloc &) { exception = std::current_exception(); return -1; }
        catch (...) { exception = std::current_exception(); return -2; }
    }

    void UseLiveCollections()
    {
        archive.collections = nullptr;
        archive.collection_config = LiveCollectionConfig();
    }

    void Rethrow()
    {
        if (exception)
            std::rethrow_exception(exception);
    }

    MMRichTextReadServices Read()
    {
        MMRichTextReadServices services;
        mm_rich_text_archive_reader(&reader, &archive, &services);
        return services;
    }

    MMRichTextWriteServices Write()
    {
        MMRichTextWriteServices services;
        mm_rich_text_archive_writer(&writer, &archive, &services);
        return services;
    }

  private:
    static const MMRichTextArchiveCollections &Collections()
    {
        static const MMRichTextArchiveCollections services{
            [](void *, MMU32, void *p) { static_cast<BASE *>(p)->Release(); },
            [](void *, MMU32 kind, void *p) -> MMI32 {
                return kind == MM_TEXT_PROPERTIES ? static_cast<PDynamicArray>(p)->CbEntry()
                                                  : static_cast<PAllocatedGroup>(p)->CbFixed();
            },
            [](void *v, MMU32 kind, const MMReadStream *stream, void **out, int *order, int *system) {
                auto &h = *static_cast<Host *>(v);
                return h.Guard([&] {
                    short bo, osk;
                    *out = kind == MM_TEXT_PROPERTIES ? static_cast<void *>(DynamicArray::PglReadStream(*stream, &bo, &osk))
                                                      : static_cast<void *>(AllocatedGroup::PagReadStream(*stream, &bo, &osk));
                    if (!*out)
                        return false;
                    *order = bo;
                    *system = osk;
                    return true;
                });
            },
            [](void *, MMU32 kind, void *p) -> MMU32 {
                return kind == MM_TEXT_PROPERTIES ? static_cast<PDynamicArray>(p)->CbOnFile()
                                                  : static_cast<PAllocatedGroup>(p)->CbOnFile();
            },
            [](void *, void *p) -> MMI32 { return static_cast<PAllocatedGroup>(p)->IvMac(); },
            [](void *v, MMU32 kind, const MMStorageWriter *writer, void *p, int order, int system) {
                auto &h = *static_cast<Host *>(v);
                return h.Guard([&] {
                    return kind == MM_TEXT_PROPERTIES ? static_cast<PDynamicArray>(p)->FWriteStream(*writer, order, system)
                                                      : static_cast<PAllocatedGroup>(p)->FWriteStream(*writer, order, system);
                });
            }};
        return services;
    }
};
} // namespace RichTextStoreHost
#endif

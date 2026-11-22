#ifndef RICH_TEXT_STORE_HOST_H
#define RICH_TEXT_STORE_HOST_H
#include <exception>
#include <new>
#include "movie_archive_host.h"
#include "handle_services.h"
#include "mm/text_store.h"
// Native representations and file services for C persistence.
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
    v.properties = s.properties;
    v.arguments = s.arguments;
    v.textOsk = static_cast<short>(s.text_system);
    v.propertyBo = static_cast<short>(s.property_order);
    v.propertyOsk = static_cast<short>(s.property_system);
    v.argumentBo = static_cast<short>(s.argument_order);
    v.argumentOsk = static_cast<short>(s.argument_system);
}
inline MMRichTextObjects Objects()
{
    return {nullptr, [](void *, void *p) { static_cast<PFileObject>(p)->AddRef(); },
            [](void *, void *p) { static_cast<PFileObject>(p)->Release(); }};
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
        archive.collection_config = LiveCollectionConfig();
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

};
} // namespace RichTextStoreHost
#endif

#include "caption_edit.h"
#include "scene_edit.h"
#include "archive_file.h"
#include "mm/native_caption.h"
#include "mm/preview.h"
#include "platform_text.h"
#include "font_catalog.h"
#include "font_host.h"
#include <exception>
#include <new>
#include <stdexcept>
#include <vector>
#include "soc_core.h"
#undef size
ASSERTNAME

namespace
{
void Require(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}
MarcusArchive Open(const std::filesystem::path &path, int mode)
{
    auto file = MarcusOpenArchive(path, mode);
    Require(bool(file), "Cannot open the movie's 2D text.");
    return file;
}

void CheckTextArchive(int status)
{
    static const char *const messages[] = {
        nullptr,
        "Cannot read the scene's text box list.",
        "The scene's text box list is invalid.",
        "Cannot allocate the scene's text box list.",
        "Cannot resolve or link a native 2D text box.",
        "The selected 2D text box has changed.",
        "Cannot save the native 2D text.",
        "Cannot save the scene's text box list."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_SCENE_ARCHIVE_TEXT_WRITE_START + 1,
                  "text archive diagnostics");
    Require(status == MM_SCENE_ARCHIVE_TEXT_OK, messages[status]);
}

void CheckCaptionEdit(int status)
{
    static const char *const messages[] = {
        nullptr,
        "The 2D text width and height must be between 1 and 4096 pixels.",
        "The 2D text position or frame range is out of range.",
        "Choose a visible frame range within the scene.",
        "2D font size must be between 4 and 255 pixels.",
        "Choose left, center or right alignment.",
        "Cannot allocate the 2D text formatting.",
        "Cannot retain the 2D text formatting.",
        "Cannot retain the first characters' formatting.",
        nullptr,
        "Enter text for the new box within the length limit.",
        "Cannot allocate the 2D text properties.",
        "This font name cannot be represented in a native 3DMM text box.",
        "Enter a font family of 1 to 255 characters in UTF-8.",
        "The 2D text is too long to edit.",
        "Cannot read the 2D text.",
        "Cannot decode the 2D text encoding.",
        "Enter valid UTF-8 within the 2D text length limit.",
        "Cannot apply the 2D text formatting.",
        "Cannot copy the selected 2D text box.",
        "Cannot allocate a new 2D text box.",
        "Cannot save the native 2D text.",
        "Cannot save the native 2D text box.",
        "Cannot read the authored text box.",
        "Cannot prepare the 2D text preview.",
        "Cannot allocate the 2D text preview."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_CAPTION_EDIT_PREVIEW_MEMORY + 1,
                  "caption edit diagnostics");
    Require(status == MM_CAPTION_EDIT_OK, messages[status]);
}

}

void MarcusReadCaptionFonts(MarcusCaptionList &list, const KauaiFonts::FontCatalog *bundled)
{
    std::vector<KauaiFonts::FamilyInfo> families;
    KauaiFonts::StudioFontFamilies(families);
    if (bundled)
    {
        const auto extra = bundled->Families(true);
        families.insert(families.end(), extra.begin(), extra.end());
    }
    if (!marcus_caption_catalog_fonts(&list, families.size(), &families,
            [](const void *p, std::size_t index, std::size_t *bytes) {
                const auto &name = (*static_cast<const std::vector<KauaiFonts::FamilyInfo> *>(p))[index].name;
                *bytes = name.size();
                return name.data();
            }, KauaiText::Services(), sizeof(achar)))
        throw std::bad_alloc();
}

void MarcusReadCaptions(const std::filesystem::path &path, const MM::Preview &preview, MarcusCaptionList &list)
{
    auto file = Open(path, MM_PRIVATE_ARCHIVE_READ);
    int detail;
    const int status = marcus_caption_catalog_load(&list, file.get(), KauaiText::Services(), koskCur, &preview,
        [](const void *p, MMU32 colour) -> MMU32 {
            return static_cast<const MM::Preview *>(p)->PresentationColour(colour);
        },
        &detail);
    switch (status)
    {
    case MARCUS_CAPTION_CATALOG_OK: return;
    case MARCUS_CAPTION_CATALOG_ARCHIVE: CheckTextArchive(detail); return;
    case MARCUS_CAPTION_CATALOG_EDIT: CheckCaptionEdit(detail); return;
    case MARCUS_CAPTION_CATALOG_MEMORY: throw std::bad_alloc();
    case MARCUS_CAPTION_CATALOG_UTF8: throw std::runtime_error("Cannot encode the 2D text as UTF-8.");
    case MARCUS_CAPTION_CATALOG_TEXT_LIMIT: throw std::runtime_error("This 2D text cannot fit in the editor.");
    case MARCUS_CAPTION_CATALOG_FONT_LIMIT: throw std::runtime_error("The 2D text font name is too long.");
    default: throw std::runtime_error("Cannot read a native 2D text box.");
    }
}

void MarcusDraftCaption(const std::filesystem::path &path, const MarcusCaptionList &list, int index,
                        const MarcusCaptionOptions &options, const char *text, MMPreviewOwnedCaption &preview)
{
    const auto *entry = marcus_caption_catalog_entry(&list, index);
    Require(index < 0 || entry, "Choose a valid 2D text box.");
    MMCaptionEditInput value, before;
    marcus_caption_catalog_input(&value, &options, text);
    if (entry)
        marcus_caption_catalog_input(&before, &entry->options, entry->text);
    auto file = entry ? Open(path, MM_PRIVATE_ARCHIVE_READ) : MarcusArchive(nullptr, mm_private_archive_close);
    const auto editor = [&] {
        if (file)
            return mm_private_archive_editor(file.get());
        MMMovieArchiveEditor format{};
        const unsigned short order = 1;
        format.text_unit = sizeof(achar);
        format.system = koskCur;
        format.big_endian = *reinterpret_cast<const unsigned char *>(&order) == 0;
        return format;
    }();
    const auto archive = mm_private_archive_rich_text(file.get(), &editor);
    const MMCaptionDraftSource source{&archive, KauaiText::Services(), list.first_frame, list.last_frame,
                                      entry ? &entry->chunk : nullptr};
    CheckCaptionEdit(MM::DraftCaptionData(source, value, entry ? &before : nullptr, preview));
}

std::shared_ptr<MarcusMovieFile> MarcusWriteCaption(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusCaptionList &list, int index,
    const MarcusCaptionOptions *options, const char *text)
{
    struct Copy
    {
        const std::filesystem::path &path;
        const std::shared_ptr<MarcusMovieFile> &origin;
        std::shared_ptr<MarcusMovieFile> file;
        std::exception_ptr failure;
    } copy{path, origin, {}, {}};
    const MarcusCaptionSaveHost host{&copy, [](void *p) -> MMPrivateArchive * {
        auto &copy = *static_cast<Copy *>(p);
        try
        {
            copy.file = MarcusCopyMovie(copy.path, copy.origin);
            return MarcusOpenArchive(copy.file->path, MM_PRIVATE_ARCHIVE_EDIT).release();
        }
        catch (...)
        {
            copy.failure = std::current_exception();
            return nullptr;
        }
    }, KauaiText::Services(), kctgSoc};
    int detail;
    const int status = marcus_caption_catalog_save(&list, index, options, text, &host, &detail);
    if (copy.failure)
        std::rethrow_exception(copy.failure);
    switch (status)
    {
    case MARCUS_CAPTION_CATALOG_OK: return copy.file;
    case MARCUS_CAPTION_CATALOG_NO_CHANGE: return {};
    case MARCUS_CAPTION_CATALOG_EDIT: CheckCaptionEdit(detail); return {};
    case MARCUS_CAPTION_CATALOG_ARCHIVE: CheckTextArchive(detail); return {};
    case MARCUS_CAPTION_CATALOG_OPEN: throw std::runtime_error("Cannot open the movie's 2D text.");
    case MARCUS_CAPTION_CATALOG_SAVE: throw std::runtime_error("Cannot save the 2D text working copy.");
    default: throw std::runtime_error("Choose a valid 2D text box.");
    }
}

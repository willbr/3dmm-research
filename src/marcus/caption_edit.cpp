#include "caption_edit.h"
#include "scene_edit.h"
#include "archive_file.h"
#include "mm/native_caption.h"
#include "mm/preview.h"
#include "platform_text.h"
#include "font_catalog.h"
#include "font_host.h"
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

struct CaptionBody
{
    MMRichTextState state;
    MMCaptionText words{};
    MMRichTextArchive archive;

    CaptionBody(MMPrivateArchive *file, const MMMovieArchiveEditor &editor)
        : archive(mm_private_archive_rich_text(file, &editor))
    {
        mm_rich_text_init(&state, editor.text_unit, editor.system);
    }

    CaptionBody(const CaptionBody &) = delete;
    CaptionBody &operator=(const CaptionBody &) = delete;
    ~CaptionBody()
    {
        mm_rich_text_archive_dispose(&state, &archive);
        mm_caption_edit_dispose_text(&words);
    }

    bool Read(const MMTextChunk &chunk)
    {
        return mm_rich_text_archive_read(&state, &chunk, &archive) > 0;
    }

};
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

MMCaptionEditInput CaptionInput(const MarcusCaptionOptions &value, const char *text)
{
    return {{value.x, value.y, value.width, value.height, value.first_frame, value.last_frame,
             value.through_end, value.scrolling, value.font_size, value.bold, value.italic,
             value.alignment, value.transparent, value.ink, value.paper}, value.font, text};
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
        "Cannot save the native 2D text box."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_CAPTION_EDIT_WRITE_BOX + 1, "caption edit diagnostics");
    Require(status == MM_CAPTION_EDIT_OK, messages[status]);
}

int ApplyCaption(const MMCaptionEditPlan &plan, const MarcusCaptionList &list, MMTextBoxState &box, CaptionBody &rich,
                 int *bodyChanged)
{
    MMRichTextStream stream;
    mm_rich_text_archive_stream(&stream, &rich.archive, &rich.state.text);
    const MMCaptionEditSource source{KauaiText::Services(), &stream.stream, koskCur};
    return mm_caption_edit_apply(&plan, list.first_frame, list.last_frame, &box, &rich.state, &source,
                                 &rich.words, bodyChanged);
}

const MarcusCaptionEntry *Entry(const MarcusCaptionList &list, int index)
{
    if (index < 0) return nullptr;
    const auto *entry = marcus_caption_catalog_entry(&list, index);
    Require(entry != nullptr, "Choose a valid 2D text box.");
    return entry;
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
    const auto *entry = Entry(list, index);
    const auto value = CaptionInput(options, text);
    const auto before = entry ? CaptionInput(entry->options, entry->text) : MMCaptionEditInput{};
    MMCaptionEditPlan plan;
    CheckCaptionEdit(mm_caption_edit_plan(&value, entry ? &before : nullptr, 0, &plan));
    MMTextBoxState box{};
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
    CaptionBody rich(file.get(), editor);
    if (entry)
    {
        MMTextChunk body;
        Require(mm_text_box_read(&box, entry->chunk, &body, &editor) > 0 && rich.Read(body),
                "Cannot read the authored text box.");
    }
    int bodyChanged;
    CheckCaptionEdit(ApplyCaption(plan, list, box, rich, &bodyChanged));
    MMRichTextStream stored;
    mm_rich_text_archive_stream(&stored, &rich.archive, &rich.state.text);
    const MMReadStream *stream = &stored.stream;
    int encoding = rich.state.text_system;
    MMMemoryStream draft;
    if (plan.text_changed)
    {
        mm_memory_stream_init(&draft, rich.words.text, static_cast<MMU32>(rich.words.count * 2));
        stream = &draft.stream;
        encoding = editor.big_endian ? MM_TEXT_UTF16_BE : MM_TEXT_UTF16_LE;
    }
    Require(MM::ReadCaptionData(box, rich.state, *stream, encoding, preview), "Cannot prepare the 2D text preview.");
}

std::shared_ptr<MarcusMovieFile> MarcusWriteCaption(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusCaptionList &list, int index,
    const MarcusCaptionOptions *options, const char *text)
{
    const auto *entry = Entry(list, index);
    MMCaptionEditPlan plan{};
    const auto value = text ? CaptionInput(*options, text) : MMCaptionEditInput{};
    const auto before = entry ? CaptionInput(entry->options, entry->text) : MMCaptionEditInput{};
    if (text)
    {
        const int status = mm_caption_edit_plan(&value, entry ? &before : nullptr, MARCUS_CAPTION_UTF8_CAPACITY, &plan);
        if (status == MM_CAPTION_EDIT_NO_CHANGE)
            return {};
        CheckCaptionEdit(status);
    }
    auto copy = MarcusCopyMovie(path, origin);
    auto file = Open(copy->path, MM_PRIVATE_ARCHIVE_EDIT);
    const MMSceneArchiveText original{entry ? entry->slot : 0, entry ? entry->chunk : 0};
    const MMCaptionArchiveChange edit{list.scene, list.first_frame, list.last_frame, entry ? &original : nullptr,
                                      text ? &plan : nullptr, KauaiText::Services()};
    int captionStatus;
    const int result = mm_caption_edit_private_archive(file.get(), &edit, &captionStatus);
    CheckCaptionEdit(captionStatus);
    CheckTextArchive(result);
    Require(mm_private_archive_save(file.get(), kctgSoc), "Cannot save the 2D text working copy.");
    return copy;
}

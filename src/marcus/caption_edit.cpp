#include "caption_edit.h"
#include "scene_edit.h"
#include "engine_movie_archive.h"
#include "mm/native_caption.h"
#include "mm/preview.h"
#include "mm/caption_edit.h"
#include "platform_text.h"
#include "font_catalog.h"
#include "font_host.h"
#include <cstring>
#include <map>
#include <exception>
#include <stdexcept>
#include <utility>
#include "soc_core.h"
#undef size
ASSERTNAME

namespace
{
constexpr MMU32 Rgb = 0x01000000, Clear = 0xffffffffU;

struct Release { template<class T> void operator()(T *p) const { if (p) p->Release(); } };
template<class T> using Owned = std::unique_ptr<T, Release>;

void Require(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}
template<class F> int CaptionCall(std::exception_ptr &failure, F function, int failed = 0)
{
    try { return function(); }
    catch (...) { failure = std::current_exception(); return failed; }
}
Owned<ChunkyFile> Open(const std::filesystem::path &path, bool write)
{
    String name;
    Filename filename;
    auto utf8 = path.u8string();
    name.SetSzs(utf8.data());
    Require(filename.FBuildFromPath(&name), "Invalid 2D text movie path.");
    Owned<ChunkyFile> file(ChunkyFile::PcflOpen(&filename, write ? fcflWriteEnable : fcflNil));
    Require(bool(file), "Cannot open the movie's 2D text.");
    return file;
}

struct CaptionBody
{
    MMRichTextState state;
    MMCaptionText words{};
    MMRichTextArchive archive;

    CaptionBody(PChunkyFile file, const MMMovieArchiveEditor &editor) : archive{
        &editor, file, MacWin(koskUniWin, koskUniMac),
        [](void *p, MMU32 number, MMTextLocation *out) -> int {
            FileLocation location;
            if (!static_cast<PChunkyFile>(p)->FFindFlo(MM_TEXT_BYTES, number, &location))
                return 0;
            *out = {location.pfil, static_cast<MMU32>(location.fp), location.cb};
            return 1;
        },
        [](void *, const MMTextLocation *text, MMU32 offset, void *out, size_t bytes) -> int {
            FileLocation location{static_cast<PFileObject>(text->file), static_cast<FilePosition>(text->offset),
                                  text->bytes};
            return location.FReadRgb(out, static_cast<LegacyLong>(bytes), offset);
        },
        [](void *, void *file) { static_cast<PFileObject>(file)->AddRef(); },
        [](void *, void *file) { static_cast<PFileObject>(file)->Release(); }, nullptr}
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

    bool Write(bool replaced, MMTextChunk *out)
    {
        return mm_caption_edit_write_body(&state, replaced ? &words : nullptr, KauaiText::Services(), out, &archive) > 0;
    }
};
std::string ToUtf8(const unsigned short *words, size_t count)
{
    std::string result(count * 3, '\0');
    auto end = result.data();
    Require(mm_text_encode_utf8(words, count, &end, [](void *p, const char *bytes, std::size_t count) {
        auto &next = *static_cast<char **>(p);
        std::memcpy(next, bytes, count);
        next += count;
        return 1;
    }) == 1, "Cannot encode the 2D text as UTF-8.");
    result.resize(end - result.data());
    return result;
}
std::string Font(const MMRichTextState &rich)
{
    unsigned short units[kcchMaxStn];
    const auto count = mm_text_utf16(KauaiText::Services(), mm_movie_title_text(&rich.font),
                                    rich.font.count * rich.font.unit, koskCur, units, kcchMaxStn);
    Require(!rich.font.count || count, "Cannot decode the 2D text encoding.");
    return ToUtf8(units, count);
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
    return mm_caption_edit_apply(&plan, list.firstFrame, list.lastFrame, &box, &rich.state, &source,
                                 &rich.words, bodyChanged);
}
}

std::vector<std::string> MarcusCaptionFontNames(const KauaiFonts::FontCatalog *bundled)
{
    std::vector<KauaiFonts::FamilyInfo> families;
    KauaiFonts::StudioFontFamilies(families);
    if (bundled)
    {
        const auto extra = bundled->Families(true);
        families.insert(families.end(), extra.begin(), extra.end());
    }
    std::map<std::string, std::string> names;
    for (auto &family : families)
    {
        MMMovieTitle font;
        mm_movie_title_init(&font, sizeof(achar));
        if (mm_caption_edit_font(family.name.data(), family.name.size(), KauaiText::Services(), &font) != MM_CAPTION_EDIT_OK)
            continue;
        auto key = family.name;
        for (auto &c : key)
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        names.insert_or_assign(std::move(key), std::move(family.name));
    }
    std::vector<std::string> result;
    result.reserve(names.size());
    for (auto &entry : names)
        result.push_back(std::move(entry.second));
    return result;
}

void MarcusReadCaptions(const std::filesystem::path &path, const MM::Preview &preview, MarcusCaptionList &list)
{
    auto file = Open(path, false);
    MovieArchiveHost host{file.get(), {}, {}};
    const auto editor = host.Editor();
    MMSceneArchiveText *links;
    MMU32 count;
    CheckTextArchive(mm_scene_archive_list_text(&editor, list.scene, &links, &count));
    const std::unique_ptr<MMSceneArchiveText, decltype(&mm_scene_archive_free_text_list)> owned(
        links, mm_scene_archive_free_text_list);
    for (MMU32 i = 0; i < count; ++i)
    {
        const auto &link = links[i];
        MMTextChunk body;
        MMTextBoxState box;
        CaptionBody rich(file.get(), editor);
        Require(mm_text_box_read(&box, link.number, &body, &editor) > 0 && rich.Read(body),
                "Cannot read a native 2D text box.");
        MMRichTextStream stream;
        mm_rich_text_archive_stream(&stream, &rich.archive, &rich.state.text);
        CheckCaptionEdit(mm_caption_edit_load_text(&stream.stream, KauaiText::Services(), rich.state.text_system,
                                                   &rich.words));
        const auto units = mm_caption_edit_normalize(rich.words.text, rich.words.count, 10);
        auto text = ToUtf8(rich.words.text, units);
        Require(text.size() < MARCUS_CAPTION_UTF8_CAPACITY && text.find('\0') == std::string::npos,
                "This 2D text cannot fit in the editor.");
        MarcusCaptionEntry entry{link.slot, link.number, {}, std::move(text)};
        auto &v = entry.options;
        MMCaptionEditOptions fields;
        CheckCaptionEdit(mm_caption_edit_read_box(&box, list.firstFrame, list.lastFrame, &fields));
        v.x = fields.x; v.y = fields.y;
        v.width = fields.width; v.height = fields.height;
        v.first_frame = fields.first; v.last_frame = fields.last;
        v.through_end = fields.through_end; v.scrolling = fields.scrolling;
        v.font_size = rich.state.defaults.height;
        v.transparent = rich.state.defaults.background == Clear;
        v.paper = v.transparent ? 0 : preview.PresentationColour(rich.state.defaults.background) & 0xffffffU;
        const auto font = Font(rich.state);
        Require(font.size() < sizeof(v.font), "The 2D text font name is too long.");
        std::memcpy(v.font, font.c_str(), font.size() + 1);
        const auto properties = static_cast<const MMTextCollection *>(rich.state.properties);
        const auto rows = mm_text_collection_count(properties);
        for (MMU32 i = 0; i < rows; ++i)
        {
            MMU32 property[2];
            if (!mm_text_collection_property(properties, i, property) || (property[0] & 0xffffffU)) continue;
            switch (property[0] >> 24)
            {
            case MM_CAPTION_STYLE: v.bold = (property[1] & 1) != 0; v.italic = (property[1] & 2) != 0; break;
            case MM_CAPTION_FOREGROUND: v.ink = preview.PresentationColour(Rgb + property[1]) & 0xffffffU; break;
            case MM_CAPTION_ALIGNMENT: v.alignment = property[1] >> 24; break;
            }
        }
        list.entries.push_back(std::move(entry));
    }
}

MM::PreviewCaption MarcusDraftCaption(const std::filesystem::path &path, const MarcusCaptionList &list, int index,
                                     const MarcusCaptionOptions &options, const char *text)
{
    const auto *entry = index < 0 ? nullptr : &list.entries.at(index);
    const auto value = CaptionInput(options, text);
    const auto before = entry ? CaptionInput(entry->options, entry->text.c_str()) : MMCaptionEditInput{};
    MMCaptionEditPlan plan;
    CheckCaptionEdit(mm_caption_edit_plan(&value, entry ? &before : nullptr, 0, &plan));
    MMTextBoxState box{};
    auto file = entry ? Open(path, false) : Owned<ChunkyFile>{};
    MovieArchiveHost host{file.get(), {}, {}};
    const auto editor = host.Editor();
    CaptionBody rich(file.get(), editor);
    if (entry)
    {
        MMTextChunk body;
        Require(mm_text_box_read(&box, entry->chunk, &body, &editor) > 0 && rich.Read(body),
                "Cannot read the authored text box.");
    }
    int bodyChanged;
    CheckCaptionEdit(ApplyCaption(plan, list, box, rich, &bodyChanged));
    MM::PreviewCaption preview;
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
    return preview;
}

std::shared_ptr<MarcusMovieFile> MarcusWriteCaption(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusCaptionList &list, int index,
    const MarcusCaptionOptions *options, const char *text)
{
    const auto *entry = index < 0 ? nullptr : &list.entries.at(index);
    MMCaptionEditPlan plan{};
    const auto value = text ? CaptionInput(*options, text) : MMCaptionEditInput{};
    const auto before = entry ? CaptionInput(entry->options, entry->text.c_str()) : MMCaptionEditInput{};
    if (text)
    {
        const int status = mm_caption_edit_plan(&value, entry ? &before : nullptr, MARCUS_CAPTION_UTF8_CAPACITY, &plan);
        if (status == MM_CAPTION_EDIT_NO_CHANGE)
            return {};
        CheckCaptionEdit(status);
    }
    auto copy = MarcusCopyMovie(path, origin);
    auto file = Open(copy->path, true);
    MovieArchiveHost host{file.get(), {}, {}};
    const auto editor = host.Editor();
    const MMSceneArchiveText original{entry ? entry->slot : 0, entry ? entry->chunk : 0};
    struct Write
    {
        PChunkyFile file;
        const MMMovieArchiveEditor &editor;
        const MarcusCaptionList &list;
        const MMCaptionEditPlan &plan;
        MMTextBoxState box;
        CaptionBody rich;
        std::exception_ptr failure;
    } write{file.get(), editor, list, plan, {}, CaptionBody(file.get(), editor), {}};
    MMSceneArchiveTextEdit edit{list.scene, list.firstFrame, entry ? &original : nullptr, &write, nullptr};
    if (text)
        edit.write_box = [](void *context, const MMSceneArchiveText *original, MMU32 *number) -> int {
            auto &write = *static_cast<Write *>(context);
            const MMCaptionArchiveBox io = {
                &write, &write.box,
                [](void *p, MMU32 original, MMU32 *number) {
                    auto &w = *static_cast<Write *>(p);
                    return CaptionCall(w.failure, [&] { return w.file->FClone(kctgTbox, original, w.file, number); });
                },
                [](void *p, const MMTextChunk *body) {
                    auto &w = *static_cast<Write *>(p);
                    return CaptionCall(w.failure, [&] { return w.rich.Read(*body); });
                },
                [](void *p, int *bodyChanged) {
                    auto &w = *static_cast<Write *>(p);
                    return CaptionCall(w.failure, [&] {
                        return ApplyCaption(w.plan, w.list, w.box, w.rich, bodyChanged);
                    }, MM_CAPTION_EDIT_WRITE_BOX);
                },
                [](void *p, MMTextChunk *body) {
                    auto &w = *static_cast<Write *>(p);
                    return CaptionCall(w.failure, [&] { return w.rich.Write(w.plan.text_changed != 0, body); });
                }
            };
            return CaptionCall(write.failure, [&] {
                const int status = mm_caption_edit_archive_box(&write.editor, original, &io, number);
                if (write.failure)
                    return 0;
                CheckCaptionEdit(status);
                return 1;
            });
        };
    const int result = mm_scene_archive_edit_text(&editor, &edit);
    if (write.failure) std::rethrow_exception(write.failure);
    CheckTextArchive(result);
    Require(file->FSave(kctgSoc), "Cannot save the 2D text working copy.");
    return copy;
}

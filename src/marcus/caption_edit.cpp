#include "caption_edit.h"
#include "scene_edit.h"
#include "movie_archive_host.h"
#include "mm/native_caption.h"
#include "mm/preview.h"
#include "mm/caption_edit.h"
#include "platform_text.h"
#include "font_catalog.h"
#include "font_host.h"
#include <codecvt>
#include <cstring>
#include <locale>
#include <map>
#include <exception>
#include <stdexcept>
#include <tuple>
#include <utility>
#include "soc_core.h"
#undef size
ASSERTNAME

namespace
{
using Encoding = KauaiText::Encoding;
constexpr int MaxText = 65536;
constexpr MMU32 Rgb = 0x01000000, Clear = 0xffffffffU;

struct Release { template<class T> void operator()(T *p) const { if (p) p->Release(); } };
template<class T> using Owned = std::unique_ptr<T, Release>;
using Utf8 = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>;
using Words = std::vector<unsigned short>;

void Require(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
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
Words Decode(const void *bytes, std::size_t length, Encoding encoding)
{
    if (!length) return {};
    Words units(KauaiText::ToUtf16(bytes, length, encoding, nullptr, 0));
    Require(!units.empty() && KauaiText::ToUtf16(bytes, length, encoding, units.data(), units.size()) == units.size(),
            "Cannot decode the 2D text encoding.");
    return units;
}
Words ReadText(RichTextData &rich)
{
    Require(rich.text.cb >= 0 && rich.text.cb <= MaxText * 4, "The 2D text is too long to edit.");
    std::vector<byte> bytes(rich.text.cb);
    Require(bytes.empty() || rich.text.FReadRgb(bytes.data(), rich.text.cb, 0), "Cannot read the 2D text.");
    return Decode(bytes.data(), bytes.size(), static_cast<Encoding>(rich.textOsk));
}
std::string ToUtf8(const Words &words)
{
    return Utf8().to_bytes(std::u16string(words.begin(), words.end()));
}
std::vector<byte> LittleEndianText(const Words &text)
{
    std::vector<byte> bytes;
    bytes.reserve(text.size() * 2);
    for (auto unit : text)
    {
        bytes.push_back(static_cast<byte>(unit));
        bytes.push_back(static_cast<byte>(unit >> 8));
    }
    return bytes;
}
void WriteText(RichTextData &rich, const Words &text)
{
    auto bytes = LittleEndianText(text);
    auto encoding = Encoding::Utf16LE;
    if (!bytes.empty())
    {
        std::vector<byte> ansi(KauaiText::ToAnsi(bytes.data(), bytes.size(), encoding, nullptr, 0));
        if (ansi.size() == text.size() &&
            KauaiText::ToAnsi(bytes.data(), bytes.size(), encoding, reinterpret_cast<char *>(ansi.data()),
                              ansi.size()) == ansi.size() &&
            Decode(ansi.data(), ansi.size(), Encoding::WindowsAnsi) == text)
        {
            bytes = std::move(ansi);
            encoding = Encoding::WindowsAnsi;
        }
    }
    Owned<FileObject> stream(FileObject::PfilCreateTemp());
    const auto length = static_cast<LegacyLong>(bytes.size());
    Require(stream && (bytes.empty() || stream->FWriteRgb(bytes.data(), length, 0)),
            "Cannot write the 2D text.");
    ReleasePpo(&rich.text.pfil);
    rich.text = {stream.release(), 0, length};
    rich.textOsk = static_cast<short>(encoding);
}
std::string Font(RichTextData &rich)
{
    return ToUtf8(Decode(rich.fontName.Psz(), rich.fontName.Cch() * sizeof(achar),
                                  static_cast<Encoding>(koskCur)));
}
LegacyLong EncodeFont(const std::u16string &text, achar (&native)[kcchMaxStn])
{
    const Words units(text.begin(), text.end());
    auto bytes = LittleEndianText(units);
    const auto length = CchTranslateRgb(bytes.data(), static_cast<LegacyLong>(bytes.size()),
                                        static_cast<short>(Encoding::Utf16LE), native, kcchMaxStn);
    return length > 0 && Decode(native, length * sizeof(achar), static_cast<Encoding>(koskCur)) == units ? length : 0;
}
void SetFont(RichTextData &rich, const char *font)
{
    const auto units = Utf8().from_bytes(font);
    Require(!units.empty() && units.size() <= kcchMaxStn, "Enter a font family of 1 to 255 characters.");
    achar native[kcchMaxStn];
    const auto length = EncodeFont(units, native);
    Require(length > 0, "This font name cannot be represented in a native 3DMM text box.");
    rich.fontName.SetRgch(native, length);
    rich.defaults.oskFont = koskCur;
}

struct PropertyMap
{
    RichTextData &rich;
    const Words &words;
    int diagnosed = 0;
    MMTextPropertyEntry Get(MMI32 index) const
    {
        MMTextPropertyEntry entry;
        rich.properties->Get(index, &entry);
        if (rich.propertyBo == kboOther) SwapBytesRglw(&entry, 2);
        return entry;
    }
    MMTextPropertyServices Services()
    {
        MMTextPropertyServices services{};
        services.context = this;
        services.count = [](void *p) -> MMI32 { return static_cast<PropertyMap *>(p)->rich.properties->IvMac(); };
        services.get = [](void *p, MMI32 row, MMTextPropertyEntry *out) { *out = static_cast<PropertyMap *>(p)->Get(row); };
        services.put = [](void *p, MMI32 row, const MMTextPropertyEntry *entry) {
            auto &map = *static_cast<PropertyMap *>(p);
            auto wire = *entry;
            if (map.rich.propertyBo == kboOther) SwapBytesRglw(&wire, 2);
            map.rich.properties->Put(row, &wire);
        };
        services.insert = [](void *p, MMI32 row, const MMTextPropertyEntry *entry) -> int {
            auto &map = *static_cast<PropertyMap *>(p);
            auto wire = *entry;
            if (map.rich.propertyBo == kboOther) SwapBytesRglw(&wire, 2);
            try { return map.rich.properties->FInsert(row, &wire) ? 1 : -1; } catch (...) { return -1; }
        };
        services.remove = [](void *p, MMI32 row) { static_cast<PropertyMap *>(p)->rich.properties->Delete(row); };
        services.clear_cache = [](void *) {};
        services.paragraph_end = [](void *p, MMI32 at, MMI32 *end) -> int {
            const auto &text = static_cast<PropertyMap *>(p)->words;
            *end = mm_caption_edit_paragraph_end(text.data(), static_cast<MMI32>(text.size()), at);
            return 1;
        };
        services.diagnostic = [](void *p, int) { static_cast<PropertyMap *>(p)->diagnosed = 1; };
        auto &args = services.arguments;
        args.context = this;
        args.objects.context = this;
        args.objects.get = [](void *p, int) -> void * { return static_cast<PropertyMap *>(p)->rich.arguments; };
        args.count = [](void *, void *g) -> MMI32 { return static_cast<PAllocatedGroup>(g)->IvMac(); };
        args.free_slot = [](void *, void *g, MMI32 row) -> int { return static_cast<PAllocatedGroup>(g)->FFree(row); };
        args.fixed = [](void *p, void *g, MMI32 row) -> MMU32 {
            MMU32 value;
            static_cast<PAllocatedGroup>(g)->GetFixed(row, &value);
            if (static_cast<PropertyMap *>(p)->rich.argumentBo == kboOther) SwapBytesRglw(&value, 1);
            return value;
        };
        args.put_fixed = [](void *p, void *g, MMI32 row, MMU32 value) {
            if (static_cast<PropertyMap *>(p)->rich.argumentBo == kboOther) SwapBytesRglw(&value, 1);
            static_cast<PAllocatedGroup>(g)->PutFixed(row, &value);
        };
        args.remove = [](void *, void *g, MMI32 row) { static_cast<PAllocatedGroup>(g)->Delete(row); };
        args.diagnostic = services.diagnostic;
        return services;
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

auto Fields(const MarcusCaptionOptions &v)
{
    return std::tie(v.x, v.y, v.width, v.height, v.first_frame, v.last_frame, v.through_end, v.font_size,
                    v.bold, v.italic, v.alignment, v.scrolling, v.transparent, v.ink, v.paper);
}
MMCaptionEditOptions EngineOptions(const MarcusCaptionOptions &value)
{
    return {value.x, value.y, value.width, value.height, value.first_frame, value.last_frame,
            value.through_end, value.scrolling, value.font_size, value.bold, value.italic,
            value.alignment, value.transparent, value.ink, value.paper};
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
        "Cannot retain the first characters' formatting."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_CAPTION_EDIT_INHERIT + 1, "caption edit diagnostics");
    Require(status == MM_CAPTION_EDIT_OK, messages[status]);
}
void UpdateBox(TextBoxData &box, const MarcusCaptionList &list, const MarcusCaptionOptions &value,
               const MarcusCaptionOptions *before)
{
    const auto options = EngineOptions(value);
    const auto prior = before ? EngineOptions(*before) : MMCaptionEditOptions{};
    CheckCaptionEdit(mm_caption_edit_write_box(&box.state, list.firstFrame, list.lastFrame,
                                               &options, before ? &prior : nullptr));
}
bool UpdateRich(RichTextData &rich, const MarcusCaptionOptions &value, const MarcusCaptionOptions *before,
                const char *text, bool wordsChanged, std::u16string *draft = nullptr)
{
    if (!before)
    {
        rich.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
        Require(rich.properties != nullptr, "Cannot allocate the 2D text properties.");
    }
    const bool fontChanged = !before || std::strcmp(before->font, value.font);
    if (fontChanged)
        SetFont(rich, value.font);
    const auto options = EngineOptions(value);
    const auto prior = before ? EngineOptions(*before) : MMCaptionEditOptions{};
    MMCaptionEditFormat format;
    CheckCaptionEdit(mm_caption_edit_format(&options, before ? &prior : nullptr, &format));
    if (format.defaults & MM_CAPTION_EDIT_SET_SIZE)
        rich.defaults.dypFont = value.font_size;
    if (format.defaults & MM_CAPTION_EDIT_SET_PAPER)
        rich.defaults.lwAcrBack = format.paper;
    if (format.defaults & MM_CAPTION_EDIT_SET_WIDTH)
        rich.defaults.dxpDef = value.width;
    if (!wordsChanged && !format.count)
        return fontChanged || format.defaults;
    const auto oldText = before ? ReadText(rich) : Words{};
    Words newText;
    if (wordsChanged)
    {
        Utf8 utf8;
        const auto decoded = utf8.from_bytes(text);
        newText.assign(decoded.begin(), decoded.end());
        newText.resize(mm_caption_edit_normalize(newText.data(), newText.size(), 13));
        Require(utf8.converted() == std::strlen(text) && newText.size() <= MaxText,
                "Enter valid UTF-8 within the 2D text length limit.");
    }
    const auto &words = wordsChanged ? newText : oldText;
    PropertyMap map{rich, words};
    const auto services = map.Services();
    const auto characters = static_cast<MMI32>(words.size()) + 1;
    if (wordsChanged)
    {
        CheckCaptionEdit(mm_caption_edit_replace(oldText.data(), static_cast<MMI32>(oldText.size()),
                                                 newText.data(), characters - 1, &services, &map.diagnosed));
        if (!draft) WriteText(rich, newText);
    }
    if (format.count)
        Require(mm_text_properties_apply_changes(0, characters, characters, format.changes,
                                                 format.count, &services) > 0 && !map.diagnosed,
                "Cannot apply the 2D text formatting.");
    if (draft && wordsChanged) draft->assign(newText.begin(), newText.end());
    return true;
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
        auto units = Utf8().from_bytes(family.name);
        achar native[kcchMaxStn];
        if (units.empty() || units.size() > kcchMaxStn || !EncodeFont(units, native))
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
        ChunkIdentification body;
        TextBoxData box;
        RichTextData rich;
        Require(box.FRead(file.get(), link.number, &body) && rich.FRead(file.get(), body.ctg, body.cno),
                "Cannot read a native 2D text box.");
        auto words = ReadText(rich);
        auto display = words;
        display.resize(mm_caption_edit_normalize(display.data(), display.size(), 10));
        auto text = ToUtf8(display);
        Require(text.size() < MARCUS_CAPTION_UTF8_CAPACITY && text.find('\0') == std::string::npos,
                "This 2D text cannot fit in the editor.");
        const auto &s = box.state;
        MarcusCaptionEntry entry{link.slot, link.number, {}, std::move(text)};
        auto &v = entry.options;
        MMCaptionEditOptions fields;
        CheckCaptionEdit(mm_caption_edit_read_box(&s, list.firstFrame, list.lastFrame, &fields));
        v.x = fields.x; v.y = fields.y;
        v.width = fields.width; v.height = fields.height;
        v.first_frame = fields.first; v.last_frame = fields.last;
        v.through_end = fields.through_end; v.scrolling = fields.scrolling;
        v.font_size = rich.defaults.dypFont;
        v.transparent = static_cast<MMU32>(rich.defaults.lwAcrBack) == Clear;
        v.paper = v.transparent ? 0 : preview.PresentationColour(rich.defaults.lwAcrBack) & 0xffffffU;
        const auto font = Font(rich);
        Require(font.size() < sizeof(v.font), "The 2D text font name is too long.");
        std::memcpy(v.font, font.c_str(), font.size() + 1);
        const PropertyMap map{rich, words};
        for (LegacyLong i = 0; i < rich.properties->IvMac(); ++i)
        {
            const auto property = map.Get(i);
            if (property.key & 0xffffffU) continue;
            switch (property.key >> 24)
            {
            case MM_CAPTION_STYLE: v.bold = (property.value & 1) != 0; v.italic = (property.value & 2) != 0; break;
            case MM_CAPTION_FOREGROUND: v.ink = preview.PresentationColour(Rgb + static_cast<MMU32>(property.value)) & 0xffffffU; break;
            case MM_CAPTION_ALIGNMENT: v.alignment = static_cast<MMU32>(property.value) >> 24; break;
            }
        }
        list.entries.push_back(std::move(entry));
    }
}

MM::PreviewCaption MarcusDraftCaption(const std::filesystem::path &path, const MarcusCaptionList &list, int index,
                                     const MarcusCaptionOptions &options, const char *text)
{
    const auto *entry = index < 0 ? nullptr : &list.entries.at(index);
    const bool wordsChanged = !entry || entry->text != text;
    TextBoxData box;
    RichTextData rich;
    // Borrow the authored stream while applying the same changes as Save.
    Owned<ChunkyFile> file;
    if (entry)
    {
        file = Open(path, false);
        ChunkIdentification body;
        Require(box.FRead(file.get(), entry->chunk, &body) && rich.FRead(file.get(), body.ctg, body.cno),
                "Cannot read the authored text box.");
    }
    UpdateBox(box, list, options, entry ? &entry->options : nullptr);
    std::u16string words;
    UpdateRich(rich, options, entry ? &entry->options : nullptr, text, wordsChanged, &words);
    MM::PreviewCaption preview;
    Require(MM::ReadCaptionData(box, rich, wordsChanged ? &words : nullptr, preview),
            "Cannot prepare the 2D text preview.");
    return preview;
}

std::shared_ptr<MarcusMovieFile> MarcusWriteCaption(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusCaptionList &list, int index,
    const MarcusCaptionOptions *options, const char *text)
{
    const auto *entry = index < 0 ? nullptr : &list.entries.at(index);
    const bool wordsChanged = !entry || (text && entry->text != text);
    if (entry && text && !wordsChanged && Fields(entry->options) == Fields(*options) &&
        !std::strcmp(entry->options.font, options->font)) return {};
    if (text)
        Require(std::strlen(text) < MARCUS_CAPTION_UTF8_CAPACITY && (entry || text[0]),
                "Enter text for the new box within the length limit.");
    auto copy = MarcusCopyMovie(path, origin);
    auto file = Open(copy->path, true);
    MovieArchiveHost host{file.get(), {}, {}};
    const auto editor = host.Editor();
    const MMSceneArchiveText original{entry ? entry->slot : 0, entry ? entry->chunk : 0};
    struct Write
    {
        PChunkyFile file;
        const MarcusCaptionList &list;
        const MarcusCaptionOptions *options;
        const MarcusCaptionEntry *entry;
        const char *text;
        bool wordsChanged;
        std::exception_ptr failure;
    } write{file.get(), list, options, entry, text, wordsChanged, {}};
    MMSceneArchiveTextEdit edit{list.scene, list.firstFrame, entry ? &original : nullptr, &write, nullptr};
    if (text)
        edit.write_box = [](void *context, const MMSceneArchiveText *original, MMU32 *number) -> int {
            auto &write = *static_cast<Write *>(context);
            try
            {
                TextBoxData box;
                RichTextData rich;
                ChunkIdentification prior;
                if (original)
                {
                    Require(write.file->FClone(kctgTbox, original->number, write.file, number) &&
                            box.FRead(write.file, *number, &prior) && rich.FRead(write.file, prior.ctg, prior.cno),
                            "Cannot copy the selected 2D text box.");
                }
                else
                    Require(write.file->FAdd(0, kctgTbox, number), "Cannot allocate a new 2D text box.");
                const auto *before = write.entry ? &write.entry->options : nullptr;
                UpdateBox(box, write.list, *write.options, before);
                if (UpdateRich(rich, *write.options, before, write.text, write.wordsChanged))
                {
                    ChunkIdentification body;
                    Require(rich.FWrite(write.file, &body) &&
                            write.file->FAdoptChild(kctgTbox, *number, body.ctg, body.cno, box.state.child),
                            "Cannot save the native 2D text.");
                    if (original)
                        write.file->DeleteChild(kctgTbox, *number, prior.ctg, prior.cno, box.state.child);
                }
                return box.FWrite(write.file, *number);
            }
            catch (...) { write.failure = std::current_exception(); return false; }
        };
    const int result = mm_scene_archive_edit_text(&editor, &edit);
    if (write.failure) std::rethrow_exception(write.failure);
    CheckTextArchive(result);
    Require(file->FSave(kctgSoc), "Cannot save the 2D text working copy.");
    return copy;
}

#include "caption_edit.h"
#include "scene_edit.h"
#include "engine_movie_archive.h"
#include "mm/native_caption.h"
#include "mm/preview.h"
#include "mm/caption_edit.h"
#include "platform_text.h"
#include "font_catalog.h"
#include "font_host.h"
#include <algorithm>
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
using Encoding = KauaiText::Encoding;
constexpr MMU32 Rgb = 0x01000000, Clear = 0xffffffffU;

struct Release { template<class T> void operator()(T *p) const { if (p) p->Release(); } };
template<class T> using Owned = std::unique_ptr<T, Release>;
using Words = std::vector<unsigned short>;

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
    Require(rich.text.cb >= 0 && rich.text.cb <= MM_CAPTION_EDIT_MAX_UNITS * 4, "The 2D text is too long to edit.");
    std::vector<byte> bytes(rich.text.cb);
    Require(bytes.empty() || rich.text.FReadRgb(bytes.data(), rich.text.cb, 0), "Cannot read the 2D text.");
    return Decode(bytes.data(), bytes.size(), static_cast<Encoding>(rich.textOsk));
}
std::string ToUtf8(const Words &words)
{
    std::string result(words.size() * 3, '\0');
    auto end = result.data();
    Require(mm_text_encode_utf8(words.data(), words.size(), &end, [](void *p, const char *bytes, std::size_t count) {
        auto &next = *static_cast<char **>(p);
        std::memcpy(next, bytes, count);
        next += count;
        return 1;
    }) == 1, "Cannot encode the 2D text as UTF-8.");
    result.resize(end - result.data());
    return result;
}
void WriteText(RichTextData &rich, const MMCaptionEditWords &text)
{
    std::vector<byte> bytes(text.count * 2);
    std::size_t count;
    const auto encoding = mm_caption_edit_encode(KauaiText::Services(), text.text, text.count, bytes.data(),
                                                 bytes.size(), &count);
    Require(encoding != 0, "Cannot encode the 2D text.");
    Owned<FileObject> stream(FileObject::PfilCreateTemp());
    const auto length = static_cast<LegacyLong>(count);
    Require(stream && (!length || stream->FWriteRgb(bytes.data(), length, 0)),
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
LegacyLong EncodeFont(const unsigned short *units, std::size_t count, achar (&native)[kcchMaxStn])
{
#ifdef UNICODE
    std::copy_n(units, count, native);
    return static_cast<LegacyLong>(count);
#else
    return static_cast<LegacyLong>(mm_text_ansi_from_units(KauaiText::Services(), units, count, native, kcchMaxStn, 1));
#endif
}
void SetFont(RichTextData &rich, const char *font)
{
    unsigned short units[kcchMaxStn];
    std::size_t count;
    Require(mm_text_read_utf8(font, std::strlen(font), units, kcchMaxStn, &count) == 1 && count,
            "Enter a font family of 1 to 255 characters in UTF-8.");
    achar native[kcchMaxStn];
    const auto length = EncodeFont(units, count, native);
    Require(length > 0, "This font name cannot be represented in a native 3DMM text box.");
    rich.fontName.SetRgch(native, length);
    rich.defaults.oskFont = koskCur;
}

struct PropertyMap
{
    RichTextData &rich;
    MMCaptionEditWords words{};
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
            *end = mm_caption_edit_paragraph_end(text.text, static_cast<MMI32>(text.count), at);
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
        "Cannot set the 2D text font.",
        "Cannot read the 2D text.",
        "Enter valid UTF-8 within the 2D text length limit.",
        "Cannot write the 2D text.",
        "Cannot apply the 2D text formatting.",
        "Cannot prepare the 2D text preview.",
        "Cannot copy the selected 2D text box.",
        "Cannot allocate a new 2D text box.",
        "Cannot save the native 2D text.",
        "Cannot save the native 2D text box."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_CAPTION_EDIT_WRITE_BOX + 1, "caption edit diagnostics");
    Require(status == MM_CAPTION_EDIT_OK, messages[status]);
}

struct CaptionEditHost
{
    PropertyMap map;
    Words oldText;
    std::u16string *draft;
    std::exception_ptr failure;

    MMCaptionEditHost Services()
    {
        MMCaptionEditHost services = {
            this,
            [](void *p) {
                auto &host = *static_cast<CaptionEditHost *>(p);
                return CaptionCall(host.failure, [&] {
                    host.map.rich.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
                    return host.map.rich.properties != nullptr;
                });
            },
            [](void *p, const char *font) {
                auto &host = *static_cast<CaptionEditHost *>(p);
                return CaptionCall(host.failure, [&] { SetFont(host.map.rich, font); return 1; });
            },
            [](void *p, MMCaptionEditWords *words) {
                auto &host = *static_cast<CaptionEditHost *>(p);
                return CaptionCall(host.failure, [&] {
                    host.oldText = ReadText(host.map.rich);
                    *words = {host.oldText.data(), host.oldText.size()};
                    return 1;
                });
            },
            [](void *p, const MMCaptionEditWords *words) {
                static_cast<CaptionEditHost *>(p)->map.words = words ? *words : MMCaptionEditWords{};
            },
            map.Services(), &map.diagnosed, nullptr, nullptr
        };
        if (draft)
            services.draft_text = [](void *p, const MMCaptionEditWords *words) {
                auto &host = *static_cast<CaptionEditHost *>(p);
                return CaptionCall(host.failure, [&] {
                    if (words->count) host.draft->assign(words->text, words->text + words->count);
                    else host.draft->clear();
                    return 1;
                });
            };
        else
            services.write_text = [](void *p, const MMCaptionEditWords *words) {
                auto &host = *static_cast<CaptionEditHost *>(p);
                return CaptionCall(host.failure, [&] { WriteText(host.map.rich, *words); return 1; });
            };
        return services;
    }
};

int ApplyCaption(const MMCaptionEditPlan &plan, const MarcusCaptionList &list, TextBoxData &box, RichTextData &rich,
                 std::u16string *draft, int *bodyChanged)
{
    CaptionEditHost host{{rich}, {}, draft, {}};
    const auto services = host.Services();
    MMRichTextDefaults defaults{rich.defaults.bo, rich.defaults.oskFont, rich.defaults.dxpDef, rich.defaults.dypFont,
                                static_cast<MMU32>(rich.defaults.lwAcrBack)};
    const int status = mm_caption_edit_apply(&plan, list.firstFrame, list.lastFrame, &box.state, &defaults, &services,
                                            bodyChanged);
    if (host.failure) std::rethrow_exception(host.failure);
    if (status == MM_CAPTION_EDIT_OK)
    {
        rich.defaults.dxpDef = defaults.width;
        rich.defaults.dypFont = defaults.height;
        rich.defaults.lwAcrBack = static_cast<MMI32>(defaults.background);
    }
    return status;
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
        unsigned short units[kcchMaxStn];
        std::size_t count;
        achar native[kcchMaxStn];
        if (mm_text_read_utf8(family.name.data(), family.name.size(), units, kcchMaxStn, &count) != 1 ||
            !count || !EncodeFont(units, count, native))
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
        const PropertyMap map{rich};
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
    const auto value = CaptionInput(options, text);
    const auto before = entry ? CaptionInput(entry->options, entry->text.c_str()) : MMCaptionEditInput{};
    MMCaptionEditPlan plan;
    CheckCaptionEdit(mm_caption_edit_plan(&value, entry ? &before : nullptr, 0, &plan));
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
    std::u16string words;
    int bodyChanged;
    CheckCaptionEdit(ApplyCaption(plan, list, box, rich, &words, &bodyChanged));
    MM::PreviewCaption preview;
    Require(MM::ReadCaptionData(box, rich, plan.text_changed ? &words : nullptr, preview),
            "Cannot prepare the 2D text preview.");
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
        TextBoxData box;
        RichTextData rich;
        std::exception_ptr failure;
    } write{file.get(), editor, list, plan, {}, {}, {}};
    MMSceneArchiveTextEdit edit{list.scene, list.firstFrame, entry ? &original : nullptr, &write, nullptr};
    if (text)
        edit.write_box = [](void *context, const MMSceneArchiveText *original, MMU32 *number) -> int {
            auto &write = *static_cast<Write *>(context);
            const MMCaptionArchiveBox io = {
                &write, &write.box.state,
                [](void *p, MMU32 original, MMU32 *number) {
                    auto &w = *static_cast<Write *>(p);
                    return CaptionCall(w.failure, [&] { return w.file->FClone(kctgTbox, original, w.file, number); });
                },
                [](void *p, MMU32 number, MMTextChunk *body) {
                    auto &w = *static_cast<Write *>(p);
                    return CaptionCall(w.failure, [&] {
                        ChunkIdentification prior;
                        if (!w.box.FRead(w.file, number, &prior) || !w.rich.FRead(w.file, prior.ctg, prior.cno))
                            return 0;
                        *body = {prior.ctg, prior.cno};
                        return 1;
                    });
                },
                [](void *p, int *bodyChanged) {
                    auto &w = *static_cast<Write *>(p);
                    return CaptionCall(w.failure, [&] {
                        return ApplyCaption(w.plan, w.list, w.box, w.rich, nullptr, bodyChanged);
                    }, MM_CAPTION_EDIT_WRITE_BOX);
                },
                [](void *p, MMTextChunk *body) {
                    auto &w = *static_cast<Write *>(p);
                    return CaptionCall(w.failure, [&] {
                        ChunkIdentification written;
                        if (!w.rich.FWrite(w.file, &written))
                            return 0;
                        *body = {written.ctg, written.cno};
                        return 1;
                    });
                },
                [](void *p, MMU32 number) {
                    auto &w = *static_cast<Write *>(p);
                    return CaptionCall(w.failure, [&] { return w.box.FWrite(w.file, number); });
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

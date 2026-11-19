#include "caption_edit.h"
#include "scene_edit.h"
#include "mm/native_caption.h"
#include "mm/preview.h"
#include "mm/text_properties.h"
#include "platform_text.h"
#include "font_catalog.h"
#include "font_host.h"
#include <algorithm>
#include <codecvt>
#include <cstring>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include "soc_core.h"
#undef size
ASSERTNAME

namespace
{
using Encoding = KauaiText::Encoding;
constexpr int MaxText = 65536;
constexpr MMU32 Rgb = 0x01000000, Clear = 0xffffffffU;
constexpr unsigned Style = 1, Foreground = 3, Alignment = 128;

struct Release { template<class T> void operator()(T *p) const { if (p) p->Release(); } };
template<class T> using Owned = std::unique_ptr<T, Release>;
using Utf8 = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>;

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
int CheckedInt(std::int64_t value)
{
    Require(value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max(),
            "The 2D text position or frame range is out of range.");
    return static_cast<int>(value);
}
std::u16string Decode(const void *bytes, std::size_t length, Encoding encoding)
{
    if (!length) return {};
    std::vector<std::uint16_t> native(KauaiText::ToUtf16(bytes, length, encoding, nullptr, 0));
    Require(!native.empty() && KauaiText::ToUtf16(bytes, length, encoding, native.data(), native.size()) == native.size(),
            "Cannot decode the 2D text encoding.");
    return {native.begin(), native.end()};
}
std::u16string ReadText(RichTextData &rich)
{
    Require(rich.text.cb >= 0 && rich.text.cb <= MaxText * 4, "The 2D text is too long to edit.");
    std::vector<byte> bytes(rich.text.cb);
    Require(bytes.empty() || rich.text.FReadRgb(bytes.data(), rich.text.cb, 0), "Cannot read the 2D text.");
    return Decode(bytes.data(), bytes.size(), static_cast<Encoding>(rich.textOsk));
}
std::u16string Paragraphs(const std::u16string &text, char16_t separator)
{
    std::u16string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = text[i];
        if (c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') ++i;
        result += c == u'\r' || c == u'\n' ? separator : c;
    }
    return result;
}
std::vector<byte> LittleEndianText(const std::u16string &text)
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
void WriteText(RichTextData &rich, const std::u16string &text)
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
    return Utf8().to_bytes(Decode(rich.fontName.Psz(), rich.fontName.Cch() * sizeof(achar),
                                  static_cast<Encoding>(koskCur)));
}
LegacyLong EncodeFont(const std::u16string &units, achar (&native)[kcchMaxStn])
{
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
    const std::u16string &words;
    bool diagnosed = false;
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
            const auto next = text.find_first_of(u"\r\n", (std::max)(0, at));
            *end = next == std::u16string::npos ? static_cast<MMI32>(text.size()) + 1 : static_cast<MMI32>(next) + 1;
            if (next != std::u16string::npos && text[next] == u'\r' && next + 1 < text.size() && text[next + 1] == u'\n')
                ++*end;
            return 1;
        };
        services.diagnostic = [](void *p, int) { static_cast<PropertyMap *>(p)->diagnosed = true; };
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

struct SceneBoxes
{
    ChunkNumber startChunk = 0;
    short order = kboCur;
    Owned<GeneralGroup> starts;

    SceneBoxes(PChunkyFile file, ChunkNumber scene)
    {
        ChildChunkIdentification child;
        DataBlock block;
        Require(file->FGetKidChidCtg(kctgScen, scene, 1, kctgStartGg, &child) &&
                file->FFind(kctgStartGg, child.cki.cno, &block), "Cannot read the scene's text box list.");
        startChunk = child.cki.cno;
        starts.reset(SceneData::PggReadEvents(&block, &order, true));
        Require(bool(starts), "The scene's text box list is invalid.");
    }
    bool Slot(LegacyLong row, MMU32 &slot) const
    {
        SceneEvent event;
        starts->GetFixed(row, &event);
        if (order == kboOther) SwapBytesBom(&event, kbomSev);
        if (event.sevt != sevtAddTbox) return false;
        starts->GetRgb(row, 0, sizeof(slot), &slot);
        if (order == kboOther) SwapBytesRglw(&slot, 1);
        return true;
    }
    MMU32 Append(PChunkyFile file, ChunkNumber scene, MMI32 frame)
    {
        std::set<MMU32> used;
        ChildChunkIdentification child;
        for (LegacyLong i = 0; file->FGetKid(kctgScen, scene, i, &child); ++i)
            if (child.cki.ctg == kctgTbox) used.insert(child.chid);
        MMU32 slot = 0;
        while (used.count(slot)) ++slot;
        auto wire = slot;
        SceneEvent event{frame, sevtAddTbox};
        if (order == kboOther)
        {
            SwapBytesRglw(&wire, 1);
            SwapBytesBom(&event, kbomSev);
        }
        Require(starts->FAdd(sizeof(wire), nullptr, &wire, &event), "Cannot register the new 2D text box.");
        return slot;
    }
    void Delete(MMU32 selected)
    {
        for (auto row = starts->IvMac(); row-- > 0;)
        {
            MMU32 slot;
            if (Slot(row, slot) && slot == selected) starts->Delete(row);
        }
    }
    void Save(PChunkyFile file, ChunkNumber scene)
    {
        DataBlock block;
        ChunkNumber replacement;
        Require(file->FAdd(starts->CbOnFile(), kctgStartGg, &replacement, &block) &&
                starts->FWrite(&block, order) && file->FAdoptChild(kctgScen, scene, kctgStartGg, replacement, 1),
                "Cannot save the scene's text box list.");
        file->DeleteChild(kctgScen, scene, kctgStartGg, startChunk, 1);
    }
};

auto Fields(const MarcusCaptionOptions &v)
{
    return std::tie(v.x, v.y, v.width, v.height, v.first_frame, v.last_frame, v.through_end, v.font_size,
                    v.bold, v.italic, v.alignment, v.scrolling, v.transparent, v.ink, v.paper);
}
bool ChangedRange(const MarcusCaptionOptions &value, const MarcusCaptionOptions *before)
{
    return !before || before->first_frame != value.first_frame || before->last_frame != value.last_frame ||
           before->through_end != value.through_end;
}
void UpdateBox(TextBoxData &box, const MarcusCaptionList &list, const MarcusCaptionOptions &value,
               const MarcusCaptionOptions *before)
{
    Require(value.width > 0 && value.height > 0 && value.width <= 4096 && value.height <= 4096,
            "The 2D text width and height must be between 1 and 4096 pixels.");
    box.SetBounds(value.x, value.y, CheckedInt(std::int64_t(value.x) + value.width),
                 CheckedInt(std::int64_t(value.y) + value.height));
    box.state.story = !value.scrolling;
    if (!ChangedRange(value, before)) return;
    const auto first = std::int64_t(list.firstFrame) + value.first_frame - 1;
    const auto last = std::int64_t(list.firstFrame) + value.last_frame - 1;
    Require(value.first_frame >= 1 && first <= list.lastFrame &&
            (value.through_end || (value.last_frame >= value.first_frame && last <= list.lastFrame)),
            "Choose a visible frame range within the scene.");
    box.state.first = CheckedInt(first);
    box.state.end = value.through_end ? knfrmInvalid : CheckedInt(last + 1);
}
bool UpdateRich(RichTextData &rich, const MarcusCaptionOptions &value, const MarcusCaptionOptions *before,
                const char *text, bool wordsChanged, std::u16string *draft = nullptr)
{
    if (!before)
    {
        rich.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
        Require(rich.properties != nullptr, "Cannot allocate the 2D text properties.");
    }
    bool changed = false;
    if (!before || std::strcmp(before->font, value.font))
    {
        SetFont(rich, value.font);
        changed = true;
    }
    if (!before || value.font_size != before->font_size)
    {
        Require(value.font_size >= 4 && value.font_size <= 255, "2D font size must be between 4 and 255 pixels.");
        rich.defaults.dypFont = value.font_size;
        changed = true;
    }
    if (!before || value.transparent != before->transparent || value.paper != before->paper)
    {
        rich.defaults.lwAcrBack = mm_signed_word(value.transparent ? Clear : Rgb | (value.paper & 0xffffffU));
        changed = true;
    }
    if (!before || value.width != before->width)
    {
        rich.defaults.dxpDef = value.width;
        changed = true;
    }
    std::vector<MMTextPropertyChange> format;
    unsigned styleMask = 0;
    if (!before || value.bold != before->bold) styleMask |= 1;
    if (!before || value.italic != before->italic) styleMask |= 2;
    if (styleMask) format.push_back({Style, ((value.bold ? 1U : 0U) | (value.italic ? 2U : 0U)) & styleMask, styleMask});
    if (!before || value.ink != before->ink) format.push_back({Foreground, static_cast<MMU32>(value.ink & 0xffffffU), Clear});
    if (!before || value.alignment != before->alignment)
    {
        Require(value.alignment >= 0 && value.alignment <= 2, "Choose left, center or right alignment.");
        format.push_back({Alignment, static_cast<MMU32>(value.alignment) << 24, 0xff000000U});
    }
    if (!wordsChanged && format.empty()) return changed;
    const auto oldText = before ? ReadText(rich) : std::u16string{};
    Utf8 utf8;
    auto newText = wordsChanged ? Paragraphs(utf8.from_bytes(text), u'\r') : oldText;
    Require(!wordsChanged || (utf8.converted() == std::strlen(text) && newText.size() <= MaxText),
            "Enter valid UTF-8 within the 2D text length limit.");
    PropertyMap map{rich, newText};
    const auto services = map.Services();
    const auto characters = static_cast<MMI32>(newText.size()) + 1;
    if (wordsChanged)
    {
        const auto prefix = std::mismatch(oldText.begin(), oldText.end(), newText.begin(), newText.end());
        const auto suffix = std::mismatch(oldText.rbegin(), std::make_reverse_iterator(prefix.first),
                                          newText.rbegin(), std::make_reverse_iterator(prefix.second));
        const auto at = static_cast<MMI32>(prefix.first - oldText.begin());
        const auto inserted = static_cast<MMI32>(suffix.second.base() - prefix.second);
        const auto deleted = static_cast<MMI32>(suffix.first.base() - prefix.first);
        std::vector<MMTextPropertyChange> inherited;
        if (!at && inserted)
            for (LegacyLong i = 0; i < rich.properties->IvMac(); ++i)
            {
                const auto prior = map.Get(i);
                const auto kind = prior.key >> 24;
                if (kind >= Alignment) break;
                if ((prior.key & 0xffffffU) || !prior.value) continue;
                inherited.push_back({kind, static_cast<MMU32>(prior.value), Clear});
                mm_text_property_ref(kind, prior.value, 1, &services.arguments);
            }
        Require(mm_text_properties_adjust(at, inserted, deleted, characters, &services) > 0 && !map.diagnosed,
                "Cannot retain the 2D text formatting.");
        if (!inherited.empty())
            Require(mm_text_properties_apply_changes(0, inserted, characters, inherited.data(),
                        static_cast<MMI32>(inherited.size()), &services) > 0 && !map.diagnosed,
                    "Cannot retain the first characters' formatting.");
        if (!draft) WriteText(rich, newText);
    }
    if (!format.empty())
        Require(mm_text_properties_apply_changes(0, characters, characters, format.data(),
                                                 static_cast<MMI32>(format.size()), &services) > 0 && !map.diagnosed,
                "Cannot apply the 2D text formatting.");
    if (draft && wordsChanged) *draft = std::move(newText);
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
    const SceneBoxes boxes(file.get(), list.scene);
    std::set<MMU32> seen;
    for (LegacyLong row = 0; row < boxes.starts->IvMac(); ++row)
    {
        MMU32 slot;
        if (!boxes.Slot(row, slot) || !seen.insert(slot).second) continue;
        ChildChunkIdentification child;
        ChunkIdentification body;
        TextBoxData box;
        RichTextData rich;
        Require(file->FGetKidChidCtg(kctgScen, list.scene, slot, kctgTbox, &child) &&
                box.FRead(file.get(), child.cki.cno, &body) && rich.FRead(file.get(), body.ctg, body.cno),
                "Cannot read a native 2D text box.");
        auto words = ReadText(rich);
        auto text = Utf8().to_bytes(Paragraphs(words, u'\n'));
        Require(text.size() < MARCUS_CAPTION_UTF8_CAPACITY && text.find('\0') == std::string::npos,
                "This 2D text cannot fit in the editor.");
        const auto &s = box.state;
        MarcusCaptionEntry entry{slot, child.cki.cno, {}, std::move(text)};
        auto &v = entry.options;
        v.x = s.left;
        v.y = s.top;
        v.width = CheckedInt(std::int64_t(s.right) - s.left);
        v.height = CheckedInt(std::int64_t(s.bottom) - s.top);
        v.first_frame = CheckedInt(std::int64_t(s.first) - list.firstFrame + 1);
        v.through_end = s.end == knfrmInvalid;
        v.last_frame = CheckedInt(std::int64_t(v.through_end ? list.lastFrame : s.end - std::int64_t(1)) - list.firstFrame + 1);
        v.scrolling = !s.story;
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
            case Style: v.bold = (property.value & 1) != 0; v.italic = (property.value & 2) != 0; break;
            case Foreground: v.ink = preview.PresentationColour(Rgb + static_cast<MMU32>(property.value)) & 0xffffffU; break;
            case Alignment: v.alignment = static_cast<MMU32>(property.value) >> 24; break;
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
    SceneBoxes boxes(file.get(), list.scene);
    ChildChunkIdentification existing;
    if (entry)
        Require(file->FGetKidChidCtg(kctgScen, list.scene, entry->slot, kctgTbox, &existing) &&
                existing.cki.cno == entry->chunk, "The selected 2D text box has changed.");

    if (!text)
    {
        boxes.Delete(entry->slot);
        boxes.Save(file.get(), list.scene);
    }
    else
    {
        ChunkNumber number;
        TextBoxData box;
        RichTextData rich;
        ChunkIdentification prior;
        if (entry)
        {
            Require(file->FClone(kctgTbox, entry->chunk, file.get(), &number) &&
                    box.FRead(file.get(), number, &prior) && rich.FRead(file.get(), prior.ctg, prior.cno),
                    "Cannot copy the selected 2D text box.");
        }
        else
            Require(file->FAdd(0, kctgTbox, &number), "Cannot allocate a new 2D text box.");
        UpdateBox(box, list, *options, entry ? &entry->options : nullptr);
        if (UpdateRich(rich, *options, entry ? &entry->options : nullptr, text, wordsChanged))
        {
            ChunkIdentification body;
            Require(rich.FWrite(file.get(), &body) &&
                    file->FAdoptChild(kctgTbox, number, body.ctg, body.cno, box.state.child),
                    "Cannot save the native 2D text.");
            if (entry) file->DeleteChild(kctgTbox, number, prior.ctg, prior.cno, box.state.child);
        }
        const auto slot = entry ? entry->slot : boxes.Append(file.get(), list.scene, list.firstFrame);
        Require(box.FWrite(file.get(), number) && file->FAdoptChild(kctgScen, list.scene, kctgTbox, number, slot),
                "Cannot link the 2D text box to its scene.");
        if (!entry) boxes.Save(file.get(), list.scene);
    }
    if (entry) file->DeleteChild(kctgScen, list.scene, kctgTbox, entry->chunk, entry->slot);
    Require(file->FSave(kctgSoc), "Cannot save the 2D text working copy.");
    return copy;
}

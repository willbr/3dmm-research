/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_freetype.h"
#include "platform_text.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include FT_TRUETYPE_TABLES_H
#include FT_SFNT_NAMES_H
#include FT_WINFONTS_H
#include <algorithm>
#include <cstring>
#include <limits>
namespace KauaiFonts
{
namespace
{
struct FontLibrary
{
    FT_Library value = nullptr;
    ~FontLibrary()
    {
        if (value)
            FT_Done_FreeType(value);
    }
};
struct FontFace
{
    FT_Face value = nullptr;
    ~FontFace()
    {
        if (value)
            FT_Done_Face(value);
    }
};
std::string LegacyFamily(FT_Face face)
{
    std::string family = face->family_name ? face->family_name : "";
    // Prefer the English legacy family over typographic grouping (name ID 16),
    // which would merge families such as Arial Narrow into Arial.
    for (FT_UInt i = 0; i < FT_Get_Sfnt_Name_Count(face); ++i)
    {
        FT_SfntName name = {};
        if (FT_Get_Sfnt_Name(face, i, &name) || name.name_id != 1 || name.platform_id != 3 ||
            name.language_id != 0x409 || (name.encoding_id != 1 && name.encoding_id != 10) || name.string_len % 2)
            continue;
        std::string result;
        for (FT_UInt j = 0; j < name.string_len; j += 2)
        {
            std::uint32_t ch = (unsigned(name.string[j]) << 8) | name.string[j + 1];
            if (ch >= 0xD800 && ch <= 0xDBFF && j + 3 < name.string_len)
            {
                const auto low = (unsigned(name.string[j + 2]) << 8) | name.string[j + 3];
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    ch = 0x10000 + ((ch - 0xD800) << 10) + low - 0xDC00;
                    j += 2;
                }
            }
            if (ch >= 0xD800 && ch <= 0xDFFF)
                ch = 0xFFFD;
            if (ch < 0x80)
                result += static_cast<char>(ch);
            else
            {
                if (ch >= 0x10000)
                    result += static_cast<char>(0xF0 | (ch >> 18));
                if (ch >= 0x800)
                    result += static_cast<char>((ch >= 0x10000 ? 0x80 : 0xE0) | ((ch >> 12) & 0x3F));
                result += static_cast<char>((ch >= 0x800 ? 0x80 : 0xC0) | ((ch >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (ch & 0x3F));
            }
        }
        if (!result.empty() && result.find('\0') == std::string::npos)
            return result;
    }
    return family;
}
bool DeviceMetrics(FT_Face face, std::int32_t pixels, std::int32_t &ascent, std::int32_t &descent)
{
    // OpenType VDMX carries hinted cell extents; OS/2 scaling alone can clip
    // glyphs and change legacy layout at small pixel sizes. Use the 1:1 group.
    FT_ULong size = 0;
    if (FT_Load_Sfnt_Table(face, FT_MAKE_TAG('V', 'D', 'M', 'X'), 0, nullptr, &size) || size < 6)
        return false;
    std::vector<std::uint8_t> bytes(size);
    if (FT_Load_Sfnt_Table(face, FT_MAKE_TAG('V', 'D', 'M', 'X'), 0, bytes.data(), &size))
        return false;
    const auto word = [&](std::size_t offset) { return (unsigned(bytes[offset]) << 8) | bytes[offset + 1]; };
    const auto ratios = word(4);
    if (word(0) > 1 || 6 + std::size_t(ratios) * 6 > bytes.size())
        return false;
    for (unsigned i = 0; i < ratios; ++i)
    {
        const auto ratio = 6 + std::size_t(i) * 4;
        const unsigned x = bytes[ratio + 1], low = bytes[ratio + 2], high = bytes[ratio + 3];
        if (!((!x && !low && !high) || (low <= x && x <= high)))
            continue;
        const auto group = word(6 + std::size_t(ratios) * 4 + std::size_t(i) * 2);
        if (group > bytes.size() || bytes.size() - group < 4)
            return false;
        const auto count = word(group);
        if (std::size_t(count) * 6 > bytes.size() - group - 4)
            return false;
        for (unsigned j = 0; j < count; ++j)
        {
            const auto record = group + 4 + std::size_t(j) * 6;
            if (word(record) != static_cast<unsigned>(pixels))
                continue;
            const auto maximum = word(record + 2), minimum = word(record + 4);
            const auto top = maximum < 0x8000 ? int(maximum) : int(maximum) - 65536;
            const auto bottom = minimum < 0x8000 ? int(minimum) : int(minimum) - 65536;
            if (top < 0 || bottom > 0 || top == bottom)
                return false;
            ascent = top;
            descent = -bottom;
            return true;
        }
    }
    return false;
}
struct Glyph
{
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};
class FreeTypeFace final : public Face
{
    std::vector<std::uint8_t> bytes;
    FreeTypeOptions options;
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    Metrics cell;
    bool Decode(Text text, std::vector<std::uint32_t> &characters)
    {
        if ((!text.data && text.units) || text.units > INT32_MAX ||
            (text.encoding != Encoding::legacyBytes && text.encoding != Encoding::utf16))
            return false;
        std::vector<std::uint16_t> units;
        if (text.encoding == Encoding::legacyBytes)
        {
            // A WinFNT face can expose a native byte charmap rather than a
            // Unicode cmap. Keep its legacy byte values, including index zero.
            if (face->charmap && face->charmap->encoding == FT_ENCODING_NONE)
            {
                auto data = static_cast<const std::uint8_t *>(text.data);
                for (std::size_t i = 0; i < text.units; ++i)
                    characters.push_back(data[i]);
                return true;
            }
            const auto count = KauaiText::ToUtf16(text.data, text.units, KauaiText::Encoding::WindowsAnsi, nullptr, 0);
            if (text.units && !count)
                return false;
            units.resize(count);
            if (count &&
                !KauaiText::ToUtf16(text.data, text.units, KauaiText::Encoding::WindowsAnsi, units.data(), count))
                return false;
        }
        else
        {
            units.resize(text.units);
            if (text.units)
                std::memcpy(units.data(), text.data, text.units * 2);
        }
        if (face->charmap && face->charmap->encoding == FT_ENCODING_NONE)
        {
            std::vector<std::uint8_t> encoded(units.size() * 2);
            for (std::size_t i = 0; i < units.size(); ++i)
            {
                encoded[2 * i] = static_cast<std::uint8_t>(units[i]);
                encoded[2 * i + 1] = static_cast<std::uint8_t>(units[i] >> 8);
            }
            const auto count =
                KauaiText::ToAnsi(encoded.data(), encoded.size(), KauaiText::Encoding::Utf16LE, nullptr, 0);
            if (text.units && !count)
                return false;
            std::vector<char> mapped(count);
            if (count &&
                !KauaiText::ToAnsi(encoded.data(), encoded.size(), KauaiText::Encoding::Utf16LE, mapped.data(), count))
                return false;
            for (unsigned char value : mapped)
                characters.push_back(value);
            return true;
        }
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            std::uint32_t value = units[i];
            if (value >= 0xD800 && value <= 0xDBFF && i + 1 < units.size() && units[i + 1] >= 0xDC00 &&
                units[i + 1] <= 0xDFFF)
                value = 0x10000 + ((value - 0xD800) << 10) + (units[++i] - 0xDC00);
            else if (value >= 0xD800 && value <= 0xDFFF)
                value = 0xFFFD;
            characters.push_back(value);
        }
        return true;
    }
    bool Load(std::uint32_t character, bool render)
    {
        if (face->charmap && face->charmap->encoding == FT_ENCODING_MS_SYMBOL && character < 256 &&
            FT_Get_Char_Index(face, character + 0xF000))
            character += 0xF000;
        const auto flags = options.monochrome ? FT_LOAD_TARGET_MONO : FT_LOAD_TARGET_NORMAL;
        if (FT_Load_Char(face, character, flags))
            return false;
        if (options.bold && !(face->style_flags & FT_STYLE_FLAG_BOLD))
            FT_GlyphSlot_Embolden(face->glyph);
        if (options.italic && !(face->style_flags & FT_STYLE_FLAG_ITALIC) &&
            face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
            FT_GlyphSlot_Oblique(face->glyph);
        return !render || face->glyph->format == FT_GLYPH_FORMAT_BITMAP ||
               !FT_Render_Glyph(face->glyph, options.monochrome ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL);
    }
    bool Run(Text text, Metrics &metrics, std::vector<Glyph> *glyphs)
    {
        metrics = cell;
        std::vector<std::uint32_t> characters;
        if (!Decode(text, characters))
            return false;
        std::int64_t pen = 0;
        for (auto character : characters)
        {
            if (!Load(character, glyphs != nullptr))
                return false;
            auto slot = face->glyph;
            if (glyphs)
            {
                const auto &bitmap = slot->bitmap;
                if (bitmap.width > INT32_MAX || bitmap.rows > INT32_MAX ||
                    (bitmap.pixel_mode != FT_PIXEL_MODE_MONO && bitmap.pixel_mode != FT_PIXEL_MODE_GRAY))
                    return false;
                const auto factor = FT_IS_SCALABLE(face) ? 1 : options.bitmapScale;
                const auto gx = pen + std::int64_t(slot->bitmap_left) * factor,
                           gy = std::int64_t(cell.ascent) - std::int64_t(slot->bitmap_top) * factor;
                if (gx < INT32_MIN || gx > INT32_MAX || gy < INT32_MIN || gy > INT32_MAX)
                    return false;
                Glyph glyph;
                glyph.x = static_cast<std::int32_t>(gx);
                glyph.y = static_cast<std::int32_t>(gy);
                if (std::uint64_t(bitmap.width) * factor > INT32_MAX || std::uint64_t(bitmap.rows) * factor > INT32_MAX)
                    return false;
                glyph.width = static_cast<std::int32_t>(bitmap.width * factor);
                glyph.height = static_cast<std::int32_t>(bitmap.rows * factor);
                const auto count = std::size_t(glyph.width) * glyph.height;
                if (count > glyph.pixels.max_size())
                    return false;
                glyph.pixels.resize(count);
                for (std::int32_t y = 0; y < glyph.height; ++y)
                {
                    const auto row = bitmap.pitch >= 0 ? bitmap.buffer + std::size_t(y / factor) * bitmap.pitch
                                                       : bitmap.buffer + std::size_t(bitmap.rows - 1 - y / factor) *
                                                                             std::size_t(-std::int64_t(bitmap.pitch));
                    for (std::int32_t x = 0; x < glyph.width; ++x)
                        glyph.pixels[std::size_t(y) * glyph.width + x] =
                            bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                                ? (row[x / factor / 8] & (0x80 >> ((x / factor) & 7)) ? 255 : 0)
                                : static_cast<std::uint8_t>(bitmap.num_grays > 1 ? unsigned(row[x / factor]) * 255 /
                                                                                       (bitmap.num_grays - 1)
                                                                                 : 0);
                }
                // Bitmap faces cannot use FreeType's outline-only obliquer.
                if (options.italic && !(face->style_flags & FT_STYLE_FLAG_ITALIC) && !FT_IS_SCALABLE(face) &&
                    glyph.height)
                {
                    const auto skew = (glyph.height - 1) / 4;
                    const auto width = std::int64_t(glyph.width) + skew;
                    if (width > INT32_MAX || std::uint64_t(width) * glyph.height > glyph.pixels.max_size())
                        return false;
                    std::vector<std::uint8_t> slanted(static_cast<std::size_t>(width) * glyph.height, 0);
                    for (std::int32_t y = 0; y < glyph.height; ++y)
                        std::memcpy(slanted.data() + std::size_t(y) * width + (glyph.height - 1 - y) / 4,
                                    glyph.pixels.data() + std::size_t(y) * glyph.width, glyph.width);
                    glyph.width = static_cast<std::int32_t>(width);
                    glyph.pixels.swap(slanted);
                }
                glyphs->push_back(std::move(glyph));
            }
            pen += ((std::int64_t(slot->advance.x) + 32) / 64) * (FT_IS_SCALABLE(face) ? 1 : options.bitmapScale);
            if (pen < 0 || pen > INT32_MAX)
                return false;
        }
        metrics.advance = static_cast<std::int32_t>(pen);
        return true;
    }

  public:
    ~FreeTypeFace() override
    {
        if (face)
            FT_Done_Face(face);
        if (library)
            FT_Done_FreeType(library);
    }
    bool Init(std::vector<std::uint8_t> data, const FreeTypeOptions &request)
    {
        if (data.empty() || data.size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max()) ||
            request.pixels < 1 || request.pixels > 32767 || request.faceIndex < 0 || request.bitmapScale < 1 ||
            request.bitmapScale > 32767)
            return false;
        bytes = std::move(data);
        options = request;
        if (FT_Init_FreeType(&library) ||
            FT_New_Memory_Face(library, bytes.data(), static_cast<FT_Long>(bytes.size()), request.faceIndex, &face))
            return false;
        if (FT_IS_SCALABLE(face))
        {
            if (FT_Set_Pixel_Sizes(face, 0, request.pixels))
                return false;
        }
        else
        {
            if (face->num_fixed_sizes < 1)
                return false;
            int chosen = 0;
            FT_Pos distance = std::numeric_limits<FT_Pos>::max();
            for (int i = 0; i < face->num_fixed_sizes; ++i)
            {
                const auto delta = std::abs(face->available_sizes[i].height - request.pixels);
                if (delta < distance)
                {
                    distance = delta;
                    chosen = i;
                }
            }
            if (FT_Select_Size(face, chosen))
                return false;
        }
        if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) && !face->charmap && face->num_charmaps > 0 &&
            FT_Set_Charmap(face, face->charmaps[0]))
            return false;
        // Apple/AAT and other outline fonts can omit Windows OS/2 tables.
        // Start from FreeType metrics and refine legacy geometry when present.
        cell.ascent = static_cast<std::int32_t>((face->size->metrics.ascender + 63) / 64);
        const auto descent = static_cast<std::int32_t>((-face->size->metrics.descender + 63) / 64);
        cell.cellHeight = cell.ascent + descent;
        cell.externalLeading = std::max<std::int32_t>(
            0, static_cast<std::int32_t>((face->size->metrics.height + 63) / 64) - cell.cellHeight);
        if (!FT_IS_SCALABLE(face))
        {
            if (std::int64_t(cell.cellHeight) * options.bitmapScale > INT32_MAX ||
                std::int64_t(cell.ascent) * options.bitmapScale > INT32_MAX ||
                std::int64_t(cell.externalLeading) * options.bitmapScale > INT32_MAX)
                return false;
            cell.cellHeight *= options.bitmapScale;
            cell.ascent *= options.bitmapScale;
            cell.externalLeading *= options.bitmapScale;
        }
        if (request.useCellMetrics)
            cell = request.cellMetrics;
        else if (request.legacyCellMetrics && FT_IS_SCALABLE(face))
        {
            auto os2 = static_cast<TT_OS2 *>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));
            auto hhea = static_cast<TT_HoriHeader *>(FT_Get_Sfnt_Table(face, ft_sfnt_hhea));
            if (os2 && hhea && face->units_per_EM && os2->version != 0xFFFF && (os2->usWinAscent || os2->usWinDescent))
            {
                const auto scale = [&](std::int64_t units) {
                    return static_cast<std::int32_t>((units * request.pixels + face->units_per_EM / 2) /
                                                     face->units_per_EM);
                };
                cell.ascent = scale(os2->usWinAscent);
                auto descent = scale(os2->usWinDescent);
                DeviceMetrics(face, request.pixels, cell.ascent, descent);
                cell.cellHeight = cell.ascent + descent;
                cell.externalLeading = std::max(
                    0, scale(hhea->Ascender - hhea->Descender + hhea->Line_Gap - os2->usWinAscent - os2->usWinDescent));
            }
        }
        else if (request.legacyCellMetrics && !FT_IS_SCALABLE(face))
        {
            FT_WinFNT_HeaderRec header = {};
            if (!FT_Get_WinFNT_Header(face, &header))
            {
                cell.ascent = header.ascent * options.bitmapScale;
                cell.cellHeight = header.pixel_height * options.bitmapScale;
                cell.externalLeading = header.external_leading * options.bitmapScale;
            }
        }
        cell.advance = 0;
        Placement check;
        return Place(cell, 0, 0, Horizontal::left, Vertical::top, check);
    }
    bool Measure(Text text, Metrics &metrics) override
    {
        try
        {
            return Run(text, metrics, nullptr);
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
    }
    bool Rasterize(Text text, Mask &mask, bool opaque) override
    {
        mask = {};
        try
        {
            Metrics metrics;
            std::vector<Glyph> glyphs;
            if (!Run(text, metrics, &glyphs))
                return false;
            std::int64_t left = 0, top = 0, right = opaque ? metrics.advance : 0,
                         bottom = opaque ? metrics.cellHeight : 0;
            for (const auto &glyph : glyphs)
            {
                left = std::min<std::int64_t>(left, glyph.x);
                top = std::min<std::int64_t>(top, glyph.y);
                right = std::max<std::int64_t>(right, std::int64_t(glyph.x) + glyph.width);
                bottom = std::max<std::int64_t>(bottom, std::int64_t(glyph.y) + glyph.height);
            }
            if (options.underline && metrics.advance)
            {
                right = std::max<std::int64_t>(right, metrics.advance);
                bottom = std::max<std::int64_t>(bottom, std::int64_t(cell.ascent) + 2);
            }
            const auto width = right - left, height = bottom - top;
            if (width <= 0 || height <= 0)
                return true;
            if (width > INT32_MAX || height > INT32_MAX || std::uint64_t(width) * height > mask.coverage.max_size())
                return false;
            mask.left = static_cast<std::int32_t>(left);
            mask.top = static_cast<std::int32_t>(top);
            mask.width = mask.pitch = static_cast<std::int32_t>(width);
            mask.height = static_cast<std::int32_t>(height);
            mask.coverage.assign(static_cast<std::size_t>(width * height), 0);
            if (opaque)
            {
                mask.background.assign(mask.coverage.size(), 0);
                for (std::int32_t y = 0; y < metrics.cellHeight; ++y)
                    std::fill_n(mask.background.data() + std::size_t(y - top) * width - left, metrics.advance, 255);
            }
            for (const auto &glyph : glyphs)
                for (std::int32_t y = 0; y < glyph.height; ++y)
                    for (std::int32_t x = 0; x < glyph.width; ++x)
                    {
                        auto &pixel = mask.coverage[std::size_t(glyph.y + y - top) * width + glyph.x + x - left];
                        pixel = std::max(pixel, glyph.pixels[std::size_t(y) * glyph.width + x]);
                    }
            if (options.underline && metrics.advance)
                std::fill_n(mask.coverage.data() + std::size_t(cell.ascent + 1 - top) * width - left, metrics.advance,
                            255);
            return true;
        }
        catch (const std::bad_alloc &)
        {
            mask = {};
            return false;
        }
    }
};
} // namespace
bool InspectFreeTypeFont(const std::vector<std::uint8_t> &bytes, std::vector<FontInfo> &faces)
{
    faces.clear();
    try
    {
        if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max()))
            return false;
        FontLibrary library;
        if (FT_Init_FreeType(&library.value))
            return false;
        FontFace probe;
        if (FT_New_Memory_Face(library.value, bytes.data(), static_cast<FT_Long>(bytes.size()), -1, &probe.value))
            return false;
        const auto count = probe.value->num_faces;
        if (count < 1 || count > INT32_MAX)
            return false;
        std::vector<FontInfo> result;
        for (FT_Long index = 0; index < count; ++index)
        {
            FontFace face;
            if (FT_New_Memory_Face(library.value, bytes.data(), static_cast<FT_Long>(bytes.size()), index, &face.value))
                return false;
            FontInfo info;
            info.family = LegacyFamily(face.value);
            info.faceIndex = static_cast<std::int32_t>(index);
            info.bold = (face.value->style_flags & FT_STYLE_FLAG_BOLD) != 0;
            info.italic = (face.value->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
            info.fixedPitch = FT_IS_FIXED_WIDTH(face.value) != 0;
            if (!FT_IS_SCALABLE(face.value))
            {
                FT_WinFNT_HeaderRec header = {};
                if (!FT_Get_WinFNT_Header(face.value, &header))
                    info.bitmapPixels = header.pixel_height - header.internal_leading;
                else if (face.value->num_fixed_sizes > 0)
                    info.bitmapPixels = face.value->available_sizes[0].height;
            }
            if (!info.family.empty())
                result.push_back(std::move(info));
        }
        faces.swap(result);
        return !faces.empty();
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
std::unique_ptr<Face> CreateFreeTypeFace(std::vector<std::uint8_t> bytes, const FreeTypeOptions &options)
{
    try
    {
        std::unique_ptr<FreeTypeFace> face(new FreeTypeFace);
        if (!face->Init(std::move(bytes), options))
            return nullptr;
        return face;
    }
    catch (const std::bad_alloc &)
    {
        return nullptr;
    }
}
} // namespace KauaiFonts

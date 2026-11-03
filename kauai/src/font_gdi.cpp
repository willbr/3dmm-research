/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_gdi.h"
#include "pixel_canvas.h"
#ifdef KAUAI_FREETYPE
#include "font_freetype.h"
#endif
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <cstdlib>
namespace KauaiFonts
{
namespace
{
#ifdef KAUAI_FREETYPE
struct DcGuard
{
    HDC value;
    ~DcGuard()
    {
        if (value)
            DeleteDC(value);
    }
};
struct FileGuard
{
    HANDLE value;
    ~FileGuard()
    {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
std::vector<std::uint8_t> ReadSfnt(HDC dc)
{
    // GetFontData(0) starts at the selected face's directory in a TTC. Table
    // offsets can still refer to the collection, so rebuild a standalone SFNT
    // from the selected face's tables instead of treating that slice as a TTF.
    std::vector<std::uint8_t> data(12);
    if (GetFontData(dc, 0, 0, data.data(), 12) != 12)
        return {};
    const auto tables = (unsigned(data[4]) << 8) | data[5];
    if (!tables)
        return {};
    const auto directory = std::size_t(12) + std::size_t(tables) * 16;
    data.resize(directory);
    if (GetFontData(dc, 0, 0, data.data(), static_cast<DWORD>(directory)) != directory)
        return {};
    const auto put = [](std::uint8_t *where, std::uint32_t value) {
        where[0] = static_cast<std::uint8_t>(value >> 24);
        where[1] = static_cast<std::uint8_t>(value >> 16);
        where[2] = static_cast<std::uint8_t>(value >> 8);
        where[3] = static_cast<std::uint8_t>(value);
    };
    const auto checksum = [](const std::uint8_t *bytes, std::size_t count) {
        std::uint32_t sum = 0;
        for (std::size_t i = 0; i < count; i += 4)
        {
            std::uint32_t word = 0;
            for (unsigned j = 0; j < 4; ++j)
                word = (word << 8) | (i + j < count ? bytes[i + j] : 0);
            sum += word;
        }
        return sum;
    };
    std::size_t head = 0;
    for (unsigned i = 0; i < tables; ++i)
    {
        const auto record = std::size_t(12) + i * 16;
        const DWORD tag = DWORD(data[record]) | (DWORD(data[record + 1]) << 8) | (DWORD(data[record + 2]) << 16) |
                          (DWORD(data[record + 3]) << 24);
        const auto bytes = GetFontData(dc, tag, 0, nullptr, 0);
        const auto offset = (data.size() + 3) & ~std::size_t(3);
        if (bytes == GDI_ERROR || offset > INT32_MAX || bytes > std::size_t(INT32_MAX) - offset)
            return {};
        data.resize((offset + bytes + 3) & ~std::size_t(3), 0);
        if (bytes && GetFontData(dc, tag, 0, data.data() + offset, bytes) != bytes)
            return {};
        if (tag == 0x64616568 && bytes >= 12)
        {
            head = offset;
            put(data.data() + head + 8, 0);
        }
        put(data.data() + record + 4, checksum(data.data() + offset, bytes));
        put(data.data() + record + 8, static_cast<std::uint32_t>(offset));
        put(data.data() + record + 12, bytes);
    }
    if (head)
        put(data.data() + head + 8, 0xB1B0AFBAu - checksum(data.data(), data.size()));
    return data;
}
#endif
class GdiFace final : public Face
{
    HDC dc = nullptr;
    HFONT normal = nullptr, monochrome = nullptr;

  public:
    bool Init(HFONT source)
    {
        LOGFONTW description = {};
        if (!GetObjectW(source, sizeof(description), &description))
            return false;
        normal = CreateFontIndirectW(&description);
        description.lfQuality = NONANTIALIASED_QUALITY;
        monochrome = CreateFontIndirectW(&description);
        dc = CreateCompatibleDC(nullptr);
        return dc && normal && monochrome;
    }
    ~GdiFace() override
    {
        if (dc)
            DeleteDC(dc);
        if (normal)
            DeleteObject(normal);
        if (monochrome)
            DeleteObject(monochrome);
    }
    bool Measure(Text text, Metrics &metrics) override
    {
        if ((!text.data && text.units) || text.units > INT_MAX ||
            (text.encoding != Encoding::legacyBytes && text.encoding != Encoding::utf16))
            return false;
        SelectObject(dc, normal);
        SIZE extent = {};
        TEXTMETRICW native = {};
        const bool measured = text.encoding == Encoding::legacyBytes
                                  ? GetTextExtentPoint32A(dc, text.data ? static_cast<const char *>(text.data) : "",
                                                          static_cast<int>(text.units), &extent)
                                  : GetTextExtentPoint32W(dc, text.data ? static_cast<const wchar_t *>(text.data) : L"",
                                                          static_cast<int>(text.units), &extent);
        if (!measured || !GetTextMetricsW(dc, &native))
            return false;
        metrics = {extent.cx, native.tmHeight, native.tmAscent, native.tmExternalLeading};
        return true;
    }
    bool Rasterize(Text text, Mask &mask, bool opaqueBackground) override
    {
        mask = {};
        Metrics metrics;
        if (!Measure(text, metrics))
            return false;
        if (!text.units)
            return true;
        // Padding includes italic/combining overhang without clipping glyphs to
        // their advance. The final mask is cropped to the actual nonzero ink.
        const auto padding = std::max<std::int64_t>(metrics.cellHeight, 2);
        const auto width = std::int64_t(metrics.advance) + 2 * padding;
        const auto height = std::int64_t(metrics.cellHeight) + 2 * padding;
        if (width <= 0 || height <= 0 || width > INT32_MAX / 4 || height > INT32_MAX ||
            std::uint64_t(width * 4) * height > std::numeric_limits<std::size_t>::max())
            return false;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(width);
        info.bmiHeader.biHeight = -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *pixels = nullptr;
        auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels)
        {
            if (bitmap)
                DeleteObject(bitmap);
            return false;
        }
        auto previous = SelectObject(dc, bitmap);
        std::memset(pixels, 0, static_cast<std::size_t>(width * height * 4));
        SelectObject(dc, monochrome);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        const auto draw = [&]() {
            return text.encoding == Encoding::legacyBytes
                       ? TextOutA(dc, static_cast<int>(padding), static_cast<int>(padding),
                                  static_cast<const char *>(text.data), static_cast<int>(text.units))
                       : TextOutW(dc, static_cast<int>(padding), static_cast<int>(padding),
                                  static_cast<const wchar_t *>(text.data), static_cast<int>(text.units));
        };
        bool drawn = draw();
        GdiFlush();
        std::vector<std::uint8_t> glyphs;
        auto data = static_cast<const std::uint32_t *>(pixels);
        if (opaqueBackground && drawn)
        {
            const auto bytes = static_cast<std::size_t>(width * height);
            if (bytes > glyphs.max_size())
                drawn = false;
            else
            {
                try
                {
                    glyphs.resize(bytes);
                }
                catch (const std::bad_alloc &)
                {
                    drawn = false;
                }
                if (drawn)
                {
                    for (std::size_t i = 0; i < bytes; ++i)
                        glyphs[i] = (data[i] & 0xFFFFFF) ? 255 : 0;
                    std::memset(pixels, 0, bytes * 4);
                    SetBkMode(dc, OPAQUE);
                    SetBkColor(dc, RGB(255, 255, 255));
                    drawn = draw();
                    GdiFlush();
                }
            }
        }
        std::int32_t left = static_cast<std::int32_t>(width), right = 0, top = static_cast<std::int32_t>(height),
                     bottom = 0;
        if (drawn)
            for (std::int32_t y = 0; y < height; ++y)
                for (std::int32_t x = 0; x < width; ++x)
                    if (data[std::size_t(y) * width + x] & 0xFFFFFF)
                    {
                        left = std::min(left, x);
                        right = std::max(right, x + 1);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y + 1);
                    }
        bool ok = drawn;
        if (left < right && top < bottom)
        {
            mask.width = mask.pitch = right - left;
            mask.height = bottom - top;
            mask.left = left - static_cast<std::int32_t>(padding);
            mask.top = top - static_cast<std::int32_t>(padding);
            const auto bytes = std::size_t(mask.width) * mask.height;
            if (bytes > mask.coverage.max_size())
                ok = false;
            else
            {
                try
                {
                    mask.coverage.resize(bytes);
                    if (opaqueBackground)
                        mask.background.resize(bytes);
                }
                catch (const std::bad_alloc &)
                {
                    ok = false;
                }
                if (ok)
                    for (std::int32_t y = 0; y < mask.height; ++y)
                        for (std::int32_t x = 0; x < mask.width; ++x)
                        {
                            const auto source = std::size_t(y + top) * width + x + left;
                            const auto destination = std::size_t(y) * mask.pitch + x;
                            mask.coverage[destination] = opaqueBackground            ? glyphs[source]
                                                         : (data[source] & 0xFFFFFF) ? 255
                                                                                     : 0;
                            if (opaqueBackground)
                                mask.background[destination] = (data[source] & 0xFFFFFF) ? 255 : 0;
                        }
            }
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        if (!ok)
            mask = {};
        return ok;
    }
    bool Paint32(Text text, const KauaiPixels::Surface32 &destination, const Placement &placement,
                 std::uint32_t foreground, Background background, std::uint32_t backgroundColor,
                 Image32 &image) override
    {
        image = {};
        Metrics metrics;
        if (!Measure(text, metrics) || !destination.pixels || destination.width <= 0 || destination.height <= 0 ||
            destination.width > INT32_MAX / 4 || destination.pitch < destination.width * 4 ||
            std::uint64_t(destination.pitch) * destination.height > std::numeric_limits<std::size_t>::max())
            return false;
        if (!text.units)
            return true;
        const auto padding = std::max<std::int64_t>(metrics.cellHeight, 2);
        const auto left = std::max<std::int64_t>(0, std::int64_t(placement.cell.left) - padding);
        const auto top = std::max<std::int64_t>(0, std::int64_t(placement.cell.top) - padding);
        const auto right = std::min<std::int64_t>(destination.width, std::int64_t(placement.cell.right) + padding);
        const auto bottom = std::min<std::int64_t>(destination.height, std::int64_t(placement.cell.bottom) +
                                                                           metrics.externalLeading + padding);
        if (left >= right || top >= bottom)
            return true;
        const auto width = right - left, height = bottom - top;
        const auto bytes = static_cast<std::size_t>(width * height);
        if (bytes > image.pixels.max_size())
            return false;
        try
        {
            image.pixels.resize(bytes);
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(width);
        info.bmiHeader.biHeight = -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *pixels = nullptr;
        auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels)
        {
            if (bitmap)
                DeleteObject(bitmap);
            image = {};
            return false;
        }
        auto previous = SelectObject(dc, bitmap);
        for (std::int32_t y = 0; y < height; ++y)
            std::memcpy(static_cast<std::uint32_t *>(pixels) + std::size_t(y) * width,
                        static_cast<const std::uint8_t *>(destination.pixels) +
                            std::size_t(y + top) * destination.pitch + left * 4,
                        static_cast<std::size_t>(width * 4));
        if (background == Background::invert)
            for (std::int32_t y = 0; y < height; ++y)
                for (std::int32_t x = 0; x < width; ++x)
                    if (x + left >= placement.logical.left && x + left < placement.logical.right &&
                        y + top >= placement.logical.top && y + top < placement.logical.bottom)
                        static_cast<std::uint32_t *>(pixels)[std::size_t(y) * width + x] ^= 0xFFFFFF;
        SelectObject(dc, normal);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        SetBkMode(dc, background == Background::opaque ? OPAQUE : TRANSPARENT);
        SetTextColor(dc, RGB((foreground >> 16) & 255, (foreground >> 8) & 255, foreground & 255));
        SetBkColor(dc, RGB((backgroundColor >> 16) & 255, (backgroundColor >> 8) & 255, backgroundColor & 255));
        const auto x = static_cast<int>(placement.cell.left - left), y = static_cast<int>(placement.cell.top - top);
        const bool drawn =
            text.encoding == Encoding::legacyBytes
                ? TextOutA(dc, x, y, static_cast<const char *>(text.data), static_cast<int>(text.units))
                : TextOutW(dc, x, y, static_cast<const wchar_t *>(text.data), static_cast<int>(text.units));
        GdiFlush();
        if (drawn)
        {
            std::memcpy(image.pixels.data(), pixels, bytes * 4);
            image.left = static_cast<std::int32_t>(left);
            image.top = static_cast<std::int32_t>(top);
            image.width = static_cast<std::int32_t>(width);
            image.height = static_cast<std::int32_t>(height);
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        if (!drawn)
            image = {};
        return drawn;
    }
};
} // namespace
std::unique_ptr<Face> CreateGdiFace(void *nativeFont)
{
    std::unique_ptr<GdiFace> face(new GdiFace);
    if (!face->Init(static_cast<HFONT>(nativeFont)))
        return nullptr;
    return face;
}
std::unique_ptr<Face> CreateStudioFace(void *nativeFont)
{
    RecordNativeFontBootstrap();
#ifdef KAUAI_FREETYPE
    if (std::strcmp(StudioFontProvider(), "freetype") == 0)
    {
        LOGFONTW description = {};
        TEXTMETRICW metrics = {};
        DcGuard ownedDc = {CreateCompatibleDC(nullptr)};
        HDC dc = ownedDc.value;
        std::vector<std::uint8_t> bytes;
        bool valid = dc && GetObjectW(static_cast<HFONT>(nativeFont), sizeof(description), &description);
        if (valid)
        {
            SelectObject(dc, static_cast<HFONT>(nativeFont));
            valid = GetTextMetricsW(dc, &metrics) != 0;
            bytes = ReadSfnt(dc);
            if (bytes.empty() && _wcsicmp(description.lfFaceName, L"System") == 0)
            {
                wchar_t path[MAX_PATH];
                const auto length = GetWindowsDirectoryW(path, MAX_PATH);
                if (length && length + 32 < MAX_PATH)
                {
                    std::wcscat(path, metrics.tmHeight > 18 ? L"\\Fonts\\8514sys.fon" : L"\\Fonts\\vgasys.fon");
                    FileGuard ownedFile = {
                        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)};
                    auto file = ownedFile.value;
                    if (file != INVALID_HANDLE_VALUE)
                    {
                        LARGE_INTEGER size = {};
                        DWORD read = 0;
                        if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= INT32_MAX)
                        {
                            bytes.resize(static_cast<std::size_t>(size.QuadPart));
                            if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
                                read != bytes.size())
                                bytes.clear();
                        }
                    }
                }
            }
        }
        if (valid && !bytes.empty())
        {
            FreeTypeOptions request;
            const auto pixels = description.lfHeight < 0 ? -std::int64_t(description.lfHeight) : metrics.tmHeight;
            request.pixels = pixels > 0 && pixels <= 32767 ? static_cast<std::int32_t>(pixels) : 0;
            request.bold = description.lfWeight >= FW_BOLD;
            request.italic = description.lfItalic != 0;
            request.underline = description.lfUnderline != 0;
            request.useCellMetrics = true;
            request.cellMetrics = {0, metrics.tmHeight, metrics.tmAscent, metrics.tmExternalLeading};
            auto face = CreateFreeTypeFace(std::move(bytes), request);
            if (face)
            {
                RecordFontFace(true);
                return face;
            }
        }
        RecordFontCreationFallback();
    }
#endif
    RecordFontFace(false);
    return CreateGdiFace(nativeFont);
}
} // namespace KauaiFonts

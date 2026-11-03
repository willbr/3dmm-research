/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_canvas.h"
#include "ellipse_geometry.h"
#include "dib_frame.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
#include "kauai_core.h"
#include "region.h"
#undef size
ASSERTNAME
namespace KauaiPixels
{
namespace
{
bool Valid(const Surface32 &surface)
{
    return surface.pixels && surface.width > 0 && surface.height > 0 &&
           surface.width <= std::numeric_limits<std::int32_t>::max() / 4 && surface.pitch >= surface.width * 4 &&
           std::uint64_t(surface.pitch) * surface.height <= std::numeric_limits<std::size_t>::max();
}
bool Valid(const Surface8 &surface)
{
    return surface.pixels && surface.width > 0 && surface.height > 0 && surface.pitch >= surface.width &&
           std::uint64_t(surface.pitch) * surface.height <= std::numeric_limits<std::size_t>::max();
}
std::uint8_t *Pixel(const Surface32 &surface, std::int32_t x, std::int32_t y)
{
    return static_cast<std::uint8_t *>(surface.pixels) + std::size_t(y) * surface.pitch + std::size_t(x) * 4;
}
std::uint8_t *Pixel(const Surface8 &surface, std::int32_t x, std::int32_t y)
{
    return static_cast<std::uint8_t *>(surface.pixels) + std::size_t(y) * surface.pitch + x;
}
template <class Surface, class DrawSpan>
void Spans(const Surface &surface, const RC &rectangle, const RC *clip, Region *region, DrawSpan draw)
{
    RC bounds(0, 0, surface.width, surface.height);
    RC shape = rectangle;
    if (!bounds.FIntersect(&shape))
        return;
    if (clip)
    {
        RC clipping = *clip;
        if (!bounds.FIntersect(&clipping))
            return;
    }
    RegionScanner scanner;
    if (region)
        scanner.Init(region, &bounds);
    else
        scanner.InitRc(&bounds, &bounds);
    for (LegacyLong y = bounds.ypTop; y < bounds.ypBottom;)
    {
        const auto height = LwMin(scanner.DypCur(), bounds.ypBottom - y);
        while (scanner.XpCur() < klwMax)
        {
            const auto left = scanner.XpCur() + bounds.xpLeft;
            const auto right = scanner.XpFetch() + bounds.xpLeft;
            scanner.XpFetch();
            for (LegacyLong row = y; row < y + height; ++row)
                draw(row, left, right);
        }
        y += height;
        if (y < bounds.ypBottom)
            scanner.ScanNext(height);
    }
}
void Paint(std::uint8_t *pixel, const Ink &ink)
{
    if (ink.mode == InkMode::clear)
        return;
    std::uint32_t value = ink.rgb & 0xFFFFFF;
    if (ink.mode == InkMode::invert)
    {
        std::memcpy(&value, pixel, 4);
        value ^= 0xFFFFFF;
    }
    std::memcpy(pixel, &value, 4);
}
void Paint(std::uint8_t *pixel, const Ink8 &ink)
{
    *pixel = (*pixel & ink.andMask) ^ ink.xorMask;
}
std::uint8_t NearestIndex(const Color *palette, std::int32_t colors, std::uint32_t rgb)
{
    std::int32_t distance = INT32_MAX;
    std::uint8_t result = 0;
    for (std::int32_t i = 0; i < colors; ++i)
    {
        const auto red = std::int32_t(palette[i].bRed) - std::int32_t((rgb >> 16) & 255);
        const auto green = std::int32_t(palette[i].bGreen) - std::int32_t((rgb >> 8) & 255);
        const auto blue = std::int32_t(palette[i].bBlue) - std::int32_t(rgb & 255);
        const auto candidate = red * red + green * green + blue * blue;
        if (candidate < distance)
        {
            distance = candidate;
            result = static_cast<std::uint8_t>(i);
        }
    }
    return result;
}
std::uint32_t Rgb(const Color &color)
{
    return (std::uint32_t(color.bRed) << 16) | (std::uint32_t(color.bGreen) << 8) | color.bBlue;
}
bool ValidStorage(const PackedSurface &surface)
{
    const auto depth = surface.depth;
    if (!surface.pixels || surface.width < 1 || surface.height < 1 || surface.pitch < 1 ||
        (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32) ||
        (std::uint64_t(surface.width) * depth + 7) / 8 > static_cast<unsigned>(surface.pitch) ||
        std::uint64_t(surface.pitch) * surface.height > std::numeric_limits<std::size_t>::max())
        return false;
    return true;
}
bool Valid(const PackedSurface &surface)
{
    if (!ValidStorage(surface))
        return false;
    const auto depth = surface.depth;
    if (depth == 1)
        return (!surface.palette && surface.colors == 0) || (surface.palette && surface.colors == 2);
    return (depth != 4 && depth != 8) || (surface.palette && surface.colors > 0 && surface.colors <= (1 << depth));
}
std::uint32_t PackedRead(const PackedSurface &surface, std::int32_t x, std::int32_t y)
{
    const auto row = static_cast<const std::uint8_t *>(surface.pixels) + std::size_t(y) * surface.pitch;
    if (surface.depth == 1)
        return (row[x / 8] >> (7 - (x & 7))) & 1;
    if (surface.depth == 4)
        return (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
    const auto pixel = row + std::size_t(x) * (surface.depth / 8);
    std::uint32_t raw = pixel[0];
    if (surface.depth >= 16)
        raw |= std::uint32_t(pixel[1]) << 8;
    if (surface.depth >= 24)
        raw |= std::uint32_t(pixel[2]) << 16;
    if (surface.depth == 32)
        raw |= std::uint32_t(pixel[3]) << 24;
    return raw;
}
void PackedWrite(const PackedSurface &surface, std::int32_t x, std::int32_t y, std::uint32_t raw)
{
    const auto row = static_cast<std::uint8_t *>(surface.pixels) + std::size_t(y) * surface.pitch;
    if (surface.depth == 1)
    {
        const unsigned shift = 7 - (x & 7), mask = 1u << shift;
        row[x / 8] = static_cast<std::uint8_t>((row[x / 8] & ~mask) | ((raw & 1) << shift));
        return;
    }
    if (surface.depth == 4)
    {
        const unsigned shift = (x & 1) ? 0 : 4, mask = 15u << shift;
        row[x / 2] = static_cast<std::uint8_t>((row[x / 2] & ~mask) | ((raw & 15) << shift));
        return;
    }
    const auto pixel = row + std::size_t(x) * (surface.depth / 8);
    pixel[0] = static_cast<std::uint8_t>(raw);
    if (surface.depth >= 16)
        pixel[1] = static_cast<std::uint8_t>(raw >> 8);
    if (surface.depth >= 24)
        pixel[2] = static_cast<std::uint8_t>(raw >> 16);
    if (surface.depth == 32)
        pixel[3] = static_cast<std::uint8_t>(raw >> 24);
}
std::uint32_t Rgb555(std::uint32_t rgb)
{
    return ((rgb >> 9) & 0x7C00) | ((rgb >> 6) & 0x3E0) | ((rgb >> 3) & 31);
}
std::uint32_t Expand555(std::uint32_t raw)
{
    const auto r = (raw >> 10) & 31, g = (raw >> 5) & 31, b = raw & 31;
    return (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | (b << 3) | (b >> 2);
}
void TranslateIndices8(const Color *source, std::int32_t sourceColors, const Color *destination,
                       std::int32_t destinationColors, std::uint8_t *indices)
{
    bool identity = destinationColors == 256;
    for (int i = 0; identity && i < 256; ++i)
        identity = Rgb(i < sourceColors ? source[i] : Color{}) == Rgb(destination[i]);
    for (int i = 0; i < 256; ++i)
        indices[i] = identity
                         ? static_cast<std::uint8_t>(i)
                         : NearestIndex(destination, destinationColors, Rgb(i < sourceColors ? source[i] : Color{}));
}
template <class Surface, class Sample>
bool MappedCopy(const Surface &destination, const RC &sourceRect, const RC &destinationRect, const RC *clip,
                Region *region, Sample sample, bool nearSizeCopy = false)
{
    const auto sw = std::int64_t(sourceRect.xpRight) - sourceRect.xpLeft;
    const auto sh = std::int64_t(sourceRect.ypBottom) - sourceRect.ypTop;
    const auto dw = std::int64_t(destinationRect.xpRight) - destinationRect.xpLeft;
    const auto dh = std::int64_t(destinationRect.ypBottom) - destinationRect.ypTop;
    if (!Valid(destination) || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || sw > INT32_MAX || sh > INT32_MAX ||
        dw > INT32_MAX || dh > INT32_MAX)
        return false;
    const bool nearCopy = nearSizeCopy && std::abs(sw - dw) <= 1 && std::abs(sh - dh) <= 1;
    const auto coordinate = [&](std::int64_t position, std::int64_t size, std::int64_t target) {
        return nearCopy ? std::min(position, size - 1) : (2 * position + 1) * size / (2 * target);
    };
    Spans(destination, destinationRect, clip, region, [&](LegacyLong y, LegacyLong left, LegacyLong right) {
        // COLORONCOLOR chooses the source cell under the destination pixel's
        // center. Sampling the left edge drifts on non-integral stretches.
        const auto sy = sourceRect.ypTop + coordinate(std::int64_t(y) - destinationRect.ypTop, sh, dh);
        for (auto x = left; x < right; ++x)
        {
            const auto sx = sourceRect.xpLeft + coordinate(std::int64_t(x) - destinationRect.xpLeft, sw, dw);
            sample(Pixel(destination, x, y), sx, sy);
        }
    });
    return true;
}
struct PackedInk
{
    std::uint32_t andMask = 0, xorMask = 0;
};
struct PackedBrush
{
    PackedInk foreground, background;
    std::uint8_t pattern[8] = {255, 255, 255, 255, 255, 255, 255, 255};
};
struct PackedPixel
{
    const PackedSurface &surface;
    std::int32_t x, y;
};
PackedPixel Pixel(const PackedSurface &surface, std::int32_t x, std::int32_t y)
{
    return {surface, x, y};
}
void Paint(const PackedPixel &pixel, const PackedInk &ink)
{
    PackedWrite(pixel.surface, pixel.x, pixel.y,
                (PackedRead(pixel.surface, pixel.x, pixel.y) & ink.andMask) ^ ink.xorMask);
}
template <class Surface, class PixelBrush>
void FillRectCore(const Surface &surface, const RC &rectangle, const PixelBrush &brush, const RC *clip, Region *region,
                  std::int32_t frameX, std::int32_t frameY)
{
    if (!Valid(surface))
        return;
    const bool frame = frameX >= 0 && frameY >= 0 &&
                       std::int64_t(frameX) * 2 < std::int64_t(rectangle.xpRight) - rectangle.xpLeft &&
                       std::int64_t(frameY) * 2 < std::int64_t(rectangle.ypBottom) - rectangle.ypTop;
    Spans(surface, rectangle, clip, region, [&](LegacyLong y, LegacyLong left, LegacyLong right) {
        for (auto x = left; x < right; ++x)
        {
            if (frame && x >= rectangle.xpLeft + frameX && x < rectangle.xpRight - frameX &&
                y >= rectangle.ypTop + frameY && y < rectangle.ypBottom - frameY)
                continue;
            const auto &ink = brush.pattern[y & 7] & (0x80 >> (x & 7)) ? brush.foreground : brush.background;
            Paint(Pixel(surface, x, y), ink);
        }
    });
}
template <class PixelBrush, class Convert>
void ResolveBrushCore(const Brush &source, PixelBrush &indexed, Convert nearest, bool highlight = false)
{
    using PixelInk = decltype(indexed.foreground);
    using Word = decltype(indexed.foreground.andMask);
    const auto full = static_cast<Word>(~Word{});
    indexed = {};
    if (highlight)
    {
        indexed.foreground = PixelInk{full, full};
        return;
    }
    const auto white = nearest(0xFFFFFF), black = nearest(0);
    const auto solid = [&](const Ink &ink) {
        return ink.mode == InkMode::clear    ? PixelInk{full, 0}
               : ink.mode == InkMode::invert ? PixelInk{full, white}
                                             : PixelInk{0, nearest(ink.rgb)};
    };
    bool allFore = true, allBack = true;
    for (auto row : source.pattern)
    {
        allFore &= row == 255;
        allBack &= row == 0;
    }
    const bool same = source.foreground.mode == source.background.mode &&
                      (source.foreground.mode != InkMode::color || source.foreground.rgb == source.background.rgb);
    if (allFore || same)
    {
        indexed.foreground = solid(source.foreground);
        return;
    }
    if (allBack)
    {
        indexed.foreground = solid(source.background);
        return;
    }
    Brush brush = source;
    if (brush.background.mode == InkMode::invert ||
        (brush.background.mode == InkMode::clear && brush.foreground.mode != InkMode::invert))
    {
        std::swap(brush.foreground, brush.background);
        for (auto &row : brush.pattern)
            row ^= 255;
    }
    std::memcpy(indexed.pattern, brush.pattern, 8);
    const auto transform = [&](bool foreground) {
        const auto mapping = [&](Word before) {
            const Word mask = foreground ? white : black;
            if (brush.foreground.mode == InkMode::invert)
            {
                before ^= mask;
                if (brush.background.mode == InkMode::clear)
                    return before;
            }
            if (brush.foreground.mode == InkMode::clear || brush.foreground.mode == InkMode::invert)
                return static_cast<Word>((before | static_cast<Word>(~mask)) &
                                         (foreground ? white : nearest(brush.background.rgb)));
            return nearest(foreground ? brush.foreground.rgb : brush.background.rgb);
        };
        const auto zero = mapping(0), ones = mapping(full);
        return PixelInk{static_cast<Word>(zero ^ ones), zero};
    };
    indexed.foreground = transform(true);
    indexed.background = transform(false);
}
} // namespace
std::uint8_t ResolveMonochromeColor(std::uint32_t rgb, std::uint8_t reference, const MonochromeBrushState &state,
                                    const Color *palette)
{
    static constexpr Color standard[] = {{0, 0, 0, 0}, {255, 255, 255, 0}};
    if (!palette)
        palette = standard;
    rgb &= 0xFFFFFF;
    for (unsigned i = 0; i < 2; ++i)
        if (Rgb(palette[i]) == rgb)
            return static_cast<std::uint8_t>(i);
    const auto background = NearestIndex(palette, 2, state.backgroundRgb);
    return rgb == (state.backgroundRgb & 0xFFFFFF) && reference == state.backgroundReference ? background
                                                                                             : 1u - background;
}
namespace
{
bool MonochromeSolidBrush(const PackedSurface &surface, const Brush &brush, PackedBrush &packed,
                          const MonochromeBrushState &state)
{
    bool allFore = true, allBack = true;
    for (auto row : brush.pattern)
    {
        allFore &= row == 255;
        allBack &= row == 0;
    }
    const bool same = brush.foreground.mode == brush.background.mode &&
                      (brush.foreground.mode != InkMode::color ||
                       (state.collapseEqualColors && brush.foreground.rgb == brush.background.rgb &&
                        state.foregroundReference == state.alternateReference));
    if (!allFore && !allBack && !same)
        return false;
    const bool alternate = allBack && !same;
    const auto &ink = alternate ? brush.background : brush.foreground;
    const auto pixel = [&](std::uint32_t rgb, std::uint8_t reference) -> std::uint32_t {
        return ResolveMonochromeColor(rgb, reference, state, surface.palette);
    };
    packed = {};
    packed.foreground =
        ink.mode == InkMode::clear ? PackedInk{UINT32_MAX, 0}
        : ink.mode == InkMode::invert
            ? PackedInk{UINT32_MAX, pixel(0xFFFFFF, 0)}
            : PackedInk{0, pixel(ink.rgb, alternate ? state.alternateReference : state.foregroundReference)};
    return true;
}
bool PackedShapeBrush(const PackedSurface &surface, const Brush &brush, PackedBrush &packed,
                      const MonochromeBrushState &monochrome)
{
    if (!Valid(surface) || (surface.depth != 1 && surface.depth != 4 && surface.depth != 16 && surface.depth != 24))
        return false;
    if (surface.depth == 1)
        return MonochromeSolidBrush(surface, brush, packed, monochrome);
    ResolveBrushCore(brush, packed, [&](std::uint32_t rgb) -> std::uint32_t {
        if (surface.depth == 4)
            return NearestIndex(surface.palette, surface.colors, rgb);
        return surface.depth == 16 ? Rgb555(rgb) : rgb & 0xFFFFFF;
    });
    return true;
}
} // namespace
bool ResolveBrush8(const Brush &source, const Color *palette, std::int32_t colors, Brush8 &indexed, bool highlight)
{
    if (!palette || colors < 1 || colors > 256)
        return false;
    ResolveBrushCore(
        source, indexed, [&](std::uint32_t rgb) { return NearestIndex(palette, colors, rgb); }, highlight);
    return true;
}

void FillRect(const Surface32 &surface, const RC &rectangle, const Brush &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY)
{
    FillRectCore(surface, rectangle, brush, clip, region, frameX, frameY);
}
void FillRect(const Surface8 &surface, const RC &rectangle, const Brush8 &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY)
{
    FillRectCore(surface, rectangle, brush, clip, region, frameX, frameY);
}
bool FillRect(const PackedSurface &surface, const RC &rectangle, const Brush &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY, const MonochromeBrushState &monochrome)
{
    PackedBrush packed;
    if (!PackedShapeBrush(surface, brush, packed, monochrome))
        return false;
    FillRectCore(surface, rectangle, packed, clip, region, frameX, frameY);
    return true;
}
bool InvertRect(const PackedSurface &surface, const RC &rectangle, const RC *clip, Region *region)
{
    if (!ValidStorage(surface) || (surface.depth != 1 && surface.depth != 4 && surface.depth != 16 && surface.depth != 24))
        return false;
    PackedBrush packed;
    packed.foreground = {UINT32_MAX, UINT32_MAX};
    FillRectCore(surface, rectangle, packed, clip, region, -1, -1);
    return true;
}

namespace
{
template <class Surface, class PixelBrush>
bool FillOvalCore(const Surface &surface, const RC &rectangle, const PixelBrush &brush, const RC *clip, Region *region,
                  std::int32_t frameX, std::int32_t frameY)
{
    const auto width = std::int64_t(rectangle.xpRight) - rectangle.xpLeft;
    const auto height = std::int64_t(rectangle.ypBottom) - rectangle.ypTop;
    if (!Valid(surface) || width < 0 || height < 0 || width > (1 << 26) || height > (1 << 26))
        return false;
    RC bounds(0, 0, surface.width, surface.height), shape = rectangle;
    if (!bounds.FIntersect(&shape))
        return true;
    if (clip)
    {
        RC clipping = *clip;
        if (!bounds.FIntersect(&clipping))
            return true;
    }
    const bool frame = frameX >= 0 && frameY >= 0 && 2LL * frameX < width && 2LL * frameY < height;
    const auto first = bounds.ypTop - rectangle.ypTop;
    std::vector<EllipseSpan> outside, inside;
    if (!EllipseRows(static_cast<std::int32_t>(width - (frame ? 1 : 0)),
                     static_cast<std::int32_t>(height - (frame ? 1 : 0)), first, bounds.Dyp(), outside))
        return false;
    if (frame &&
        !EllipseRows(static_cast<std::int32_t>(width - 2LL * frameX - 1),
                     static_cast<std::int32_t>(height - 2LL * frameY - 1), first - frameY, bounds.Dyp(), inside))
        return false;
    Spans(surface, rectangle, clip, region, [&](LegacyLong y, LegacyLong left, LegacyLong right) {
        const auto row = y - bounds.ypTop;
        const auto &outer = outside[row];
        const auto outerLeft = rectangle.xpLeft + outer.left, outerRight = rectangle.xpLeft + outer.right;
        const auto innerLeft = frame ? rectangle.xpLeft + frameX + inside[row].left : 0;
        const auto innerRight = frame ? rectangle.xpLeft + frameX + inside[row].right : 0;
        for (auto x = left; x < right; ++x)
        {
            const bool inOuter = x >= outerLeft && x < outerRight;
            const bool inInner = frame && x >= innerLeft && x < innerRight;
            if (inOuter != inInner)
            {
                const auto &ink = brush.pattern[y & 7] & (0x80 >> (x & 7)) ? brush.foreground : brush.background;
                Paint(Pixel(surface, x, y), ink);
            }
        }
    });
    return true;
}
} // namespace
bool FillOval(const Surface32 &surface, const RC &rectangle, const Brush &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY)
{
    return FillOvalCore(surface, rectangle, brush, clip, region, frameX, frameY);
}
bool FillOval(const Surface8 &surface, const RC &rectangle, const Brush8 &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY)
{
    return FillOvalCore(surface, rectangle, brush, clip, region, frameX, frameY);
}
bool FillOval(const PackedSurface &surface, const RC &rectangle, const Brush &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY, const MonochromeBrushState &monochrome)
{
    PackedBrush packed;
    return PackedShapeBrush(surface, brush, packed, monochrome) &&
           FillOvalCore(surface, rectangle, packed, clip, region, frameX, frameY);
}

namespace
{
template <class Surface, class Blend>
void CoverageCore(const Surface &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y, const RC *clip,
                  Region *region, Blend blend)
{
    if (!Valid(destination) || !mask.pixels || mask.width <= 0 || mask.height <= 0 || mask.pitch < mask.width ||
        std::uint64_t(mask.pitch) * mask.height > std::numeric_limits<std::size_t>::max())
        return;
    const auto right = std::min<std::int64_t>(INT32_MAX, std::int64_t(x) + mask.width);
    const auto bottom = std::min<std::int64_t>(INT32_MAX, std::int64_t(y) + mask.height);
    RC bounds(x, y, static_cast<LegacyLong>(right), static_cast<LegacyLong>(bottom));
    Spans(destination, bounds, clip, region, [&](LegacyLong row, LegacyLong left, LegacyLong end) {
        const auto source = mask.pixels + std::size_t(std::int64_t(row) - y) * mask.pitch;
        for (auto column = left; column < end; ++column)
            blend(Pixel(destination, column, row), source[std::size_t(std::int64_t(column) - x)]);
    });
}
} // namespace
void DrawCoverage(const Surface32 &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y,
                  std::uint32_t rgb, const RC *clip, Region *region)
{
    CoverageCore(destination, mask, x, y, clip, region, [&](std::uint8_t *pixel, std::uint8_t alpha) {
        if (!alpha)
            return;
        std::uint32_t result = rgb & 0xFFFFFF;
        if (alpha != 255)
        {
            std::uint32_t previous;
            std::memcpy(&previous, pixel, 4);
            result = 0;
            for (unsigned shift = 0; shift < 24; shift += 8)
                result |= (((((previous >> shift) & 255) * (255 - alpha) + ((rgb >> shift) & 255) * alpha + 127) / 255)
                           << shift);
        }
        std::memcpy(pixel, &result, 4);
    });
}
void DrawCoverage(const Surface8 &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y,
                  std::uint8_t index, const RC *clip, Region *region)
{
    CoverageCore(destination, mask, x, y, clip, region, [=](std::uint8_t *pixel, std::uint8_t alpha) {
        if (alpha >= 128)
            *pixel = index;
    });
}
bool DrawCoverage(const PackedSurface &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y,
                  std::uint32_t rawPixel, const RC *clip, Region *region)
{
    if (!ValidStorage(destination) || !mask.pixels || mask.width <= 0 || mask.height <= 0 || mask.pitch < mask.width ||
        std::uint64_t(mask.pitch) * mask.height > std::numeric_limits<std::size_t>::max())
        return false;
    const auto right = std::min<std::int64_t>(INT32_MAX, std::int64_t(x) + mask.width);
    const auto bottom = std::min<std::int64_t>(INT32_MAX, std::int64_t(y) + mask.height);
    RC bounds(x, y, static_cast<LegacyLong>(right), static_cast<LegacyLong>(bottom));
    Spans(destination, bounds, clip, region, [&](LegacyLong row, LegacyLong left, LegacyLong end) {
        const auto source = mask.pixels + std::size_t(std::int64_t(row) - y) * mask.pitch;
        for (auto column = left; column < end; ++column)
            if (source[std::size_t(std::int64_t(column) - x)] >= 128)
                PackedWrite(destination, column, row, rawPixel);
    });
    return true;
}
namespace
{
template <class Surface, class PixelBrush>
bool FillPolygonCore(const Surface &surface, const Point *points, std::size_t count, const PixelBrush &brush,
                     const RC *clip, Region *region)
{
    if (!Valid(surface) || (!points && count))
        return false;
    if (count < 3)
        return true;
    RC bounds(points[0].x, points[0].y, points[0].x, points[0].y);
    for (std::size_t i = 1; i < count; ++i)
    {
        bounds.xpLeft = std::min<LegacyLong>(bounds.xpLeft, points[i].x);
        bounds.xpRight = std::max<LegacyLong>(bounds.xpRight, points[i].x);
        bounds.ypTop = std::min<LegacyLong>(bounds.ypTop, points[i].y);
        bounds.ypBottom = std::max<LegacyLong>(bounds.ypBottom, points[i].y);
    }
    struct Crossing
    {
        std::int64_t x;
        std::int64_t winding;
    };
    std::vector<Crossing> crossings;
    if (count > crossings.max_size())
        return false;
    try
    {
        crossings.reserve(count);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    std::int64_t lastRow = INT64_MIN;
    Spans(surface, bounds, clip, region, [&](LegacyLong y, LegacyLong left, LegacyLong right) {
        if (lastRow != y)
        {
            crossings.clear();
            for (std::size_t i = 0; i < count; ++i)
            {
                Point a = points[i], b = points[i + 1 == count ? 0 : i + 1];
                const auto winding = a.y < b.y ? 1 : -1;
                if (a.y > b.y)
                    std::swap(a, b);
                if (y < a.y || y >= b.y)
                    continue;
                const auto dy = std::uint64_t(std::int64_t(b.y) - a.y);
                const auto dx = std::int64_t(b.x) - a.x;
                // Each factor fits 32 unsigned bits, including for edges
                // crossing the full signed coordinate range. No signed
                // product overflow or floating-point edge rounding.
                const auto product = std::uint64_t(std::int64_t(y) - a.y) * std::uint64_t(dx < 0 ? -dx : dx);
                const auto quotient = static_cast<std::int64_t>(product / dy);
                const auto cut = std::int64_t(a.x) + (dx < 0 ? -quotient : quotient + (product % dy != 0));
                crossings.push_back({cut, winding});
            }
            std::sort(crossings.begin(), crossings.end(),
                      [](const Crossing &a, const Crossing &b) { return a.x < b.x; });
            lastRow = y;
        }
        std::int64_t winding = 0, start = 0;
        for (std::size_t i = 0; i < crossings.size();)
        {
            const auto cut = crossings[i].x;
            const bool inside = winding != 0;
            do
            {
                winding += crossings[i++].winding;
            } while (i < crossings.size() && crossings[i].x == cut);
            if (!inside && winding)
                start = cut;
            if (inside && !winding)
                for (auto x = std::max<std::int64_t>(left, start); x < std::min<std::int64_t>(right, cut); ++x)
                {
                    const auto &ink = brush.pattern[y & 7] & (0x80 >> (x & 7)) ? brush.foreground : brush.background;
                    Paint(Pixel(surface, static_cast<std::int32_t>(x), y), ink);
                }
        }
    });
    return true;
}
} // namespace
bool FillPolygon(const Surface32 &surface, const Point *points, std::size_t count, const Brush &brush, const RC *clip,
                 Region *region)
{
    return FillPolygonCore(surface, points, count, brush, clip, region);
}
bool FillPolygon(const Surface8 &surface, const Point *points, std::size_t count, const Brush8 &brush, const RC *clip,
                 Region *region)
{
    return FillPolygonCore(surface, points, count, brush, clip, region);
}
bool FillPolygon(const PackedSurface &surface, const Point *points, std::size_t count, const Brush &brush,
                 const RC *clip, Region *region, const MonochromeBrushState &monochrome)
{
    PackedBrush packed;
    return PackedShapeBrush(surface, brush, packed, monochrome) &&
           FillPolygonCore(surface, points, count, packed, clip, region);
}

bool Copy(const Surface32 &destination, const Surface32 &source, const RC &sourceRect, const RC &destinationRect,
          const RC *clip, Region *region)
{
    if (!Valid(destination) || !Valid(source))
        return false;
    Surface32 input = source;
    std::vector<std::uint8_t> snapshot;
    const auto sourceStart = reinterpret_cast<std::uintptr_t>(source.pixels);
    const auto destinationStart = reinterpret_cast<std::uintptr_t>(destination.pixels);
    const auto sourceBytes = std::size_t(source.pitch) * source.height;
    const auto destinationBytes = std::size_t(destination.pitch) * destination.height;
    if ((sourceStart <= destinationStart && destinationStart - sourceStart < sourceBytes) ||
        (destinationStart < sourceStart && sourceStart - destinationStart < destinationBytes))
    {
        if (sourceBytes > snapshot.max_size())
            return false;
        try
        {
            snapshot.resize(sourceBytes);
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        std::memcpy(snapshot.data(), source.pixels, sourceBytes);
        input.pixels = snapshot.data();
    }
    return MappedCopy(destination, sourceRect, destinationRect, clip, region,
                      [&](std::uint8_t *pixel, std::int64_t sx, std::int64_t sy) {
                          if (sx >= 0 && sx < source.width && sy >= 0 && sy < source.height)
                              std::memcpy(
                                  pixel, Pixel(input, static_cast<std::int32_t>(sx), static_cast<std::int32_t>(sy)), 4);
                      });
}
bool DrawDibFrame(const Surface32 &destination, const DibFrame &frame, const RC &rectangle, const RC *clip,
                  Region *region)
{
    const auto width = std::int64_t(rectangle.xpRight) - rectangle.xpLeft;
    const auto height = std::int64_t(rectangle.ypBottom) - rectangle.ypTop;
    if (!Valid(destination) || frame.width <= 0 || frame.height <= 0 || frame.width > INT32_MAX / 4 ||
        frame.pitch < frame.width * 4 || (frame.sourceDepth != 24 && frame.sourceDepth != 32) ||
        std::uint64_t(frame.pitch) * frame.height > frame.pixels.size() || width < 0 || height < 0 ||
        width > INT32_MAX || height > INT32_MAX)
        return false;
    if (!width || !height)
        return true;
    const auto src = reinterpret_cast<std::uintptr_t>(frame.pixels.data());
    const auto dst = reinterpret_cast<std::uintptr_t>(destination.pixels);
    const auto srcBytes = frame.pixels.size(), dstBytes = std::size_t(destination.pitch) * destination.height;
    if ((src <= dst && dst - src < srcBytes) || (dst < src && src - dst < dstBytes))
        return false;
    const auto sample = [](std::int64_t p, std::int64_t source, std::int64_t target) {
        return (p * source + (source > target ? target - 1 : 0)) / target;
    };
    Spans(destination, rectangle, clip, region, [&](LegacyLong y, LegacyLong left, LegacyLong right) {
        const auto row = height - 1 - (std::int64_t(y) - rectangle.ypTop);
        const auto y0 =
            frame.sourceDepth == 32
                ? frame.height - 1 - (2 * (std::int64_t(y) - rectangle.ypTop) + 1) * frame.height / (2 * height)
                : sample(row, frame.height, height);
        for (auto x = left; x < right; ++x)
        {
            const auto column = std::int64_t(x) - rectangle.xpLeft;
            const auto x0 = frame.sourceDepth == 32 ? (2 * column + 1) * frame.width / (2 * width)
                                                    : sample(column, frame.width, width);
            const auto pixel =
                frame.pixels.data() + std::size_t(frame.height - 1 - y0) * frame.pitch + std::size_t(x0) * 4;
            std::memcpy(Pixel(destination, x, y), pixel, 4);
        }
    });
    return true;
}
bool CopyPacked(const PackedSurface &destination, const PackedSurface &source, const RC &sourceRect,
                const RC &destinationRect, const RC *clip, Region *region, const PackedCopyOptions &options)
{
    if (!Valid(destination) || !Valid(source))
        return false;
    const auto sw = std::int64_t(sourceRect.xpRight) - sourceRect.xpLeft;
    const auto sh = std::int64_t(sourceRect.ypBottom) - sourceRect.ypTop;
    const auto dw = std::int64_t(destinationRect.xpRight) - destinationRect.xpLeft;
    const auto dh = std::int64_t(destinationRect.ypBottom) - destinationRect.ypTop;
    if (sw < 0 || sh < 0 || dw < 0 || dh < 0 || sw > INT32_MAX || sh > INT32_MAX || dw > INT32_MAX || dh > INT32_MAX)
        return false;
    if (!sw || !sh || !dw || !dh)
        return true;
    const bool near = options.nearSizeCopy && std::abs(sw - dw) <= 1 && std::abs(sh - dh) <= 1;
    if (near && source.depth >= 16 && destination.depth == 8)
        return false;
    // Snapshot palette metadata before any destination byte can overwrite it.
    Color sourcePalette[256] = {}, destinationPalette[256] = {};
    const int sourceColors = source.depth == 1 ? 2 : source.depth <= 8 ? source.colors : 0;
    const int destinationColors = destination.depth == 1 ? 2 : destination.depth <= 8 ? destination.colors : 0;
    if (sourceColors && source.palette)
        std::memcpy(sourcePalette, source.palette, sourceColors * sizeof(Color));
    else if (source.depth == 1)
        sourcePalette[1] = {255, 255, 255, 0};
    if (destinationColors && destination.palette)
        std::memcpy(destinationPalette, destination.palette, destinationColors * sizeof(Color));
    else if (destination.depth == 1)
        destinationPalette[1] = {255, 255, 255, 0};
    bool rawCopy = source.depth == destination.depth && (source.depth == 1 || source.depth >= 16);
    if (destinationColors && source.depth == destination.depth && destinationColors == (1 << destination.depth))
    {
        rawCopy = true;
        for (int i = 0; i < destinationColors; ++i)
            rawCopy &= Rgb(sourcePalette[i]) == Rgb(destinationPalette[i]);
    }
    const auto encode = [&](std::uint32_t raw) {
        if (rawCopy)
            return raw;
        const auto rgb = sourceColors         ? Rgb(sourcePalette[raw & 255])
                         : source.depth == 16 ? Expand555(raw)
                                              : raw & 0xFFFFFF;
        if (destinationColors)
            return std::uint32_t(NearestIndex(destinationPalette, destinationColors, rgb));
        return destination.depth == 16 ? Rgb555(rgb) : rgb;
    };
    std::uint32_t mapping[256] = {};
    if (source.depth <= 8)
        for (unsigned i = 0; i < (1u << source.depth); ++i)
            mapping[i] = encode(i);
    PackedSurface input = source;
    std::vector<std::uint8_t> snapshot;
    const auto srcStart = reinterpret_cast<std::uintptr_t>(source.pixels);
    const auto dstStart = reinterpret_cast<std::uintptr_t>(destination.pixels);
    const auto srcBytes = std::size_t(source.pitch) * source.height;
    const auto dstBytes = std::size_t(destination.pitch) * destination.height;
    if ((srcStart <= dstStart && dstStart - srcStart < srcBytes) ||
        (dstStart < srcStart && srcStart - dstStart < dstBytes))
    {
        if (srcBytes > snapshot.max_size())
            return false;
        try
        {
            snapshot.resize(srcBytes);
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        std::memcpy(snapshot.data(), source.pixels, srcBytes);
        input.pixels = snapshot.data();
    }
    const auto coordinate = [&](std::int64_t p, std::int64_t extent, std::int64_t target) {
        return near ? std::min(p, extent - 1) : (2 * p + 1) * extent / (2 * target);
    };
    Spans(destination, destinationRect, clip, region, [&](LegacyLong y, LegacyLong left, LegacyLong right) {
        const auto sy = sourceRect.ypTop + coordinate(std::int64_t(y) - destinationRect.ypTop, sh, dh);
        if (sy < 0 || sy >= input.height)
            return;
        for (auto x = left; x < right; ++x)
        {
            const auto sx = sourceRect.xpLeft + coordinate(std::int64_t(x) - destinationRect.xpLeft, sw, dw);
            if (sx < 0 || sx >= input.width)
                continue;
            const auto raw = PackedRead(input, static_cast<std::int32_t>(sx), static_cast<std::int32_t>(sy));
            PackedWrite(destination, x, y, source.depth <= 8 ? mapping[raw] : encode(raw));
        }
    });
    return true;
}
namespace
{
template <class Surface> bool ValidScroll(const Surface &surface)
{
    return Valid(surface);
}
bool ValidScroll(const PackedSurface &surface)
{
    // Scrolling moves stored samples without interpreting their colors.
    return ValidStorage(surface);
}
void ScrollPixel(const Surface32 &destination, const Surface32 &source, std::int32_t x, std::int32_t y, std::int32_t sx,
                 std::int32_t sy)
{
    std::memcpy(Pixel(destination, x, y), Pixel(source, sx, sy), 4);
}
void ScrollPixel(const Surface8 &destination, const Surface8 &source, std::int32_t x, std::int32_t y, std::int32_t sx,
                 std::int32_t sy)
{
    *Pixel(destination, x, y) = *Pixel(source, sx, sy);
}
void ScrollPixel(const PackedSurface &destination, const PackedSurface &source, std::int32_t x, std::int32_t y,
                 std::int32_t sx, std::int32_t sy)
{
    PackedWrite(destination, x, y, PackedRead(source, sx, sy));
}
struct CopyScrollPixel
{
    template <class Surface>
    void operator()(const Surface &destination, const Surface &source, std::int32_t x, std::int32_t y, std::int32_t sx,
                    std::int32_t sy) const
    {
        ScrollPixel(destination, source, x, y, sx, sy);
    }
};
template <class Surface, class Draw = CopyScrollPixel>
bool ScrollCore(const Surface &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy, const RC *clip,
                Region *region, Draw draw = {})
{
    if (!ValidScroll(surface))
        return false;
    if (rectangle.xpRight < rectangle.xpLeft || rectangle.ypBottom < rectangle.ypTop)
        return false;
    RC area = rectangle, bounds(0, 0, surface.width, surface.height);
    if ((!dx && !dy) || !area.FIntersect(&bounds) || (region && region->FEmpty()))
        return true;
    if (clip)
    {
        RC clipping = *clip;
        if (!area.FIntersect(&clipping))
            return true;
    }
    if (dx >= area.Dxp() || std::int64_t(dx) <= -std::int64_t(area.Dxp()) || dy >= area.Dyp() ||
        std::int64_t(dy) <= -std::int64_t(area.Dyp()))
        return true;
    std::vector<std::uint8_t> snapshot, coverage;
    const auto count = std::size_t(surface.pitch) * surface.height;
    const auto coverageCount = std::uint64_t(surface.width) * surface.height;
    if (count > snapshot.max_size() || (region && coverageCount > coverage.max_size()))
        return false;
    try
    {
        snapshot.resize(count);
        if (region)
            coverage.resize(static_cast<std::size_t>(coverageCount));
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    std::memcpy(snapshot.data(), surface.pixels, count);
    if (region)
        Spans(surface, rectangle, clip, region, [&](LegacyLong y, LegacyLong left, LegacyLong right) {
            std::memset(coverage.data() + std::size_t(y) * surface.width + left, 1, right - left);
        });
    auto source = surface;
    source.pixels = snapshot.data();
    Spans(surface, rectangle, clip, region, [&](LegacyLong y, LegacyLong left, LegacyLong right) {
        const auto sy = std::int64_t(y) - dy;
        if (sy < 0 || sy >= surface.height || sy < rectangle.ypTop || sy >= rectangle.ypBottom ||
            (clip && (sy < clip->ypTop || sy >= clip->ypBottom)))
            return;
        for (auto x = left; x < right; ++x)
        {
            const auto sx = std::int64_t(x) - dx;
            if (sx >= 0 && sx < surface.width && sx >= rectangle.xpLeft && sx < rectangle.xpRight &&
                (!clip || (sx >= clip->xpLeft && sx < clip->xpRight)) &&
                (!region || coverage[std::size_t(sy) * surface.width + std::size_t(sx)]))
                draw(surface, source, x, y, static_cast<std::int32_t>(sx), static_cast<std::int32_t>(sy));
        }
    });
    return true;
}
} // namespace
bool Scroll(const Surface32 &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy, const RC *clip,
            Region *region)
{
    return ScrollCore(surface, rectangle, dx, dy, clip, region);
}
bool Scroll(const Surface8 &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy, const RC *clip,
            Region *region)
{
    return ScrollCore(surface, rectangle, dx, dy, clip, region);
}
bool Scroll(const PackedSurface &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy, const RC *clip,
            Region *region, const PackedScrollOptions &options)
{
    if (!options.legacyMonochromeEdges || dx >= 0 || surface.depth != 1 || !ValidStorage(surface))
        return ScrollCore(surface, rectangle, dx, dy, clip, region);
    // The native monochrome self-blit moves complete bytes before its
    // masked edge bytes. Edge reads can therefore observe the completed body.
    // Keep this legacy Studio policy explicit; the default API uses a snapshot.
    std::vector<std::uint8_t> masks, snapshot;
    const auto count = std::size_t(surface.pitch) * surface.height;
    try
    {
        if (!ScrollCore(surface, rectangle, dx, dy, clip, region,
                        [&](const PackedSurface &, const PackedSurface &, std::int32_t x, std::int32_t y, std::int32_t,
                            std::int32_t) {
                            if (masks.empty())
                                masks.resize(count);
                            const auto perByte = 8 / surface.depth;
                            masks[std::size_t(y) * surface.pitch + x / perByte] |= static_cast<std::uint8_t>(
                                ((1u << surface.depth) - 1) << (8 - surface.depth - (x % perByte) * surface.depth));
                        }))
            return false;
        if (masks.empty())
            return true;
        snapshot.resize(count);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    catch (const std::length_error &)
    {
        return false;
    }
    struct Area
    {
        std::int32_t left, top, right, bottom;
    };
    std::vector<Area> areas;
    const auto perByte = 8 / surface.depth;
    try
    {
        for (std::int32_t top = 0; top < surface.height;)
        {
            auto bottom = top + 1;
            while (bottom < surface.height &&
                   !std::memcmp(masks.data() + std::size_t(top) * surface.pitch,
                                masks.data() + std::size_t(bottom) * surface.pitch, surface.pitch))
                ++bottom;
            const auto selected = [&](std::int32_t x) {
                return masks[std::size_t(top) * surface.pitch + x / perByte] &
                       (1u << (7 - (x % perByte) * surface.depth));
            };
            for (std::int32_t x = 0; x < surface.width;)
            {
                while (x < surface.width && !selected(x))
                    ++x;
                const auto left = x;
                while (x < surface.width && selected(x))
                    ++x;
                if (x > left)
                    areas.push_back({left, top, x, bottom});
            }
            top = bottom;
        }
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    catch (const std::length_error &)
    {
        return false;
    }
    if (dy > 0)
        std::reverse(areas.begin(), areas.end());
    auto pixels = static_cast<std::uint8_t *>(surface.pixels);
    PackedSurface input = surface;
    input.pixels = snapshot.data();
    for (const auto &area : areas)
    {
        const auto firstFull = area.left / perByte + (area.left % perByte != 0), lastFull = area.right / perByte;
        for (bool body : {true, false})
        {
            std::memcpy(snapshot.data(), pixels, count);
            for (auto y = area.top; y < area.bottom; ++y)
                for (auto x = area.left; x < area.right; ++x)
                    if ((x / perByte >= firstFull && x / perByte < lastFull) == body)
                        ScrollPixel(surface, input, x, y, static_cast<std::int32_t>(std::int64_t(x) - dx),
                                    static_cast<std::int32_t>(std::int64_t(y) - dy));
        }
    }
    return true;
}

bool CopyIndexed(const Surface32 &destination, const Indexed8 &source, const RC &sourceRect, const RC &destinationRect,
                 const RC *clip, Region *region)
{
    if (!source.pixels || !source.palette || source.colors < 1 || source.colors > 256 || source.width < 1 ||
        source.height < 1 || source.pitch < source.width ||
        std::uint64_t(source.pitch) * source.height > std::numeric_limits<std::size_t>::max())
        return false;
    return MappedCopy(destination, sourceRect, destinationRect, clip, region,
                      [&](std::uint8_t *pixel, std::int64_t sx, std::int64_t sy) {
                          if (sx < 0 || sx >= source.width || sy < 0 || sy >= source.height)
                              return;
                          const auto index = static_cast<const std::uint8_t *>(
                              source.pixels)[std::size_t(sy) * source.pitch + std::size_t(sx)];
                          if (index < source.colors)
                          {
                              const Color color = source.palette[index];
                              const std::uint32_t rgb =
                                  (std::uint32_t(color.bRed) << 16) | (std::uint32_t(color.bGreen) << 8) | color.bBlue;
                              std::memcpy(pixel, &rgb, 4);
                          }
                      });
}
bool CopyIndexed(const Surface8 &destination, const Indexed8 &source, const RC &sourceRect, const RC &destinationRect,
                 const Color *palette, std::int32_t colors, const RC *clip, Region *region)
{
    if (!Valid(destination) || !source.pixels || !source.palette || source.colors < 1 || source.colors > 256 ||
        source.width < 1 || source.height < 1 || source.pitch < source.width || !palette || colors < 1 ||
        colors > 256 || std::uint64_t(source.pitch) * source.height > std::numeric_limits<std::size_t>::max())
        return false;
    auto input = static_cast<const std::uint8_t *>(source.pixels);
    std::vector<std::uint8_t> snapshot;
    const auto sourceStart = reinterpret_cast<std::uintptr_t>(source.pixels);
    const auto destinationStart = reinterpret_cast<std::uintptr_t>(destination.pixels);
    const auto sourceBytes = std::size_t(source.pitch) * source.height;
    const auto destinationBytes = std::size_t(destination.pitch) * destination.height;
    if ((sourceStart <= destinationStart && destinationStart - sourceStart < sourceBytes) ||
        (destinationStart < sourceStart && sourceStart - destinationStart < destinationBytes))
    {
        if (sourceBytes > snapshot.max_size())
            return false;
        try
        {
            snapshot.resize(sourceBytes);
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        std::memcpy(snapshot.data(), input, sourceBytes);
        input = snapshot.data();
    }
    std::uint8_t indices[256];
    TranslateIndices8(source.palette, source.colors, palette, colors, indices);
    return MappedCopy(
        destination, sourceRect, destinationRect, clip, region,
        [&](std::uint8_t *pixel, std::int64_t sx, std::int64_t sy) {
            if (sx >= 0 && sx < source.width && sy >= 0 && sy < source.height)
                *pixel = indices[input[std::size_t(sy) * source.pitch + std::size_t(sx)]];
        },
        true);
}
namespace
{
class RleRows
{
    const RleBitmap8 &source;
    std::vector<std::size_t> offsets;
    std::int32_t decoded = -1;

  public:
    std::vector<std::uint8_t> indices, opaque;
    explicit RleRows(const RleBitmap8 &bitmap) : source(bitmap)
    {
    }
    bool Init()
    {
        if (!source.rowBytes || (!source.data && source.bytes) || source.width <= 0 || source.height <= 0 ||
            std::size_t(source.height) + 1 > offsets.max_size() || std::size_t(source.width) > indices.max_size())
            return false;
        try
        {
            offsets.resize(std::size_t(source.height) + 1);
            indices.resize(source.width);
            opaque.resize(source.width);
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        std::size_t offset = 0;
        for (std::int32_t y = 0; y < source.height; ++y)
        {
            std::int16_t length;
            std::memcpy(&length, source.rowBytes + y, sizeof(length));
            if (length < 0 || std::size_t(length) > source.bytes - offset)
                return false;
            offsets[y] = offset;
            const auto end = offset + length;
            std::int32_t x = 0;
            while (offset < end)
            {
                const auto skip = source.data[offset++];
                if (skip > source.width - x)
                    return false;
                x += skip;
                if (offset == end)
                    break;
                const auto run = source.data[offset++];
                if (run > source.width - x || (!source.solid && run > end - offset))
                    return false;
                x += run;
                if (!source.solid)
                    offset += run;
            }
        }
        offsets[source.height] = offset;
        return offset == source.bytes;
    }
    void Decode(std::int32_t y)
    {
        if (decoded == y)
            return;
        std::fill(opaque.begin(), opaque.end(), 0);
        auto cursor = offsets[y];
        const auto end = offsets[std::size_t(y) + 1];
        std::int32_t x = 0;
        while (cursor < end)
        {
            x += source.data[cursor++];
            if (cursor == end)
                break;
            const auto run = source.data[cursor++];
            for (std::int32_t count = 0; count < run; ++count, ++x)
            {
                indices[x] = source.solid ? source.fillIndex : source.data[cursor++];
                opaque[x] = 1;
            }
        }
        decoded = y;
    }
};
template <class Surface, class Write>
bool DrawRleCore(const Surface &destination, const RleBitmap8 &source, const RC &destinationRect, const RC *clip,
                 Region *region, Write write)
{
    if (!Valid(destination))
        return false;
    RleRows rows(source);
    if (!rows.Init())
        return false;
    RC bounds(0, 0, source.width, source.height);
    return MappedCopy(destination, bounds, destinationRect, clip, region,
                      [&](std::uint8_t *pixel, std::int64_t sx, std::int64_t sy) {
                          rows.Decode(static_cast<std::int32_t>(sy));
                          const bool covered = rows.opaque[std::size_t(sx)] != 0;
                          write(pixel, covered ? rows.indices[std::size_t(sx)] : 0, covered);
                      });
}
template <class Surface, class Transfer, class Write>
bool DrawRleTransfer(const Surface &destination, const RleBitmap8 &source, const RC &rectangle,
                     const Transfer &transfer, const RC *clip, Region *region, bool andShrink, Write write)
{
    const auto width = std::int64_t(rectangle.xpRight) - rectangle.xpLeft;
    const auto height = std::int64_t(rectangle.ypBottom) - rectangle.ypTop;
    if (!Valid(destination) || width <= 0 || height <= 0 || width > INT32_MAX || height > INT32_MAX)
        return false;
    RleRows rows(source);
    if (!rows.Init())
        return false;
    std::vector<std::uint8_t> colors, covered;
    try
    {
        colors.resize(source.width);
        covered.resize(source.width);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    // When both dimensions differ by at most one pixel, GDI copies samples
    // directly and repeats the last one when enlarging. Otherwise BLACKONWHITE
    // reduces through each nearest sample, starting after the previous sample;
    // the tail after the last sample is not consumed.
    const bool nearCopy = std::abs(width - source.width) <= 1 && std::abs(height - source.height) <= 1;
    const auto range = [&](std::int64_t position, std::int64_t sourceSize, std::int64_t targetSize) {
        const auto sample = [&](std::int64_t p) {
            return nearCopy ? std::min(p, sourceSize - 1) : (2 * p + 1) * sourceSize / (2 * targetSize);
        };
        const auto end = sample(position) + 1;
        const auto first =
            andShrink && !nearCopy && sourceSize > targetSize ? (position ? sample(position - 1) + 1 : 0) : end - 1;
        return std::pair<std::int64_t, std::int64_t>(first, end);
    };
    std::int32_t lastRow = -1;
    Spans(destination, rectangle, clip, region, [&](LegacyLong y, LegacyLong left, LegacyLong right) {
        if (lastRow != y)
        {
            std::fill(colors.begin(), colors.end(), 255);
            std::fill(covered.begin(), covered.end(), 1);
            auto vertical = range(std::int64_t(y) - rectangle.ypTop, source.height, height);
            for (auto sy = vertical.first; sy < vertical.second; ++sy)
            {
                rows.Decode(static_cast<std::int32_t>(sy));
                for (std::int32_t x = 0; x < source.width; ++x)
                {
                    covered[x] &= rows.opaque[x];
                    colors[x] &= rows.opaque[x] ? rows.indices[x] : transfer.backgroundIndex;
                }
            }
            lastRow = y;
        }
        for (auto x = left; x < right; ++x)
        {
            auto horizontal = range(std::int64_t(x) - rectangle.xpLeft, source.width, width);
            std::uint8_t color = 255, opaque = 1;
            for (auto sx = horizontal.first; sx < horizontal.second; ++sx)
            {
                color &= colors[std::size_t(sx)];
                opaque &= covered[std::size_t(sx)];
            }
            write(x, y, opaque ? transfer.maskWhite : transfer.maskBlack, transfer.colorIndices[color]);
        }
    });
    return true;
}
} // namespace
bool DrawRle(const Surface32 &destination, const RleBitmap8 &source, const RC &destinationRect, const Color *palette,
             std::int32_t colors, const RC *clip, Region *region)
{
    if (!palette || colors < 1 || colors > 256)
        return false;
    return DrawRleCore(destination, source, destinationRect, clip, region,
                       [&](std::uint8_t *pixel, std::uint8_t index, bool covered) {
                           if (covered && index < colors)
                           {
                               const auto color = palette[index];
                               const std::uint32_t rgb =
                                   (std::uint32_t(color.bRed) << 16) | (std::uint32_t(color.bGreen) << 8) | color.bBlue;
                               std::memcpy(pixel, &rgb, 4);
                           }
                       });
}
bool ResolveMaskedTransfer8(const Color *source, std::int32_t sourceColors, const Color *destination,
                            std::int32_t destinationColors, MaskedTransfer8 &transfer)
{
    if (!source || !destination || sourceColors < 1 || sourceColors > 256 || destinationColors < 1 ||
        destinationColors > 256)
        return false;
    transfer.maskWhite = NearestIndex(destination, destinationColors, 0xFFFFFF);
    transfer.maskBlack = NearestIndex(destination, destinationColors, 0);
    transfer.backgroundIndex = NearestIndex(source, sourceColors, 0xFFFFFF);
    TranslateIndices8(source, sourceColors, destination, destinationColors, transfer.colorIndices);
    return true;
}
bool DrawRle(const Surface8 &destination, const RleBitmap8 &source, const RC &destinationRect, const RC *clip,
             Region *region)
{
    return DrawRleCore(destination, source, destinationRect, clip, region,
                       [](std::uint8_t *pixel, std::uint8_t index, bool covered) {
                           if (covered)
                               *pixel = index;
                       });
}
bool DrawRle(const Surface8 &destination, const RleBitmap8 &source, const RC &destinationRect,
             const MaskedTransfer8 &transfer, const RC *clip, Region *region, bool andShrink)
{
    return DrawRleTransfer(destination, source, destinationRect, transfer, clip, region, andShrink,
                           [&](LegacyLong x, LegacyLong y, std::uint8_t mask, std::uint8_t color) {
                               auto pixel = Pixel(destination, x, y);
                               *pixel = (*pixel | mask) & color;
                           });
}
bool DrawRle(const PackedSurface &destination, const RleBitmap8 &source, const RC &rectangle, const Color *palette,
             std::int32_t colors, const RC *clip, Region *region, bool andShrink)
{
    if ((destination.depth != 1 && destination.depth != 4 && destination.depth != 16 && destination.depth != 24) ||
        !Valid(destination) || !palette || colors < 1 || colors > 256)
        return false;
    struct Transfer
    {
        std::uint32_t colorIndices[256] = {};
        std::uint32_t maskWhite = 0, maskBlack = 0;
        std::uint8_t backgroundIndex = 0;
    } transfer;
    static constexpr Color monochrome[] = {{0, 0, 0, 0}, {255, 255, 255, 0}};
    const auto targetPalette = destination.palette ? destination.palette : monochrome;
    const auto targetColors = destination.depth == 1 ? 2 : destination.colors;
    transfer.maskWhite = destination.depth == 1    ? 1
                         : destination.depth == 4  ? NearestIndex(destination.palette, destination.colors, 0xFFFFFF)
                         : destination.depth == 16 ? 0x7FFF
                                                   : 0xFFFFFF;
    if (destination.depth == 4)
        transfer.maskBlack = NearestIndex(destination.palette, destination.colors, 0);
    transfer.backgroundIndex = NearestIndex(palette, colors, 0xFFFFFF);
    for (int i = 0; i < 256; ++i)
    {
        const auto rgb = i < colors ? Rgb(palette[i]) : 0;
        transfer.colorIndices[i] = destination.depth <= 4    ? NearestIndex(targetPalette, targetColors, rgb)
                                   : destination.depth == 16 ? Rgb555(rgb)
                                                             : rgb;
    }
    return DrawRleTransfer(destination, source, rectangle, transfer, clip, region, andShrink,
                           [&](LegacyLong x, LegacyLong y, std::uint32_t mask, std::uint32_t color) {
                               PackedWrite(destination, x, y, (PackedRead(destination, x, y) | mask) & color);
                           });
}
} // namespace KauaiPixels

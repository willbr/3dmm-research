/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_canvas.h"
#include "dib_frame.h"
#include "mm/dib_frame.h"
#include "mm/pixel_paint.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <vector>
#include "kauai_core.h"
#include "region.h"
#include "masked_region.h"
#undef size
ASSERTNAME
namespace KauaiPixels
{
namespace
{
const unsigned char *PaletteBytes(const Color *palette)
{
    static_assert(sizeof(Color) == 4, "Raster palette layout");
    return reinterpret_cast<const unsigned char *>(palette);
}
MMPixelImage RasterImage(const PackedSurface &surface)
{
    return {{surface.pixels, surface.width, surface.height, surface.pitch, surface.depth}, PaletteBytes(surface.palette),
            surface.colors};
}
MMPixelImage RasterImage(const Indexed8 &image)
{
    return {{const_cast<void *>(image.pixels), image.width, image.height, image.pitch, 8}, PaletteBytes(image.palette),
            image.colors};
}
MMPixelSurface RasterSurface(const Surface32 &surface)
{
    return {surface.pixels, surface.width, surface.height, surface.pitch, 32};
}
MMPixelRect RasterRect(const RC &rect)
{
    return {rect.xpLeft, rect.ypTop, rect.xpRight, rect.ypBottom};
}
int StorageOverlaps(const void *source, std::size_t sourceBytes, const void *target, std::size_t targetBytes)
{
    const auto src = reinterpret_cast<std::uintptr_t>(source);
    const auto dst = reinterpret_cast<std::uintptr_t>(target);
    return (src <= dst && dst - src < sourceBytes) || (dst < src && src - dst < targetBytes);
}
bool CopyRaster(const MMPixelImage &target, const MMPixelImage &source, const RC &sourceRect, const RC &targetRect,
                const RC *clip, Region *region, int options)
{
    const auto from = RasterRect(sourceRect), to = RasterRect(targetRect);
    const auto clipping = clip ? RasterRect(*clip) : MMPixelRect{};
    MaskedRegionSpans scanner(region);
    const auto spans = scanner.Services();
    return mm_pixel_copy(&target, &source, &from, &to, clip ? &clipping : nullptr, region ? &spans : nullptr, options,
                         StorageOverlaps) != 0;
}

MMPixelSurface RasterSurface(const Surface8 &surface)
{
    return {surface.pixels, surface.width, surface.height, surface.pitch, 8};
}
struct RasterClip
{
    MMPixelRect bounds;
    MaskedRegionSpans scanner;
    MMPixelSpans spans;
    const MMPixelRect *rectangle;
    const MMPixelSpans *region;

    RasterClip(const RC *clip, Region *r)
        : bounds(clip ? RasterRect(*clip) : MMPixelRect{}), scanner(r), spans(scanner.Services()),
          rectangle(clip ? &bounds : nullptr), region(r ? &spans : nullptr)
    {
    }
};
MMPixelColorBrush RasterColors(const Brush &brush)
{
    static_assert(int(InkMode::color) == MM_PIXEL_COLOR && int(InkMode::clear) == MM_PIXEL_CLEAR &&
                  int(InkMode::invert) == MM_PIXEL_INVERT, "Raster ink modes");
    MMPixelColorBrush result = {{brush.foreground.rgb, int(brush.foreground.mode)},
                                {brush.background.rgb, int(brush.background.mode)}, {}};
    std::memcpy(result.pattern, brush.pattern, sizeof(result.pattern));
    return result;
}
MMPixelMono RasterMono(const MonochromeBrushState &state)
{
    return {state.backgroundRgb, state.backgroundReference, state.foregroundReference, state.alternateReference,
            state.collapseEqualColors};
}
MMPixelBrush RasterBrush(const Brush &brush)
{
    const auto colors = RasterColors(brush);
    MMPixelBrush result;
    mm_pixel_resolve_rgb(&colors, &result);
    return result;
}
MMPixelBrush RasterBrush(const Brush8 &brush)
{
    MMPixelBrush result = {{brush.foreground.andMask, brush.foreground.xorMask},
                           {brush.background.andMask, brush.background.xorMask}, {}};
    std::memcpy(result.pattern, brush.pattern, sizeof(result.pattern));
    return result;
}
bool ResolvePacked(const MMPixelImage &image, const Brush &brush, const MonochromeBrushState &state,
                   MMPixelBrush &result)
{
    const auto colors = RasterColors(brush);
    const auto mono = RasterMono(state);
    return mm_pixel_resolve_packed(&image, &colors, &mono, &result) != 0;
}
void FillRectRaster(const MMPixelSurface &surface, const MMPixelBrush &brush, const RC &rectangle, const RC *clip,
                    Region *region, MMI32 frameX, MMI32 frameY)
{
    const auto shape = RasterRect(rectangle);
    RasterClip clipped(clip, region);
    mm_pixel_fill_rect(&surface, &brush, &shape, clipped.rectangle, clipped.region, frameX, frameY);
}
bool FillOvalRaster(const MMPixelSurface &surface, const MMPixelBrush &brush, const RC &rectangle, const RC *clip,
                    Region *region, MMI32 frameX, MMI32 frameY)
{
    const auto shape = RasterRect(rectangle);
    RasterClip clipped(clip, region);
    return mm_pixel_fill_oval(&surface, &brush, &shape, clipped.rectangle, clipped.region, frameX, frameY) != 0;
}
bool DrawCoverageRaster(const MMPixelSurface &target, const Coverage8 &mask, MMI32 x, MMI32 y, MMU32 sample,
                        const RC *clip, Region *region, bool blend)
{
    const MMPixelCoverage coverage = {mask.pixels, mask.width, mask.height, mask.pitch};
    RasterClip clipped(clip, region);
    return mm_pixel_draw_coverage(&target, &coverage, x, y, sample, blend, clipped.rectangle, clipped.region) != 0;
}
bool FillPolygonRaster(const MMPixelSurface &surface, const MMPixelBrush &brush, const Point *points, std::size_t count,
                       const RC *clip, Region *region)
{
    MMPixelPoint local[32];
    auto input = local;
    std::vector<MMPixelPoint> extended;
    if (!points && count)
        return false;
    if (count > sizeof(local) / sizeof(local[0]))
    {
        if (count > extended.max_size())
            return false;
        try
        {
            extended.resize(count);
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        input = extended.data();
    }
    for (std::size_t i = 0; i < count; ++i)
        input[i] = {points[i].x, points[i].y};
    RasterClip clipped(clip, region);
    return mm_pixel_fill_polygon(&surface, &brush, input, count, clipped.rectangle, clipped.region) != 0;
}
} // namespace

std::uint8_t ResolveMonochromeColor(std::uint32_t rgb, std::uint8_t reference, const MonochromeBrushState &state,
                                    const Color *palette)
{
    const auto mono = RasterMono(state);
    return mm_pixel_monochrome_colour(rgb, reference, &mono, PaletteBytes(palette));
}
bool ResolveBrush8(const Brush &source, const Color *palette, std::int32_t colors, Brush8 &indexed, bool highlight)
{
    const auto input = RasterColors(source);
    MMPixelBrush brush;
    if (!mm_pixel_resolve_indexed(&input, PaletteBytes(palette), colors, highlight, &brush))
        return false;
    indexed.foreground = {byte(brush.foreground.and_mask), byte(brush.foreground.xor_mask)};
    indexed.background = {byte(brush.background.and_mask), byte(brush.background.xor_mask)};
    std::memcpy(indexed.pattern, brush.pattern, sizeof(indexed.pattern));
    return true;
}

void FillRect(const Surface32 &surface, const RC &rectangle, const Brush &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY)
{
    FillRectRaster(RasterSurface(surface), RasterBrush(brush), rectangle, clip, region, frameX, frameY);
}
void FillRect(const Surface8 &surface, const RC &rectangle, const Brush8 &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY)
{
    FillRectRaster(RasterSurface(surface), RasterBrush(brush), rectangle, clip, region, frameX, frameY);
}
bool FillRect(const PackedSurface &surface, const RC &rectangle, const Brush &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY, const MonochromeBrushState &monochrome)
{
    const auto image = RasterImage(surface);
    MMPixelBrush result;
    if (!ResolvePacked(image, brush, monochrome, result))
        return false;
    FillRectRaster(image.surface, result, rectangle, clip, region, frameX, frameY);
    return true;
}
bool InvertRect(const PackedSurface &surface, const RC &rectangle, const RC *clip, Region *region)
{
    const auto target = RasterImage(surface);
    const auto shape = RasterRect(rectangle);
    RasterClip clipped(clip, region);
    return mm_pixel_invert_packed_rect(&target, &shape, clipped.rectangle, clipped.region) != 0;
}

bool FillOval(const Surface32 &surface, const RC &rectangle, const Brush &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY)
{
    return FillOvalRaster(RasterSurface(surface), RasterBrush(brush), rectangle, clip, region, frameX, frameY);
}
bool FillOval(const Surface8 &surface, const RC &rectangle, const Brush8 &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY)
{
    return FillOvalRaster(RasterSurface(surface), RasterBrush(brush), rectangle, clip, region, frameX, frameY);
}
bool FillOval(const PackedSurface &surface, const RC &rectangle, const Brush &brush, const RC *clip, Region *region,
              std::int32_t frameX, std::int32_t frameY, const MonochromeBrushState &monochrome)
{
    const auto image = RasterImage(surface);
    MMPixelBrush result;
    return ResolvePacked(image, brush, monochrome, result) &&
           FillOvalRaster(image.surface, result, rectangle, clip, region, frameX, frameY);
}

void DrawCoverage(const Surface32 &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y,
                  std::uint32_t rgb, const RC *clip, Region *region)
{
    DrawCoverageRaster(RasterSurface(destination), mask, x, y, rgb, clip, region, true);
}
void DrawCoverage(const Surface8 &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y,
                  std::uint8_t index, const RC *clip, Region *region)
{
    DrawCoverageRaster(RasterSurface(destination), mask, x, y, index, clip, region, false);
}
bool DrawCoverage(const PackedSurface &destination, const Coverage8 &mask, std::int32_t x, std::int32_t y,
                  std::uint32_t rawPixel, const RC *clip, Region *region)
{
    return DrawCoverageRaster(RasterImage(destination).surface, mask, x, y, rawPixel, clip, region, false);
}

bool FillPolygon(const Surface32 &surface, const Point *points, std::size_t count, const Brush &brush, const RC *clip,
                 Region *region)
{
    return FillPolygonRaster(RasterSurface(surface), RasterBrush(brush), points, count, clip, region);
}
bool FillPolygon(const Surface8 &surface, const Point *points, std::size_t count, const Brush8 &brush, const RC *clip,
                 Region *region)
{
    return FillPolygonRaster(RasterSurface(surface), RasterBrush(brush), points, count, clip, region);
}
bool FillPolygon(const PackedSurface &surface, const Point *points, std::size_t count, const Brush &brush,
                 const RC *clip, Region *region, const MonochromeBrushState &monochrome)
{
    const auto image = RasterImage(surface);
    MMPixelBrush result;
    return ResolvePacked(image, brush, monochrome, result) &&
           FillPolygonRaster(image.surface, result, points, count, clip, region);
}

bool Copy(const Surface32 &destination, const Surface32 &source, const RC &sourceRect, const RC &destinationRect,
          const RC *clip, Region *region)
{
    return CopyRaster({RasterSurface(destination), nullptr, 0}, {RasterSurface(source), nullptr, 0}, sourceRect,
                      destinationRect, clip, region, 0);
}
bool DrawDibFrame(const Surface32 &destination, const DibFrame &frame, const RC &rectangle, const RC *clip,
                  Region *region)
{
    const auto to = RasterSurface(destination);
    const MMPixelSurface image = {const_cast<byte *>(frame.pixels.data()), frame.width, frame.height, frame.pitch, 32};
    const auto target = RasterRect(rectangle), clipping = clip ? RasterRect(*clip) : MMPixelRect{};
    MaskedRegionSpans scanner(region);
    const auto spans = scanner.Services();
    return mm_dib_draw(&to, &image, frame.pixels.size(), frame.sourceDepth, &target, clip ? &clipping : nullptr,
                       region ? &spans : nullptr, StorageOverlaps) != 0;
}
bool SnapshotIndexed(const Surface8 &destination, const Surface32 &source, const RC &sourceRect,
                     const RC &destinationRect, const Color *palette, std::int32_t colors)
{
    const auto from = RasterSurface(source);
    const MMPixelImage to = {{destination.pixels, destination.width, destination.height, destination.pitch, 8},
                             PaletteBytes(palette), colors};
    const auto src = RasterRect(sourceRect), dst = RasterRect(destinationRect);
    return mm_pixel_snapshot_indexed(&to, &from, &src, &dst) != 0;
}
bool CopyPacked(const PackedSurface &destination, const PackedSurface &source, const RC &sourceRect,
                const RC &destinationRect, const RC *clip, Region *region, const PackedCopyOptions &options)
{
    const int flags = MM_PIXEL_COPY_ALLOW_EMPTY | (options.nearSizeCopy ? MM_PIXEL_COPY_NEAR_SIZE : 0);
    return CopyRaster(RasterImage(destination), RasterImage(source), sourceRect, destinationRect, clip, region, flags);
}
namespace
{
bool ScrollRaster(const MMPixelSurface &surface, const RC &rectangle, MMI32 dx, MMI32 dy, const RC *clip,
                  Region *region, bool legacyMonochrome)
{
    const auto area = RasterRect(rectangle);
    const auto clipping = clip ? RasterRect(*clip) : MMPixelRect{};
    MaskedRegionSpans scanner(region);
    const auto spans = scanner.Services();
    return mm_pixel_scroll(&surface, &area, dx, dy, clip ? &clipping : nullptr, region ? &spans : nullptr,
                           legacyMonochrome) != 0;
}
} // namespace
bool Scroll(const Surface32 &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy, const RC *clip,
            Region *region)
{
    return ScrollRaster(RasterSurface(surface), rectangle, dx, dy, clip, region, false);
}
bool Scroll(const Surface8 &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy, const RC *clip,
            Region *region)
{
    return ScrollRaster({surface.pixels, surface.width, surface.height, surface.pitch, 8}, rectangle, dx, dy, clip,
                        region, false);
}
bool Scroll(const PackedSurface &surface, const RC &rectangle, std::int32_t dx, std::int32_t dy, const RC *clip,
            Region *region, const PackedScrollOptions &options)
{
    return ScrollRaster({surface.pixels, surface.width, surface.height, surface.pitch, surface.depth}, rectangle,
                        dx, dy, clip, region, options.legacyMonochromeEdges);
}

bool CopyIndexed(const Surface32 &destination, const Indexed8 &source, const RC &sourceRect, const RC &destinationRect,
                 const RC *clip, Region *region)
{
    const auto from = RasterImage(source);
    const auto to = RasterSurface(destination);
    const auto src = RasterRect(sourceRect), dst = RasterRect(destinationRect);
    const auto clipping = clip ? RasterRect(*clip) : MMPixelRect{};
    MaskedRegionSpans scanner(region);
    const auto spans = scanner.Services();
    return mm_pixel_expand_indexed(&to, &from, &src, &dst, clip ? &clipping : nullptr, region ? &spans : nullptr) != 0;
}
bool CopyIndexed(const Surface8 &destination, const Indexed8 &source, const RC &sourceRect, const RC &destinationRect,
                 const Color *palette, std::int32_t colors, const RC *clip, Region *region)
{
    const MMPixelImage to = {{destination.pixels, destination.width, destination.height, destination.pitch, 8},
                             PaletteBytes(palette), colors};
    return CopyRaster(to, RasterImage(source), sourceRect, destinationRect, clip, region, MM_PIXEL_COPY_NEAR_SIZE);
}
namespace
{
bool StretchMasked(const MMPixelSurface &destination, const RleBitmap8 &source, const RC &rectangle, const RC *clip,
                   Region *region, const Color *palette, std::int32_t count, const MMMaskedTransfer *transfer,
                   bool andShrink)
{
    const MMMaskedRle image = {source.data, source.bytes, source.rowBytes, source.width, source.height,
                               source.solid, source.fillIndex};
    const MMMaskedRect target = {rectangle.xpLeft, rectangle.ypTop, rectangle.xpRight, rectangle.ypBottom};
    MMMaskedRect clipped;
    if (clip)
        clipped = {clip->xpLeft, clip->ypTop, clip->xpRight, clip->ypBottom};
    const auto clipBounds = clip ? &clipped : nullptr;
    if (!region)
        return mm_masked_stretch(&destination, &image, &target, clipBounds, nullptr, PaletteBytes(palette),
                                 count, transfer, andShrink) != 0;
    MaskedRegionSpans scanner(region);
    const auto spans = scanner.Services();
    return mm_masked_stretch(&destination, &image, &target, clipBounds, &spans, PaletteBytes(palette),
                             count, transfer, andShrink) != 0;
}
} // namespace

bool DrawRle(const Surface32 &destination, const RleBitmap8 &source, const RC &destinationRect, const Color *palette,
             std::int32_t colors, const RC *clip, Region *region)
{
    const MMPixelSurface surface = {destination.pixels, destination.width, destination.height, destination.pitch, 32};
    return StretchMasked(surface, source, destinationRect, clip, region, palette, colors, nullptr, false);
}
bool ResolveMaskedTransfer8(const Color *source, std::int32_t sourceColors, const Color *destination,
                            std::int32_t destinationColors, MaskedTransfer8 &transfer)
{
    MMMaskedTransfer resolved;
    if (!mm_masked_transfer_resolve(8, PaletteBytes(source), sourceColors, PaletteBytes(destination),
                                    destinationColors, &resolved))
        return false;
    std::copy(resolved.colours, resolved.colours + 256, transfer.colorIndices);
    transfer.maskWhite = static_cast<std::uint8_t>(resolved.mask_white);
    transfer.maskBlack = static_cast<std::uint8_t>(resolved.mask_black);
    transfer.backgroundIndex = resolved.background;
    return true;
}
bool DrawRle(const Surface8 &destination, const RleBitmap8 &source, const RC &destinationRect, const RC *clip,
             Region *region)
{
    const MMPixelSurface surface = {destination.pixels, destination.width, destination.height, destination.pitch, 8};
    return StretchMasked(surface, source, destinationRect, clip, region, nullptr, 0, nullptr, false);
}
bool DrawRle(const Surface8 &destination, const RleBitmap8 &source, const RC &destinationRect,
             const MaskedTransfer8 &transfer, const RC *clip, Region *region, bool andShrink)
{
    const MMPixelSurface surface = {destination.pixels, destination.width, destination.height, destination.pitch, 8};
    MMMaskedTransfer resolved;
    std::copy(transfer.colorIndices, transfer.colorIndices + 256, resolved.colours);
    resolved.mask_white = transfer.maskWhite;
    resolved.mask_black = transfer.maskBlack;
    resolved.background = transfer.backgroundIndex;
    return StretchMasked(surface, source, destinationRect, clip, region, nullptr, 0, &resolved, andShrink);
}
bool DrawRle(const PackedSurface &destination, const RleBitmap8 &source, const RC &rectangle, const Color *palette,
             std::int32_t colors, const RC *clip, Region *region, bool andShrink)
{
    const auto depth = destination.depth;
    const MMPixelSurface surface = {destination.pixels, destination.width, destination.height, destination.pitch, depth};
    MMMaskedTransfer transfer;
    return depth != 8 && mm_masked_transfer_resolve(depth, PaletteBytes(palette), colors, PaletteBytes(destination.palette),
                                                    destination.colors, &transfer) &&
           StretchMasked(surface, source, rectangle, clip, region, nullptr, 0, &transfer, andShrink);
}
} // namespace KauaiPixels

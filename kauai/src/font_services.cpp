/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_services.h"
#include "pixel_canvas.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace KauaiFonts
{
bool Face::Paint32(Text text, const KauaiPixels::Surface32 &destination, const Placement &placement,
                   std::uint32_t foreground, Background background, std::uint32_t backgroundColor, Image32 &image)
{
    image = {};
    if (!destination.pixels || destination.width <= 0 || destination.height <= 0 || destination.width > INT32_MAX / 4 ||
        destination.pitch < destination.width * 4 ||
        std::uint64_t(destination.pitch) * destination.height > std::numeric_limits<std::size_t>::max() ||
        (background != Background::transparent && background != Background::opaque && background != Background::invert))
        return false;
    Mask mask;
    if (!Rasterize(text, mask, background == Background::opaque) || mask.width < 0 || mask.height < 0 ||
        mask.pitch < mask.width || std::uint64_t(mask.pitch) * mask.height > mask.coverage.size() ||
        (!mask.background.empty() && std::uint64_t(mask.pitch) * mask.height > mask.background.size()))
        return false;
    const auto originX = std::int64_t(placement.cell.left) + mask.left,
               originY = std::int64_t(placement.cell.top) + mask.top;
    std::int64_t left = originX, top = originY, right = left + mask.width, bottom = top + mask.height;
    if (background == Background::invert || (background == Background::opaque && mask.background.empty()))
    {
        const auto &bounds = background == Background::invert ? placement.logical : placement.cell;
        left = std::min<std::int64_t>(left, bounds.left);
        right = std::max<std::int64_t>(right, bounds.right);
        top = std::min<std::int64_t>(top, bounds.top);
        bottom = std::max<std::int64_t>(bottom, bounds.bottom);
    }
    left = std::max<std::int64_t>(left, 0);
    top = std::max<std::int64_t>(top, 0);
    right = std::min<std::int64_t>(right, destination.width);
    bottom = std::min<std::int64_t>(bottom, destination.height);
    if (left >= right || top >= bottom)
        return true;
    const auto width = right - left, height = bottom - top;
    const auto count = static_cast<std::size_t>(width * height);
    if (count > image.pixels.max_size())
        return false;
    try
    {
        image.pixels.resize(count);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    const auto blend = [](std::uint32_t before, std::uint32_t color, std::uint8_t alpha) {
        if (!alpha)
            return before;
        std::uint32_t result = 0;
        for (unsigned shift = 0; shift < 24; shift += 8)
            result |=
                (((((before >> shift) & 255) * (255 - alpha) + ((color >> shift) & 255) * alpha + 127) / 255) << shift);
        return result;
    };
    const auto inside = [](const Bounds &bounds, std::int64_t x, std::int64_t y) {
        return x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom;
    };
    for (std::int32_t y = 0; y < height; ++y)
        for (std::int32_t x = 0; x < width; ++x)
        {
            const auto px = x + left, py = y + top;
            std::uint32_t pixel;
            std::memcpy(&pixel,
                        static_cast<const std::uint8_t *>(destination.pixels) + std::size_t(py) * destination.pitch +
                            px * 4,
                        4);
            const auto mx = px - originX, my = py - originY;
            const bool covered = mx >= 0 && mx < mask.width && my >= 0 && my < mask.height;
            const auto offset = covered ? static_cast<std::size_t>(my) * mask.pitch + static_cast<std::size_t>(mx) : 0;
            if (background == Background::invert && inside(placement.logical, px, py))
                pixel ^= 0xFFFFFF;
            if (background == Background::opaque)
            {
                const auto alpha = mask.background.empty() ? (inside(placement.cell, px, py) ? 255 : 0)
                                   : covered               ? mask.background[offset]
                                                           : 0;
                pixel = blend(pixel, backgroundColor, static_cast<std::uint8_t>(alpha));
            }
            if (covered)
                pixel = blend(pixel, foreground, mask.coverage[offset]);
            image.pixels[static_cast<std::size_t>(y) * width + x] = pixel;
        }
    image.left = static_cast<std::int32_t>(left);
    image.top = static_cast<std::int32_t>(top);
    image.width = static_cast<std::int32_t>(width);
    image.height = static_cast<std::int32_t>(height);
    return true;
}
bool Place(const Metrics &metrics, std::int32_t x, std::int32_t y, Horizontal horizontal, Vertical vertical,
           Placement &placement)
{
    if (metrics.advance < 0 || metrics.cellHeight < 0 || metrics.ascent < 0 || metrics.ascent > metrics.cellHeight ||
        metrics.externalLeading < 0)
        return false;
    const auto height = std::int64_t(metrics.cellHeight) + metrics.externalLeading;
    if (height > INT32_MAX)
        return false;
    std::int64_t dx = 0, dy = 0, cellDy = 0;
    switch (horizontal)
    {
    case Horizontal::left:
        break;
    case Horizontal::center:
        dx = -metrics.advance / 2;
        break;
    case Horizontal::right:
        dx = -std::int64_t(metrics.advance);
        break;
    default:
        return false;
    }
    switch (vertical)
    {
    case Vertical::top:
        break;
    case Vertical::center:
        dy = cellDy = -height / 2;
        break;
    case Vertical::baseline:
        dy = cellDy = -std::int64_t(metrics.ascent);
        break;
    case Vertical::bottom:
        dy = -height;
        cellDy = -std::int64_t(metrics.cellHeight);
        break;
    default:
        return false;
    }
    const auto left = x + dx, right = left + metrics.advance;
    const auto top = y + dy, bottom = top + height;
    // GDI centers odd-width runs half a pixel to the left of its legacy
    // reported layout rectangle. Preserve both conventions independently.
    const auto cellLeft =
        std::int64_t(x) + (horizontal == Horizontal::center ? -(std::int64_t(metrics.advance) + 1) / 2 : dx);
    const auto cellRight = cellLeft + metrics.advance;
    const auto cellTop = y + cellDy, cellBottom = cellTop + metrics.cellHeight;
    const auto fits = [](std::int64_t value) { return value >= INT32_MIN && value <= INT32_MAX; };
    if (!fits(left) || !fits(right) || !fits(top) || !fits(bottom) || !fits(cellLeft) || !fits(cellRight) ||
        !fits(cellTop) || !fits(cellBottom))
        return false;
    placement.logical = {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                         static_cast<std::int32_t>(right), static_cast<std::int32_t>(bottom)};
    placement.cell = {static_cast<std::int32_t>(cellLeft), static_cast<std::int32_t>(cellTop),
                      static_cast<std::int32_t>(cellRight), static_cast<std::int32_t>(cellBottom)};
    return true;
}
} // namespace KauaiFonts

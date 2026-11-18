/* Pixel scrolling policy converted from Kauai pixel_canvas.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/pixel_raster.h"
#include "mm/region.h"
#include <stdlib.h>
#include <string.h>

typedef struct ScrollMask
{
    MMPixelRect bounds;
    unsigned char *bits;
    size_t pitch;
} ScrollMask;

static int allocate_mask(ScrollMask *mask, const MMPixelRect *bounds)
{
    mask->bounds = *bounds;
    /* A 1-bit mask never exceeds the validated surface storage. */
    mask->pitch = (size_t)((bounds->right - bounds->left - 1) / 8 + 1);
    mask->bits = (unsigned char *)calloc((size_t)(bounds->bottom - bounds->top), mask->pitch);
    return mask->bits != NULL;
}

static unsigned char *mask_row(const ScrollMask *mask, MMI32 y)
{
    return mask->bits + (size_t)(y - mask->bounds.top) * mask->pitch;
}

static int mask_contains(const ScrollMask *mask, MMI32 x, MMI32 y)
{
    return mm_pixel_read(mask_row(mask, y), x - mask->bounds.left, 1) != 0;
}

static int mark_region(ScrollMask *mask, const MMPixelRect *bounds, const struct MMRegion *region)
{
    MMRegionCursor cursor = {0};
    MMI32 y, left, right, height, row;
    int ok = 0;
    mm_region_cursor_open(&cursor, region, bounds);
    for (y = bounds->top; y < bounds->bottom; y += height)
    {
        height = cursor.scan.rows;
        if (height > bounds->bottom - y)
            height = bounds->bottom - y;
        while (mm_region_scan_pair(&cursor.scan, &left, &right))
        {
            if (!mask->bits && !allocate_mask(mask, bounds))
                goto done;
            mm_pixel_mask_span(mask_row(mask, y), left, right, 1);
        }
        if (mask->bits)
            for (row = y + 1; row < y + height; ++row)
                memcpy(mask_row(mask, row), mask_row(mask, y), mask->pitch);
        if (y + height < bounds->bottom)
            mm_region_scan_advance(&cursor.scan, height);
    }
    ok = 1;
done:
    mm_region_cursor_close(&cursor);
    return ok;
}

static int covered_move(const ScrollMask *mask, MMI32 x, MMI32 y, MMI32 dx, MMI32 dy)
{
    return mask_contains(mask, x, y) && mask_contains(mask, x - dx, y - dy);
}

static void move_rect(const MMPixelSurface *surface, const MMPixelRect *moved, MMI32 dx, MMI32 dy,
                      const ScrollMask *coverage)
{
    const MMI32 width = moved->right - moved->left, height = moved->bottom - moved->top;
    const MMI32 stride = surface->depth / 8;
    unsigned char *pixels = (unsigned char *)surface->pixels, *target;
    const unsigned char *source;
    MMI32 i, j, x, y;
    if (width <= 0)
        return;

    /* Read each source row or pixel before a move can overwrite it. */
    for (i = 0; i < height; ++i)
    {
        y = dy > 0 ? moved->bottom - 1 - i : moved->top + i;
        source = pixels + (size_t)(y - dy) * (size_t)surface->pitch;
        target = pixels + (size_t)y * (size_t)surface->pitch;
        if (!coverage && stride)
        {
            memmove(target + (size_t)moved->left * stride, source + (size_t)(moved->left - dx) * stride,
                    (size_t)width * stride);
            continue;
        }
        for (j = 0; j < width; ++j)
        {
            x = dx > 0 ? moved->right - 1 - j : moved->left + j;
            if (!coverage || covered_move(coverage, x, y, dx, dy))
                mm_pixel_write(target, x, surface->depth, mm_pixel_read(source, x - dx, surface->depth));
        }
    }
}

static void move_mono_area(const MMPixelSurface *surface, const MMPixelRect *area, MMI32 dx, MMI32 dy)
{
    MMI32 first = area->left / 8 + (area->left % 8 != 0), limit = area->right / 8;
    MMPixelRect part = *area;
    if (first >= limit)
    {
        move_rect(surface, area, dx, dy, NULL);
        return;
    }

    /* Native leftward monochrome moves copy full bytes before reading the
     * masked edges. The right edge reads to its right, away from the left edge. */
    part.left = first * 8;
    part.right = limit * 8;
    move_rect(surface, &part, dx, dy, NULL);
    part.left = area->left;
    part.right = first * 8;
    move_rect(surface, &part, dx, dy, NULL);
    part.left = limit * 8;
    part.right = area->right;
    move_rect(surface, &part, dx, dy, NULL);
}

static int band_contains(const unsigned char *row, MMI32 width, MMI32 offset, int reverse)
{
    return mm_pixel_read(row, reverse ? width - 1 - offset : offset, 1) != 0;
}

static void move_mono_bands(const MMPixelSurface *surface, const ScrollMask *selected, MMI32 dx, MMI32 dy)
{
    const MMPixelRect *bounds = &selected->bounds;
    MMI32 width = bounds->right - bounds->left, height = bounds->bottom - bounds->top;
    MMI32 rows, y, x, start;
    int reverse = dy > 0;
    const unsigned char *row;
    MMPixelRect area;

    /* Each band's horizontal coverage is constant. Traverse bands and runs in
     * the same order as the legacy region move. */
    for (rows = 0; rows < height; rows += area.bottom - area.top)
    {
        y = reverse ? bounds->bottom - 1 - rows : bounds->top + rows;
        area.top = y;
        area.bottom = y + 1;
        row = mask_row(selected, y);
        if (reverse)
            while (area.top > bounds->top && !memcmp(row, mask_row(selected, area.top - 1), selected->pitch))
                --area.top;
        else
            while (area.bottom < bounds->bottom && !memcmp(row, mask_row(selected, area.bottom), selected->pitch))
                ++area.bottom;
        for (x = 0; x < width;)
        {
            while (x < width && !band_contains(row, width, x, reverse))
                ++x;
            start = x;
            while (x < width && band_contains(row, width, x, reverse))
                ++x;
            if (x > start)
            {
                area.left = reverse ? bounds->right - x : bounds->left + start;
                area.right = reverse ? bounds->right - start : bounds->left + x;
                move_mono_area(surface, &area, dx, dy);
            }
        }
    }
}

int mm_pixel_scroll(const MMPixelSurface *surface, const MMPixelRect *rectangle, MMI32 dx, MMI32 dy,
                    const MMPixelRect *clip, const struct MMRegion *region, int legacy_monochrome)
{
    ScrollMask coverage = {0}, selected = {0};
    MMPixelRect area, moved;
    MMI32 width, height, x, y;
    unsigned char *row;
    int result;
    if (!mm_pixel_surface_valid(surface) || rectangle->left > rectangle->right || rectangle->top > rectangle->bottom)
        return 0;
    area.left = area.top = 0;
    area.right = surface->width;
    area.bottom = surface->height;
    mm_pixel_rect_intersect(&area, rectangle);
    if (clip)
        mm_pixel_rect_intersect(&area, clip);
    if ((!dx && !dy) || area.left >= area.right || area.top >= area.bottom)
        return 1;
    width = area.right - area.left;
    height = area.bottom - area.top;
    if (dx <= -width || dx >= width || dy <= -height || dy >= height)
        return 1;

    /* In the intersection, each translated source coordinate is also in area. */
    moved = area;
    if (dx > 0)
        moved.left += dx;
    else
        moved.right += dx;
    if (dy > 0)
        moved.top += dy;
    else
        moved.bottom += dy;

    legacy_monochrome = legacy_monochrome && surface->depth == 1 && dx < 0;
    if (!region)
    {
        if (legacy_monochrome)
            move_mono_area(surface, &moved, dx, dy);
        else
            move_rect(surface, &moved, dx, dy, NULL);
        return 1;
    }

    result = mark_region(&coverage, &area, region);
    if (result && coverage.bits)
    {
        if (!legacy_monochrome)
            move_rect(surface, &moved, dx, dy, &coverage);
        else if ((result = allocate_mask(&selected, &moved)) != 0)
        {
            for (y = moved.top; y < moved.bottom; ++y)
            {
                row = mask_row(&selected, y);
                for (x = moved.left; x < moved.right; ++x)
                    if (covered_move(&coverage, x, y, dx, dy))
                        mm_pixel_write(row, x - moved.left, 1, 1);
            }
            move_mono_bands(surface, &selected, dx, dy);
        }
    }
    free(selected.bits);
    free(coverage.bits);
    return result;
}

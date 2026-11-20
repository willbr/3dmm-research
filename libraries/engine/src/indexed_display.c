/* Indexed display policy converted from Kauai pixel_stretch.cpp and palette_state.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/indexed_display.h"
#include "mm/collections.h"
#include "mm/region.h"
#include "mm/wide.h"
#include <string.h>

static int nonempty(const MMPixelRect *rect)
{
    return rect->left < rect->right && rect->top < rect->bottom;
}

int mm_indexed_double_plan(const MMPixelRect *source_bounds, const MMPixelRect *source, const MMPixelRect *target,
                           const MMPixelRect *clip, MMPixelRect *source_crop, MMPixelRect *target_clip)
{
    MMI32 sw, sh, dw, dh, factor;
    MMPixelRect cropped, visible;
    if (!mm_pixel_rect_extent(source, &sw, &sh) || !mm_pixel_rect_extent(target, &dw, &dh) ||
        (MMU32)dh != 2 * (MMU32)sh || (dw != sw && (MMU32)dw != 2 * (MMU32)sw))
        return MM_DOUBLE_UNAVAILABLE;

    cropped = *source;
    mm_pixel_rect_intersect(&cropped, source_bounds);
    if (!nonempty(&cropped))
        return MM_DOUBLE_CLIPPED;

    factor = dw == sw ? 1 : 2;
    visible.left = target->left + factor * (cropped.left - source->left);
    visible.right = target->right - factor * (source->right - cropped.right);
    visible.top = target->top + 2 * (cropped.top - source->top);
    visible.bottom = target->bottom - 2 * (source->bottom - cropped.bottom);
    if (clip)
        mm_pixel_rect_intersect(&visible, clip);
    if (!nonempty(&visible))
        return MM_DOUBLE_CLIPPED;

    *source_crop = cropped;
    *target_clip = visible;
    return factor == 1 ? MM_DOUBLE_VERTICAL : MM_DOUBLE_BOTH;
}

static MMI32 stretch_limit(MMI32 origin, MMU32 length)
{
    if (length > (MMU32)2147483647L - (MMU32)origin)
        return 2147483647L;
    return mm_signed_word((MMU32)origin + length);
}

static void stretch_span(const unsigned char *source, unsigned char *target, unsigned char *second, MMU32 offset,
                         MMI32 count, int double_width)
{
    MMI32 i;
    unsigned char index;
    if (!double_width)
    {
        source += offset;
        for (i = 0; i < count; ++i)
            target[i] = source[i];
        if (second)
            for (i = 0; i < count; ++i)
                second[i] = source[i];
        return;
    }

    source += offset / 2;
    if ((offset & 1) && count)
    {
        index = *source++;
        *target++ = index;
        if (second)
            *second++ = index;
        --count;
    }
    for (i = 0; i + 1 < count; i += 2)
    {
        index = *source++;
        target[i] = target[i + 1] = index;
        if (second)
            second[i] = second[i + 1] = index;
    }
    if (i < count)
    {
        index = *source;
        target[i] = index;
        if (second)
            second[i] = index;
    }
}

int mm_indexed_double_draw(const MMPixelSurface *target, const MMPixelSurface *source, const MMPixelRect *source_rect,
                           MMI32 x, MMI32 y, int double_width, const MMPixelRect *clip, const struct MMRegion *region)
{
    MMRegionCursor cursor = {0};
    MMPixelRect shape, bounds;
    const unsigned char *from;
    unsigned char *to, *second;
    MMI32 sw, sh, row, left, right, height, advance;
    MMU32 offset;
    if (target->depth != 8 || source->depth != 8 || !mm_pixel_surface_valid(target) ||
        !mm_pixel_surface_valid(source) || !mm_pixel_rect_extent(source_rect, &sw, &sh) ||
        source_rect->left < 0 || source_rect->top < 0 || source_rect->right > source->width ||
        source_rect->bottom > source->height)
        return 0;
    shape.left = x;
    shape.top = y;
    shape.right = stretch_limit(x, (MMU32)sw * (double_width ? 2 : 1));
    shape.bottom = stretch_limit(y, 2 * (MMU32)sh);
    if (!mm_pixel_visible_rect(target, &shape, clip, &bounds))
        return 1;

    if (region)
        mm_region_cursor_open(&cursor, region, &bounds);
    for (row = bounds.top; row < bounds.bottom; row += advance)
    {
        height = region ? cursor.scan.rows : bounds.bottom - row;
        left = bounds.left;
        right = bounds.right;
        if (region && !mm_region_cursor_pair(&cursor, &left, &right))
            advance = height;
        else
        {
            offset = (MMU32)row - (MMU32)y;
            from = (const unsigned char *)source->pixels +
                   (size_t)(source_rect->top + (MMI32)(offset / 2)) * (size_t)source->pitch + source_rect->left;
            to = (unsigned char *)target->pixels + (size_t)row * (size_t)target->pitch;
            /* Kauai treats an even-phase pair in one region band as a unit.
             * The second row must still fit in the physical destination. */
            advance = !(offset & 1) && height > 1 && row < target->height - 1 ? 2 : 1;
            second = advance == 2 ? to + target->pitch : NULL;
            do
            {
                stretch_span(from, to + left, second ? second + left : NULL, (MMU32)left - (MMU32)x,
                             right - left, double_width);
            } while (region && mm_region_cursor_pair(&cursor, &left, &right));
        }
        if (advance >= bounds.bottom - row)
            break;
        if (region)
            mm_region_scan_advance(&cursor.scan, advance);
    }
    mm_region_cursor_close(&cursor);
    return 1;
}

int mm_display_palette_set(unsigned char current[1024], size_t *count, const unsigned char *requested,
                           size_t requested_count, MMU32 flags)
{
    /* The first and last ten entries are the Windows identity colours. */
    static const unsigned char standard[20][4] = {
        {0, 0, 0, 0},       {0, 0, 128, 0},     {0, 128, 0, 0},     {0, 128, 128, 0},   {128, 0, 0, 0},
        {128, 0, 128, 0},   {128, 128, 0, 0},   {192, 192, 192, 0}, {192, 220, 192, 0}, {240, 202, 166, 0},
        {240, 251, 255, 0}, {164, 160, 160, 0}, {128, 128, 128, 0}, {0, 0, 255, 0},     {0, 255, 0, 0},
        {0, 255, 255, 0},   {255, 0, 0, 0},     {255, 0, 255, 0},   {255, 255, 0, 0},   {255, 255, 255, 0}
    };
    unsigned char source[1024], reserved_red[256] = {0};
    size_t used, updated, i;
    unsigned int red;
    if ((!requested && requested_count) || *count > 256)
        return MM_DISPLAY_PALETTE_INVALID;
    if (flags & MM_DISPLAY_PALETTE_INIT_ANIMATION)
        flags &= ~(MMU32)MM_DISPLAY_PALETTE_ANIMATE;
    if (!requested_count)
    {
        if (flags & MM_DISPLAY_PALETTE_ANIMATE)
            return MM_DISPLAY_PALETTE_UNCHANGED;
        *count = 0;
        return MM_DISPLAY_PALETTE_CHANGED;
    }

    used = requested_count < 256 ? requested_count : 256;
    memcpy(source, requested, used * 4);
    updated = (flags & MM_DISPLAY_PALETTE_ANIMATE) && *count < used ? *count : used;
    if (flags & MM_DISPLAY_PALETTE_IDENTITY)
    {
        for (i = 0; i < 20; ++i)
        {
            memcpy(current + 4 * (i < 10 ? i : i + 236), standard[i], 4);
            reserved_red[standard[i][2]] = 1;
        }
    }
    for (i = 10; i < updated && i < 246; ++i)
    {
        red = source[4 * i + 2];
        while (red && reserved_red[red])
            --red;
        while (reserved_red[red])
            ++red;
        current[4 * i] = source[4 * i];
        current[4 * i + 1] = source[4 * i + 1];
        current[4 * i + 2] = (unsigned char)red;
        current[4 * i + 3] = 0;
    }
    *count = updated;
    return MM_DISPLAY_PALETTE_CHANGED;
}

int mm_display_palette_overlay(unsigned char *base, size_t base_count, size_t first,
                               const unsigned char *colours, size_t count)
{
    if (base_count > 256 || first > base_count || count > base_count - first ||
        (count && (!base || !colours)))
        return 0;
    if (count)
        memmove(base + 4 * first, colours, 4 * count);
    return 1;
}

int mm_palette_read_256(const MMReadStream *stream, unsigned char colours[1024])
{
    const unsigned short native_order = 1;
    MMCollectionHeader header;
    if (mm_collection_header_read(stream, MM_COLLECTION_ARRAY, 0, &header) != MM_STORAGE_OK ||
        header.width != 4 || header.count != 256 ||
        !mm_read_exact(stream, header.header_bytes, colours, 1024))
        return 0;
    if (header.big_endian != (*(const unsigned char *)&native_order == 0))
        mm_palette_swap_word_order(colours, header.count);
    return 1;
}

void mm_palette_swap_word_order(unsigned char *colours, size_t count)
{
    unsigned char swap;
    size_t i;
    for (i = 0; i < count; ++i, colours += 4)
    {
        swap = colours[0];
        colours[0] = colours[3];
        colours[3] = swap;
        swap = colours[1];
        colours[1] = colours[2];
        colours[2] = swap;
    }
}

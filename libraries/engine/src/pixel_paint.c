/* Brush and shape policy converted from Kauai pixel_canvas.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/pixel_paint.h"
#include <stdlib.h>
#include <string.h>

#define RGB_BITS 0xffffffUL
#define WORD_BITS ((MMU32)0xffffffffUL)

static const MMPixelMono default_mono = {RGB_BITS, 0, 0, 0, 1};

static MMPixelInk ink(MMU32 and_mask, MMU32 xor_mask)
{
    MMPixelInk result;
    result.and_mask = and_mask;
    result.xor_mask = xor_mask;
    return result;
}

static MMPixelInk rgb_ink(const MMPixelColorInk *colour)
{
    if (colour->mode == MM_PIXEL_CLEAR)
        return ink(WORD_BITS, 0);
    if (colour->mode == MM_PIXEL_INVERT)
        return ink(WORD_BITS, RGB_BITS);
    return ink(0, colour->rgb & RGB_BITS);
}

void mm_pixel_resolve_rgb(const MMPixelColorBrush *source, MMPixelBrush *result)
{
    result->foreground = rgb_ink(&source->foreground);
    result->background = rgb_ink(&source->background);
    memcpy(result->pattern, source->pattern, sizeof(result->pattern));
}

static void solid_pattern(MMPixelBrush *brush)
{
    brush->foreground = brush->background = ink(0, 0);
    memset(brush->pattern, 255, sizeof(brush->pattern));
}

static void pattern_extremes(const MMPixelColorBrush *brush, int *all_fore, int *all_back)
{
    unsigned i;
    *all_fore = *all_back = 1;
    for (i = 0; i < sizeof(brush->pattern); ++i)
    {
        *all_fore &= brush->pattern[i] == 255;
        *all_back &= brush->pattern[i] == 0;
    }
}

static MMU32 quantise(MMI32 depth, const unsigned char *palette, MMI32 count, MMU32 rgb)
{
    if (depth <= 8)
        return mm_pixel_nearest(palette, count, rgb);
    return depth == 16 ? mm_pixel_rgb555(rgb) : rgb & RGB_BITS;
}

static MMPixelInk solid_ink(const MMPixelColorInk *colour, MMI32 depth, const unsigned char *palette, MMI32 count,
                            MMU32 full, MMU32 white)
{
    if (colour->mode == MM_PIXEL_CLEAR)
        return ink(full, 0);
    if (colour->mode == MM_PIXEL_INVERT)
        return ink(full, white);
    return ink(0, quantise(depth, palette, count, colour->rgb));
}

static MMU32 map_sample(const MMPixelColorBrush *brush, MMU32 before, MMU32 mask, MMU32 colour)
{
    if (brush->foreground.mode == MM_PIXEL_INVERT)
    {
        before ^= mask;
        if (brush->background.mode == MM_PIXEL_CLEAR)
            return before;
    }
    if (brush->foreground.mode == MM_PIXEL_CLEAR || brush->foreground.mode == MM_PIXEL_INVERT)
        return (before | ~mask) & colour;
    return colour;
}

static MMPixelInk map_ink(const MMPixelColorBrush *brush, MMU32 full, MMU32 mask, MMU32 colour)
{
    MMU32 zero = map_sample(brush, 0, mask, colour);
    return ink(zero ^ map_sample(brush, full, mask, colour), zero);
}

static void resolve_palette_brush(const MMPixelColorBrush *source, MMI32 depth, const unsigned char *palette,
                                  MMI32 count, int highlight, MMPixelBrush *result)
{
    const MMU32 full = depth == 8 ? 255 : WORD_BITS;
    MMU32 white, black, fore, back;
    MMPixelColorBrush brush;
    int all_fore, all_back, same;
    unsigned i;
    solid_pattern(result);
    if (highlight)
    {
        result->foreground = ink(full, full);
        return;
    }
    white = quantise(depth, palette, count, RGB_BITS);
    black = quantise(depth, palette, count, 0);
    pattern_extremes(source, &all_fore, &all_back);
    same = source->foreground.mode == source->background.mode &&
           (source->foreground.mode != MM_PIXEL_COLOR || source->foreground.rgb == source->background.rgb);
    if (all_fore || same || all_back)
    {
        result->foreground = solid_ink(all_fore || same ? &source->foreground : &source->background,
                                      depth, palette, count, full, white);
        return;
    }

    brush = *source;
    if (source->background.mode == MM_PIXEL_INVERT ||
        (source->background.mode == MM_PIXEL_CLEAR && source->foreground.mode != MM_PIXEL_INVERT))
    {
        brush.foreground = source->background;
        brush.background = source->foreground;
        for (i = 0; i < sizeof(brush.pattern); ++i)
            brush.pattern[i] ^= 255;
    }
    memcpy(result->pattern, brush.pattern, sizeof(result->pattern));
    fore = brush.foreground.mode == MM_PIXEL_CLEAR || brush.foreground.mode == MM_PIXEL_INVERT
             ? white : quantise(depth, palette, count, brush.foreground.rgb);
    back = quantise(depth, palette, count, brush.background.rgb);
    result->foreground = map_ink(&brush, full, white, fore);
    result->background = map_ink(&brush, full, black, back);
}

int mm_pixel_resolve_indexed(const MMPixelColorBrush *source, const unsigned char *palette, MMI32 count,
                             int highlight, MMPixelBrush *result)
{
    if (!palette || count < 1 || count > 256)
        return 0;
    resolve_palette_brush(source, 8, palette, count, highlight, result);
    return 1;
}

unsigned char mm_pixel_monochrome_colour(MMU32 rgb, unsigned char reference, const MMPixelMono *state,
                                        const unsigned char *palette)
{
    static const unsigned char black_white[] = {0, 0, 0, 0, 255, 255, 255, 0};
    unsigned char background;
    if (!state)
        state = &default_mono;
    if (!palette)
        palette = black_white;
    rgb &= RGB_BITS;
    if (rgb == mm_pixel_rgb(palette))
        return 0;
    if (rgb == mm_pixel_rgb(palette + 4))
        return 1;
    background = mm_pixel_nearest(palette, 2, state->background_rgb);
    return rgb == (state->background_rgb & RGB_BITS) && reference == state->background_reference
           ? background : (unsigned char)(1 - background);
}

int mm_pixel_resolve_packed(const MMPixelImage *image, const MMPixelColorBrush *brush, const MMPixelMono *state,
                            MMPixelBrush *result)
{
    MMI32 depth = image->surface.depth;
    int all_fore, all_back, same, alternate;
    unsigned char reference;
    const MMPixelColorInk *colour;
    if (!mm_pixel_image_valid(image) || (depth != 1 && depth != 4 && depth != 16 && depth != 24))
        return 0;
    if (depth != 1)
    {
        resolve_palette_brush(brush, depth, image->palette, image->colours, 0, result);
        return 1;
    }

    if (!state)
        state = &default_mono;
    pattern_extremes(brush, &all_fore, &all_back);
    same = brush->foreground.mode == brush->background.mode &&
           (brush->foreground.mode != MM_PIXEL_COLOR ||
            (state->collapse_equal_colours && brush->foreground.rgb == brush->background.rgb &&
             state->foreground_reference == state->alternate_reference));
    if (!all_fore && !all_back && !same)
        return 0;
    alternate = all_back && !same;
    colour = alternate ? &brush->background : &brush->foreground;
    reference = alternate ? state->alternate_reference : state->foreground_reference;
    solid_pattern(result);
    if (colour->mode == MM_PIXEL_CLEAR)
        result->foreground = ink(WORD_BITS, 0);
    else if (colour->mode == MM_PIXEL_INVERT)
        result->foreground = ink(WORD_BITS, mm_pixel_monochrome_colour(RGB_BITS, 0, state, image->palette));
    else
        result->foreground = ink(0, mm_pixel_monochrome_colour(colour->rgb, reference, state, image->palette));
    return 1;
}

void mm_pixel_paint_span(const MMPixelSurface *surface, const MMPixelBrush *brush, MMI32 y, MMI32 left, MMI32 right)
{
    unsigned char *row = (unsigned char *)surface->pixels + (size_t)y * (size_t)surface->pitch;
    const unsigned char pattern = brush->pattern[y & 7];
    const MMPixelInk *choice;
    MMI32 x;
    MMU32 raw;
    for (x = left; x < right; ++x)
    {
        choice = (pattern & (128 >> (x & 7))) ? &brush->foreground : &brush->background;
        raw = 0;
        if (surface->depth == 32)
        {
            if (choice->and_mask == WORD_BITS && !choice->xor_mask)
                continue;
            if (choice->and_mask)
                memcpy(&raw, row + (size_t)x * 4, 4);
            raw = (raw & choice->and_mask) ^ choice->xor_mask;
            memcpy(row + (size_t)x * 4, &raw, 4);
        }
        else
        {
            if (choice->and_mask)
                raw = mm_pixel_read(row, x, surface->depth);
            raw = (raw & choice->and_mask) ^ choice->xor_mask;
            mm_pixel_write(row, x, surface->depth, raw);
        }
    }
}

typedef struct RectPaint
{
    const MMPixelSurface *surface;
    const MMPixelBrush *brush;
    MMPixelRect inset;
} RectPaint;

static void paint_rect_span(void *context, MMI32 y, MMI32 left, MMI32 right)
{
    const RectPaint *paint = (const RectPaint *)context;
    if (y < paint->inset.top || y >= paint->inset.bottom)
        mm_pixel_paint_span(paint->surface, paint->brush, y, left, right);
    else
    {
        if (left < paint->inset.left)
            mm_pixel_paint_span(paint->surface, paint->brush, y, left,
                                right < paint->inset.left ? right : paint->inset.left);
        if (right > paint->inset.right)
            mm_pixel_paint_span(paint->surface, paint->brush, y,
                                left > paint->inset.right ? left : paint->inset.right, right);
    }
}

void mm_pixel_fill_rect(const MMPixelSurface *surface, const MMPixelBrush *brush, const MMPixelRect *rect,
                        const MMPixelRect *clip, const MMPixelSpans *spans, MMI32 frame_x, MMI32 frame_y)
{
    RectPaint paint;
    MMPixelRect bounds;
    if (!mm_pixel_surface_valid(surface) || !mm_pixel_visible_rect(surface, rect, clip, &bounds))
        return;
    paint.surface = surface;
    paint.brush = brush;
    paint.inset.top = paint.inset.bottom = 0;
    if (frame_x >= 0 && frame_y >= 0 &&
        2 * (MMU32)frame_x < (MMU32)rect->right - (MMU32)rect->left &&
        2 * (MMU32)frame_y < (MMU32)rect->bottom - (MMU32)rect->top)
    {
        paint.inset.left = rect->left + frame_x;
        paint.inset.top = rect->top + frame_y;
        paint.inset.right = rect->right - frame_x;
        paint.inset.bottom = rect->bottom - frame_y;
    }
    mm_pixel_visit_spans(&bounds, spans, paint_rect_span, &paint);
}

int mm_pixel_invert_packed_rect(const MMPixelImage *image, const MMPixelRect *rect, const MMPixelRect *clip,
                                const MMPixelSpans *spans)
{
    static const MMPixelBrush brush = {{WORD_BITS, WORD_BITS}, {0, 0}, {255, 255, 255, 255, 255, 255, 255, 255}};
    MMI32 depth = image->surface.depth;
    if (!mm_pixel_surface_valid(&image->surface) || (depth != 1 && depth != 4 && depth != 16 && depth != 24))
        return 0;
    /* Keep the packed fill's palette admission even though inversion is raw. */
    if (mm_pixel_image_valid(image))
        mm_pixel_fill_rect(&image->surface, &brush, rect, clip, spans, -1, -1);
    return 1;
}

typedef struct CoveragePaint
{
    const MMPixelSurface *target;
    const MMPixelCoverage *mask;
    MMI32 x, y;
    MMU32 sample;
    int blend_rgb;
} CoveragePaint;

static void paint_coverage_span(void *context, MMI32 y, MMI32 left, MMI32 right)
{
    const CoveragePaint *paint = (const CoveragePaint *)context;
    const unsigned char *source = paint->mask->pixels + (size_t)(y - paint->y) * (size_t)paint->mask->pitch;
    unsigned char *row = (unsigned char *)paint->target->pixels + (size_t)y * (size_t)paint->target->pitch;
    MMI32 x;
    MMU32 alpha, result, previous, from, to;
    unsigned shift;
    for (x = left; x < right; ++x)
    {
        alpha = source[x - paint->x];
        if (!paint->blend_rgb)
        {
            if (alpha >= 128)
                mm_pixel_write(row, x, paint->target->depth, paint->sample);
        }
        else if (alpha)
        {
            result = paint->sample & RGB_BITS;
            if (alpha != 255)
            {
                memcpy(&previous, row + (size_t)x * 4, 4);
                result = 0;
                for (shift = 0; shift < 24; shift += 8)
                {
                    from = (previous >> shift) & 255;
                    to = (paint->sample >> shift) & 255;
                    result |= ((from * (255 - alpha) + to * alpha + 127) / 255) << shift;
                }
            }
            memcpy(row + (size_t)x * 4, &result, 4);
        }
    }
}

int mm_pixel_draw_coverage(const MMPixelSurface *target, const MMPixelCoverage *mask, MMI32 x, MMI32 y, MMU32 sample,
                            int blend_rgb, const MMPixelRect *clip, const MMPixelSpans *spans)
{
    MMPixelSurface input;
    MMPixelRect shape, bounds;
    CoveragePaint paint;
    input.pixels = (void *)mask->pixels;
    input.width = mask->width;
    input.height = mask->height;
    input.pitch = mask->pitch;
    input.depth = 8;
    if (!mm_pixel_surface_valid(target) || !mm_pixel_surface_valid(&input) || (blend_rgb && target->depth != 32))
        return 0;
    shape.left = x;
    shape.top = y;
    shape.right = x > 2147483647L - mask->width ? 2147483647L : x + mask->width;
    shape.bottom = y > 2147483647L - mask->height ? 2147483647L : y + mask->height;
    if (!mm_pixel_visible_rect(target, &shape, clip, &bounds))
        return 1;
    paint.target = target;
    paint.mask = mask;
    paint.x = x;
    paint.y = y;
    paint.sample = sample;
    paint.blend_rgb = blend_rgb;
    mm_pixel_visit_spans(&bounds, spans, paint_coverage_span, &paint);
    return 1;
}

typedef struct Crossing
{
    MMI32 x;
    int ascending;
} Crossing;

typedef struct PolygonPaint
{
    const MMPixelSurface *surface;
    const MMPixelBrush *brush;
    const MMPixelPoint *points;
    size_t count;
    Crossing *crossings;
    size_t used;
    MMI32 last_row;
} PolygonPaint;

static int compare_crossings(const void *left, const void *right)
{
    MMI32 a = ((const Crossing *)left)->x, b = ((const Crossing *)right)->x;
    return a < b ? -1 : a > b;
}

static void collect_crossings(PolygonPaint *paint, MMI32 y)
{
    const MMPixelPoint *a, *b, *swap;
    Crossing *next;
    size_t i;
    int ascending;
    paint->used = 0;
    paint->last_row = y;
    for (i = 0; i < paint->count; ++i)
    {
        a = paint->points + i;
        b = paint->points + (i + 1 == paint->count ? 0 : i + 1);
        ascending = a->y < b->y;
        if (!ascending)
        {
            swap = a;
            a = b;
            b = swap;
        }
        if (y < a->y || y >= b->y)
            continue;
        next = paint->crossings + paint->used++;
        next->x = mm_pixel_edge_ceil(a->x, a->y, b->x, b->y, y);
        next->ascending = ascending;
    }
    qsort(paint->crossings, paint->used, sizeof(*paint->crossings), compare_crossings);
}

static void paint_polygon_span(void *context, MMI32 y, MMI32 left, MMI32 right)
{
    PolygonPaint *paint = (PolygonPaint *)context;
    size_t up = 0, down = 0, i = 0;
    MMI32 cut, start = 0, first, limit;
    int inside;
    if (paint->last_row != y)
        collect_crossings(paint, y);
    while (i < paint->used)
    {
        cut = paint->crossings[i].x;
        inside = up != down;
        do
        {
            if (paint->crossings[i++].ascending)
                ++up;
            else
                ++down;
        } while (i < paint->used && paint->crossings[i].x == cut);
        if (!inside && up != down)
            start = cut;
        if (inside && up == down)
        {
            first = left > start ? left : start;
            limit = right < cut ? right : cut;
            if (first < limit)
                mm_pixel_paint_span(paint->surface, paint->brush, y, first, limit);
        }
    }
}

int mm_pixel_fill_polygon(const MMPixelSurface *surface, const MMPixelBrush *brush, const MMPixelPoint *points,
                          size_t count, const MMPixelRect *clip, const MMPixelSpans *spans)
{
    MMPixelRect shape, bounds;
    PolygonPaint paint;
    size_t i;
    if (!mm_pixel_surface_valid(surface) || (!points && count) || count > (size_t)-1 / sizeof(Crossing))
        return 0;
    if (count < 3)
        return 1;
    shape.left = shape.right = points[0].x;
    shape.top = shape.bottom = points[0].y;
    for (i = 1; i < count; ++i)
    {
        if (points[i].x < shape.left)
            shape.left = points[i].x;
        if (points[i].x > shape.right)
            shape.right = points[i].x;
        if (points[i].y < shape.top)
            shape.top = points[i].y;
        if (points[i].y > shape.bottom)
            shape.bottom = points[i].y;
    }
    if (!mm_pixel_visible_rect(surface, &shape, clip, &bounds))
        return 1;
    paint.crossings = (Crossing *)malloc(count * sizeof(*paint.crossings));
    if (!paint.crossings)
        return 0;
    paint.surface = surface;
    paint.brush = brush;
    paint.points = points;
    paint.count = count;
    paint.used = 0;
    paint.last_row = -1;
    mm_pixel_visit_spans(&bounds, spans, paint_polygon_span, &paint);
    free(paint.crossings);
    return 1;
}

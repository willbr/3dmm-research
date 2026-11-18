/*
 * SPDX-License-Identifier: Apache-2.0
 * HFD fixed-point curve subdivision adapted from emf-converter's gdi-raster.ts.
 * Copyright 2025-present pptx-viewer contributors.
 * Licensed under the Apache License, Version 2.0. See
 * docs/licenses/ellipse-Apache-2.0.txt and ellipse-NOTICE.txt.
 *
 * Modified for 3DMMForever: C implementation, separate large-bound path,
 * ellipse construction, bounded row generation and integer scan conversion.
 */
#include "mm/ellipse_geometry.h"
#include "mm/pixel_paint.h"
#include <stdlib.h>
#include <string.h>

typedef struct EllipseOutline
{
    MMPixelPoint *vertices;
    size_t count, capacity;
} EllipseOutline;

/* The fixed path uses radii smaller than 16384 subpixels. Each control point
 * fits within 15 bits plus sign. Scaling positions by 8192 and second
 * differences by 6144 leaves room in a signed 32-bit word. */
typedef struct FixedBasis
{
    MMI32 position, advance, end_error, start_error;
} FixedBasis;

typedef struct LargeBasis
{
    double position, advance, end_error, start_error;
} LargeBasis;

static MMPixelPoint point(MMI32 x, MMI32 y)
{
    MMPixelPoint vertex;
    vertex.x = x;
    vertex.y = y;
    return vertex;
}

static int add_vertex(EllipseOutline *outline, MMI32 x, MMI32 y)
{
    size_t capacity;
    MMPixelPoint *vertices;
    if (outline->count == outline->capacity)
    {
        capacity = outline->capacity ? outline->capacity * 2 : 64;
        if (capacity <= outline->capacity || capacity > (size_t)-1 / sizeof(*vertices))
            return 0;
        vertices = (MMPixelPoint *)realloc(outline->vertices, capacity * sizeof(*vertices));
        if (!vertices)
            return 0;
        outline->vertices = vertices;
        outline->capacity = capacity;
    }
    outline->vertices[outline->count++] = point(x, y);
    return 1;
}

/* C89 does not fix the rounding direction of signed integer division. */
static MMI32 floor_shift(MMI32 value, unsigned bits)
{
    MMU32 divisor;
    if (!bits)
        return value;
    divisor = (MMU32)1 << bits;
    if (value < 0)
        return -(MMI32)(((MMU32)0 - (MMU32)value + divisor - 1) / divisor);
    return (MMI32)((MMU32)value / divisor);
}

static FixedBasis fixed_basis(MMI32 a, MMI32 b, MMI32 c, MMI32 d)
{
    FixedBasis basis;
    basis.position = a * 1024;
    basis.advance = (d - a) * 1024;
    basis.end_error = (b - 2 * c + d) * 6144;
    basis.start_error = (a - 2 * b + c) * 6144;
    return basis;
}

static int fixed_exceeds(const FixedBasis *basis, MMI32 limit)
{
    return basis->end_error < -limit || basis->end_error > limit ||
           basis->start_error < -limit || basis->start_error > limit;
}

static void fixed_initial_halve(FixedBasis *basis, unsigned shift)
{
    basis->end_error = floor_shift(basis->end_error + basis->start_error, 1);
    basis->advance = floor_shift(basis->advance - floor_shift(basis->end_error, shift), 1);
}

static void fixed_normalise(FixedBasis *basis, unsigned shift)
{
    basis->position *= 8;
    basis->advance *= 8;
    if (shift >= 3)
    {
        basis->end_error = floor_shift(basis->end_error, shift - 3);
        basis->start_error = floor_shift(basis->start_error, shift - 3);
    }
    else
    {
        basis->end_error *= 1 << (3 - shift);
        basis->start_error *= 1 << (3 - shift);
    }
}

static void fixed_halve(FixedBasis *basis)
{
    basis->end_error = floor_shift(basis->end_error + basis->start_error, 3);
    basis->advance = floor_shift(basis->advance - basis->end_error, 1);
    basis->start_error = floor_shift(basis->start_error, 2);
}

static int fixed_can_double(const FixedBasis *basis)
{
    MMI32 other = 2 * basis->end_error - basis->start_error;
    return basis->start_error >= -130944L && basis->start_error <= 130944L &&
           other >= -130944L && other <= 130944L;
}

static void fixed_double(FixedBasis *basis)
{
    basis->advance = 2 * basis->advance + basis->end_error;
    basis->start_error *= 4;
    basis->end_error = 8 * basis->end_error - basis->start_error;
}

static MMI32 fixed_step(FixedBasis *basis)
{
    MMI32 previous = basis->end_error;
    basis->position += basis->advance;
    basis->advance += basis->end_error;
    basis->end_error = 2 * basis->end_error - basis->start_error;
    basis->start_error = previous;
    return floor_shift(basis->position + 4096, 13);
}

static int flatten_fixed(const MMPixelPoint *p, EllipseOutline *outline)
{
    FixedBasis x = fixed_basis(p[0].x, p[1].x, p[2].x, p[3].x);
    FixedBasis y = fixed_basis(p[0].y, p[1].y, p[2].y, p[3].y);
    unsigned shift = 0;
    MMU32 steps = 1;
    MMI32 limit = 65472L;
    while (fixed_exceeds(&x, limit) || fixed_exceeds(&y, limit))
    {
        shift += 2;
        fixed_initial_halve(&x, shift);
        fixed_initial_halve(&y, shift);
        steps *= 2;
        limit *= 4;
    }
    fixed_normalise(&x, shift);
    fixed_normalise(&y, shift);
    while (steps)
    {
        if (fixed_exceeds(&x, 523776L) || fixed_exceeds(&y, 523776L))
        {
            fixed_halve(&x);
            fixed_halve(&y);
            steps *= 2;
        }
        while (!(steps & 1) && fixed_can_double(&x) && fixed_can_double(&y))
        {
            fixed_double(&x);
            fixed_double(&y);
            steps /= 2;
        }
        --steps;
        if (!add_vertex(outline, fixed_step(&x), fixed_step(&y)))
            return 0;
    }
    return 1;
}

static LargeBasis large_basis(double a, double b, double c, double d)
{
    LargeBasis basis;
    basis.position = a;
    basis.advance = d - a;
    basis.end_error = 6 * (b - 2 * c + d);
    basis.start_error = 6 * (a - 2 * b + c);
    return basis;
}

static int large_exceeds(const LargeBasis *basis, double limit)
{
    return basis->end_error < -limit || basis->end_error > limit ||
           basis->start_error < -limit || basis->start_error > limit;
}

static void large_halve(LargeBasis *basis)
{
    basis->end_error = (basis->end_error + basis->start_error) / 8;
    basis->advance = (basis->advance - basis->end_error) / 2;
    basis->start_error /= 4;
}

static int large_can_double(const LargeBasis *basis)
{
    double other = 2 * basis->end_error - basis->start_error;
    return basis->start_error >= -16 && basis->start_error <= 16 && other >= -16 && other <= 16;
}

static void large_double(LargeBasis *basis)
{
    basis->advance = 2 * basis->advance + basis->end_error;
    basis->start_error *= 4;
    basis->end_error = 8 * basis->end_error - basis->start_error;
}

static MMI32 large_step(LargeBasis *basis)
{
    double previous = basis->end_error, rounded;
    MMI32 integral;
    basis->position += basis->advance;
    basis->advance += basis->end_error;
    basis->end_error = 2 * basis->end_error - basis->start_error;
    basis->start_error = previous;
    rounded = basis->position + 0.5;
    integral = (MMI32)rounded;
    return integral - (integral > rounded);
}

static int flatten_large(const MMPixelPoint *p, EllipseOutline *outline)
{
    LargeBasis x = large_basis(p[0].x, p[1].x, p[2].x, p[3].x);
    LargeBasis y = large_basis(p[0].y, p[1].y, p[2].y, p[3].y);
    MMU32 steps = 1;
    while (steps)
    {
        while (large_exceeds(&x, 64) || large_exceeds(&y, 64))
        {
            large_halve(&x);
            large_halve(&y);
            steps *= 2;
        }
        while (!(steps & 1) && large_can_double(&x) && large_can_double(&y))
        {
            large_double(&x);
            large_double(&y);
            steps /= 2;
        }
        --steps;
        if (!add_vertex(outline, large_step(&x), large_step(&y)))
            return 0;
    }
    return 1;
}

static int build_outline(MMI32 width, MMI32 height, EllipseOutline *outline)
{
    static const double kappa = 0.5522847498307936;
    const MMI32 cx = 8 * (width - 1), cy = 8 * (height - 1);
    const MMI32 rx = 8 * width - 4, ry = 8 * height - 4;
    const MMI32 kx = (MMI32)(rx * kappa) + 1, ky = (MMI32)(ry * kappa);
    MMPixelPoint p[13];
    unsigned i;
    int fixed = rx < 16384 && ry < 16384;
    p[0] = point(cx + rx, cy);
    p[1] = point(cx + rx, cy - ky);
    p[2] = point(cx + kx, cy - ry);
    p[3] = point(cx, cy - ry);
    p[4] = point(cx - kx, cy - ry);
    p[5] = point(cx - rx, cy - ky);
    p[6] = point(cx - rx, cy);
    p[7] = point(cx - rx, cy + ky);
    p[8] = point(cx - kx, cy + ry);
    p[9] = point(cx, cy + ry);
    p[10] = point(cx + kx, cy + ry);
    p[11] = point(cx + rx, cy + ky);
    p[12] = p[0];
    if (!add_vertex(outline, p[0].x, p[0].y))
        return 0;
    for (i = 0; i < 12; i += 3)
        if (!(fixed ? flatten_fixed(p + i, outline) : flatten_large(p + i, outline)))
            return 0;
    return 1;
}

static void outline_row(const EllipseOutline *outline, MMI32 width, MMI32 y, MMEllipseSpan *row)
{
    const MMPixelPoint *a, *b, *swap;
    MMI32 left = width, right = 0, x;
    size_t i;
    y *= 16;
    for (i = 1; i < outline->count; ++i)
    {
        a = outline->vertices + i - 1;
        b = a + 1;
        if (a->y > b->y)
        {
            swap = a;
            a = b;
            b = swap;
        }
        if (a->y <= y && y < b->y)
        {
            /* Rounding the intersection upward before converting sixteenths
             * to pixels gives the same result as one upward rounding. */
            x = floor_shift(mm_pixel_edge_ceil(a->x, a->y, b->x, b->y, y) + 15, 4);
            if (x < left)
                left = x;
            if (x > right)
                right = x;
        }
    }
    if (left < right)
    {
        row->left = left < 0 ? 0 : left;
        row->right = right > width ? width : right;
    }
}

static int span_storage(MMI32 count, size_t arrays, size_t *bytes)
{
    size_t rows = (size_t)count, stride = sizeof(MMEllipseSpan) * arrays;
    if (rows != (MMU32)count || rows > (size_t)-1 / stride)
        return 0;
    *bytes = rows * stride;
    return 1;
}

int mm_ellipse_rows(MMI32 width, MMI32 height, MMI32 first, MMI32 count, MMEllipseSpan *output)
{
    EllipseOutline outline = {NULL, 0, 0};
    MMI32 row, start, y, visible;
    size_t bytes;
    if (width < 0 || height < 0 || width > MM_ELLIPSE_MAX_EXTENT || height > MM_ELLIPSE_MAX_EXTENT ||
        count < 0 || count > MM_ELLIPSE_MAX_EXTENT || !span_storage(count, 1, &bytes))
        return 0;
    if (!count)
        return 1;
    if (!output)
        return 0;
    if (!width || !height || first >= height || first <= -count)
    {
        memset(output, 0, bytes);
        return 1;
    }
    if (!build_outline(width, height, &outline))
    {
        free(outline.vertices);
        return 0;
    }

    memset(output, 0, bytes);
    start = first < 0 ? -first : 0;
    y = first + start;
    visible = count - start;
    if (visible > height - y)
        visible = height - y;
    for (row = 0; row < visible; ++row)
        outline_row(&outline, width, y + row, output + start + row);
    free(outline.vertices);
    return 1;
}

typedef struct OvalPaint
{
    const MMPixelSurface *surface;
    const MMPixelBrush *brush;
    const MMEllipseSpan *outer, *inner;
    MMI32 x, top, frame_x;
} OvalPaint;

static void paint_interval(const OvalPaint *state, MMI32 y, MMI32 left, MMI32 right, MMI32 start, MMI32 end)
{
    if (start < left)
        start = left;
    if (end > right)
        end = right;
    if (start < end)
        mm_pixel_paint_span(state->surface, state->brush, y, start, end);
}

static void order_edges(MMI32 *a, MMI32 *b)
{
    if (*a > *b)
    {
        MMI32 swap = *a;
        *a = *b;
        *b = swap;
    }
}

static void paint_oval_span(void *context, MMI32 y, MMI32 left, MMI32 right)
{
    const OvalPaint *state = (const OvalPaint *)context;
    MMI32 row = y - state->top;
    MMI32 a = state->x + state->outer[row].left, b = state->x + state->outer[row].right;
    MMI32 c, d;
    if (!state->inner)
    {
        paint_interval(state, y, left, right, a, b);
        return;
    }

    /* XOR the framed regions by pairing the four sorted edge transitions. */
    c = state->x + state->frame_x + state->inner[row].left;
    d = state->x + state->frame_x + state->inner[row].right;
    order_edges(&a, &c);
    order_edges(&b, &d);
    order_edges(&b, &c);
    paint_interval(state, y, left, right, a, b);
    paint_interval(state, y, left, right, c, d);
}

int mm_pixel_fill_oval(const MMPixelSurface *surface, const MMPixelBrush *brush, const MMPixelRect *shape,
                       const MMPixelRect *clip, const struct MMRegion *region, MMI32 frame_x, MMI32 frame_y)
{
    MMPixelRect bounds;
    MMI32 width, height, first, count;
    int frame;
    size_t bytes;
    MMEllipseSpan *output;
    OvalPaint paint;
    if (!mm_pixel_surface_valid(surface) || !mm_pixel_rect_extent(shape, &width, &height) ||
        width > MM_ELLIPSE_MAX_EXTENT || height > MM_ELLIPSE_MAX_EXTENT)
        return 0;
    if (!mm_pixel_visible_rect(surface, shape, clip, &bounds))
        return 1;

    frame = frame_x >= 0 && frame_y >= 0 && 2 * (MMU32)frame_x < (MMU32)width && 2 * (MMU32)frame_y < (MMU32)height;
    first = bounds.top - shape->top;
    count = bounds.bottom - bounds.top;
    if (!span_storage(count, frame ? 2 : 1, &bytes))
        return 0;
    output = (MMEllipseSpan *)malloc(bytes);
    if (!output)
        return 0;
    if (!mm_ellipse_rows(width - frame, height - frame, first, count, output) ||
        (frame && !mm_ellipse_rows(width - 2 * frame_x - 1, height - 2 * frame_y - 1, first - frame_y, count,
                                   output + count)))
    {
        free(output);
        return 0;
    }

    paint.surface = surface;
    paint.brush = brush;
    paint.x = shape->left;
    paint.top = bounds.top;
    paint.frame_x = frame_x;
    paint.outer = output;
    paint.inner = frame ? output + count : NULL;
    mm_pixel_visit_spans(&bounds, region, paint_oval_span, &paint);
    free(output);
    return 1;
}

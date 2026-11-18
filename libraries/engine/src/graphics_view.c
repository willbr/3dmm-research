/* View geometry converted from Kauai gfx.cpp and gfx_port.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/graphics_view.h"
#include "mm/wide.h"

static const MMPixelRect empty = {0, 0, 0, 0};
static const MMPixelRect open_view = {-1073741824L, -1073741824L, 1073741823L, 1073741823L};
static const MMPixelRect unit = {0, 0, 1, 1};

static MMPixelPoint point(MMI32 x, MMI32 y)
{
    MMPixelPoint result;
    result.x = x;
    result.y = y;
    return result;
}

static MMI32 add(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a + (MMU32)b);
}

static MMI32 subtract(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a - (MMU32)b);
}

static int rect_empty(const MMPixelRect *rect)
{
    return rect->left >= rect->right || rect->top >= rect->bottom;
}

static MMPixelRect nonempty(MMPixelRect rect)
{
    if (rect_empty(&rect))
    {
        rect.right = add(rect.left, 1);
        rect.bottom = add(rect.top, 1);
    }
    return rect;
}

static MMPixelRect intersection(MMPixelRect rect, const MMPixelRect *clip)
{
    mm_pixel_rect_intersect(&rect, clip);
    return rect_empty(&rect) ? empty : rect;
}

static MMI32 scale(MMI32 value, MMU32 from, MMU32 to, int round_size)
{
    MMU32 magnitude, quotient;
    MMI32 result;
    if (!value || from == to)
        return value;
    magnitude = value < 0 ? 0 - (MMU32)value : (MMU32)value;
    quotient = mm_pixel_muldiv(magnitude, to, from);
    result = mm_signed_word(value < 0 ? 0 - quotient : quotient);
    /* The remainder fits in a word. The legacy size rounding uses the
     * truncated quotient's sign, so a nonintegral size truncated to 0 grows to 1. */
    if (round_size && magnitude * to != quotient * from)
        result = add(result, result < 0 ? -1 : 1);
    return result;
}

static MMI32 map(MMI32 value, MMI32 from_start, MMI32 from_end, MMI32 to_start, MMI32 to_end)
{
    return add(to_start, scale(subtract(value, from_start), (MMU32)from_end - (MMU32)from_start,
                               (MMU32)to_end - (MMU32)to_start, 0));
}

void mm_graphics_view_init(MMGraphicsView *view, MMPixelPoint port_base)
{
    view->source = unit;
    mm_graphics_view_set_destination(view, &unit, port_base);
}

void mm_graphics_view_set_source(MMGraphicsView *view, const MMPixelRect *rect)
{
    view->source = nonempty(*rect);
}

void mm_graphics_view_set_destination(MMGraphicsView *view, const MMPixelRect *rect, MMPixelPoint port_base)
{
    MMPixelRect dest = nonempty(*rect);
    dest.left = subtract(dest.left, port_base.x);
    dest.right = subtract(dest.right, port_base.x);
    dest.top = subtract(dest.top, port_base.y);
    dest.bottom = subtract(dest.bottom, port_base.y);
    view->destination = dest;
    mm_graphics_view_set_visible(view, NULL);
}

MMPixelRect mm_graphics_view_destination(const MMGraphicsView *view, MMPixelPoint port_base)
{
    MMPixelRect rect = view->destination;
    rect.left = add(rect.left, port_base.x);
    rect.right = add(rect.right, port_base.x);
    rect.top = add(rect.top, port_base.y);
    rect.bottom = add(rect.bottom, port_base.y);
    return rect;
}

MMPixelPoint mm_graphics_view_map_point(const MMGraphicsView *view, MMPixelPoint p)
{
    const MMPixelRect *from = &view->source, *to = &view->destination;
    return point(map(p.x, from->left, from->right, to->left, to->right),
                 map(p.y, from->top, from->bottom, to->top, to->bottom));
}

MMPixelRect mm_graphics_view_map_rect(const MMGraphicsView *view, MMPixelRect rect, int reverse)
{
    const MMPixelRect *from = reverse ? &view->destination : &view->source;
    const MMPixelRect *to = reverse ? &view->source : &view->destination;
    rect.left = map(rect.left, from->left, from->right, to->left, to->right);
    rect.right = map(rect.right, from->left, from->right, to->left, to->right);
    rect.top = map(rect.top, from->top, from->bottom, to->top, to->bottom);
    rect.bottom = map(rect.bottom, from->top, from->bottom, to->top, to->bottom);
    return rect;
}

static void publish_visible(MMGraphicsView *view, MMPixelRect visible, int has_clip)
{
    view->visible = view->clip = visible;
    view->has_clip = has_clip;
}

void mm_graphics_view_set_visible(MMGraphicsView *view, const MMPixelRect *rect)
{
    publish_visible(view, rect ? mm_graphics_view_map_rect(view, *rect, 0) : open_view, rect != NULL);
}

void mm_graphics_view_intersect_visible(MMGraphicsView *view, const MMPixelRect *rect)
{
    MMPixelRect mapped = mm_graphics_view_map_rect(view, *rect, 0);
    publish_visible(view, intersection(view->visible, &mapped), 1);
}

void mm_graphics_view_clip(MMGraphicsView *view, const MMPixelRect *rect)
{
    if (rect)
    {
        view->clip = intersection(mm_graphics_view_map_rect(view, *rect, 0), &view->visible);
        view->has_clip = 1;
    }
    else
    {
        view->clip = view->visible;
        view->has_clip = view->visible.left != open_view.left || view->visible.top != open_view.top ||
                         view->visible.right != open_view.right || view->visible.bottom != open_view.bottom;
    }
}

MMPixelPoint mm_graphics_view_pattern_origin(const MMGraphicsView *view)
{
    return point(subtract(view->destination.left, view->source.left),
                 subtract(view->destination.top, view->source.top));
}

MMPixelPoint mm_graphics_view_scale_size(const MMGraphicsView *view, MMPixelPoint size)
{
    const MMPixelRect *from = &view->source, *to = &view->destination;
    return point(scale(size.x, (MMU32)from->right - (MMU32)from->left, (MMU32)to->right - (MMU32)to->left, 1),
                 scale(size.y, (MMU32)from->bottom - (MMU32)from->top, (MMU32)to->bottom - (MMU32)to->top, 1));
}

static MMPixelRect expose_vertical(MMPixelRect area, MMI32 dy)
{
    if (dy > 0)
        area.bottom = add(area.top, dy);
    else
        area.top = add(area.bottom, dy);
    return area;
}

void mm_graphics_scroll_exposed(const MMPixelRect *rect, MMI32 dx, MMI32 dy, MMPixelRect *first, MMPixelRect *second)
{
    MMPixelRect area = *rect, strip;
    if (first)
    {
        strip = area;
        if (dx > 0)
            strip.right = add(area.left, dx);
        else if (dx < 0)
            strip.left = add(area.right, dx);
        else
            strip = expose_vertical(area, dy);
        *first = intersection(strip, &area);
    }
    if (second)
    {
        *second = empty;
        if (dx && dy)
        {
            strip = expose_vertical(area, dy);
            if (dx > 0)
                strip.left = add(strip.left, dx);
            else
                strip.right = add(strip.right, dx);
            *second = intersection(strip, &area);
        }
    }
}

void mm_graphics_line_polygon(MMPixelPoint first, MMPixelPoint last, MMPixelPoint pen, MMPixelPoint polygon[6])
{
    MMPixelPoint swap;
    MMI32 first_x, first_y, last_x, last_y;
    if (last.x < first.x)
    {
        swap = first;
        first = last;
        last = swap;
    }
    first_x = add(first.x, pen.x);
    first_y = add(first.y, pen.y);
    last_x = add(last.x, pen.x);
    last_y = add(last.y, pen.y);
    polygon[0] = first;
    if (subtract(last.y, first.y) < 0)
    {
        polygon[1] = point(first.x, first_y);
        polygon[2] = point(first_x, first_y);
        polygon[3] = point(last_x, last_y);
        polygon[4] = point(last_x, last.y);
        polygon[5] = last;
    }
    else
    {
        polygon[1] = point(first_x, first.y);
        polygon[2] = point(last_x, last.y);
        polygon[3] = point(last_x, last_y);
        polygon[4] = point(last.x, last_y);
        polygon[5] = point(first.x, first_y);
    }
}

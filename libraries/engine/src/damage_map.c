/* Pixel damage policy converted from Kauai damage_map.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/damage_map.h"
#include <stdlib.h>
#include <string.h>

static const MMPixelRect empty_rect = {0, 0, 0, 0};

int mm_damage_init(MMDamageMap *map, MMI32 width, MMI32 height)
{
    if (!mm_pixel_buffer_init(&map->mask, width, height, 1, 8))
        return 0;
    map->bounds = empty_rect;
    map->bounds_valid = 1;
    return 1;
}

int mm_damage_clone(MMDamageMap *target, const MMDamageMap *source)
{
    MMDamageMap copy;
    if (target == source)
        return 1;
    copy = *source;
    if (source->mask.surface.pixels)
    {
        copy.mask.surface.pixels = malloc(source->mask.bytes);
        if (!copy.mask.surface.pixels)
            return 0;
        memcpy(copy.mask.surface.pixels, source->mask.surface.pixels, source->mask.bytes);
    }
    mm_damage_reset(target);
    *target = copy;
    return 1;
}

void mm_damage_reset(MMDamageMap *map)
{
    mm_pixel_buffer_reset(&map->mask);
    map->bounds = empty_rect;
    map->bounds_valid = 0;
}

static unsigned char *damage_row(const MMPixelSurface *surface, MMI32 y)
{
    return (unsigned char *)surface->pixels + (size_t)y * (size_t)surface->pitch;
}

static void set_area(MMDamageMap *map, const MMPixelRect *area, int marked)
{
    MMI32 y;
    if (area->left >= area->right || area->top >= area->bottom)
        return;
    for (y = area->top; y < area->bottom; ++y)
        mm_pixel_mask_span(damage_row(&map->mask.surface, y), area->left, area->right, marked);
    map->bounds_valid = 0;
}

void mm_damage_set(MMDamageMap *map, const MMPixelRect *rectangle, int marked)
{
    const MMPixelSurface *surface = &map->mask.surface;
    MMPixelRect area;
    if (!surface->pixels)
        return;
    if (!rectangle && !marked)
    {
        memset(surface->pixels, 0, map->mask.bytes);
        map->bounds = empty_rect;
        map->bounds_valid = 1;
        return;
    }
    area.left = area.top = 0;
    area.right = surface->width;
    area.bottom = surface->height;
    if (rectangle)
        mm_pixel_rect_intersect(&area, rectangle);
    set_area(map, &area, marked);
}

static void find_bounds(MMDamageMap *map)
{
    const MMPixelSurface *surface = &map->mask.surface;
    const unsigned char *row;
    MMPixelRect out;
    MMI32 y, first, last, x, bytes;
    map->bounds_valid = 1;
    map->bounds = empty_rect;
    if (!surface->pixels)
        return;
    out.left = surface->width;
    out.top = surface->height;
    out.right = out.bottom = 0;
    bytes = (surface->width - 1) / 8 + 1;
    for (y = 0; y < surface->height; ++y)
    {
        row = damage_row(surface, y);
        first = 0;
        while (first < bytes && !row[first])
            ++first;
        if (first == bytes)
            continue;
        last = bytes - 1;
        while (!row[last])
            --last;
        x = first * 8;
        while (!mm_pixel_read(row, x, 1))
            ++x;
        if (x < out.left)
            out.left = x;
        x = last * 8 + 7;
        while (!mm_pixel_read(row, x, 1))
            --x;
        if (x >= out.right)
            out.right = x + 1;
        if (y < out.top)
            out.top = y;
        out.bottom = y + 1;
    }
    if (out.left < out.right)
        map->bounds = out;
}

int mm_damage_bounds(MMDamageMap *map, MMPixelRect *out)
{
    if (!map->bounds_valid)
        find_bounds(map);
    *out = map->bounds;
    return out->left < out->right && out->top < out->bottom;
}

int mm_damage_visit(const MMDamageMap *map, MMDamageEmit emit, void *context)
{
    const MMPixelSurface *surface = &map->mask.surface;
    const unsigned char *row;
    MMPixelRect part;
    MMI32 x;
    for (part.top = 0; part.top < surface->height; part.top = part.bottom)
    {
        row = damage_row(surface, part.top);
        part.bottom = part.top + 1;
        while (part.bottom < surface->height &&
               !memcmp(row, damage_row(surface, part.bottom), (size_t)surface->pitch))
            ++part.bottom;
        for (x = 0; x < surface->width;)
        {
            while (x < surface->width && !mm_pixel_read(row, x, 1))
                ++x;
            part.left = x;
            while (x < surface->width && mm_pixel_read(row, x, 1))
                ++x;
            part.right = x;
            if (part.left < part.right && !emit(context, &part))
                return 0;
        }
    }
    return 1;
}

int mm_damage_take(MMDamageMap *map, MMDamageEmit emit, void *context)
{
    if (!mm_damage_visit(map, emit, context))
        return 0;
    mm_damage_set(map, NULL, 0);
    return 1;
}

int mm_damage_scroll(MMDamageMap *map, const MMPixelRect *rectangle, MMI32 dx, MMI32 dy)
{
    MMPixelSurface *surface = &map->mask.surface;
    MMPixelRect area, pending, exposed;
    MMI32 width, height;
    if ((!dx && !dy) || !mm_pixel_visible_rect(surface, rectangle, NULL, &area) || !mm_damage_bounds(map, &pending))
        return 1;
    mm_pixel_rect_intersect(&pending, &area);
    if (pending.left >= pending.right || pending.top >= pending.bottom)
        return 1;

    width = area.right - area.left;
    height = area.bottom - area.top;
    if (dx <= -width || dx >= width || dy <= -height || dy >= height)
    {
        set_area(map, &area, 0);
        return 1;
    }
    if (!mm_pixel_scroll(surface, &area, dx, dy, NULL, NULL, 0))
        return 0;

    exposed = area;
    if (dy > 0)
        exposed.bottom = area.top + dy;
    else
        exposed.top = area.bottom + dy;
    set_area(map, &exposed, 0);
    exposed = area;
    if (dx > 0)
        exposed.right = area.left + dx;
    else
        exposed.left = area.right + dx;
    set_area(map, &exposed, 0);
    return 1;
}

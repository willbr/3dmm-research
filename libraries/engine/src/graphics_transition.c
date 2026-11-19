/* Transition control converted from Kauai gfx.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/graphics_transition.h"
#include "mm/indexed_display.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

#define MM_DISSOLVE_LIMIT 0x07ffffffUL

static MMI32 add(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a + (MMU32)b);
}

static MMI32 subtract(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a - (MMU32)b);
}

static int empty(const MMPixelRect *rect)
{
    return rect->left >= rect->right || rect->top >= rect->bottom;
}

static int equal(const MMPixelRect *a, const MMPixelRect *b)
{
    if (empty(a))
        return empty(b);
    return a->left == b->left && a->right == b->right && a->top == b->top && a->bottom == b->bottom;
}

static MMPixelRect orient(MMPixelRect rect, unsigned direction, int inverse)
{
    MMI32 swap;
    if (direction == 0 || (direction == 2 && inverse))
    {
        swap = rect.left;
        rect.left = subtract(0, rect.right);
        rect.right = subtract(0, swap);
    }
    if (direction == 2 && !inverse)
    {
        swap = rect.top;
        rect.top = subtract(0, rect.bottom);
        rect.bottom = subtract(0, swap);
    }
    if (direction >= 2)
    {
        swap = rect.top;
        rect.top = rect.left;
        rect.left = swap;
        swap = rect.bottom;
        rect.bottom = rect.right;
        rect.right = swap;
    }
    return rect;
}

MMU32 mm_graphics_transition_duration(MMU32 duration)
{
    return duration > 0 && duration < 30000 ? duration : 1000;
}

static MMU32 elapsed(const MMGraphicsTransitionServices *s, MMU32 start, MMU32 duration)
{
    MMU32 time = s->ticks(s->context) - start;
    return time < duration ? time : duration;
}

static MMI32 interpolate(MMI32 from, MMI32 to, MMU32 step, MMU32 total)
{
    MMI32 delta = subtract(to, from);
    MMU32 scaled = mm_pixel_muldiv(delta < 0 ? 0 - (MMU32)delta : (MMU32)delta, step, total);
    return mm_signed_word((MMU32)from + (delta < 0 ? 0 - scaled : scaled));
}

void mm_graphics_transition_palette(unsigned char *out, MMI32 count,
                                   const unsigned char *old, MMI32 old_count,
                                   const unsigned char *next, MMI32 next_count,
                                   const unsigned char *substitute, MMU32 step, MMU32 total)
{
    static const unsigned char black[4] = {0, 0, 0, 0};
    const unsigned char *a, *b;
    size_t offset;
    unsigned colour;
    if (old && old_count < count)
        count = old_count;
    if (next && next_count < count)
        count = next_count;
    if (!substitute)
        substitute = black;
    while (count > 0)
    {
        offset = (size_t)--count * 4;
        a = old ? old + offset : substitute;
        b = next ? next + offset : substitute;
        for (colour = 0; colour < 3; ++colour)
            out[offset + colour] = (unsigned char)interpolate(a[colour], b[colour], step, total);
        out[offset + 3] = a[3];
    }
}

static void strip_transition(const MMGraphicsTransition *t, const MMGraphicsTransitionServices *s,
                             unsigned direction, int fill, int palette, MMU32 duration)
{
    const int slide = t->kind == MM_GRAPHICS_SLIDE;
    const MMU32 start = s->ticks(s->context);
    const MMPixelRect source = orient(t->source, direction, 0);
    const MMPixelRect dest = orient(t->destination, direction, 0);
    const MMI32 width = subtract(dest.right, dest.left);
    MMPixelRect from, to;
    MMPixelPoint move;
    MMI32 x = 0, previous, delta;
    MMU32 time;
    while (x < width)
    {
        previous = x;
        time = elapsed(s, start, duration);
        x = (MMI32)mm_pixel_muldiv((MMU32)width, time, duration);
        delta = x - previous;
        if (slide && delta)
        {
            move.x = direction < 2 ? delta : 0;
            move.y = direction < 2 ? 0 : delta;
            if (direction == 0)
                move.x = -move.x;
            else if (direction == 2)
                move.y = -move.y;
            s->scroll(s->context, move);
            from = source;
            to = dest;
            from.left = subtract(from.right, x);
            from.right = subtract(from.right, previous);
            to.right = add(to.left, delta);
            from = orient(from, direction, 1);
            to = orient(to, direction, 1);
            s->draw(s->context, 1, fill, &from, &to);
        }
        if (palette)
            s->animate_palette(s->context, MM_GRAPHICS_PALETTE_BLEND, time, duration);
        if (slide)
        {
            if (delta)
            {
                s->present(s->context);
                s->flush(s->context);
            }
        }
        else
        {
            from = source;
            to = dest;
            from.left = add(source.left, previous);
            from.right = add(source.left, x);
            to.left = add(dest.left, previous);
            to.right = add(dest.left, x);
            if (!empty(&to))
            {
                from = orient(from, direction, 1);
                to = orient(to, direction, 1);
                s->draw(s->context, 0, fill, &from, &to);
                s->flush(s->context);
            }
        }
    }
}

static MMI32 bound(MMI32 value, MMI32 first, MMI32 last)
{
    return value < first ? first : value > last ? last : value;
}

static int iris_transition(const MMGraphicsTransitionServices *s, const MMPixelRect *dest, MMPixelPoint focus,
                           int close, int fill, int palette, MMU32 duration)
{
    MMPixelRect rect, previous;
    MMU32 start, time, extent;
    rect = *dest;
    if (!close)
    {
        rect.left = rect.right = focus.x;
        rect.top = rect.bottom = focus.y;
    }
    start = s->ticks(s->context);
    for (time = 0; time < duration;)
    {
        previous = rect;
        time = elapsed(s, start, duration);
        extent = close ? duration - time : time;
        rect.left = interpolate(focus.x, dest->left, extent, duration);
        rect.right = interpolate(focus.x, dest->right, extent, duration);
        rect.top = interpolate(focus.y, dest->top, extent, duration);
        rect.bottom = interpolate(focus.y, dest->bottom, extent, duration);
        if (palette)
            s->animate_palette(s->context, MM_GRAPHICS_PALETTE_BLEND, time, duration);
        if (!equal(&rect, &previous) && !s->iris(s->context, fill, close ? &previous : &rect, close ? &rect : &previous))
            return 0;
    }
    return 1;
}

static void fade_transition(const MMGraphicsTransition *t, const MMGraphicsTransitionServices *s, MMU32 duration)
{
    const MMU32 steps = t->fade_steps > 0 && t->fade_steps < 256 ? (MMU32)t->fade_steps : 256;
    MMU32 start, step, previous;
    int phase;
    if (!s->start_palette(s->context, 8))
    {
        s->draw(s->context, 0, 0, &t->source, &t->destination);
        s->flush(s->context);
        return;
    }
    s->flush(s->context);
    for (phase = 0; phase < 2; ++phase)
    {
        start = s->ticks(s->context);
        for (step = 0; step < steps;)
        {
            previous = step;
            step = mm_pixel_muldiv(steps, elapsed(s, start, duration), duration);
            if (step > previous)
                s->animate_palette(s->context, phase ? MM_GRAPHICS_PALETTE_FROM_FILL : MM_GRAPHICS_PALETTE_TO_FILL,
                                   step, steps);
        }
        if (!phase)
        {
            s->draw(s->context, 0, 0, &t->source, &t->destination);
            s->flush(s->context);
        }
    }
    s->set_palette(s->context, 1);
}

/* 32767 generates all 65536 nonzero residues modulo 65537. Each residue
 * visits its column of indices in reverse, retaining the legacy draw order. */
static MMU32 dissolve_next(MMU32 index, MMU32 count, MMU32 *word)
{
    MMU32 product;
    MMI32 remainder;
    if (index >= 65536UL)
        return index - 65536UL;
    do
    {
        product = *word * 32767UL;
        remainder = (MMI32)(product & 65535UL) - (MMI32)(product >> 16);
        *word = (MMU32)(remainder < 0 ? remainder + 65537L : remainder);
    } while (*word > count);
    return count - *word;
}

static MMU32 dissolve_word(MMU32 count, MMU32 initial_index)
{
    return (count - initial_index - 1) % 65536UL + 1;
}

MMGraphicsMovieTransition mm_graphics_movie_transition(MMI32 authored_kind)
{
    static const MMGraphicsMovieTransition effects[] = {
        {0, -1, 0}, {2000, 0, 4000}, {2000, 255, 4000}, {4000, -1, 4000}, {0, 0, 1000.0 / 6.0}
    };
    if (authored_kind < MM_GRAPHICS_MOVIE_CUT || authored_kind > MM_GRAPHICS_MOVIE_BLACK)
        authored_kind = MM_GRAPHICS_MOVIE_CUT;
    return effects[authored_kind];
}

int mm_graphics_movie_transition_indexed(MMI32 kind, int force_cut, void *context,
                                        int (*draw)(void *, MMGraphicsMovieIndexedOperation,
                                                    const MMGraphicsMovieTransition *))
{
    MMGraphicsMovieTransition effect;
    int status;
    if (force_cut)
        kind = MM_GRAPHICS_MOVIE_CUT;
    if (kind < MM_GRAPHICS_MOVIE_CUT || kind > MM_GRAPHICS_MOVIE_BLACK)
        return 0;
    effect = mm_graphics_movie_transition(kind);
    if (effect.dissolve_phase_ms)
        status = draw(context, MM_MOVIE_TRANSITION_DISSOLVE, &effect);
    else
    {
        status = draw(context, MM_MOVIE_TRANSITION_FILL_BLACK, &effect);
        if (status >= 0 && kind == MM_GRAPHICS_MOVIE_CUT)
        {
            status = draw(context, MM_MOVIE_TRANSITION_SET_PALETTE, &effect);
            if (status >= 0)
                status = draw(context, MM_MOVIE_TRANSITION_COPY, &effect);
        }
    }
    return status < 0 ? status : 1;
}

void mm_graphics_movie_snapshot_dispose(MMGraphicsMovieSnapshot *snapshot)
{
    free(snapshot->indices);
    snapshot->indices = NULL;
}

int mm_graphics_movie_snapshot_prepare(MMGraphicsMovieSnapshot *snapshot, MMI32 kind,
                                      const MMPixelSurface *outgoing, const unsigned char *palette,
                                      const MMGraphicsMoviePaletteSource *source)
{
    const MMGraphicsMovieTransition effect = mm_graphics_movie_transition(kind);
    MMGraphicsMovieSnapshot next;
    size_t count;
    if (!effect.duration_ms || !outgoing || outgoing->depth != 32 || !mm_pixel_surface_valid(outgoing) ||
        (MMU32)outgoing->height > MM_DISSOLVE_LIMIT / (MMU32)outgoing->width)
        return 0;
    memset(&next, 0, sizeof(next));
    next.indices = NULL;
    if (palette && effect.dissolve_phase_ms)
    {
        if (kind == MM_GRAPHICS_MOVIE_DISSOLVE && source && source->read &&
            source->read(source->context, next.incoming))
        {
            memcpy(next.target, palette, sizeof(next.target));
            if (source->overlay)
                source->overlay(source->context, next.target);
            if (memcmp(palette, next.target, sizeof(next.target)) ||
                memcmp(next.incoming, next.target, sizeof(next.target)))
            {
                count = (size_t)outgoing->width * (size_t)outgoing->height;
                next.indices = (unsigned short *)malloc(count * sizeof(next.indices[0]));
                if (!next.indices)
                    return -1;
                memcpy(next.outgoing, palette, sizeof(next.outgoing));
            }
        }
        if (!mm_pixel_remap_dissolve_rgba(outgoing, palette, 256, next.indices))
            mm_graphics_movie_snapshot_dispose(&next);
    }
    mm_graphics_movie_snapshot_dispose(snapshot);
    *snapshot = next;
    return 1;
}

int mm_graphics_movie_snapshot_render(const MMGraphicsMovieSnapshot *snapshot, MMI32 kind, double elapsed_ms,
                                     MMU32 seed, const MMPixelSurface *outgoing, const MMPixelSurface *incoming,
                                     const unsigned char *incoming_indices)
{
    MMGraphicsMovieDissolvePalette palette;
    if (!snapshot->indices)
        return mm_graphics_movie_transition_rgba(kind, elapsed_ms, seed, outgoing, incoming, NULL);
    palette.outgoing = snapshot->outgoing;
    palette.incoming = snapshot->incoming;
    palette.target = snapshot->target;
    palette.outgoing_indices = snapshot->indices;
    palette.incoming_indices = incoming_indices;
    return mm_graphics_movie_transition_rgba(kind, elapsed_ms, seed, outgoing, incoming, &palette);
}

static void show_palette_colour(unsigned char *pixel, const unsigned char *palette, int index)
{
    if (index >= 0 && index < 256)
    {
        const unsigned char *colour = palette + 4 * index;
        pixel[0] = colour[2];
        pixel[1] = colour[1];
        pixel[2] = colour[0];
    }
}

int mm_graphics_movie_transition_rgba(MMI32 authored_kind, double elapsed_ms, MMU32 seed,
                                      const MMPixelSurface *outgoing, const MMPixelSurface *incoming,
                                      const MMGraphicsMovieDissolvePalette *palette)
{
    const MMGraphicsMovieTransition effect = mm_graphics_movie_transition(authored_kind);
    const int indexed = authored_kind == MM_GRAPHICS_MOVIE_DISSOLVE && palette && palette->outgoing &&
                        palette->incoming && palette->target && palette->outgoing_indices;
    MMPixelPaletteLookup lookup;
    const unsigned char *old;
    unsigned char *next, *pixel;
    unsigned char fill[4] = {0, 0, 0, 255};
    unsigned char active[1024], blend[1024];
    size_t colours = 256;
    MMU32 width, height, count, index, word, reveal, i, x, y, old_x, old_y, rgb;
    int filling, fading, resized, colour;
    if (!(elapsed_ms >= 0))
        return 0;
    if (elapsed_ms >= effect.duration_ms)
        return 1;
    if (!incoming || incoming->depth != 32 || !mm_pixel_surface_valid(incoming) ||
        (MMU32)incoming->height > MM_DISSOLVE_LIMIT / (MMU32)incoming->width)
        return 0;
    width = (MMU32)incoming->width;
    height = (MMU32)incoming->height;
    next = incoming->pixels;
    if (!effect.dissolve_phase_ms)
    {
        for (y = 0; y < height; ++y)
            for (x = 0; x < width; ++x)
                memcpy(next + (size_t)y * (MMU32)incoming->pitch + 4 * x, fill, sizeof(fill));
        return 1;
    }
    fading = effect.fill >= 0;
    filling = fading && elapsed_ms < effect.dissolve_phase_ms;
    if (fading)
    {
        fill[0] = fill[1] = fill[2] = (unsigned char)effect.fill;
        if (!filling)
        {
            elapsed_ms -= effect.dissolve_phase_ms;
            seed = seed * 1566083941UL + 2531011UL;
        }
    }
    if ((!fading || filling) && (!outgoing || outgoing->depth != 32 || !mm_pixel_surface_valid(outgoing)))
        return 0;
    old = outgoing ? outgoing->pixels : NULL;
    resized = outgoing && (outgoing->width != incoming->width || outgoing->height != incoming->height);
    if (indexed)
    {
        mm_graphics_transition_palette(blend, 256, palette->outgoing, 256, palette->target, 256,
                                       NULL, (MMU32)elapsed_ms, effect.dissolve_phase_ms);
        memcpy(active, palette->outgoing, sizeof(active));
        mm_display_palette_set(active, &colours, blend, 256,
                               MM_DISPLAY_PALETTE_IDENTITY | MM_DISPLAY_PALETTE_ANIMATE);
        mm_pixel_palette_lookup(&lookup, palette->incoming, 256);
    }
    count = width * height;
    reveal = mm_pixel_muldiv(count, (MMU32)elapsed_ms, effect.dissolve_phase_ms);
    index = seed % count;
    word = dissolve_word(count, index);
    for (i = 0; i < count; ++i)
    {
        index = dissolve_next(index, count, &word);
        if (indexed || filling || i >= reveal)
        {
            x = index % width;
            y = index / width;
            pixel = next + (size_t)y * (MMU32)incoming->pitch + 4 * x;
            if ((filling && i < reveal) || (fading && !filling))
                memcpy(pixel, fill, sizeof(fill));
            else if (filling || i >= reveal)
            {
                old_x = resized ? (MMU32)mm_pixel_sample((MMI32)x, outgoing->width, incoming->width, 0) : x;
                old_y = resized ? (MMU32)mm_pixel_sample((MMI32)y, outgoing->height, incoming->height, 0) : y;
                memcpy(pixel, old + (size_t)old_y * (MMU32)outgoing->pitch + 4 * old_x, 4);
                if (indexed)
                    show_palette_colour(pixel, active,
                                        palette->outgoing_indices[(size_t)old_y * (MMU32)outgoing->width + old_x]);
            }
            else if (pixel[3])
            {
                /* Captions and true-colour renderers may replace a raster
                 * sample. Use its index only while its colour still matches. */
                rgb = ((MMU32)pixel[0] << 16) | ((MMU32)pixel[1] << 8) | pixel[2];
                colour = palette->incoming_indices ? palette->incoming_indices[index] : -1;
                if (colour < 0 || mm_pixel_rgb(palette->incoming + 4 * colour) != rgb)
                    colour = mm_pixel_palette_find(&lookup, rgb);
                show_palette_colour(pixel, active, colour);
            }
        }
    }
    return 1;
}

static void dissolve_cell(const MMGraphicsTransition *t, const MMGraphicsTransitionServices *s,
                          MMU32 width, MMU32 height, MMU32 columns, MMU32 rows, MMU32 index, int fill)
{
    const MMU32 x = index % columns, y = index / columns;
    const MMU32 left = mm_pixel_muldiv(width, x, columns), right = mm_pixel_muldiv(width, x + 1, columns);
    const MMU32 top = mm_pixel_muldiv(height, y, rows), bottom = mm_pixel_muldiv(height, y + 1, rows);
    MMPixelRect from = t->source, to;
    to.left = add(t->destination.left, (MMI32)left);
    to.right = add(t->destination.left, (MMI32)right);
    to.top = add(t->destination.top, (MMI32)top);
    to.bottom = add(t->destination.top, (MMI32)bottom);
    if (!fill)
    {
        from.left = interpolate(t->source.left, t->source.right, left, width);
        from.right = interpolate(t->source.left, t->source.right, right, width);
        from.top = interpolate(t->source.top, t->source.bottom, top, height);
        from.bottom = interpolate(t->source.top, t->source.bottom, bottom, height);
    }
    s->draw(s->context, 0, fill, &from, &to);
}

static int dissolve_pixels_valid(const MMGraphicsDissolvePixels *pixels, MMI32 width, MMI32 height, int has_source)
{
    const MMPixelSurface *dest = &pixels->destination, *source = &pixels->source;
    const MMPixelPoint origin = pixels->source_origin;
    if (dest->depth != 8 || dest->width != width || dest->height != height || !mm_pixel_surface_valid(dest))
        return 0;
    return !has_source || (source->depth == 8 && mm_pixel_surface_valid(source) &&
                           origin.x >= 0 && origin.x <= source->width - width &&
                           origin.y >= 0 && origin.y <= source->height - height);
}

static void dissolve_pixel(const MMGraphicsDissolvePixels *pixels, MMU32 index, int fill, unsigned char fill_index)
{
    unsigned char *dest = pixels->destination.pixels;
    const unsigned char *source = pixels->source.pixels;
    size_t row, column, pitch;
    if (fill)
        dest[index] = fill_index;
    else
    {
        pitch = (size_t)pixels->source.pitch;
        row = (size_t)pixels->source_origin.y + index / (MMU32)pixels->destination.pitch;
        column = (size_t)pixels->source_origin.x + index % (MMU32)pixels->destination.pitch;
        /* Padding participates in the permutation even if the corresponding
         * byte lies outside the source buffer. Padding never gets presented. */
        if (column < ((size_t)pixels->source.height - row) * pitch)
            dest[index] = source[row * pitch + column];
    }
}

static void dissolve_transition(const MMGraphicsTransition *t, const MMGraphicsTransitionServices *s, MMU32 duration)
{
    const int cells = t->dissolve_cells.x > 0 && t->dissolve_cells.y > 0;
    MMGraphicsDissolvePixels pixels = {0};
    MMPixelRect sample;
    unsigned char *first;
    unsigned char original, fill_index = 0;
    MMI32 width, height;
    MMU32 columns, rows, last_row, count, done, end, index, word, start, time;
    int phase, palette = t->palette, pixel_lock = 0;
    if (empty(&t->destination))
        return;
    if (!mm_pixel_rect_extent(&t->destination, &width, &height))
        goto fallback;
    if (cells)
    {
        columns = (MMU32)(width < t->dissolve_cells.x ? width : t->dissolve_cells.x);
        rows = (MMU32)(height < t->dissolve_cells.y ? height : t->dissolve_cells.y);
        last_row = columns;
    }
    else
    {
        pixel_lock = 1;
        if (!s->lock_dissolve(s->context, &pixels) || !dissolve_pixels_valid(&pixels, width, height, t->has_source))
            goto fallback;
        columns = (MMU32)pixels.destination.pitch;
        rows = (MMU32)height;
        last_row = (MMU32)width;
    }
    if (last_row > MM_DISSOLVE_LIMIT || rows - 1 > (MM_DISSOLVE_LIMIT - last_row) / columns)
        goto fallback;
    count = (rows - 1) * columns + last_row;
    if (!cells && t->fill)
    {
        sample = t->destination;
        sample.right = sample.left + 1;
        sample.bottom = sample.top + 1;
        first = pixels.destination.pixels;
        original = *first;
        s->draw(s->context, 1, 1, &t->source, &sample);
        s->flush(s->context);
        fill_index = *first;
        *first = original;
    }
    for (phase = 0; phase < 2; ++phase)
    {
        if (phase == 0 && !t->fill)
            continue;
        if (phase == 0 || t->has_source)
        {
            if (phase && palette && !s->start_palette(s->context, cells ? 8 : 0))
                palette = 0;
            index = s->random_below(s->context, count);
            word = dissolve_word(count, index);
            start = s->ticks(s->context);
            for (done = 0; done < count;)
            {
                time = elapsed(s, start, duration);
                end = mm_pixel_muldiv(count, time, duration);
                if (done < end)
                {
                    for (; done < end; ++done)
                    {
                        index = dissolve_next(index, count, &word);
                        if (cells)
                            dissolve_cell(t, s, (MMU32)width, (MMU32)height, columns, rows, index, !phase);
                        else
                            dissolve_pixel(&pixels, index, !phase, fill_index);
                    }
                    if (!cells)
                    {
                        s->present(s->context);
                        s->flush(s->context);
                        if (s->show_dissolve)
                            s->show_dissolve(s->context);
                    }
                }
                if (phase && palette)
                    s->animate_palette(s->context, MM_GRAPHICS_PALETTE_BLEND, time, duration);
            }
        }
        if (palette)
        {
            s->set_palette(s->context, 0);
            if (!cells && phase && s->depth(s->context) != 8)
            {
                s->present(s->context);
                s->flush(s->context);
                if (s->show_dissolve)
                    s->show_dissolve(s->context);
            }
            palette = 0;
        }
    }
    if (pixel_lock)
        s->unlock_dissolve(s->context);
    return;
fallback:
    if (pixel_lock)
        s->unlock_dissolve(s->context);
    if (palette)
        s->set_palette(s->context, 0);
    s->draw(s->context, 0, !t->has_source, &t->source, &t->destination);
    s->flush(s->context);
}

void mm_graphics_transition_run(const MMGraphicsTransition *t, const MMGraphicsTransitionServices *s)
{
    const MMU32 duration = mm_graphics_transition_duration(t->duration);
    const int slide = t->kind == MM_GRAPHICS_SLIDE, iris = t->kind == MM_GRAPHICS_IRIS;
    MMPixelRect dest = t->destination;
    MMPixelPoint focus = t->focus;
    int phase, palette = t->palette;
    if (t->kind == MM_GRAPHICS_DISSOLVE)
    {
        dissolve_transition(t, s, duration);
        return;
    }
    if (t->kind == MM_GRAPHICS_FADE)
    {
        fade_transition(t, s, duration);
        return;
    }
    if (!slide)
        s->flush(s->context);
    if ((slide || iris) && !s->prepare(s->context, t->kind))
        goto fallback;
    if (iris)
    {
        focus.x = bound(focus.x, dest.left, dest.right);
        focus.y = bound(focus.y, dest.top, dest.bottom);
        focus = mm_graphics_view_map_point(t->view, focus);
        focus.x = add(focus.x, t->port_base.x);
        focus.y = add(focus.y, t->port_base.y);
        dest = mm_graphics_view_map_rect(t->view, dest, 0);
        dest.left = add(dest.left, t->port_base.x);
        dest.right = add(dest.right, t->port_base.x);
        dest.top = add(dest.top, t->port_base.y);
        dest.bottom = add(dest.bottom, t->port_base.y);
    }
    for (phase = 0; phase < 2; ++phase)
    {
        if (!phase && !t->fill)
            continue;
        if (phase && palette && !s->start_palette(s->context, slide ? 0 : 8))
            palette = 0;
        if (iris)
        {
            if (!iris_transition(s, &dest, focus, (t->directions & (1U << phase)) != 0,
                                 !phase, phase && palette, duration))
                goto fallback;
        }
        else
            strip_transition(t, s, (t->directions >> (2 * phase)) & 3, !phase, phase && palette, duration);
        if (palette)
        {
            s->set_palette(s->context, 0);
            if (slide && phase && s->depth(s->context) != 8)
            {
                s->present(s->context);
                s->flush(s->context);
            }
            palette = 0;
        }
    }
    return;
fallback:
    if (palette)
        s->set_palette(s->context, 0);
    s->draw(s->context, 0, 0, &t->source, &t->destination);
    if (slide)
        s->flush(s->context);
}

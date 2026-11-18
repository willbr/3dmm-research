/* Transition control converted from Kauai gfx.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/graphics_transition.h"
#include "mm/wide.h"

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

void mm_graphics_transition_run(const MMGraphicsTransition *t, const MMGraphicsTransitionServices *s)
{
    const MMU32 duration = mm_graphics_transition_duration(t->duration);
    const int slide = t->kind == MM_GRAPHICS_SLIDE, iris = t->kind == MM_GRAPHICS_IRIS;
    MMPixelRect dest = t->destination;
    MMPixelPoint focus = t->focus;
    int phase, palette = t->palette;
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

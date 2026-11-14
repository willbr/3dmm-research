/* MIDI render policy converted from Kauai midi_render.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/midi_render.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
struct ThreeMMMidiRenderBuffer
{
    ThreeMMMidiStreamEvent *data;
    size_t tag, count, index;
    ThreeMMI32 repeats;
    int delay_loaded;
    ThreeMMMidiRenderBuffer *next;
};
static void *allocate(void *p, size_t n)
{
    (void)p;
    return malloc(n);
}
static void release(void *p, void *v)
{
    (void)p;
    free(v);
}
static void increment(ThreeMMAudioCounter *p)
{
    *p = three_mm_audio_count_add(*p, three_mm_audio_counter(1, 0));
}
static ThreeMMAudioCounter multiply(ThreeMMAudioCounter a, ThreeMMU32 n)
{
    ThreeMMAudioCounter p = three_mm_audio_count_multiply(a.low, n);
    p.high += a.high * n;
    return p;
}
static ThreeMMAudioCounter delay(ThreeMMMidiRenderer *r, ThreeMMU32 ticks)
{
    ThreeMMAudioCounter duration, whole, rem;
    duration = three_mm_audio_count_multiply(ticks, r->tempo);
    whole = three_mm_audio_count_divide(duration, three_mm_audio_counter(1000000000, 0), &rem);
    rem = three_mm_audio_count_add(multiply(rem, r->rate), r->fraction);
    duration = three_mm_audio_count_divide(rem, three_mm_audio_counter(1000000000, 0), &r->fraction);
    return three_mm_audio_count_add(multiply(whole, r->rate), duration);
}
static void return_head(ThreeMMMidiRenderer *r)
{
    ThreeMMMidiRenderBuffer *b = r->head;
    r->head = b->next;
    if (!r->head)
        r->tail = NULL;
    b->next = NULL;
    if (r->returned_tail)
        r->returned_tail->next = b;
    else
        r->returned = b;
    r->returned_tail = b;
    increment(&r->completed);
}
void three_mm_midi_render_clock_reset(ThreeMMMidiRenderer *r, ThreeMMU32 rate)
{
    r->wait_frames = r->fraction = three_mm_audio_counter(0, 0);
    r->tempo = 1000000;
    r->rate = rate;
}
void three_mm_midi_render_init(ThreeMMMidiRenderer *r, const ThreeMMMidiRenderServices *s,
                               const ThreeMMAudioAllocator *a)
{
    memset(r, 0, sizeof(*r));
    r->services = *s;
    if (a)
        r->allocator = *a;
    else
    {
        r->allocator.allocate = allocate;
        r->allocator.release = release;
    }
    r->volume = 1;
    three_mm_midi_render_clock_reset(r, 48000);
}
void three_mm_midi_render_dispose(ThreeMMMidiRenderer *r)
{
    void *data;
    size_t tag;
    while (r->head)
        return_head(r);
    while (three_mm_midi_render_pop(r, &data, &tag))
    {
    }
}
int three_mm_midi_render_queue(ThreeMMMidiRenderer *r, ThreeMMMidiStreamEvent *data, ThreeMMI32 bytes,
                               ThreeMMI32 offset, ThreeMMI32 repeats, size_t tag)
{
    ThreeMMMidiRenderBuffer *b;
    size_t count, i;
    int timed = 0;
    ThreeMMU32 type;
    if (!data || bytes <= 0 || offset < 0 || offset >= bytes || bytes % sizeof(*data) || offset % sizeof(*data))
        return 0;
    count = (size_t)bytes / sizeof(*data);
    for (i = 0; i < count; ++i)
    {
        type = data[i].eventWord >> 24;
        if (data[i].streamId || type > 2 || (type == 1 && !(data[i].eventWord & 0xffffffUL)))
            return 0;
        if (data[i].deltaTicks)
            timed = 1;
    }
    if (repeats != 1 && !timed)
        return 0;
    b = (ThreeMMMidiRenderBuffer *)r->allocator.allocate(r->allocator.context, sizeof(*b));
    if (!b)
        return 0;
    if (!r->services.ready(r->services.context))
    {
        r->allocator.release(r->allocator.context, b);
        return 0;
    }
    memset(b, 0, sizeof(*b));
    b->data = data;
    b->tag = tag;
    b->count = count;
    b->index = (size_t)offset / sizeof(*data);
    b->repeats = repeats;
    if (r->tail)
        r->tail->next = b;
    else
        r->head = b;
    r->tail = b;
    increment(&r->queued);
    return 1;
}
int three_mm_midi_render(ThreeMMMidiRenderer *r, float *out, size_t frames)
{
    size_t requested = frames, count, i;
    ThreeMMMidiRenderBuffer *b;
    ThreeMMMidiStreamEvent *event;
    ThreeMMU32 type;
    float left, right;
    if ((!out && frames) || frames > (size_t)-1 / (2 * sizeof(float)))
        return 0;
    for (;;)
    {
        while (r->head && !r->wait_frames.low && !r->wait_frames.high)
        {
            b = r->head;
            if (b->index == b->count)
            {
                if (b->repeats == 1)
                    return_head(r);
                else
                {
                    if (b->repeats > 0)
                        --b->repeats;
                    b->index = 0;
                    b->delay_loaded = 0;
                }
                continue;
            }
            event = b->data + b->index;
            if (!b->delay_loaded)
            {
                r->wait_frames = delay(r, event->deltaTicks);
                b->delay_loaded = 1;
                if (r->wait_frames.low || r->wait_frames.high)
                    break;
            }
            type = event->eventWord >> 24;
            if (type == 1)
                r->tempo = event->eventWord & 0xffffffUL;
            else if (type == 0)
            {
                r->services.message(r->services.context, event->eventWord);
                increment(&r->messages);
                if ((event->eventWord & 0xf0) == 0x90 && (event->eventWord & 0x7f0000UL))
                    increment(&r->notes);
            }
            ++b->index;
            b->delay_loaded = 0;
        }
        if (!frames)
            break;
        count = frames;
        if ((r->wait_frames.low || r->wait_frames.high) &&
            three_mm_audio_count_compare(r->wait_frames, three_mm_audio_count_size(frames)) < 0)
            count = three_mm_audio_count_to_size(r->wait_frames);
        r->services.render(r->services.context, out, count);
        for (i = 0; i < count; ++i)
        {
            left = out[2 * i];
            right = out[2 * i + 1];
            left = left >= -FLT_MAX && left <= FLT_MAX ? left * r->volume : 0;
            right = right >= -FLT_MAX && right <= FLT_MAX ? right * r->volume : 0;
            out[2 * i] = left < 1 ? (left < -1 ? -1 : left) : 1;
            out[2 * i + 1] = right < 1 ? (right < -1 ? -1 : right) : 1;
            if (out[2 * i] != 0 || out[2 * i + 1] != 0)
                increment(&r->audible_frames);
        }
        frames -= count;
        out += count * 2;
        if (r->wait_frames.low || r->wait_frames.high)
            r->wait_frames = three_mm_audio_count_sub(r->wait_frames, three_mm_audio_count_size(count));
    }
    r->frames = three_mm_audio_count_add(r->frames, three_mm_audio_count_size(requested));
    return r->returned != NULL;
}
void three_mm_midi_render_stop(ThreeMMMidiRenderer *r)
{
    while (r->head)
        return_head(r);
    r->services.silence(r->services.context);
    three_mm_midi_render_clock_reset(r, r->rate);
}
int three_mm_midi_render_pop(ThreeMMMidiRenderer *r, void **data, size_t *tag)
{
    ThreeMMMidiRenderBuffer *b = r->returned;
    if (!b)
        return 0;
    r->returned = b->next;
    if (!r->returned)
        r->returned_tail = NULL;
    *data = b->data;
    *tag = b->tag;
    r->allocator.release(r->allocator.context, b);
    return 1;
}
int three_mm_midi_render_reset(ThreeMMMidiRenderer *r)
{
    if (r->head || r->returned)
        return 0;
    r->services.reset(r->services.context);
    three_mm_midi_render_clock_reset(r, r->rate);
    return 1;
}
void three_mm_midi_render_idle(ThreeMMMidiRenderer *r)
{
    if (!r->head)
        r->services.silence(r->services.context);
}
void three_mm_midi_render_volume(ThreeMMMidiRenderer *r, float f)
{
    r->volume = f >= -FLT_MAX && f <= FLT_MAX ? (f < 0 ? 0 : f > 1 ? 1 : f) : 0;
}

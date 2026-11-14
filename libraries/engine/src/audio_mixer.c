/* Mixer policy converted from Kauai audio_mixer.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/audio_mixer.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
typedef struct Playing
{
    ThreeMMAudioVoice voice;
    ThreeMMAudioCounter position;
    int done, started, grouped;
} Playing;
struct ThreeMMAudioQueue
{
    ThreeMMAudioQueue *next;
    ThreeMMAudioCounter id;
    Playing *voices;
    size_t count, head;
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
static int finite_volume(float f)
{
    return f >= 0 && f <= FLT_MAX;
}
static ThreeMMAudioQueue *find(const ThreeMMAudioMixer *m, ThreeMMAudioCounter id)
{
    ThreeMMAudioQueue *q;
    for (q = m->queues; q; q = q->next)
        if (!three_mm_audio_count_compare(q->id, id))
            return q;
    return NULL;
}
void three_mm_audio_mixer_init(ThreeMMAudioMixer *m, const ThreeMMAudioAllocator *a)
{
    memset(m, 0, sizeof(*m));
    if (a)
        m->allocator = *a;
    else
    {
        m->allocator.allocate = allocate;
        m->allocator.release = release;
    }
    m->next.low = 1;
    m->volume = 1;
}
void three_mm_audio_mixer_dispose(ThreeMMAudioMixer *m)
{
    while (m->queues)
        three_mm_audio_mixer_remove(m, m->queues->id);
}
ThreeMMAudioCounter three_mm_audio_mixer_add(ThreeMMAudioMixer *m)
{
    ThreeMMAudioQueue *q, **tail;
    ThreeMMAudioCounter id = m->next;
    if (!id.low && !id.high)
        return id;
    /* Original allocation failure consumes the attempted queue identifier. */
    m->next = three_mm_audio_count_add(id, three_mm_audio_counter(1, 0));
    q = (ThreeMMAudioQueue *)m->allocator.allocate(m->allocator.context, sizeof(*q));
    if (!q)
        return three_mm_audio_counter(0, 0);
    memset(q, 0, sizeof(*q));
    q->id = id;
    for (tail = &m->queues; *tail; tail = &(*tail)->next)
    {
    }
    *tail = q;
    return id;
}
void three_mm_audio_mixer_remove(ThreeMMAudioMixer *m, ThreeMMAudioCounter id)
{
    ThreeMMAudioQueue **link, *q;
    for (link = &m->queues; *link; link = &(*link)->next)
        if (!three_mm_audio_count_compare((*link)->id, id))
        {
            q = *link;
            *link = q->next;
            if (q->voices)
                m->allocator.release(m->allocator.context, q->voices);
            m->allocator.release(m->allocator.context, q);
            return;
        }
}
int three_mm_audio_mixer_sync(ThreeMMAudioMixer *m, ThreeMMAudioCounter id, const ThreeMMAudioVoice *v, size_t n)
{
    ThreeMMAudioQueue *q = find(m, id);
    Playing *p = NULL;
    size_t i, j;
    if (!q || (n && !v) || n > (size_t)-1 / sizeof(*p))
        return 0;
    if (n)
    {
        p = (Playing *)m->allocator.allocate(m->allocator.context, n * sizeof(*p));
        if (!p)
            return 0;
    }
    for (i = 0; i < n; ++i)
    {
        if (!v[i].id || v[i].plays < 0 || !v[i].sample || (v[i].frames && !v[i].stereo) || !finite_volume(v[i].volume))
            goto fail;
        for (j = 0; j < i; ++j)
            if (v[j].id == v[i].id)
                goto fail;
        memset(p + i, 0, sizeof(*p));
        for (j = 0; j < q->count; ++j)
            if (q->voices[j].voice.id == v[i].id)
                break;
        if (j < q->count)
        {
            if (q->voices[j].voice.sample != v[i].sample || q->voices[j].voice.plays != v[i].plays ||
                q->voices[j].voice.offset_ms != v[i].offset_ms)
                goto fail;
            p[i] = q->voices[j];
        }
        else
        {
            p[i].position = three_mm_audio_count_multiply(v[i].offset_ms, 48);
            p[i].grouped = m->groups > 0;
        }
        p[i].voice = v[i];
    }
    if (q->voices)
        m->allocator.release(m->allocator.context, q->voices);
    q->voices = p;
    q->count = n;
    q->head = 0;
    return 1;
fail:
    if (p)
        m->allocator.release(m->allocator.context, p);
    return 0;
}
int three_mm_audio_mixer_done(const ThreeMMAudioMixer *m, ThreeMMAudioCounter id, ThreeMMI32 voice)
{
    ThreeMMAudioQueue *q = find(m, id);
    size_t i;
    if (!q)
        return 1;
    for (i = 0; i < q->count; ++i)
        if (q->voices[i].voice.id == voice)
            return q->voices[i].done || q->voices[i].voice.stopped;
    return 0;
}
void three_mm_audio_mixer_volume(ThreeMMAudioMixer *m, float v)
{
    if (finite_volume(v))
        m->volume = v;
}
void three_mm_audio_mixer_suspend(ThreeMMAudioMixer *m, int b)
{
    m->suspended = b;
}
void three_mm_audio_mixer_begin(ThreeMMAudioMixer *m)
{
    ++m->groups;
}
void three_mm_audio_mixer_end(ThreeMMAudioMixer *m)
{
    ThreeMMAudioQueue *q;
    size_t i;
    if (m->groups && --m->groups == 0)
        for (q = m->queues; q; q = q->next)
            for (i = 0; i < q->count; ++i)
                q->voices[i].grouped = 0;
}
static void complete(ThreeMMAudioMixer *m, Playing *p)
{
    if (!p->done)
    {
        p->done = 1;
        m->completed = three_mm_audio_count_add(m->completed, three_mm_audio_counter(1, 0));
    }
}
void three_mm_audio_mixer_render(ThreeMMAudioMixer *m, float *out, size_t frames)
{
    ThreeMMAudioQueue *q;
    Playing *p;
    ThreeMMAudioCounter loops, rest;
    size_t i, offset, position, take, count;
    float volume;
    if (!out || frames > (size_t)-1 / (2 * sizeof(float)))
        return;
    for (i = 0; i < frames * 2; ++i)
        out[i] = 0;
    if (m->suspended)
        return;
    for (q = m->queues; q; q = q->next)
    {
        offset = 0;
        while (offset < frames && q->head < q->count)
        {
            p = q->voices + q->head;
            count = p->voice.frames;
            loops = three_mm_audio_count_divide(p->position, three_mm_audio_count_size(count), &rest);
            if (p->done || p->voice.stopped || !count ||
                (p->voice.plays &&
                 three_mm_audio_count_compare(loops, three_mm_audio_counter((ThreeMMU32)p->voice.plays, 0)) >= 0))
            {
                complete(m, p);
                ++q->head;
                continue;
            }
            if (p->voice.paused || p->grouped)
                break;
            if (!p->started)
            {
                p->started = 1;
                m->started = three_mm_audio_count_add(m->started, three_mm_audio_counter(1, 0));
            }
            position = three_mm_audio_count_to_size(rest);
            take = frames - offset < count - position ? frames - offset : count - position;
            volume = p->voice.volume * m->volume;
            for (i = 0; i < take * 2; ++i)
                out[offset * 2 + i] += p->voice.stereo[position * 2 + i] * volume;
            p->position = three_mm_audio_count_add(p->position, three_mm_audio_count_size(take));
            offset += take;
            loops = three_mm_audio_count_divide(p->position, three_mm_audio_count_size(count), NULL);
            if (p->voice.plays &&
                three_mm_audio_count_compare(loops, three_mm_audio_counter((ThreeMMU32)p->voice.plays, 0)) >= 0)
            {
                complete(m, p);
                ++q->head;
            }
        }
    }
    /* Match std::max(-1, std::min(1, value)), including NaN's result of +1. */
    for (i = 0; i < frames * 2; ++i)
        out[i] = out[i] < 1 ? (out[i] < -1 ? -1 : out[i]) : 1;
    m->rendered_frames = three_mm_audio_count_add(m->rendered_frames, three_mm_audio_count_size(frames));
}

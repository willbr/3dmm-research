/* Mixer policy converted from Kauai audio_mixer.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/audio_mixer.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
typedef struct Playing
{
    MMAudioVoice voice;
    MMAudioCounter position;
    int done, started, grouped;
} Playing;
struct MMAudioQueue
{
    MMAudioQueue *next;
    MMAudioCounter id;
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
static MMAudioQueue *find(const MMAudioMixer *m, MMAudioCounter id)
{
    MMAudioQueue *q;
    for (q = m->queues; q; q = q->next)
        if (!mm_audio_count_compare(q->id, id))
            return q;
    return NULL;
}
void mm_audio_mixer_init(MMAudioMixer *m, const MMAudioAllocator *a)
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
void mm_audio_mixer_dispose(MMAudioMixer *m)
{
    while (m->queues)
        mm_audio_mixer_remove(m, m->queues->id);
}
MMAudioCounter mm_audio_mixer_add(MMAudioMixer *m)
{
    MMAudioQueue *q, **tail;
    MMAudioCounter id = m->next;
    if (!id.low && !id.high)
        return id;
    /* Original allocation failure consumes the attempted queue identifier. */
    m->next = mm_audio_count_add(id, mm_audio_counter(1, 0));
    q = (MMAudioQueue *)m->allocator.allocate(m->allocator.context, sizeof(*q));
    if (!q)
        return mm_audio_counter(0, 0);
    memset(q, 0, sizeof(*q));
    q->id = id;
    for (tail = &m->queues; *tail; tail = &(*tail)->next)
    {
    }
    *tail = q;
    return id;
}
void mm_audio_mixer_remove(MMAudioMixer *m, MMAudioCounter id)
{
    MMAudioQueue **link, *q;
    for (link = &m->queues; *link; link = &(*link)->next)
        if (!mm_audio_count_compare((*link)->id, id))
        {
            q = *link;
            *link = q->next;
            if (q->voices)
                m->allocator.release(m->allocator.context, q->voices);
            m->allocator.release(m->allocator.context, q);
            return;
        }
}
int mm_audio_mixer_sync(MMAudioMixer *m, MMAudioCounter id, const MMAudioVoice *v, size_t n)
{
    MMAudioQueue *q = find(m, id);
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
            p[i].position = mm_audio_count_multiply(v[i].offset_ms, 48);
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
int mm_audio_mixer_done(const MMAudioMixer *m, MMAudioCounter id, MMI32 voice)
{
    MMAudioQueue *q = find(m, id);
    size_t i;
    if (!q)
        return 1;
    for (i = 0; i < q->count; ++i)
        if (q->voices[i].voice.id == voice)
            return q->voices[i].done || q->voices[i].voice.stopped;
    return 0;
}
void mm_audio_mixer_volume(MMAudioMixer *m, float v)
{
    if (finite_volume(v))
        m->volume = v;
}
void mm_audio_mixer_suspend(MMAudioMixer *m, int b)
{
    m->suspended = b;
}
void mm_audio_mixer_begin(MMAudioMixer *m)
{
    ++m->groups;
}
void mm_audio_mixer_end(MMAudioMixer *m)
{
    MMAudioQueue *q;
    size_t i;
    if (m->groups && --m->groups == 0)
        for (q = m->queues; q; q = q->next)
            for (i = 0; i < q->count; ++i)
                q->voices[i].grouped = 0;
}
static void complete(MMAudioMixer *m, Playing *p)
{
    if (!p->done)
    {
        p->done = 1;
        m->completed = mm_audio_count_add(m->completed, mm_audio_counter(1, 0));
    }
}
void mm_audio_mix_stereo(float *out, const float *in, size_t frames, float volume)
{
    size_t i;
    for (i = 0; i < frames * 2; ++i)
        out[i] += in[i] * volume;
}
void mm_audio_clip_stereo(float *out, size_t frames)
{
    size_t i;
    /* Match std::max(-1, std::min(1, value)), including NaN's result of +1. */
    for (i = 0; i < frames * 2; ++i)
        out[i] = out[i] < 1 ? (out[i] < -1 ? -1 : out[i]) : 1;
}
void mm_audio_mixer_render(MMAudioMixer *m, float *out, size_t frames)
{
    MMAudioQueue *q;
    Playing *p;
    MMAudioCounter loops, rest;
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
            loops = mm_audio_count_divide(p->position, mm_audio_count_size(count), &rest);
            if (p->done || p->voice.stopped || !count ||
                (p->voice.plays &&
                 mm_audio_count_compare(loops, mm_audio_counter((MMU32)p->voice.plays, 0)) >= 0))
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
                m->started = mm_audio_count_add(m->started, mm_audio_counter(1, 0));
            }
            position = mm_audio_count_to_size(rest);
            take = frames - offset < count - position ? frames - offset : count - position;
            volume = p->voice.volume * m->volume;
            mm_audio_mix_stereo(out + offset * 2, p->voice.stereo + position * 2, take, volume);
            p->position = mm_audio_count_add(p->position, mm_audio_count_size(take));
            offset += take;
            loops = mm_audio_count_divide(p->position, mm_audio_count_size(count), NULL);
            if (p->voice.plays &&
                mm_audio_count_compare(loops, mm_audio_counter((MMU32)p->voice.plays, 0)) >= 0)
            {
                complete(m, p);
                ++q->head;
            }
        }
    }
    mm_audio_clip_stereo(out, frames);
    m->rendered_frames = mm_audio_count_add(m->rendered_frames, mm_audio_count_size(frames));
}

/* Stream arbitration converted from Kauai mididev2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/midi_mix.h"
#include "mm/wide.h"
#include <string.h>
#define MINIMUM (-2147483647 - 1)
void mm_midi_queue_run(const MMMidiQueueServices *s, MMI32 first)
{
    const MMSoundQueueServices *q = &s->entries;
    MMSoundEntry e;
    MMI32 i;
    q->enter(q->context);
    i = q->current(q->context);
    if (i == first)
    {
        for (; i < q->count(q->context); ++i)
        {
            q->set_current(q->context, i);
            q->get(q->context, i, &e);
            if (e.pause > 0)
                break;
            if (e.pause == 0 && s->play(s->context, &e))
            {
                s->set_start(s->context, s->clock(s->context) - e.offset);
                q->leave(q->context);
                return;
            }
        }
        q->set_current(q->context, i);
        s->play(s->context, NULL);
    }
    q->leave(q->context);
}
void mm_midi_queue_pause(const MMMidiQueueServices *s, MMI32 first)
{
    const MMSoundQueueServices *q = &s->entries;
    MMSoundEntry e;
    q->enter(q->context);
    if (q->current(q->context) == first && q->count(q->context) > first)
    {
        q->get(q->context, first, &e);
        e.offset = s->clock(s->context) - s->start(s->context);
        q->put(q->context, first, &e);
        mm_midi_queue_run(s, first);
    }
    q->leave(q->context);
}
void mm_midi_queue_notify(const MMMidiQueueServices *s, void *sound)
{
    const MMSoundQueueServices *q = &s->entries;
    MMSoundEntry e;
    MMI32 i;
    q->enter(q->context);
    i = q->current(q->context);
    if (q->count(q->context) > i)
    {
        q->get(q->context, i, &e);
        if (e.object == sound)
        {
            q->set_current(q->context, ++i);
            mm_midi_queue_run(s, i);
        }
    }
    q->leave(q->context);
}
void mm_midi_mix_stop(MMMidiMixState *m, const MMMidiMixServices *s)
{
    if (!m->playing)
        return;
    m->playing = 0;
    s->stop_device(s->context);
    m->waiting = 1;
    while (m->outstanding > 0)
        s->wait_return(s->context);
    m->waiting = 0;
}
void mm_midi_mix_volume(MMMidiMixState *m, const MMMidiMixServices *s, MMI32 volume)
{
    MMAudioCounter n;
    m->base_volume = volume;
    n = mm_audio_count_multiply((MMU32)volume, (MMU32)m->sound_volume);
    s->volume(s->context, mm_signed_word((n.high << 16) | (n.low >> 16)));
}
void mm_midi_mix_suspend(MMMidiMixState *m, const MMMidiMixServices *s, int suspend)
{
    if (suspend)
        mm_midi_mix_stop(m, s);
    if (!s->activate(s->context, !suspend) && !suspend)
        s->busy(s->context);
    mm_midi_mix_restart(m, s, 0);
}
int mm_midi_mix_play(MMMidiMixState *m, const MMMidiMixServices *s, const MMMidiMixVoice *input)
{
    MMMidiMixVoice v;
    MMI32 i;
    int fresh = 0, result = 1;
    if (input->sound && !s->duration(s->context, input->sound))
        return 0;
    for (i = s->count(s->context); i-- > 0;)
    {
        s->get(s->context, i, &v);
        if (v.queue == input->queue)
        {
            if (i == 0)
                mm_midi_mix_stop(m, s);
            s->erase(s->context, i);
            break;
        }
    }
    if (input->sound)
    {
        for (i = 0; i < s->count(s->context); ++i)
        {
            s->get(s->context, i, &v);
            if (v.priority < input->priority || (v.priority == input->priority && v.id < input->id))
                break;
        }
        if (!i)
        {
            fresh = 1;
            mm_midi_mix_stop(m, s);
        }
        v = *input;
        v.duration = s->duration(s->context, input->sound);
        v.start = s->clock(s->context) - v.offset;
        if (!s->insert(s->context, i, &v))
        {
            result = 0;
            fresh = 0;
        }
    }
    mm_midi_mix_restart(m, s, fresh);
    return result;
}
void mm_midi_mix_restart(MMMidiMixState *m, const MMMidiMixServices *s, int fresh)
{
    MMMidiMixVoice v;
    MMU32 now;
    if (s->active(s->context) && !m->playing && s->count(s->context) > 0)
    {
        now = s->clock(s->context);
        if (fresh)
        {
            s->get(s->context, 0, &v);
            v.start = now - v.offset;
            s->put(s->context, 0, &v);
        }
        mm_midi_mix_submit(m, s, now);
    }
    s->signal_worker(s->context);
}
void mm_midi_mix_submit(MMMidiMixState *m, const MMMidiMixServices *s, MMU32 now)
{
    MMMidiMixVoice v;
    MMI32 i, skip, bytes, loops;
    MMU32 seek;
    void *data;
    for (i = 0; i < s->count(s->context); ++i)
    {
        s->get(s->context, i, &v);
        loops = mm_signed_word((now - v.start) / v.duration);
        if (loops > 0)
        {
            if (v.plays > 0)
            {
                v.plays = mm_signed_word((MMU32)v.plays - (MMU32)loops);
                if (v.plays <= 0)
                    goto next;
            }
            seek = (now - v.start) % v.duration;
            v.start = now - seek;
            s->put(s->context, i, &v);
        }
        m->sound_volume = v.volume;
        mm_midi_mix_volume(m, s, m->base_volume);
        skip = 0;
        if (now != v.start)
        {
            if (!s->seek(s->context, v.sound, now - v.start, &skip))
                goto next;
            data = s->lock_key(s->context, &bytes);
            if (bytes > 0)
            {
                if (!s->submit(s->context, data, bytes, 0, 1, NULL))
                {
                    s->unlock_key(s->context);
                    goto next;
                }
                ++m->outstanding;
                m->playing = 1;
            }
        }
        ++m->outstanding;
        data = s->lock_sound(s->context, v.sound, &bytes);
        if (s->submit(s->context, data, bytes, skip, v.plays, v.sound))
        {
            m->playing = 1;
            break;
        }
        s->unlock_sound(s->context, v.sound);
        --m->outstanding;
    next:
        mm_midi_mix_stop(m, s);
        v.start = now - v.offset;
        v.plays = 1;
        v.id = v.priority = MINIMUM;
        s->put(s->context, i, &v);
    }
    if (m->playing && i > 0)
        s->rotate(s->context, i);
}
void mm_midi_mix_return(MMMidiMixState *m, const MMMidiMixServices *s, void *sound)
{
    MMMidiMixVoice v;
    --m->outstanding;
    if (!m->outstanding)
        s->signal_return(s->context);
    if (sound)
        s->unlock_sound(s->context, sound);
    else
        s->unlock_key(s->context);
    if (!m->playing)
        return;
    if (!m->outstanding)
    {
        m->playing = 0;
        if (s->count(s->context) > 0)
        {
            s->get(s->context, 0, &v);
            s->erase(s->context, 0);
            s->leave(s->context);
            s->notify(s->context, v.queue, v.sound);
            s->enter(s->context);
        }
        if (s->count(s->context) > 0)
            mm_midi_mix_restart(m, s, 0);
    }
}
MMU32 mm_midi_mix_expire(MMMidiMixState *m, const MMMidiMixServices *s)
{
    MMU32 now, next = 0xffffffffUL, seek, wait;
    MMI32 i, loops;
    MMMidiMixVoice v;
    if (m->waiting)
        return 1;
    now = s->clock(s->context);
    for (i = s->count(s->context); i-- > 0;)
    {
        if (!i && m->playing)
            break;
        s->get(s->context, i, &v);
        loops = mm_signed_word((now - v.start) / v.duration);
        if (loops > 0)
        {
            if (v.plays > 0)
            {
                v.plays = mm_signed_word((MMU32)v.plays - (MMU32)loops);
                if (v.plays <= 0)
                {
                    s->erase(s->context, i);
                    s->leave(s->context);
                    s->notify(s->context, v.queue, v.sound);
                    s->enter(s->context);
                    return 0;
                }
            }
            seek = (now - v.start) % v.duration;
            v.start = now - seek;
            s->put(s->context, i, &v);
        }
        wait = v.duration - (now - v.start);
        if (wait < next)
            next = wait;
    }
    return next;
}
int mm_midi_stream_build(const MMMidiStreamBuildServices *s, MMU32 *duration)
{
    MMMidiStreamEvent events[100];
    MMMidiEvent e;
    MMU32 now = 0;
    MMI32 count = 1;
    int have;
    memset(events, 0, sizeof(events));
    events[0].eventWord = 0x010f4240UL;
    for (;;)
    {
        have = s->next(s->context, &e);
        if (count == 100 || !have)
        {
            if (!s->append(s->context, events, count))
                return 0;
            if (!have)
            {
                memset(events, 0, sizeof(*events));
                events[0].eventWord = 0x02000000UL;
                if (!s->append(s->context, events, 1))
                    return 0;
                break;
            }
            count = 0;
        }
        if (e.count > 0)
        {
            events[count].deltaTicks = e.timestamp - now;
            events[count].eventWord =
                (MMU32)e.send[0] | ((MMU32)e.send[1] << 8) | ((MMU32)e.send[2] << 16);
            ++count;
            now = e.timestamp;
        }
    }
    *duration = now;
    return 1;
}
int mm_midi_stream_seek(const MMMidiStreamEvent *events, MMI32 count, MMU32 time, void *context,
                              int (*prepend)(void *, const MMMidiStreamEvent *, MMI32), MMI32 *skip)
{
    unsigned short program = 0, pressure = 0, pitch = 0, control[120], *bits;
    unsigned short mask;
    int tempo = 0, copy;
    MMMidiStreamEvent batch[100];
    MMI32 i, n = 100;
    MMU32 elapsed = 0, status, value;
    memset(control, 0, sizeof(control));
    memset(batch, 0, sizeof(batch));
    for (i = 0; i < count; ++i)
    {
        elapsed += events[i].deltaTicks;
        if (elapsed >= time)
            break;
    }
    if (i + 1 >= count)
        return 0;
    batch[--n].deltaTicks = elapsed - time;
    batch[n].eventWord = events[i].eventWord;
    *skip = (i + 1) * (MMI32)sizeof(*events);
    for (;;)
    {
        if (!n || i <= 0)
        {
            if (!prepend(context, batch + n, 100 - n))
                return 0;
            if (i <= 0)
                break;
            n = 100;
        }
        --i;
        value = events[i].eventWord;
        copy = 0;
        bits = NULL;
        if ((value >> 24) == 0)
        {
            status = value & 0xf0;
            if (status == 0xb0 && ((value >> 8) & 255U) < 120)
                bits = control + ((value >> 8) & 255U);
            if (status == 0xc0)
                bits = &program;
            if (status == 0xd0)
                bits = &pressure;
            if (status == 0xe0)
                bits = &pitch;
            mask = (unsigned short)(1U << (value & 15));
            if (bits && !(*bits & mask))
            {
                *bits |= mask;
                copy = 1;
            }
        }
        else if ((value >> 24) == 1 && !tempo)
        {
            tempo = 1;
            copy = 1;
        }
        if (copy)
        {
            --n;
            batch[n].deltaTicks = 0;
            batch[n].eventWord = value;
        }
    }
    return 1;
}

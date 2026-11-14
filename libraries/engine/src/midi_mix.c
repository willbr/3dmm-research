/* Stream arbitration converted from Kauai mididev2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/midi_mix.h"
#include "three_mm/wide.h"
#include <string.h>
#define MINIMUM (-2147483647 - 1)
void three_mm_midi_queue_run(const ThreeMMMidiQueueServices *s, ThreeMMI32 first)
{
    const ThreeMMSoundQueueServices *q = &s->entries;
    ThreeMMSoundEntry e;
    ThreeMMI32 i;
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
void three_mm_midi_queue_pause(const ThreeMMMidiQueueServices *s, ThreeMMI32 first)
{
    const ThreeMMSoundQueueServices *q = &s->entries;
    ThreeMMSoundEntry e;
    q->enter(q->context);
    if (q->current(q->context) == first && q->count(q->context) > first)
    {
        q->get(q->context, first, &e);
        e.offset = s->clock(s->context) - s->start(s->context);
        q->put(q->context, first, &e);
        three_mm_midi_queue_run(s, first);
    }
    q->leave(q->context);
}
void three_mm_midi_queue_notify(const ThreeMMMidiQueueServices *s, void *sound)
{
    const ThreeMMSoundQueueServices *q = &s->entries;
    ThreeMMSoundEntry e;
    ThreeMMI32 i;
    q->enter(q->context);
    i = q->current(q->context);
    if (q->count(q->context) > i)
    {
        q->get(q->context, i, &e);
        if (e.object == sound)
        {
            q->set_current(q->context, ++i);
            three_mm_midi_queue_run(s, i);
        }
    }
    q->leave(q->context);
}
void three_mm_midi_mix_stop(ThreeMMMidiMixState *m, const ThreeMMMidiMixServices *s)
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
void three_mm_midi_mix_volume(ThreeMMMidiMixState *m, const ThreeMMMidiMixServices *s, ThreeMMI32 volume)
{
    ThreeMMAudioCounter n;
    m->base_volume = volume;
    n = three_mm_audio_count_multiply((ThreeMMU32)volume, (ThreeMMU32)m->sound_volume);
    s->volume(s->context, three_mm_signed_word((n.high << 16) | (n.low >> 16)));
}
void three_mm_midi_mix_suspend(ThreeMMMidiMixState *m, const ThreeMMMidiMixServices *s, int suspend)
{
    if (suspend)
        three_mm_midi_mix_stop(m, s);
    if (!s->activate(s->context, !suspend) && !suspend)
        s->busy(s->context);
    three_mm_midi_mix_restart(m, s, 0);
}
int three_mm_midi_mix_play(ThreeMMMidiMixState *m, const ThreeMMMidiMixServices *s, const ThreeMMMidiMixVoice *input)
{
    ThreeMMMidiMixVoice v;
    ThreeMMI32 i;
    int fresh = 0, result = 1;
    if (input->sound && !s->duration(s->context, input->sound))
        return 0;
    for (i = s->count(s->context); i-- > 0;)
    {
        s->get(s->context, i, &v);
        if (v.queue == input->queue)
        {
            if (i == 0)
                three_mm_midi_mix_stop(m, s);
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
            three_mm_midi_mix_stop(m, s);
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
    three_mm_midi_mix_restart(m, s, fresh);
    return result;
}
void three_mm_midi_mix_restart(ThreeMMMidiMixState *m, const ThreeMMMidiMixServices *s, int fresh)
{
    ThreeMMMidiMixVoice v;
    ThreeMMU32 now;
    if (s->active(s->context) && !m->playing && s->count(s->context) > 0)
    {
        now = s->clock(s->context);
        if (fresh)
        {
            s->get(s->context, 0, &v);
            v.start = now - v.offset;
            s->put(s->context, 0, &v);
        }
        three_mm_midi_mix_submit(m, s, now);
    }
    s->signal_worker(s->context);
}
void three_mm_midi_mix_submit(ThreeMMMidiMixState *m, const ThreeMMMidiMixServices *s, ThreeMMU32 now)
{
    ThreeMMMidiMixVoice v;
    ThreeMMI32 i, skip, bytes, loops;
    ThreeMMU32 seek;
    void *data;
    for (i = 0; i < s->count(s->context); ++i)
    {
        s->get(s->context, i, &v);
        loops = three_mm_signed_word((now - v.start) / v.duration);
        if (loops > 0)
        {
            if (v.plays > 0)
            {
                v.plays = three_mm_signed_word((ThreeMMU32)v.plays - (ThreeMMU32)loops);
                if (v.plays <= 0)
                    goto next;
            }
            seek = (now - v.start) % v.duration;
            v.start = now - seek;
            s->put(s->context, i, &v);
        }
        m->sound_volume = v.volume;
        three_mm_midi_mix_volume(m, s, m->base_volume);
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
        three_mm_midi_mix_stop(m, s);
        v.start = now - v.offset;
        v.plays = 1;
        v.id = v.priority = MINIMUM;
        s->put(s->context, i, &v);
    }
    if (m->playing && i > 0)
        s->rotate(s->context, i);
}
void three_mm_midi_mix_return(ThreeMMMidiMixState *m, const ThreeMMMidiMixServices *s, void *sound)
{
    ThreeMMMidiMixVoice v;
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
            three_mm_midi_mix_restart(m, s, 0);
    }
}
ThreeMMU32 three_mm_midi_mix_expire(ThreeMMMidiMixState *m, const ThreeMMMidiMixServices *s)
{
    ThreeMMU32 now, next = 0xffffffffUL, seek, wait;
    ThreeMMI32 i, loops;
    ThreeMMMidiMixVoice v;
    if (m->waiting)
        return 1;
    now = s->clock(s->context);
    for (i = s->count(s->context); i-- > 0;)
    {
        if (!i && m->playing)
            break;
        s->get(s->context, i, &v);
        loops = three_mm_signed_word((now - v.start) / v.duration);
        if (loops > 0)
        {
            if (v.plays > 0)
            {
                v.plays = three_mm_signed_word((ThreeMMU32)v.plays - (ThreeMMU32)loops);
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
int three_mm_midi_stream_build(const ThreeMMMidiStreamBuildServices *s, ThreeMMU32 *duration)
{
    ThreeMMMidiStreamEvent events[100];
    ThreeMMMidiEvent e;
    ThreeMMU32 now = 0;
    ThreeMMI32 count = 1;
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
                (ThreeMMU32)e.send[0] | ((ThreeMMU32)e.send[1] << 8) | ((ThreeMMU32)e.send[2] << 16);
            ++count;
            now = e.timestamp;
        }
    }
    *duration = now;
    return 1;
}
int three_mm_midi_stream_seek(const ThreeMMMidiStreamEvent *events, ThreeMMI32 count, ThreeMMU32 time, void *context,
                              int (*prepend)(void *, const ThreeMMMidiStreamEvent *, ThreeMMI32), ThreeMMI32 *skip)
{
    unsigned short program = 0, pressure = 0, pitch = 0, control[120], *bits;
    unsigned short mask;
    int tempo = 0, copy;
    ThreeMMMidiStreamEvent batch[100];
    ThreeMMI32 i, n = 100;
    ThreeMMU32 elapsed = 0, status, value;
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
    *skip = (i + 1) * (ThreeMMI32)sizeof(*events);
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

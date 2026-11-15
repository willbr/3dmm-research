/* Sound queue policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/sound_queue.h"
#include "mm/audio_clock.h"
#include "mm/wide.h"
#define MINIMUM (-2147483647 - 1)
#define MAXIMUM 2147483647
void mm_sound_queue_flush(const MMSoundQueueServices *s)
{
    MMI32 i;
    MMSoundEntry e;
    s->enter(s->context);
    while ((i = s->current(s->context)) > 0)
    {
        --i;
        s->set_current(s->context, i);
        s->get(s->context, i, &e);
        s->erase(s->context, i);
        s->release(s->context, e.object);
    }
    s->leave(s->context);
}
void mm_sound_queue_enqueue(const MMSoundQueueServices *s, const MMSoundEntry *e)
{
    MMI32 index;
    s->enter(s->context);
    s->flush(s->context);
    if (s->add(s->context, e, &index))
        s->changed(s->context, MM_SOUND_STOP, index);
    else
        s->release(s->context, e->object);
    s->leave(s->context);
}
MMI32 mm_sound_queue_priority(const MMSoundQueueServices *s)
{
    MMI32 result = MINIMUM;
    MMSoundEntry e;
    s->enter(s->context);
    s->flush(s->context);
    /* Preserve retail's inverted test: a valid queue returns the minimum. */
    if (s->count(s->context) < s->current(s->context))
    {
        s->get(s->context, s->current(s->context), &e);
        result = e.priority;
    }
    s->leave(s->context);
    return result;
}
void mm_sound_queue_edit(const MMSoundQueueServices *s, int operation, int all, MMI32 value,
                               MMI32 any_class)
{
    MMI32 i, first = MAXIMUM, old;
    MMSoundEntry e;
    int changed;
    s->enter(s->context);
    s->flush(s->context);
    for (i = s->count(s->context); i-- > s->current(s->context);)
    {
        s->get(s->context, i, &e);
        if (all ? value == any_class || value == e.sound_class : value == e.id)
        {
            old = e.pause;
            changed = 0;
            if (operation == MM_SOUND_STOP && old >= 0)
            {
                e.pause = -1;
                changed = 1;
            }
            if (operation == MM_SOUND_PAUSE && old >= 0)
            {
                e.pause = mm_signed_word((MMU32)old + 1U);
                changed = old == 0;
            }
            if (operation == MM_SOUND_RESUME && old > 0)
            {
                --e.pause;
                changed = e.pause == 0;
            }
            if (e.pause != old)
                s->put(s->context, i, &e);
            if (changed)
            {
                first = i;
                if (!all)
                    s->changed(s->context, operation, i);
            }
            if (!all)
                break;
        }
    }
    if (all && first < MAXIMUM)
        s->changed(s->context, operation, first);
    s->leave(s->context);
}
int mm_sound_queue_playing(const MMSoundQueueServices *s, int all, MMI32 value, MMI32 any_class)
{
    MMI32 i;
    int result = 0;
    MMSoundEntry e;
    s->enter(s->context);
    s->flush(s->context);
    for (i = s->count(s->context); i-- > s->current(s->context);)
    {
        s->get(s->context, i, &e);
        if (all ? (value == any_class || value == e.sound_class) && e.pause >= 0 : value == e.id)
        {
            result = e.pause >= 0;
            break;
        }
    }
    s->leave(s->context);
    return result;
}
void mm_sound_queue_release(const MMSoundQueueServices *s)
{
    MMI32 i;
    MMSoundEntry e;
    s->enter(s->context);
    for (i = s->count(s->context); i-- > 0;)
    {
        s->get(s->context, i, &e);
        s->release(s->context, e.object);
    }
    s->leave(s->context);
}
MMU32 mm_sound_volume_scale(MMU32 system, MMI32 volume)
{
    MMAudioCounter a = mm_audio_count_multiply(system & 65535U, (MMU32)volume);
    MMAudioCounter b = mm_audio_count_multiply(system >> 16, (MMU32)volume);
    return ((b.high ? 65535U : b.low >> 16) << 16) | (a.high ? 65535U : a.low >> 16);
}

/* Sound queue policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/sound_queue.h"
#include "mm/audio_clock.h"
#include "mm/wide.h"
#include <string.h>
#define MINIMUM (-2147483647 - 1)
#define MAXIMUM 2147483647

int mm_sound_queue_init(MMSoundQueue *queue, const MMStorageAllocator *allocator, MMU32 min_grow,
                        int trash_unused)
{
    if (mm_storage_init(&queue->storage, allocator) != MM_STORAGE_OK)
        return 0;
    memset(&queue->rows, 0, sizeof(queue->rows));
    queue->rows.storage = &queue->storage;
    queue->rows.width = sizeof(MMSoundEntry);
    queue->rows.trash_unused = trash_unused;
    queue->storage.min_grow[0] = min_grow;
    queue->current = 0;
    return 1;
}

MMI32 mm_sound_queue_count(const MMSoundQueue *queue)
{
    return queue->rows.storage ? (MMI32)queue->rows.count : -1;
}

MMSoundEntry mm_sound_queue_get(const MMSoundQueue *queue, MMI32 index)
{
    const MMSoundEntry *entries = (const MMSoundEntry *)queue->storage.buffers[0].data;
    return entries[index];
}

void mm_sound_queue_put(MMSoundQueue *queue, MMI32 index, const MMSoundEntry *entry)
{
    memcpy(queue->storage.buffers[0].data + index * sizeof(*entry), entry, sizeof(*entry));
}

void mm_sound_queue_flush(const MMSoundQueueServices *s)
{
    MMSoundQueue *queue = s->queue;
    MMI32 i;
    MMSoundEntry e;
    s->enter(s->context);
    while (queue->current > 0)
    {
        i = --queue->current;
        e = mm_sound_queue_get(queue, i);
        mm_array_delete(&queue->rows, i, 1);
        s->release(s->context, e.object);
    }
    s->leave(s->context);
}
void mm_sound_queue_enqueue(const MMSoundQueueServices *s, const MMSoundEntry *e)
{
    MMU32 index;
    s->enter(s->context);
    s->flush(s->context);
    if (mm_array_add(&s->queue->rows, e, &index) == MM_STORAGE_OK)
        s->changed(s->context, MM_SOUND_STOP, index);
    else
        s->release(s->context, e->object);
    s->leave(s->context);
}
MMI32 mm_sound_queue_priority(const MMSoundQueueServices *s)
{
    MMSoundQueue *queue = s->queue;
    MMI32 result = MINIMUM;
    s->enter(s->context);
    s->flush(s->context);
    /* Preserve retail's inverted test: a valid queue returns the minimum. */
    if (mm_sound_queue_count(queue) < queue->current)
        result = mm_sound_queue_get(queue, queue->current).priority;
    s->leave(s->context);
    return result;
}
void mm_sound_queue_edit(const MMSoundQueueServices *s, int operation, int all, MMI32 value,
                               MMI32 any_class)
{
    MMSoundQueue *queue = s->queue;
    MMI32 i, first = MAXIMUM, old;
    MMSoundEntry e;
    int changed;
    s->enter(s->context);
    s->flush(s->context);
    for (i = mm_sound_queue_count(queue); i-- > queue->current;)
    {
        e = mm_sound_queue_get(queue, i);
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
                mm_sound_queue_put(queue, i, &e);
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
    MMSoundQueue *queue = s->queue;
    MMI32 i;
    int result = 0;
    MMSoundEntry e;
    s->enter(s->context);
    s->flush(s->context);
    for (i = mm_sound_queue_count(queue); i-- > queue->current;)
    {
        e = mm_sound_queue_get(queue, i);
        if (all ? (value == any_class || value == e.sound_class) && e.pause >= 0 : value == e.id)
        {
            result = e.pause >= 0;
            break;
        }
    }
    s->leave(s->context);
    return result;
}
void mm_sound_queue_dispose(const MMSoundQueueServices *s)
{
    MMSoundQueue *queue = s->queue;
    MMI32 i;
    MMSoundEntry e;
    s->enter(s->context);
    if (queue->rows.storage)
    {
        for (i = mm_sound_queue_count(queue); i-- > 0;)
        {
            e = mm_sound_queue_get(queue, i);
            if (e.object)
                s->release(s->context, e.object);
        }
        mm_storage_dispose(&queue->storage);
        memset(&queue->rows, 0, sizeof(queue->rows));
        queue->current = 0;
    }
    s->leave(s->context);
}
MMU32 mm_sound_volume_scale(MMU32 system, MMI32 volume)
{
    MMAudioCounter a = mm_audio_count_multiply(system & 65535U, (MMU32)volume);
    MMAudioCounter b = mm_audio_count_multiply(system >> 16, (MMU32)volume);
    return ((b.high ? 65535U : b.low >> 16) << 16) | (a.high ? 65535U : a.low >> 16);
}

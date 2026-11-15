/* Actor sound policy converted from engine actrsnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_sound.h"
#include "mm/wide.h"
#include <string.h>
int mm_actor_sound_cel(MMI32 cel, MMI32 count, MMI32 *out)
{
    if (count <= 0) return 0;
    *out = cel < 0 ? -(MMI32)((0U - (MMU32)cel) % (MMU32)count) : cel % count;
    return 1;
}
MMI32 mm_sound_queue_number(MMI32 style, MMI32 object, int actor)
{
    MMU32 low = (MMU32)object & 65535U;
    if (low >= 32768U) low |= 0xffff0000UL;
    return mm_signed_word((actor ? 0x10000000UL : 0x20000000UL) | ((MMU32)style << 16) | low);
}
static MMArray view(MMActorSounds *list)
{
    MMArray array;
    array.storage = &list->storage;
    array.width = sizeof(MMActorMotionSound);
    array.count = list->count;
    array.free_count = 0;
    array.allocated = array.trash_unused = 0;
    return array;
}
int mm_actor_sounds_init(MMActorSounds *list, const MMStorageAllocator *allocator)
{
    if (mm_storage_init(&list->storage, allocator) != MM_STORAGE_OK)
        return 0;
    list->count = 0;
    list->storage.min_grow[0] = MM_ACTOR_SOUND_GROW * sizeof(MMActorMotionSound);
    return 1;
}
void mm_actor_sounds_dispose(MMActorSounds *list)
{
    mm_storage_dispose(&list->storage);
    list->count = 0;
}
int mm_actor_sounds_reserve(MMActorSounds *list, MMU32 additional)
{
    MMArray array = view(list);
    return mm_array_reserve(&array, additional, 0) == MM_STORAGE_OK;
}
void mm_actor_sounds_clear(MMActorSounds *list) { list->count = 0; }
int mm_actor_sounds_get(const MMActorSounds *list, MMU32 i, MMActorMotionSound *out)
{
    if (i >= list->count)
        return 0;
    memcpy(out, list->storage.buffers[0].data + i * sizeof(*out), sizeof(*out));
    return 1;
}
int mm_actor_sounds_put(MMActorSounds *list, MMU32 i, const MMActorMotionSound *record)
{
    if (i >= list->count)
        return 0;
    memmove(list->storage.buffers[0].data + i * sizeof(*record), record, sizeof(*record));
    return 1;
}
int mm_actor_sounds_delete(MMActorSounds *list, MMU32 i)
{
    MMArray array = view(list);
    if (mm_array_delete(&array, i, 1) != MM_STORAGE_OK)
        return 0;
    list->count = array.count;
    return 1;
}
int mm_actor_sounds_insert(MMActorSounds *list, const MMActorEvent *event, const MMActorSound *sound)
{
    MMActorMotionSound record, *records;
    MMArray array;
    MMU32 i;
    memset(&record, 0, sizeof(record));
    record.event = *event;
    record.sound = *sound;
    records = (MMActorMotionSound *)list->storage.buffers[0].data;
    for (i = 0; i < list->count; ++i)
        if (records[i].sound.cel == record.sound.cel && records[i].sound.style == record.sound.style)
            return mm_actor_sounds_put(list, i, &record);
    array = view(list);
    if (mm_array_add(&array, &record, 0) != MM_STORAGE_OK)
        return 0;
    list->count = array.count;
    return 1;
}
void mm_actor_sounds_swap(MMActorSounds *a, MMActorSounds *b)
{
    MMActorSounds temp = *a;
    *a = *b;
    *b = temp;
}
int mm_actor_sounds_clone(MMActorSounds *destination, const MMActorSounds *source)
{
    MMActorSounds fresh;
    if (destination == source)
        return 1;
    if (!mm_actor_sounds_init(&fresh, &destination->storage.allocator))
        return 0;
    if (!mm_actor_sounds_reserve(&fresh, source->count))
    {
        mm_actor_sounds_dispose(&fresh);
        return 0;
    }
    if (source->count)
        memcpy(fresh.storage.buffers[0].data, source->storage.buffers[0].data,
               source->count * sizeof(MMActorMotionSound));
    fresh.count = source->count;
    mm_actor_sounds_swap(destination, &fresh);
    mm_actor_sounds_dispose(&fresh);
    return 1;
}
enum { EMIT_OK, EMIT_MISSING, EMIT_FAILED };
static int emit(MMActorSound *sound, MMI32 index, int motion, const MMActorSoundServices *s)
{
    void *resource;
    int kind, success;
    MMI32 priority;
    if (s->cues_only)
        return s->cue(s->context, sound) ? EMIT_OK : EMIT_FAILED;
    if (!sound->tag.sid)
    {
        if (!s->resolve(s->context, &sound->tag, sound->child))
        {
            if (s->resolution_failed)
                s->resolution_failed(s->context);
            return EMIT_FAILED;
        }
        if (!motion)
            s->write_event(s->context, index, sound);
    }
    resource = s->fetch(s->context, &sound->tag);
    if (!resource)
        return EMIT_MISSING;
    kind = motion ? MM_ACTOR_SOUND_MATCH : sound->loop ? MM_ACTOR_SOUND_LOOP : MM_ACTOR_SOUND_ORDINARY;
    priority = s->priority(s->context, resource, kind);
    success = s->enqueue(s->context, resource, sound, priority, motion);
    s->release(s->context, resource);
    return success ? EMIT_OK : EMIT_FAILED;
}
int mm_actor_sound_event(MMActorSounds *list, const MMActorEvent *event, const MMActorSound *sound,
                               MMI32 index, const MMActorSoundServices *s)
{
    MMActorSound copy;
    if (sound->cel != -1)
        return mm_actor_sounds_insert(list, event, sound);
    copy = *sound;
    return emit(&copy, index, 0, s) == EMIT_OK;
}
int mm_actor_sound_motion(MMActorSounds *list, MMI32 action, MMI32 cel,
                                const MMActorSoundServices *s)
{
    MMI32 count = 0, frame;
    MMU32 i;
    MMActorMotionSound *records;
    int result, success = 1;
    if (!s->cel_count(s->context, action, &count) || !mm_actor_sound_cel(cel, count, &frame))
        return 0;
    records = (MMActorMotionSound *)list->storage.buffers[0].data;
    for (i = 0; i < list->count; ++i)
        if (records[i].sound.cel == frame)
        {
            result = emit(&records[i].sound, -1, 1, s);
            if (result == EMIT_FAILED)
                return 0;
            if (result == EMIT_MISSING)
                success = 0;
        }
    return success;
}

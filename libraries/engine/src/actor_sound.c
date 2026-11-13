/* Actor sound policy converted from engine actrsnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_sound.h"
#include "three_mm/wide.h"
#include <string.h>
int three_mm_actor_sound_cel(ThreeMMI32 cel, ThreeMMI32 count, ThreeMMI32 *out)
{
    if (count <= 0) return 0;
    *out = cel < 0 ? -(ThreeMMI32)((0U - (ThreeMMU32)cel) % (ThreeMMU32)count) : cel % count;
    return 1;
}
ThreeMMI32 three_mm_sound_queue_number(ThreeMMI32 style, ThreeMMI32 object, int actor)
{
    ThreeMMU32 low = (ThreeMMU32)object & 65535U;
    if (low >= 32768U) low |= 0xffff0000UL;
    return three_mm_signed_word((actor ? 0x10000000UL : 0x20000000UL) | ((ThreeMMU32)style << 16) | low);
}
static ThreeMMArray view(ThreeMMActorSounds *list)
{
    ThreeMMArray array;
    array.storage = &list->storage;
    array.width = sizeof(ThreeMMActorMotionSound);
    array.count = list->count;
    array.free_count = 0;
    array.allocated = array.trash_unused = 0;
    return array;
}
int three_mm_actor_sounds_init(ThreeMMActorSounds *list, const ThreeMMStorageAllocator *allocator)
{
    if (three_mm_storage_init(&list->storage, allocator) != THREE_MM_STORAGE_OK)
        return 0;
    list->count = 0;
    list->storage.min_grow[0] = THREE_MM_ACTOR_SOUND_GROW * sizeof(ThreeMMActorMotionSound);
    return 1;
}
void three_mm_actor_sounds_dispose(ThreeMMActorSounds *list)
{
    three_mm_storage_dispose(&list->storage);
    list->count = 0;
}
int three_mm_actor_sounds_reserve(ThreeMMActorSounds *list, ThreeMMU32 additional)
{
    ThreeMMArray array = view(list);
    return three_mm_array_reserve(&array, additional, 0) == THREE_MM_STORAGE_OK;
}
void three_mm_actor_sounds_clear(ThreeMMActorSounds *list) { list->count = 0; }
int three_mm_actor_sounds_get(const ThreeMMActorSounds *list, ThreeMMU32 i, ThreeMMActorMotionSound *out)
{
    if (i >= list->count)
        return 0;
    memcpy(out, list->storage.buffers[0].data + i * sizeof(*out), sizeof(*out));
    return 1;
}
int three_mm_actor_sounds_put(ThreeMMActorSounds *list, ThreeMMU32 i, const ThreeMMActorMotionSound *record)
{
    if (i >= list->count)
        return 0;
    memmove(list->storage.buffers[0].data + i * sizeof(*record), record, sizeof(*record));
    return 1;
}
int three_mm_actor_sounds_delete(ThreeMMActorSounds *list, ThreeMMU32 i)
{
    ThreeMMArray array = view(list);
    if (three_mm_array_delete(&array, i, 1) != THREE_MM_STORAGE_OK)
        return 0;
    list->count = array.count;
    return 1;
}
int three_mm_actor_sounds_insert(ThreeMMActorSounds *list, const ThreeMMActorEvent *event, const ThreeMMActorSound *sound)
{
    ThreeMMActorMotionSound record, *records;
    ThreeMMArray array;
    ThreeMMU32 i;
    memset(&record, 0, sizeof(record));
    record.event = *event;
    record.sound = *sound;
    records = (ThreeMMActorMotionSound *)list->storage.buffers[0].data;
    for (i = 0; i < list->count; ++i)
        if (records[i].sound.cel == record.sound.cel && records[i].sound.style == record.sound.style)
            return three_mm_actor_sounds_put(list, i, &record);
    array = view(list);
    if (three_mm_array_add(&array, &record, 0) != THREE_MM_STORAGE_OK)
        return 0;
    list->count = array.count;
    return 1;
}
void three_mm_actor_sounds_swap(ThreeMMActorSounds *a, ThreeMMActorSounds *b)
{
    ThreeMMActorSounds temp = *a;
    *a = *b;
    *b = temp;
}
int three_mm_actor_sounds_clone(ThreeMMActorSounds *destination, const ThreeMMActorSounds *source)
{
    ThreeMMActorSounds fresh;
    if (destination == source)
        return 1;
    if (!three_mm_actor_sounds_init(&fresh, &destination->storage.allocator))
        return 0;
    if (!three_mm_actor_sounds_reserve(&fresh, source->count))
    {
        three_mm_actor_sounds_dispose(&fresh);
        return 0;
    }
    if (source->count)
        memcpy(fresh.storage.buffers[0].data, source->storage.buffers[0].data,
               source->count * sizeof(ThreeMMActorMotionSound));
    fresh.count = source->count;
    three_mm_actor_sounds_swap(destination, &fresh);
    three_mm_actor_sounds_dispose(&fresh);
    return 1;
}
enum { EMIT_OK, EMIT_MISSING, EMIT_FAILED };
static int emit(ThreeMMActorSound *sound, ThreeMMI32 index, int motion, const ThreeMMActorSoundServices *s)
{
    void *resource;
    int kind, success;
    ThreeMMI32 priority;
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
    kind = motion ? THREE_MM_ACTOR_SOUND_MATCH : sound->loop ? THREE_MM_ACTOR_SOUND_LOOP : THREE_MM_ACTOR_SOUND_ORDINARY;
    priority = s->priority(s->context, resource, kind);
    success = s->enqueue(s->context, resource, sound, priority, motion);
    s->release(s->context, resource);
    return success ? EMIT_OK : EMIT_FAILED;
}
int three_mm_actor_sound_event(ThreeMMActorSounds *list, const ThreeMMActorEvent *event, const ThreeMMActorSound *sound,
                               ThreeMMI32 index, const ThreeMMActorSoundServices *s)
{
    ThreeMMActorSound copy;
    if (sound->cel != -1)
        return three_mm_actor_sounds_insert(list, event, sound);
    copy = *sound;
    return emit(&copy, index, 0, s) == EMIT_OK;
}
int three_mm_actor_sound_motion(ThreeMMActorSounds *list, ThreeMMI32 action, ThreeMMI32 cel,
                                const ThreeMMActorSoundServices *s)
{
    ThreeMMI32 count = 0, frame;
    ThreeMMU32 i;
    ThreeMMActorMotionSound *records;
    int result, success = 1;
    if (!s->cel_count(s->context, action, &count) || !three_mm_actor_sound_cel(cel, count, &frame))
        return 0;
    records = (ThreeMMActorMotionSound *)list->storage.buffers[0].data;
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

/* Sound management policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/sound_manager.h"
#include "mm/wide.h"
#include <string.h>

int mm_sound_endpoints_init(MMSoundEndpointList *list, const MMStorageAllocator *allocator, MMU32 min_grow,
                            int trash_unused)
{
    if (mm_storage_init(&list->storage, allocator) != MM_STORAGE_OK)
        return 0;
    memset(&list->rows, 0, sizeof(list->rows));
    list->rows.storage = &list->storage;
    list->rows.width = sizeof(MMSoundEndpoint);
    list->rows.trash_unused = trash_unused;
    list->storage.min_grow[0] = min_grow;
    return 1;
}

const MMSoundEndpoint *mm_sound_endpoint_at(const MMSoundEndpointList *list, MMU32 index)
{
    return (const MMSoundEndpoint *)list->storage.buffers[0].data + index;
}

static void put_endpoint(MMSoundEndpointList *list, MMU32 index, const MMSoundEndpoint *endpoint)
{
    memcpy(list->storage.buffers[0].data + index * sizeof(*endpoint), endpoint, sizeof(*endpoint));
}

int mm_sound_manager_find(const MMSoundManagerServices *s, MMU32 tag, MMSoundEndpoint *out,
                                MMI32 *index)
{
    MMU32 i;
    for (i = 0; i < s->endpoints->rows.count; ++i)
    {
        const MMSoundEndpoint *e = mm_sound_endpoint_at(s->endpoints, i);
        if (e->tag == tag)
        {
            if (out)
                *out = *e;
            if (index)
                *index = i;
            return 1;
        }
    }
    if (index)
        *index = i;
    return 0;
}
int mm_sound_manager_add(const MMSoundManagerServices *s, const MMSoundControl *state, MMU32 tag,
                               void *object)
{
    MMSoundEndpoint e;
    MMI32 i, n;
    s->retain(s->context, object);
    if (mm_sound_manager_find(s, tag, &e, &i))
    {
        s->release(s->context, e.object);
        e.object = object;
        put_endpoint(s->endpoints, i, &e);
    }
    else
    {
        e.tag = tag;
        e.object = object;
        if (mm_array_add(&s->endpoints->rows, &e, NULL) != MM_STORAGE_OK)
        {
            s->release(s->context, object);
            return 0;
        }
    }
    s->call(s->context, object, MM_SOUND_ACTIVE, state->active, 0);
    for (n = 0; n < state->suspended; ++n)
        s->call(s->context, object, MM_SOUND_SUSPEND, 1, 0);
    return 1;
}
void mm_sound_manager_remove(const MMSoundManagerServices *s, MMU32 tag)
{
    MMSoundEndpoint e;
    MMI32 i;
    if (!mm_sound_manager_find(s, tag, &e, &i))
        return;
    mm_array_delete(&s->endpoints->rows, i, 1);
    s->release(s->context, e.object);
}
MMI32 mm_sound_dispatch(const MMSoundManagerServices *s, int op, int filter, MMU32 tag,
                                   MMI32 first, MMI32 second)
{
    MMU32 i;
    MMI32 value, result = 0;
    for (i = 0; i < s->endpoints->rows.count; ++i)
    {
        const MMSoundEndpoint *e = mm_sound_endpoint_at(s->endpoints, i);
        if (filter && e->tag != tag)
            continue;
        value = s->call(s->context, e->object, op, first, second);
        if ((op == MM_SOUND_INSTANCE_PLAYING || op == MM_SOUND_ALL_PLAYING) && value)
            return 1;
        if (op == MM_SOUND_GET_VOLUME && value > result)
            result = value;
    }
    return result;
}
void mm_sound_activate(const MMSoundManagerServices *s, MMSoundControl *state, int active, int queues)
{
    if ((active != 0) == (state->active != 0))
        return;
    state->active = active != 0;
    if (queues)
        s->call(s->context, NULL, MM_SOUND_SUSPEND, state->suspended > 0 || !state->active, 0);
    else
        mm_sound_dispatch(s, MM_SOUND_ACTIVE, 0, 0, active, 0);
}
void mm_sound_suspend(const MMSoundManagerServices *s, MMSoundControl *state, int suspend, int queues)
{
    state->suspended = mm_signed_word((MMU32)state->suspended + (suspend ? 1U : 0xffffffffUL));
    if (queues)
        s->call(s->context, NULL, MM_SOUND_SUSPEND, state->suspended > 0 || !state->active, 0);
    else
        mm_sound_dispatch(s, MM_SOUND_SUSPEND, 0, 0, suspend, 0);
}
MMI32 mm_sound_next_id(MMI32 previous)
{
    return mm_signed_word((MMU32)previous + 1U);
}
MMI32 mm_sound_manager_play(const MMSoundManagerServices *s, const MMSoundPlay *p,
                                       MMI32 first, MMI32 last)
{
    MMSoundEndpoint e;
    if (p->sound_class < first || p->sound_class > last || !mm_sound_manager_find(s, p->type, &e, NULL))
        return s->allocate_id(s->context);
    return s->play(s->context, e.object, p);
}
void mm_sound_enqueue_resource(const MMSoundQueueServices *s, const MMSoundPlay *p, MMI32 id,
                               MMResourceBlockReader reader, const MMResourceObjectServices *objects)
{
    MMResourceRequest request;
    MMSoundEntry e;
    request.type = p->type;
    request.number = p->number;
    request.scope = 0;
    request.reader = reader;
    request.priority = MM_RESOURCE_NORMAL_PRIORITY;
    e.object = mm_resource_access_fetch(&p->source, &request, objects, NULL);
    if (!e.object)
        return;
    e.id = id;
    e.volume = p->volume;
    e.plays = p->plays;
    e.offset = p->offset;
    e.priority = p->priority;
    e.sound_class = p->sound_class;
    e.pause = 0;
    mm_sound_queue_enqueue(s, &e);
}
int mm_sound_ensure_queue(const MMSoundManagerServices *s, MMI32 queue, MMI32 anonymous,
                                MMI32 any_class, MMSoundEndpoint *out, MMI32 *index)
{
    MMI32 i, empty = -1;
    MMSoundEndpoint e;
    for (i = s->endpoints->rows.count; i-- > 0;)
    {
        e = *mm_sound_endpoint_at(s->endpoints, i);
        if (e.tag == (MMU32)queue && queue != anonymous)
            goto found;
        if (empty == -1 && !s->call(s->context, e.object, MM_SOUND_ALL_PLAYING, any_class, 0))
        {
            empty = i;
            if (queue == anonymous)
                break;
        }
    }
    if ((i = empty) != -1)
    {
        e = *mm_sound_endpoint_at(s->endpoints, i);
        e.tag = (MMU32)queue;
        put_endpoint(s->endpoints, i, &e);
        goto found;
    }
    e.tag = (MMU32)queue;
    e.object = s->create_queue(s->context);
    i = s->endpoints->rows.count;
    if (!e.object || mm_array_add(&s->endpoints->rows, &e, NULL) != MM_STORAGE_OK)
    {
        if (e.object)
            s->release(s->context, e.object);
        return 0;
    }
found:
    if (out)
        *out = e;
    if (index)
        *index = i;
    return 1;
}
void mm_sound_queues_flush(const MMSoundManagerServices *s, MMI32 any_class)
{
    MMI32 i;
    MMSoundEndpoint e;
    for (i = s->endpoints->rows.count; i-- > 0;)
    {
        e = *mm_sound_endpoint_at(s->endpoints, i);
        s->call(s->context, e.object, MM_SOUND_FLUSH, 0, 0);
        if (!s->call(s->context, e.object, MM_SOUND_ALL_PLAYING, any_class, 0) && s->endpoints->rows.count > 1)
        {
            s->release(s->context, e.object);
            mm_array_delete(&s->endpoints->rows, i, 1);
        }
    }
}
void mm_sound_endpoints_dispose(const MMSoundManagerServices *s, int reverse)
{
    MMSoundEndpointList *list = s->endpoints;
    MMU32 i, count = list->rows.count;
    for (i = 0; i < (reverse ? count : list->rows.count); ++i)
    {
        const MMSoundEndpoint *e = mm_sound_endpoint_at(list, reverse ? count - 1 - i : i);
        s->release(s->context, e->object);
    }
    mm_storage_dispose(&list->storage);
    list->rows.count = 0;
}

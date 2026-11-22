/* Sound management policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/sound_manager.h"
#include "mm/wide.h"
#include <stddef.h>
int mm_sound_manager_find(const MMSoundManagerServices *s, MMU32 tag, MMSoundEndpoint *out,
                                MMI32 *index)
{
    MMI32 i;
    MMSoundEndpoint e;
    for (i = 0; i < s->count(s->context); ++i)
    {
        s->get(s->context, i, &e);
        if (e.tag == tag)
        {
            if (out)
                *out = e;
            if (index)
                *index = i;
            return 1;
        }
    }
    if (index)
        *index = s->count(s->context);
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
        s->put(s->context, i, &e);
    }
    else
    {
        e.tag = tag;
        e.object = object;
        if (!s->insert(s->context, i, &e))
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
    s->erase(s->context, i);
    s->release(s->context, e.object);
}
MMI32 mm_sound_dispatch(const MMSoundManagerServices *s, int op, int filter, MMU32 tag,
                                   MMI32 first, MMI32 second)
{
    MMI32 i, value, result = 0;
    MMSoundEndpoint e;
    for (i = 0; i < s->count(s->context); ++i)
    {
        s->get(s->context, i, &e);
        if (filter && e.tag != tag)
            continue;
        value = s->call(s->context, e.object, op, first, second);
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
    for (i = s->count(s->context); i-- > 0;)
    {
        s->get(s->context, i, &e);
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
        s->get(s->context, i, &e);
        e.tag = (MMU32)queue;
        s->put(s->context, i, &e);
        goto found;
    }
    e.tag = (MMU32)queue;
    e.object = s->create_queue(s->context);
    i = s->count(s->context);
    if (!e.object || !s->insert(s->context, i, &e))
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
    for (i = s->count(s->context); i-- > 0;)
    {
        s->get(s->context, i, &e);
        s->call(s->context, e.object, MM_SOUND_FLUSH, 0, 0);
        if (!s->call(s->context, e.object, MM_SOUND_ALL_PLAYING, any_class, 0) && s->count(s->context) > 1)
        {
            s->release(s->context, e.object);
            s->erase(s->context, i);
        }
    }
}
void mm_sound_endpoints_release(const MMSoundManagerServices *s, int reverse)
{
    MMI32 i, count = s->count(s->context);
    MMSoundEndpoint e;
    for (i = 0; i < (reverse ? count : s->count(s->context)); ++i)
    {
        s->get(s->context, reverse ? count - 1 - i : i, &e);
        s->release(s->context, e.object);
    }
}

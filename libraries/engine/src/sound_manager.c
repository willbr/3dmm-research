/* Sound management policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/sound_manager.h"
#include "three_mm/wide.h"
#include <stddef.h>
int three_mm_sound_manager_find(const ThreeMMSoundManagerServices *s, ThreeMMU32 tag, ThreeMMSoundEndpoint *out,
                                ThreeMMI32 *index)
{
    ThreeMMI32 i;
    ThreeMMSoundEndpoint e;
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
int three_mm_sound_manager_add(const ThreeMMSoundManagerServices *s, const ThreeMMSoundControl *state, ThreeMMU32 tag,
                               void *object)
{
    ThreeMMSoundEndpoint e;
    ThreeMMI32 i, n;
    s->retain(s->context, object);
    if (three_mm_sound_manager_find(s, tag, &e, &i))
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
    s->call(s->context, object, THREE_MM_SOUND_ACTIVE, state->active, 0);
    for (n = 0; n < state->suspended; ++n)
        s->call(s->context, object, THREE_MM_SOUND_SUSPEND, 1, 0);
    return 1;
}
void three_mm_sound_manager_remove(const ThreeMMSoundManagerServices *s, ThreeMMU32 tag)
{
    ThreeMMSoundEndpoint e;
    ThreeMMI32 i;
    if (!three_mm_sound_manager_find(s, tag, &e, &i))
        return;
    s->erase(s->context, i);
    s->release(s->context, e.object);
}
ThreeMMI32 three_mm_sound_dispatch(const ThreeMMSoundManagerServices *s, int op, int filter, ThreeMMU32 tag,
                                   ThreeMMI32 first, ThreeMMI32 second)
{
    ThreeMMI32 i, value, result = 0;
    ThreeMMSoundEndpoint e;
    for (i = 0; i < s->count(s->context); ++i)
    {
        s->get(s->context, i, &e);
        if (filter && e.tag != tag)
            continue;
        value = s->call(s->context, e.object, op, first, second);
        if ((op == THREE_MM_SOUND_INSTANCE_PLAYING || op == THREE_MM_SOUND_ALL_PLAYING) && value)
            return 1;
        if (op == THREE_MM_SOUND_GET_VOLUME && value > result)
            result = value;
    }
    return result;
}
void three_mm_sound_activate(const ThreeMMSoundManagerServices *s, ThreeMMSoundControl *state, int active, int queues)
{
    if ((active != 0) == (state->active != 0))
        return;
    state->active = active != 0;
    if (queues)
        s->call(s->context, NULL, THREE_MM_SOUND_SUSPEND, state->suspended > 0 || !state->active, 0);
    else
        three_mm_sound_dispatch(s, THREE_MM_SOUND_ACTIVE, 0, 0, active, 0);
}
void three_mm_sound_suspend(const ThreeMMSoundManagerServices *s, ThreeMMSoundControl *state, int suspend, int queues)
{
    state->suspended = three_mm_signed_word((ThreeMMU32)state->suspended + (suspend ? 1U : 0xffffffffUL));
    if (queues)
        s->call(s->context, NULL, THREE_MM_SOUND_SUSPEND, state->suspended > 0 || !state->active, 0);
    else
        three_mm_sound_dispatch(s, THREE_MM_SOUND_SUSPEND, 0, 0, suspend, 0);
}
ThreeMMI32 three_mm_sound_next_id(ThreeMMI32 previous)
{
    return three_mm_signed_word((ThreeMMU32)previous + 1U);
}
ThreeMMI32 three_mm_sound_manager_play(const ThreeMMSoundManagerServices *s, const ThreeMMSoundPlay *p,
                                       ThreeMMI32 first, ThreeMMI32 last)
{
    ThreeMMSoundEndpoint e;
    if (p->sound_class < first || p->sound_class > last || !three_mm_sound_manager_find(s, p->type, &e, NULL))
        return s->allocate_id(s->context);
    return s->play(s->context, e.object, p);
}
int three_mm_sound_ensure_queue(const ThreeMMSoundManagerServices *s, ThreeMMI32 queue, ThreeMMI32 anonymous,
                                ThreeMMI32 any_class, ThreeMMSoundEndpoint *out, ThreeMMI32 *index)
{
    ThreeMMI32 i, empty = -1;
    ThreeMMSoundEndpoint e;
    for (i = s->count(s->context); i-- > 0;)
    {
        s->get(s->context, i, &e);
        if (e.tag == (ThreeMMU32)queue && queue != anonymous)
            goto found;
        if (empty == -1 && !s->call(s->context, e.object, THREE_MM_SOUND_ALL_PLAYING, any_class, 0))
        {
            empty = i;
            if (queue == anonymous)
                break;
        }
    }
    if ((i = empty) != -1)
    {
        s->get(s->context, i, &e);
        e.tag = (ThreeMMU32)queue;
        s->put(s->context, i, &e);
        goto found;
    }
    e.tag = (ThreeMMU32)queue;
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
void three_mm_sound_queues_flush(const ThreeMMSoundManagerServices *s, ThreeMMI32 any_class)
{
    ThreeMMI32 i;
    ThreeMMSoundEndpoint e;
    for (i = s->count(s->context); i-- > 0;)
    {
        s->get(s->context, i, &e);
        s->call(s->context, e.object, THREE_MM_SOUND_FLUSH, 0, 0);
        if (!s->call(s->context, e.object, THREE_MM_SOUND_ALL_PLAYING, any_class, 0) && s->count(s->context) > 1)
        {
            s->release(s->context, e.object);
            s->erase(s->context, i);
        }
    }
}
void three_mm_sound_endpoints_release(const ThreeMMSoundManagerServices *s, int reverse)
{
    ThreeMMI32 i, count = s->count(s->context);
    ThreeMMSoundEndpoint e;
    for (i = 0; i < (reverse ? count : s->count(s->context)); ++i)
    {
        s->get(s->context, reverse ? count - 1 - i : i, &e);
        s->release(s->context, e.object);
    }
}

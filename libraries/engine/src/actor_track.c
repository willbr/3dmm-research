/* Actor track ownership converted from engine actor_track.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_track.h"
#include <stddef.h>
#include <string.h>
void three_mm_actor_track_init(ThreeMMActorTrack *t)
{
    t->events = NULL;
    t->route = NULL;
}
void *three_mm_actor_event_tag(void *events, ThreeMMU32 index, const ThreeMMActorTrackServices *s)
{
    ThreeMMI32 type, custom;
    unsigned char *data;
    memcpy(&type, s->fixed(events, index), sizeof(type));
    if (type == THREE_MM_ACTOR_COSTUME)
    {
        data = (unsigned char *)s->data(events, index);
        memcpy(&custom, data + offsetof(ThreeMMActorCostume, custom), sizeof(custom));
        return custom ? NULL : data + offsetof(ThreeMMActorCostume, tag);
    }
    if (type == THREE_MM_ACTOR_SOUND)
        return (unsigned char *)s->data(events, index) + offsetof(ThreeMMActorSound, tag);
    return NULL;
}
static void tags(ThreeMMActorTrack *t, int close, const ThreeMMActorTrackServices *s)
{
    ThreeMMU32 i;
    void *bytes;
    ThreeMMTag tag;
    if (!t->events)
        return;
    s->lock(t->events);
    for (i = 0; i < s->count(t->events); ++i)
    {
        bytes = three_mm_actor_event_tag(t->events, i, s);
        if (!bytes)
            continue;
        memcpy(&tag, bytes, sizeof(tag));
        if (tag.sid || !tag.cache)
            continue;
        if (close)
        {
            s->release_cache(tag.cache);
            tag.cache = NULL;
            memcpy(bytes, &tag, sizeof(tag));
        }
        else
            s->retain_cache(tag.cache);
    }
    s->unlock(t->events);
}
void three_mm_actor_track_close_tags(ThreeMMActorTrack *t, const ThreeMMActorTrackServices *s)
{
    tags(t, 1, s);
}
void three_mm_actor_track_dispose(ThreeMMActorTrack *t, const ThreeMMActorTrackServices *s)
{
    three_mm_actor_track_close_tags(t, s);
    if (t->events)
        s->release_events(t->events);
    t->events = NULL;
    if (t->route)
        s->release_route(t->route);
    t->route = NULL;
}
void three_mm_actor_track_swap(ThreeMMActorTrack *a, ThreeMMActorTrack *b)
{
    ThreeMMActorTrack temporary = *a;
    *a = *b;
    *b = temporary;
}
int three_mm_actor_track_create(ThreeMMActorTrack *t, const ThreeMMActorTrackServices *s)
{
    ThreeMMActorTrack created;
    three_mm_actor_track_init(&created);
    created.events = s->new_events(s->context, 20, 10, 20);
    if (!created.events)
        return 0;
    created.route = s->new_route(s->context, 16, 32);
    if (!created.route)
    {
        three_mm_actor_track_dispose(&created, s);
        return 0;
    }
    s->route_growth(created.route, 32);
    three_mm_actor_track_swap(t, &created);
    three_mm_actor_track_dispose(&created, s);
    return 1;
}
int three_mm_actor_track_clone(ThreeMMActorTrack *t, const ThreeMMActorTrack *source,
                               const ThreeMMActorTrackServices *s)
{
    ThreeMMActorTrack clone;
    if (t == source)
        return 1;
    three_mm_actor_track_init(&clone);
    if (source->route)
    {
        clone.route = s->clone_route(source->route);
        if (!clone.route)
            return 0;
    }
    if (source->events)
    {
        clone.events = s->clone_events(source->events);
        if (!clone.events)
        {
            three_mm_actor_track_dispose(&clone, s);
            return 0;
        }
        tags(&clone, 0, s);
    }
    three_mm_actor_track_swap(t, &clone);
    three_mm_actor_track_dispose(&clone, s);
    return 1;
}
void three_mm_actor_track_adopt_events(ThreeMMActorTrack *t, void *events, const ThreeMMActorTrackServices *s)
{
    if (events == t->events)
        return;
    three_mm_actor_track_close_tags(t, s);
    if (t->events)
        s->release_events(t->events);
    t->events = events;
}
void three_mm_actor_track_adopt_route(ThreeMMActorTrack *t, void *route, const ThreeMMActorTrackServices *s)
{
    if (route == t->route)
        return;
    if (t->route)
        s->release_route(t->route);
    t->route = route;
}

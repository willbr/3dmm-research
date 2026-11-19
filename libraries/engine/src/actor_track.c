/* Actor track ownership converted from engine actor_track.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_track.h"
#include <stddef.h>
#include <string.h>
void mm_actor_track_init(MMActorTrack *t)
{
    t->events = NULL;
    t->route = NULL;
}
void *mm_actor_event_tag(void *events, MMU32 index, const MMActorTrackServices *s)
{
    MMI32 type;
    memcpy(&type, s->fixed(events, index), sizeof(type));
    if (type == MM_ACTOR_COSTUME || type == MM_ACTOR_SOUND)
        return (void *)mm_actor_payload_tag(type, s->data(events, index));
    return NULL;
}
static void tags(MMActorTrack *t, int close, const MMActorTrackServices *s)
{
    MMU32 i;
    void *bytes;
    MMTag tag;
    if (!t->events)
        return;
    s->lock(t->events);
    for (i = 0; i < s->count(t->events); ++i)
    {
        bytes = mm_actor_event_tag(t->events, i, s);
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
void mm_actor_track_close_tags(MMActorTrack *t, const MMActorTrackServices *s)
{
    tags(t, 1, s);
}
void mm_actor_track_dispose(MMActorTrack *t, const MMActorTrackServices *s)
{
    mm_actor_track_close_tags(t, s);
    if (t->events)
        s->release_events(t->events);
    t->events = NULL;
    if (t->route)
        s->release_route(t->route);
    t->route = NULL;
}
void mm_actor_track_swap(MMActorTrack *a, MMActorTrack *b)
{
    MMActorTrack temporary = *a;
    *a = *b;
    *b = temporary;
}
int mm_actor_track_create(MMActorTrack *t, const MMActorTrackServices *s)
{
    MMActorTrack created;
    mm_actor_track_init(&created);
    created.events = s->new_events(s->context, 20, 10, 20);
    if (!created.events)
        return 0;
    created.route = s->new_route(s->context, 16, 32);
    if (!created.route)
    {
        mm_actor_track_dispose(&created, s);
        return 0;
    }
    s->route_growth(created.route, 32);
    mm_actor_track_swap(t, &created);
    mm_actor_track_dispose(&created, s);
    return 1;
}
int mm_actor_track_clone(MMActorTrack *t, const MMActorTrack *source,
                               const MMActorTrackServices *s)
{
    MMActorTrack clone;
    if (t == source)
        return 1;
    mm_actor_track_init(&clone);
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
            mm_actor_track_dispose(&clone, s);
            return 0;
        }
        tags(&clone, 0, s);
    }
    mm_actor_track_swap(t, &clone);
    mm_actor_track_dispose(&clone, s);
    return 1;
}
void mm_actor_track_adopt_events(MMActorTrack *t, void *events, const MMActorTrackServices *s)
{
    if (events == t->events)
        return;
    mm_actor_track_close_tags(t, s);
    if (t->events)
        s->release_events(t->events);
    t->events = events;
}
void mm_actor_track_adopt_route(MMActorTrack *t, void *route, const MMActorTrackServices *s)
{
    if (route == t->route)
        return;
    if (t->route)
        s->release_route(t->route);
    t->route = route;
}

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

static void *event_tag_address(const MMActorEventTable *events, MMU32 index)
{
    MMActorEvent event;
    MMI32 bytes;
    const void *payload;
    if (!mm_actor_events_get_fixed(events, index, &event))
        return NULL;
    payload = mm_actor_events_payload(events, index, &bytes);
    if (!payload)
        return NULL;
    if ((event.aet == MM_ACTOR_COSTUME && bytes == (MMI32)sizeof(MMActorCostume)) ||
        (event.aet == MM_ACTOR_SOUND && bytes == (MMI32)sizeof(MMActorSound)))
        return (void *)mm_actor_payload_tag(event.aet, payload);
    return NULL;
}

int mm_actor_track_get_tag(const MMActorEventTable *events, MMU32 index, MMTag *tag)
{
    const void *address = event_tag_address(events, index);
    if (!address)
        return 0;
    memcpy(tag, address, sizeof(*tag));
    return 1;
}

static void update_tag(MMActorEventTable *events, MMU32 index, int close, const MMTagServices *s)
{
    void *address = event_tag_address(events, index);
    MMTag tag;
    if (!address)
        return;
    memcpy(&tag, address, sizeof(tag));
    if (tag.sid || !tag.cache)
        return;
    if (close)
    {
        mm_tag_close(&tag, s);
        memcpy(address, &tag, sizeof(tag));
    }
    else
        mm_tag_dup(&tag, s);
}

static void update_row_tag(MMActorEventTable *events, MMU32 index, int close, const MMTagServices *s)
{
    if (mm_actor_events_lock(events))
    {
        update_tag(events, index, close, s);
        mm_actor_events_unlock(events);
    }
}

void mm_actor_track_retain_tag(MMActorEventTable *events, MMU32 index, const MMTagServices *s)
{
    update_row_tag(events, index, 0, s);
}

void mm_actor_track_close_tag(MMActorEventTable *events, MMU32 index, const MMTagServices *s)
{
    update_row_tag(events, index, 1, s);
}

int mm_actor_track_open_tags(MMActorTrack *t, void *destination, void *source, const MMTagServices *s)
{
    MMActorEventTable *events = t->events;
    MMU32 i;
    void *address;
    MMTag tag;
    int ok;
    if (!events)
        return 1;
    if (!mm_actor_events_lock(events))
        return 0;
    for (i = 0; i < events->rows.count; ++i)
    {
        address = event_tag_address(events, i);
        if (!address)
            continue;
        memcpy(&tag, address, sizeof(tag));
        ok = mm_tag_open(&tag, destination, source, s);
        memcpy(address, &tag, sizeof(tag));
        if (!ok)
        {
            while (i)
                update_tag(events, --i, 1, s);
            mm_actor_events_unlock(events);
            return 0;
        }
    }
    mm_actor_events_unlock(events);
    return 1;
}

static void update_tags(MMActorTrack *t, int close, const MMTagServices *s)
{
    MMU32 i;
    if (!t->events || !mm_actor_events_lock(t->events))
        return;
    for (i = 0; i < t->events->rows.count; ++i)
        update_tag(t->events, i, close, s);
    mm_actor_events_unlock(t->events);
}

void mm_actor_track_close_tags(MMActorTrack *t, const MMTagServices *s)
{
    update_tags(t, 1, s);
}

void mm_actor_track_dispose(MMActorTrack *t, const MMTagServices *s)
{
    mm_actor_track_close_tags(t, s);
    mm_actor_events_release(t->events);
    if (t->route)
        mm_actor_path_release(t->route);
    mm_actor_track_init(t);
}

void mm_actor_track_swap(MMActorTrack *a, MMActorTrack *b)
{
    MMActorTrack temporary = *a;
    *a = *b;
    *b = temporary;
}

int mm_actor_track_create(MMActorTrack *t, const MMStorageAllocator *allocator, int trash_unused,
                          const MMTagServices *s)
{
    MMActorTrack created;
    MMActorEventFactory factory;
    factory.allocator = allocator;
    factory.trash_unused = trash_unused;
    mm_actor_track_init(&created);
    created.events = mm_actor_events_create(&factory, 10, 20);
    if (!created.events)
        return 0;
    created.route = mm_actor_path_create(allocator, 32, trash_unused);
    if (!created.route)
    {
        mm_actor_track_dispose(&created, s);
        return 0;
    }
    created.route->storage.min_grow[0] = 32 * MM_ACTOR_ROUTE_BYTES;
    mm_actor_track_swap(t, &created);
    mm_actor_track_dispose(&created, s);
    return 1;
}

int mm_actor_track_clone(MMActorTrack *t, const MMActorTrack *source, const MMTagServices *s)
{
    MMActorTrack clone;
    if (t == source)
        return 1;
    mm_actor_track_init(&clone);
    if (source->route)
    {
        clone.route = mm_actor_path_clone(source->route);
        if (!clone.route)
            return 0;
    }
    if (source->events)
    {
        clone.events = mm_actor_events_clone(source->events);
        if (!clone.events)
        {
            mm_actor_track_dispose(&clone, s);
            return 0;
        }
        update_tags(&clone, 0, s);
    }
    mm_actor_track_swap(t, &clone);
    mm_actor_track_dispose(&clone, s);
    return 1;
}

void mm_actor_track_adopt_events(MMActorTrack *t, MMActorEventTable *events, const MMTagServices *s)
{
    if (events == t->events)
        return;
    mm_actor_track_close_tags(t, s);
    mm_actor_events_release(t->events);
    t->events = events;
}

void mm_actor_track_adopt_route(MMActorTrack *t, MMActorPath *route)
{
    if (route == t->route)
        return;
    if (t->route)
        mm_actor_path_release(t->route);
    t->route = route;
}

static MMU32 path_width(void *p) { return ((const MMActorPath *)p)->rows.width; }
static MMI32 path_count(void *p) { return mm_actor_path_count((const MMActorPath *)p); }
static void path_point(void *p, MMI32 i, MMActorRoutePoint *out)
{
    mm_actor_path_get((const MMActorPath *)p, i, out);
}
static MMU32 event_width(void *p) { return ((const MMActorEventTable *)p)->rows.fixed_bytes; }
static MMI32 event_count(void *p) { return mm_actor_events_count((const MMActorEventTable *)p); }
static void event_fixed(void *p, MMI32 i, MMActorEvent *out)
{
    mm_actor_events_get_fixed((const MMActorEventTable *)p, i, out);
}
static MMI32 event_size(void *p, MMI32 i) { return mm_actor_events_size((const MMActorEventTable *)p, i); }
static void event_payload(void *p, MMI32 i, void *out)
{
    mm_actor_events_get_payload((const MMActorEventTable *)p, i, out);
}

MMActorMotionServices mm_actor_track_motion_services(void)
{
    MMActorMotionServices services = {
        path_width, path_count, path_point, event_width, event_count, event_fixed, event_size, event_payload
    };
    return services;
}

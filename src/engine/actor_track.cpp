/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME
namespace
{
MMActorTrackServices Services()
{
    return {nullptr,
            [](void *, MMU32 fixed, MMU32 entries, MMU32 variable) -> void * {
                Assert(fixed == MM_ACTOR_EVENT_BYTES, "invalid actor event width");
                const auto allocator = CollectionAllocator();
                const MMActorEventFactory factory = {&allocator, DebugShip(1, 0)};
                return mm_actor_events_create(&factory, entries, variable);
            },
            [](void *, MMU32 width, MMU32 entries) -> void * {
                Assert(width == MM_ACTOR_ROUTE_BYTES, "invalid actor path width");
                const auto allocator = CollectionAllocator();
                return mm_actor_path_create(&allocator, entries, DebugShip(1, 0));
            },
            [](void *p, MMU32 growth) { static_cast<MMActorPath *>(p)->storage.min_grow[0] = growth * MM_ACTOR_ROUTE_BYTES; },
            [](void *g) -> void * { return mm_actor_events_clone(static_cast<const MMActorEventTable *>(g)); },
            [](void *g) -> void * { return mm_actor_path_clone(static_cast<const MMActorPath *>(g)); },
            [](void *g) { mm_actor_events_release(static_cast<MMActorEventTable *>(g)); },
            [](void *g) { mm_actor_path_release(static_cast<MMActorPath *>(g)); },
            [](void *g) { AssertDo(mm_actor_events_lock(static_cast<MMActorEventTable *>(g)), "cannot lock actor events"); },
            [](void *g) { AssertDo(mm_actor_events_unlock(static_cast<MMActorEventTable *>(g)), "cannot unlock actor events"); },
            [](void *g) -> MMU32 { return mm_actor_events_count(static_cast<const MMActorEventTable *>(g)); },
            [](void *g, MMU32 i) -> void * { return mm_actor_events_fixed(static_cast<const MMActorEventTable *>(g), i); },
            [](void *g, MMU32 i) -> void * { return mm_actor_events_payload(static_cast<const MMActorEventTable *>(g), i, nullptr); },
            [](void *cache) { static_cast<PChunkyResourceFile>(cache)->AddRef(); },
            [](void *cache) { static_cast<PChunkyResourceFile>(cache)->Release(); }};
}
} // namespace
ActorTrack::~ActorTrack()
{
    Clear();
}
void ActorTrack::Clear()
{
    const auto s = Services();
    mm_actor_track_dispose(&_storage, &s);
}
void ActorTrack::CloseTags()
{
    const auto s = Services();
    mm_actor_track_close_tags(&_storage, &s);
}
bool ActorTrack::FEventTag(MMActorEventTable *events, LegacyLong index, PTAG *tag, ActorEvent::PBase *event)
{
    const auto s = Services();
    if (event)
        *event = static_cast<ActorEvent::PBase>(mm_actor_events_fixed(events, index));
    *tag = static_cast<PTAG>(mm_actor_event_tag(events, index, &s));
    return *tag != nullptr;
}
void ActorTrack::Swap(ActorTrack &other)
{
    mm_actor_track_swap(&_storage, &other._storage);
}
bool ActorTrack::FCreate()
{
    const auto s = Services();
    return mm_actor_track_create(&_storage, &s) != 0;
}
bool ActorTrack::FCloneFrom(const ActorTrack &source)
{
    const auto s = Services();
    return mm_actor_track_clone(&_storage, &source._storage, &s) != 0;
}
MMActorEventTable *ActorTrack::AdoptEvents(MMActorEventTable *events)
{
    const auto s = Services();
    mm_actor_track_adopt_events(&_storage, events, &s);
    return Events();
}
MMActorPath *ActorTrack::AdoptRoute(MMActorPath *route)
{
    const auto s = Services();
    mm_actor_track_adopt_route(&_storage, route, &s);
    return Route();
}

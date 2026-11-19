/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
const MMTagServices &ActorTrack::TagServices()
{
    static const auto services = [] {
        MMTagServices s = {};
        s.retain = [](void *p) { static_cast<PChunkyResourceFile>(p)->AddRef(); };
        s.release = [](void *p) { static_cast<PChunkyResourceFile>(p)->Release(); };
        return s;
    }();
    return services;
}

ActorTrack::~ActorTrack()
{
    Clear();
}
void ActorTrack::Clear()
{
    mm_actor_track_dispose(&_storage, &TagServices());
}
void ActorTrack::CloseTags()
{
    mm_actor_track_close_tags(&_storage, &TagServices());
}
void ActorTrack::RetainEventTag(LegacyLong index)
{
    mm_actor_track_retain_tag(Events(), index, &TagServices());
}
void ActorTrack::CloseEventTag(LegacyLong index)
{
    mm_actor_track_close_tag(Events(), index, &TagServices());
}
void ActorTrack::Swap(ActorTrack &other)
{
    mm_actor_track_swap(&_storage, &other._storage);
}
bool ActorTrack::FCreate()
{
    const auto allocator = CollectionAllocator();
    return mm_actor_track_create(&_storage, &allocator, DebugShip(1, 0), &TagServices()) != 0;
}
bool ActorTrack::FCloneFrom(const ActorTrack &source)
{
    return mm_actor_track_clone(&_storage, &source._storage, &TagServices()) != 0;
}
MMActorEventTable *ActorTrack::AdoptEvents(MMActorEventTable *events)
{
    mm_actor_track_adopt_events(&_storage, events, &TagServices());
    return Events();
}
MMActorPath *ActorTrack::AdoptRoute(MMActorPath *route)
{
    mm_actor_track_adopt_route(&_storage, route);
    return Route();
}

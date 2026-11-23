/* Actor track fixture with scoped cleanup for native integration tests. */
#ifndef ACTOR_TRACK_FIXTURE_H
#define ACTOR_TRACK_FIXTURE_H
#include "soc_core.h"
#include "mm/actor_persistence.h"

class ActorTrack
{
    MMActorTrack storage = {};

  public:
    ActorTrack() = default;
    ~ActorTrack() { Clear(); }
    ActorTrack(const ActorTrack &) = delete;
    ActorTrack &operator=(const ActorTrack &) = delete;
    MMActorEventTable *Events() const { return storage.events; }
    MMActorPath *Route() const { return storage.route; }
    MMActorEventTable *AdoptEvents(MMActorEventTable *events)
    {
        mm_actor_track_adopt_events(&storage, events, mm_tag_local_cache_services());
        return Events();
    }
    MMActorPath *AdoptRoute(MMActorPath *route)
    {
        mm_actor_track_adopt_route(&storage, route);
        return Route();
    }
    bool FCreate()
    {
        const auto allocator = CollectionAllocator();
        return mm_actor_track_create(&storage, &allocator, DebugShip(1, 0), mm_tag_local_cache_services()) != 0;
    }
    bool FCloneFrom(const ActorTrack &source)
    {
        return mm_actor_track_clone(&storage, &source.storage, mm_tag_local_cache_services()) != 0;
    }
    void Swap(ActorTrack &other) { mm_actor_track_swap(&storage, &other.storage); }
    void Clear() { mm_actor_track_dispose(&storage, mm_tag_local_cache_services()); }
    void CloseTags() { mm_actor_track_close_tags(&storage, mm_tag_local_cache_services()); }
    void RetainEventTag(LegacyLong index) { mm_actor_track_retain_tag(Events(), index, mm_tag_local_cache_services()); }
    void CloseEventTag(LegacyLong index) { mm_actor_track_close_tag(Events(), index, mm_tag_local_cache_services()); }
    const MMActorTrack &Core() const { return storage; }
    MMActorTrack &Core() { return storage; }
};
#endif

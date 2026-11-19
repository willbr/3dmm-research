/* Actor track fixture with scoped cleanup for native integration tests. */
#ifndef ACTOR_TRACK_FIXTURE_H
#define ACTOR_TRACK_FIXTURE_H
#include "soc_core.h"

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
        mm_actor_track_adopt_events(&storage, events, &EngineActorTagServices());
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
        return mm_actor_track_create(&storage, &allocator, DebugShip(1, 0), &EngineActorTagServices()) != 0;
    }
    bool FCloneFrom(const ActorTrack &source)
    {
        return mm_actor_track_clone(&storage, &source.storage, &EngineActorTagServices()) != 0;
    }
    void Swap(ActorTrack &other) { mm_actor_track_swap(&storage, &other.storage); }
    void Clear() { mm_actor_track_dispose(&storage, &EngineActorTagServices()); }
    void CloseTags() { mm_actor_track_close_tags(&storage, &EngineActorTagServices()); }
    void RetainEventTag(LegacyLong index) { mm_actor_track_retain_tag(Events(), index, &EngineActorTagServices()); }
    void CloseEventTag(LegacyLong index) { mm_actor_track_close_tag(Events(), index, &EngineActorTagServices()); }
    const MMActorTrack &Core() const { return storage; }
    MMActorTrack &Core() { return storage; }
};
#endif

/* Temporary native table/value adapters for the C89 actor evaluator. */
#pragma once
#include "mm/actor_motion.h"
#include "mm/actor_path.h"
#include <cstring>
#include <cstddef>
static_assert(BASED_FIXED && sizeof(BRS) == 4, "actor C89 scalar ABI is signed 16.16");
static_assert(sizeof(RoutePoint) == sizeof(MMActorPoint) && offsetof(RoutePoint, dyr) == 4 &&
                  offsetof(RoutePoint, dzr) == 8,
              "actor point ABI");
static_assert(sizeof(RouteDistancePoint) == sizeof(MMActorRoutePoint) && offsetof(RouteDistancePoint, dwr) == 12,
              "actor route ABI");
static_assert(sizeof(RouteLocation) == sizeof(MMActorLocation) && offsetof(RouteLocation, dwrOffset) == 4 &&
                  offsetof(RouteLocation, dnfrm) == 8,
              "actor location ABI");
static_assert(sizeof(ActorEvent::Base) == sizeof(MMActorEvent) && offsetof(ActorEvent::Base, rtel) == 8,
              "actor event ABI");
namespace EngineActorMotion
{
inline MMActorMotionServices Services()
{
    return {[](void *p) -> MMU32 { return static_cast<const MMActorPath *>(p)->rows.width; },
            [](void *p) -> MMI32 { return mm_actor_path_count(static_cast<const MMActorPath *>(p)); },
            [](void *p, MMI32 i, MMActorRoutePoint *out) {
                mm_actor_path_get(static_cast<const MMActorPath *>(p), i, out);
            },
            [](void *g) -> MMU32 { return static_cast<const MMActorEventTable *>(g)->rows.fixed_bytes; },
            [](void *g) -> MMI32 { return mm_actor_events_count(static_cast<MMActorEventTable *>(g)); },
            [](void *g, MMI32 i, MMActorEvent *out) { mm_actor_events_get_fixed(static_cast<MMActorEventTable *>(g), i, out); },
            [](void *g, MMI32 i) -> MMI32 { return mm_actor_events_size(static_cast<MMActorEventTable *>(g), i); },
            [](void *g, MMI32 i, void *out) { mm_actor_events_get_payload(static_cast<MMActorEventTable *>(g), i, out); }};
}
inline MMActorMatrix Matrix(const BMAT34 &native)
{
    MMActorMatrix out;
    static_assert(sizeof(out) == sizeof(native), "actor matrix ABI");
    std::memcpy(&out, &native, sizeof(out));
    return out;
}
inline const MMActorState &Core(const ActorFrameState &native)
{
    return native;
}
inline void Publish(ActorFrameState &native, const MMActorState &value)
{
    static_cast<MMActorState &>(native) = value;
}
} // namespace EngineActorMotion

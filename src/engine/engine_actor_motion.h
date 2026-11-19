/* Temporary native table/value adapters for the C89 actor evaluator. */
#pragma once
#include "mm/actor_track.h"
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
    return mm_actor_track_motion_services();
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

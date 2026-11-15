/* Temporary native table/value adapters for the C89 actor evaluator. */
#pragma once
#include "mm/actor_motion.h"
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
static_assert(sizeof(ActorEvent::FramePosition) == sizeof(MMActorTransform) &&
                  offsetof(ActorEvent::FramePosition, bmat34Fwd) == offsetof(MMActorTransform, bmat34Fwd) &&
                  offsetof(ActorEvent::FramePosition, aevpull) == offsetof(MMActorTransform, aevpull) &&
                  offsetof(ActorEvent::FramePosition, rScaleStep) == offsetof(MMActorTransform, rScaleStep) &&
                  offsetof(ActorEvent::FramePosition, bmat34Cur) == offsetof(MMActorTransform, bmat34Cur) &&
                  offsetof(ActorEvent::FramePosition, xaPath) == offsetof(MMActorTransform, xaPath) &&
                  offsetof(ActorEvent::FramePosition, yaPath) == offsetof(MMActorTransform, yaPath) &&
                  offsetof(ActorEvent::FramePosition, zaPath) == offsetof(MMActorTransform, zaPath),
              "actor transform ABI");
namespace EngineActorMotion
{
inline MMActorMotionServices Services()
{
    return {[](void *g) -> MMU32 { return static_cast<PDynamicArray>(g)->CbEntry(); },
            [](void *g) -> MMI32 { return static_cast<PDynamicArray>(g)->IvMac(); },
            [](void *g, MMI32 i, MMActorRoutePoint *out) { static_cast<PDynamicArray>(g)->Get(i, out); },
            [](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->CbFixed(); },
            [](void *g) -> MMI32 { return static_cast<PGeneralGroup>(g)->IvMac(); },
            [](void *g, MMI32 i, MMActorEvent *out) { static_cast<PGeneralGroup>(g)->GetFixed(i, out); },
            [](void *g, MMI32 i) -> MMI32 { return static_cast<PGeneralGroup>(g)->Cb(i); },
            [](void *g, MMI32 i, void *out) { static_cast<PGeneralGroup>(g)->Get(i, out); }};
}
inline MMActorPoint Point(const RoutePoint &p)
{
    MMActorPoint out;
    std::memcpy(&out, &p, sizeof(out));
    return out;
}
inline MMActorLocation Location(const RouteLocation &p)
{
    MMActorLocation out;
    std::memcpy(&out, &p, sizeof(out));
    return out;
}
inline MMActorState Core(const ActorFrameState &native)
{
    MMActorState out = {};
    out.onStage = native.onStage;
    out.frozen = native.frozen;
    out.useCurrentRotation = native.useCurrentRotation;
    out.step = native.step;
    out.action = native.action;
    out.celCount = native.celCount;
    out.cel = native.cel;
    out.frame = native.frame;
    out.nextEvent = native.nextEvent;
    out.firstEventInFrame = native.firstEventInFrame;
    out.actionEvent = native.actionEvent;
    out.addEvent = native.addEvent;
    out.actionFlags = native.actionFlags;
    std::memcpy(&out.routeTranslation, &native.routeTranslation, sizeof(out.routeTranslation));
    std::memcpy(&out.subrouteTranslation, &native.subrouteTranslation, sizeof(out.subrouteTranslation));
    std::memcpy(&out.location, &native.location, sizeof(out.location));
    std::memcpy(&out.position, &native.position, sizeof(out.position));
    std::memcpy(&out.transform, &native.transform, sizeof(out.transform));
    return out;
}
inline void Publish(ActorFrameState &native, const MMActorState &value)
{
    native.onStage = value.onStage != 0;
    native.frozen = value.frozen != 0;
    native.useCurrentRotation = value.useCurrentRotation != 0;
    native.step = value.step;
    native.action = value.action;
    native.celCount = value.celCount;
    native.cel = value.cel;
    native.frame = value.frame;
    native.nextEvent = value.nextEvent;
    native.firstEventInFrame = value.firstEventInFrame;
    native.actionEvent = value.actionEvent;
    native.addEvent = value.addEvent;
    native.actionFlags = value.actionFlags;
    std::memcpy(&native.routeTranslation, &value.routeTranslation, sizeof(value.routeTranslation));
    std::memcpy(&native.subrouteTranslation, &value.subrouteTranslation, sizeof(value.subrouteTranslation));
    std::memcpy(&native.location, &value.location, sizeof(value.location));
    std::memcpy(&native.position, &value.position, sizeof(value.position));
    std::memcpy(&native.transform, &value.transform, sizeof(value.transform));
}
} // namespace EngineActorMotion

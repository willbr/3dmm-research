/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_motion.h"
ASSERTNAME
namespace
{
ThreeMMActorState Core(const ActorFrameState &native)
{
    ThreeMMActorState out = {};
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
void Publish(ActorFrameState &native, const ThreeMMActorState &value)
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
} // namespace
ActorFrameState::ActorFrameState()
{
    ThreeMMActorState value;
    three_mm_actor_state_init(&value);
    Publish(*this, value);
}
void ActorFrameState::InitTransform()
{
    auto value = Core(*this);
    three_mm_actor_state_init_transform(&value);
    Publish(*this, value);
}
void ActorFrameState::RewindRoute(const ActorTrack &track)
{
    auto value = Core(*this);
    const auto services = EngineActorMotion::Services();
    three_mm_actor_state_rewind(&value, track.Route(), &services);
    Publish(*this, value);
}
void ActorFrameState::UpdateOrigin(const RoutePoint &origin)
{
    auto value = Core(*this);
    const auto point = EngineActorMotion::Point(origin);
    three_mm_actor_state_origin(&value, &point);
    Publish(*this, value);
}
ActorValueEventResult ActorFrameState::ApplyValueEvent(const ActorTrack &track, LegacyLong index,
                                                       const RoutePoint &origin)
{
    auto value = Core(*this);
    const auto point = EngineActorMotion::Point(origin);
    const auto services = EngineActorMotion::Services();
    int result = three_mm_actor_state_apply(&value, track.Route(), track.Events(), index, &point, &services);
    if (result == THREE_MM_ACTOR_APPLIED)
        Publish(*this, value);
    return static_cast<ActorValueEventResult>(result);
}
static_assert(static_cast<int>(ActorValueEventResult::Applied) == THREE_MM_ACTOR_APPLIED &&
                  static_cast<int>(ActorValueEventResult::NeedsRuntime) == THREE_MM_ACTOR_NEEDS_RUNTIME &&
                  static_cast<int>(ActorValueEventResult::Invalid) == THREE_MM_ACTOR_INVALID,
              "actor result ABI");

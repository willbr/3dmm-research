/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_motion.h"
ASSERTNAME
using namespace EngineActorMotion;
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

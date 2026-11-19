/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_motion.h"
ASSERTNAME
using namespace EngineActorMotion;
ActorFrameState::ActorFrameState()
{
    mm_actor_state_init(this);
}
void ActorFrameState::InitTransform()
{
    mm_actor_state_init_transform(this);
}
void ActorFrameState::RewindRoute(const MMActorTrack &track)
{
    const auto services = EngineActorMotion::Services();
    mm_actor_state_rewind(this, track.route, &services);
}
void ActorFrameState::UpdateOrigin(const RoutePoint &origin)
{
    mm_actor_state_origin(this, &origin);
}
ActorValueEventResult ActorFrameState::ApplyValueEvent(const MMActorTrack &track, LegacyLong index,
                                                       const RoutePoint &origin)
{
    const auto services = EngineActorMotion::Services();
    int result = mm_actor_state_apply(this, track.route, track.events, index, &origin, &services);
    return static_cast<ActorValueEventResult>(result);
}
static_assert(static_cast<int>(ActorValueEventResult::Applied) == MM_ACTOR_APPLIED &&
                  static_cast<int>(ActorValueEventResult::NeedsRuntime) == MM_ACTOR_NEEDS_RUNTIME &&
                  static_cast<int>(ActorValueEventResult::Invalid) == MM_ACTOR_INVALID,
              "actor result ABI");

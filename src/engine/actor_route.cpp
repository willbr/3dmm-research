/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_motion.h"
ASSERTNAME
void ActorRouteEvaluator::Advance(const MMActorTrack &track, BRS step, RouteLocation *location, LegacyLong nextEvent,
                                  LegacyLong frame, LegacyLong firstFrame, bool clamp, bool *end)
{
    Assert(track.route, "missing actor path");
    Assert(track.events, "missing actor events");
    AssertVarMem(location);
    AssertNilOrVarMem(end);
    const auto services = EngineActorMotion::Services();
    int ended;
    mm_actor_route_advance(track.route, track.events, step, location, nextEvent, frame, firstFrame, clamp,
                                 &ended, &services);
    if (end)
        *end = ended != 0;
}
void ActorRouteEvaluator::Position(const MMActorTrack &track, const RouteLocation *location, RoutePoint *position)
{
    Assert(track.route, "missing actor path");
    AssertVarMem(const_cast<RouteLocation *>(location));
    AssertVarMem(position);
    const auto services = EngineActorMotion::Services();
    mm_actor_route_position(track.route, location, position, &services);
}
void ActorRouteEvaluator::Interpolate(const RoutePoint *first, const RoutePoint *second, BRS fraction,
                                      RoutePoint *position)
{
    AssertVarMem(const_cast<RoutePoint *>(first));
    AssertVarMem(const_cast<RoutePoint *>(second));
    AssertVarMem(position);
    mm_actor_interpolate(first, second, fraction, position);
}

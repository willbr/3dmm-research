/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_motion.h"
ASSERTNAME
void ActorRouteEvaluator::Advance(const ActorTrack &track, BRS step, RouteLocation *location, LegacyLong nextEvent,
                                  LegacyLong frame, LegacyLong firstFrame, bool clamp, bool *end)
{
    AssertPo(track.Route(), 0);
    AssertPo(track.Events(), 0);
    AssertVarMem(location);
    AssertNilOrVarMem(end);
    auto value = EngineActorMotion::Location(*location);
    const auto services = EngineActorMotion::Services();
    int ended;
    three_mm_actor_route_advance(track.Route(), track.Events(), step, &value, nextEvent, frame, firstFrame, clamp,
                                 &ended, &services);
    std::memcpy(location, &value, sizeof(value));
    if (end)
        *end = ended != 0;
}
void ActorRouteEvaluator::Position(const ActorTrack &track, const RouteLocation *location, RoutePoint *position)
{
    AssertPo(track.Route(), 0);
    AssertVarMem(const_cast<RouteLocation *>(location));
    AssertVarMem(position);
    const auto value = EngineActorMotion::Location(*location);
    const auto services = EngineActorMotion::Services();
    ThreeMMActorPoint out;
    three_mm_actor_route_position(track.Route(), &value, &out, &services);
    std::memcpy(position, &out, sizeof(out));
}
void ActorRouteEvaluator::Interpolate(const RoutePoint *first, const RoutePoint *second, BRS fraction,
                                      RoutePoint *position)
{
    AssertVarMem(const_cast<RoutePoint *>(first));
    AssertVarMem(const_cast<RoutePoint *>(second));
    AssertVarMem(position);
    const auto a = EngineActorMotion::Point(*first), b = EngineActorMotion::Point(*second);
    ThreeMMActorPoint out;
    three_mm_actor_interpolate(&a, &b, fraction, &out);
    std::memcpy(position, &out, sizeof(out));
}

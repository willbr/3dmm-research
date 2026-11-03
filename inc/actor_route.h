/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef ACTOR_ROUTE_H
#define ACTOR_ROUTE_H
#include "actor_track.h"

// Deterministic route arithmetic over authored runtime tables. The caller
// supplies frame/step inputs and the unconsumed event cursor; no clock, Body,
// Scene or sound service is consulted. Route locations must address a valid
// segment and every subroute must terminate with a zero-distance node.
struct ActorRouteEvaluator
{
    // clampAtEvents stops at the first encountered action/step event. Recording
    // and rejoining disable this. Reaching the final node is not an end signal;
    // a later attempt to advance beyond it is. Zero steps accumulate dwell frames.
    static void Advance(const ActorTrack &track, BRS step, RouteLocation *location, LegacyLong nextEvent, LegacyLong frame,
                        LegacyLong firstFrame, bool clampAtEvents, bool *endRoute = pvNil);
    static void Position(const ActorTrack &track, const RouteLocation *location, RoutePoint *position);
    // The output may alias either input point.
    static void Interpolate(const RoutePoint *first, const RoutePoint *second, BRS fraction, RoutePoint *position);
};
#endif

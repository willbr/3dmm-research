/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef ACTOR_STATE_H
#define ACTOR_STATE_H
#include "actor_route.h"

enum class ActorValueEventResult
{
    Applied,
    NeedsRuntime,
    Invalid
};

// Copyable evaluated values, separate from authored tables, resource ownership,
// renderer bindings and recording clocks. No pointers or services are owned.
struct ActorFrameState
{
    RoutePoint routeTranslation = {};
    RoutePoint subrouteTranslation = {};
    bool onStage = false;
    bool frozen = false;
    bool useCurrentRotation = false;
    BRS step = rZero;
    LegacyLong action = 0;
    LegacyLong celCount = 0;
    LegacyLong cel = 0;
    LegacyLong frame = knfrmInvalid;
    LegacyLong nextEvent = 0;
    LegacyLong firstEventInFrame = 0;
    LegacyLong actionEvent = ivNil;
    LegacyLong addEvent = ivNil;
    LegacyUlong actionFlags = 0;
    RouteLocation location = {};
    RoutePoint position = {};
    ActorEvent::FramePosition transform = {};

    ActorFrameState();
    void InitTransform();
    // Matches runtime rewind: preserves stage visibility, frame and cached
    // action metadata; the host controls hiding and recording state. A non-null
    // route must contain RouteDistancePoint entries, as in ActorTrack::FCreate.
    void RewindRoute(const ActorTrack &track);
    void UpdateOrigin(const RoutePoint &origin);
    // Applies pure value events without modifying authored data or emitting
    // effects. Resource/stage/sound events return NeedsRuntime without changes.
    // Pure-event widths and zero-step route locations are checked before reads;
    // malformed input is Invalid. NeedsRuntime leaves payload validation to host.
    ActorValueEventResult ApplyValueEvent(const ActorTrack &track, LegacyLong index, const RoutePoint &origin);
};
#endif

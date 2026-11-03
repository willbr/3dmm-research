/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef ACTOR_POSE_H
#define ACTOR_POSE_H
#include "actor_state.h"

struct ActorPoseInputs
{
    BRA restX = aZero;
    BRA restY = aZero;
    BRA restZ = aZero;
    bool fixedPathOrientation = false; // Text templates retain authored orientation.
};

// Pose math over valid evaluated state and runtime route/event tables. Template
// orientation is an explicit input; no Body, Template or application is used.
struct ActorPoseEvaluator
{
    // Updates position/current rotation/path angles and returns the scaled,
    // rest-oriented matrix for a renderer to apply. Non-current rotation needs
    // a valid Add-event cursor, matching ActorFrameState's evaluated state.
    static void Update(ActorFrameState &state, const ActorTrack &track, const ActorPoseInputs &inputs,
                       const RoutePoint *position, BMAT34 *matrix);
    static void RouteOrientation(const ActorFrameState &state, const ActorTrack &track, BMAT34 *matrix, BRA *x = pvNil,
                                 BRA *y = pvNil, BRA *z = pvNil, LegacyUlong *axes = pvNil);
    // actionFlags uses the template's factnRotateX/Y bits. Banking remains zero.
    static void ApplyVectorRotation(LegacyUlong actionFlags, RoutePoint *vector, BMAT34 *matrix, BRA *x = pvNil,
                                    BRA *y = pvNil, BRA *z = pvNil, LegacyUlong *axes = pvNil);

  private:
    static void AccumulateTangent(const ActorTrack &track, RoutePoint *vector, LegacyLong node, BRS weight);
};
#endif

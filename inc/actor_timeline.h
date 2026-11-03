/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef ACTOR_TIMELINE_H
#define ACTOR_TIMELINE_H
#include "actor_frame.h"

// Borrowed action/cel metadata source. The resolver returns an unscaled step;
// it must not change authored tables or evaluated state. No renderer is needed.
struct ActorStepSource
{
    void *context = pvNil;
    bool (*resolve)(void *, LegacyLong action, LegacyLong cel, BRS *step) = pvNil;
    bool FStep(LegacyLong action, LegacyLong cel, BRS *step) const;
    bool FPlaybackStep(const ActorFrameState &state, BRS *step) const;
};

struct ActorQuickSeekResult
{
    bool success = false;
    bool valid = false;        // False requests full replay, e.g. across a vertical transition.
    bool applyEffects = false; // Motion sound, pose and cel application belong to the host.
};

struct ActorTimeline
{
    // Valid runtime tables required. Updates event frame caches as calculation
    // proceeds; failure leaves partial caches and the last attempted frame.
    // Recording deferral/lifetime-dirty flags remain host responsibilities.
    static bool FComputeLifetime(const ActorTrack &track, LegacyLong firstFrame, bool rejoining, const ActorStepSource &steps,
                                 LegacyLong *lastFrame);
    // Bounded host variant; the original runtime entry point remains available.
    static bool FComputeLifetime(const ActorTrack &track, LegacyLong firstFrame, bool rejoining, const ActorStepSource &steps,
                                 LegacyLong *lastFrame, LegacyLong maxFrames);
    static bool FIsStalled(const ActorTrack &track, LegacyLong firstEvent, RouteLocation *location, LegacyLong *lastEvent = pvNil);
    // Rolls back the individual step on resolver failure. A read-only probe
    // restores cel/frame/location; its output must not alias state.location.
    static bool FStepBack(ActorFrameState &state, const ActorTrack &track, const ActorStepSource &steps,
                          RouteLocation *location, bool updateState);
    // Only valid when no events intervene; failure may leave completed earlier
    // backward steps in state, matching runtime recovery semantics.
    static ActorQuickSeekResult QuickBackup(ActorFrameState &state, const ActorTrack &track, LegacyLong lastFrame,
                                            LegacyLong targetFrame, const ActorStepSource &steps);
};
#endif

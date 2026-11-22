/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_timeline.h"
ASSERTNAME
bool ActorStepSource::FStep(LegacyLong action, LegacyLong cel, BRS *step) const
{
    AssertVarMem(step);
    *step = rZero;
    EngineActorTimeline::Host host = {this, nullptr, step};
    const auto s = EngineActorTimeline::Services(&host);
    MMI32 value;
    const bool ok = mm_actor_step(action, cel, &value, &s) != 0;
    *step = value;
    return ok;
}
bool ActorStepSource::FPlaybackStep(const MMActorState &state, BRS *step) const
{
    AssertVarMem(step);
    *step = rZero;
    EngineActorTimeline::Host host = {this, nullptr, step};
    const auto s = EngineActorTimeline::Services(&host);
    MMI32 output;
    const bool ok = mm_actor_playback_step(&state, &output, &s) != 0;
    *step = output;
    return ok;
}
bool ActorTimeline::FComputeLifetime(const MMActorTrack &track, LegacyLong first, bool rejoining,
                                     const ActorStepSource &steps, LegacyLong *last)
{
    return FComputeLifetime(track, first, rejoining, steps, last, klwMax);
}
bool ActorTimeline::FComputeLifetime(const MMActorTrack &track, LegacyLong first, bool rejoining,
                                     const ActorStepSource &steps, LegacyLong *last, LegacyLong bound)
{
    AssertVarMem(last);
    EngineActorTimeline::Host host = {&steps, nullptr, nullptr, last};
    const auto s = EngineActorTimeline::Services(&host);
    MMI32 output;
    const bool ok = mm_actor_lifetime(track.route, track.events, first, rejoining, &output, bound, &s) != 0;
    *last = output;
    return ok;
}
bool ActorTimeline::FIsStalled(const MMActorTrack &track, LegacyLong first, RouteLocation *location, LegacyLong *last)
{
    AssertIn(first, 0, mm_actor_events_count(track.events));
    AssertNilOrVarMem(last);
    const ActorStepSource steps;
    EngineActorTimeline::Host host = {&steps};
    const auto s = EngineActorTimeline::Services(&host);
    MMI32 output;
    const bool ok = mm_actor_stalled(track.events, first, location, last ? &output : nullptr, &s) != 0;
    if (ok && last)
        *last = output;
    return ok;
}
bool ActorTimeline::FStepBack(MMActorState &state, const MMActorTrack &track, const ActorStepSource &steps,
                              RouteLocation *location, bool update)
{
    AssertVarMem(location);
    Assert(update || location != &state.location, "Probe output aliases restored state");
    EngineActorTimeline::Host host = {&steps, &state};
    const auto s = EngineActorTimeline::Services(&host);
    auto value = EngineActorMotion::Core(state);
    MMActorLocation out;
    auto destination = location == &state.location ? &value.location : &out;
    const bool ok = mm_actor_step_back(&value, track.route, track.events, destination, update, &s) != 0;
    EngineActorMotion::Publish(state, value);
    if (ok && location != &state.location)
        std::memcpy(location, &out, sizeof(out));
    return ok;
}
ActorQuickSeekResult ActorTimeline::QuickBackup(MMActorState &state, const MMActorTrack &track, LegacyLong last,
                                                LegacyLong target, const ActorStepSource &steps)
{
    Assert(target < state.frame, "Illegal call to _FBackupToFrm");
#ifdef DEBUG
    ActorEvent::Base event;
    mm_actor_events_get_fixed(track.events, state.nextEvent - 1, &event);
    Assert(event.nfrm < target, "Invalid Call to _FQuickBackupToFrm()");
#endif
    EngineActorTimeline::Host host = {&steps, &state};
    const auto s = EngineActorTimeline::Services(&host);
    auto value = EngineActorMotion::Core(state);
    const auto result = mm_actor_quick_backup(&value, track.route, track.events, last, target, &s);
    EngineActorMotion::Publish(state, value);
    return {result.success != 0, result.valid != 0, result.applyEffects != 0};
}

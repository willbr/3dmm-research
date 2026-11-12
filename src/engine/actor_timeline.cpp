/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_timeline.h"
ASSERTNAME
void EngineActorTimeline::Diagnostic(void *, int fault)
{
    switch (fault)
    {
    case THREE_MM_ACTOR_CORRUPT_ROUTE:
        Bug("Corrupted event list");
        break;
    case THREE_MM_ACTOR_UNKNOWN_EVENT:
        Assert(false, "Unknown event type");
        break;
    case THREE_MM_ACTOR_LIFETIME_LOGIC:
        Assert(false, "Logic error");
        break;
    }
}
bool ActorStepSource::FStep(LegacyLong action, LegacyLong cel, BRS *step) const
{
    AssertVarMem(step);
    *step = rZero;
    EngineActorTimeline::Host host = {this, nullptr, step};
    const auto s = EngineActorTimeline::Services(&host);
    ThreeMMI32 value;
    const bool ok = three_mm_actor_step(action, cel, &value, &s) != 0;
    *step = value;
    return ok;
}
bool ActorStepSource::FPlaybackStep(const ActorFrameState &state, BRS *step) const
{
    AssertVarMem(step);
    *step = rZero;
    EngineActorTimeline::Host host = {this, nullptr, step};
    const auto s = EngineActorTimeline::Services(&host);
    const auto value = EngineActorMotion::Core(state);
    ThreeMMI32 output;
    const bool ok = three_mm_actor_playback_step(&value, &output, &s) != 0;
    *step = output;
    return ok;
}
bool ActorTimeline::FComputeLifetime(const ActorTrack &track, LegacyLong first, bool rejoining,
                                     const ActorStepSource &steps, LegacyLong *last)
{
    return FComputeLifetime(track, first, rejoining, steps, last, klwMax);
}
bool ActorTimeline::FComputeLifetime(const ActorTrack &track, LegacyLong first, bool rejoining,
                                     const ActorStepSource &steps, LegacyLong *last, LegacyLong bound)
{
    AssertVarMem(last);
    EngineActorTimeline::Host host = {&steps, nullptr, nullptr, last};
    const auto s = EngineActorTimeline::Services(&host);
    ThreeMMI32 output;
    const bool ok = three_mm_actor_lifetime(track.Route(), track.Events(), first, rejoining, &output, bound, &s) != 0;
    *last = output;
    return ok;
}
bool ActorTimeline::FIsStalled(const ActorTrack &track, LegacyLong first, RouteLocation *location, LegacyLong *last)
{
    AssertIn(first, 0, track.Events()->IvMac());
    AssertNilOrVarMem(last);
    const ActorStepSource steps;
    EngineActorTimeline::Host host = {&steps};
    const auto s = EngineActorTimeline::Services(&host);
    const auto value = EngineActorMotion::Location(*location);
    ThreeMMI32 output;
    const bool ok = three_mm_actor_stalled(track.Events(), first, &value, last ? &output : nullptr, &s) != 0;
    if (ok && last)
        *last = output;
    return ok;
}
bool ActorTimeline::FStepBack(ActorFrameState &state, const ActorTrack &track, const ActorStepSource &steps,
                              RouteLocation *location, bool update)
{
    AssertVarMem(location);
    Assert(update || location != &state.location, "Probe output aliases restored state");
    EngineActorTimeline::Host host = {&steps, &state};
    const auto s = EngineActorTimeline::Services(&host);
    auto value = EngineActorMotion::Core(state);
    ThreeMMActorLocation out;
    auto destination = location == &state.location ? &value.location : &out;
    const bool ok = three_mm_actor_step_back(&value, track.Route(), track.Events(), destination, update, &s) != 0;
    EngineActorMotion::Publish(state, value);
    if (ok && location != &state.location)
        std::memcpy(location, &out, sizeof(out));
    return ok;
}
ActorQuickSeekResult ActorTimeline::QuickBackup(ActorFrameState &state, const ActorTrack &track, LegacyLong last,
                                                LegacyLong target, const ActorStepSource &steps)
{
    Assert(target < state.frame, "Illegal call to _FBackupToFrm");
#ifdef DEBUG
    ActorEvent::Base event;
    track.Events()->GetFixed(state.nextEvent - 1, &event);
    Assert(event.nfrm < target, "Invalid Call to _FQuickBackupToFrm()");
#endif
    EngineActorTimeline::Host host = {&steps, &state};
    const auto s = EngineActorTimeline::Services(&host);
    auto value = EngineActorMotion::Core(state);
    const auto result = three_mm_actor_quick_backup(&value, track.Route(), track.Events(), last, target, &s);
    EngineActorMotion::Publish(state, value);
    return {result.success != 0, result.valid != 0, result.applyEffects != 0};
}

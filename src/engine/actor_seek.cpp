/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_timeline.h"
ASSERTNAME
static_assert(static_cast<int>(ActorSeekCommandKind::Hide) == THREE_MM_ACTOR_SEEK_HIDE &&
                  static_cast<int>(ActorSeekCommandKind::ResetCostume) == THREE_MM_ACTOR_SEEK_RESET_COSTUME &&
                  static_cast<int>(ActorSeekCommandKind::ApplyQuickEffects) == THREE_MM_ACTOR_SEEK_QUICK_EFFECTS &&
                  static_cast<int>(ActorSeekCommandKind::EvaluateFrame) == THREE_MM_ACTOR_SEEK_FRAME &&
                  static_cast<int>(ActorSeekCommandKind::Finished) == THREE_MM_ACTOR_SEEK_FINISHED,
              "actor seek command ABI");
ActorSeekEvaluator::ActorSeekEvaluator(ActorFrameState &state, const ActorTrack &track, LegacyLong first,
                                       LegacyLong last, LegacyLong target, const ActorStepSource &steps)
    : _state(state), _track(track), _steps(steps)
{
    AssertIn(target, klwMin, klwMax);
    three_mm_actor_seek_init(&_sequence, first, last, target);
}
ActorSeekCommand ActorSeekEvaluator::Next()
{
    auto value = EngineActorMotion::Core(_state);
    EngineActorTimeline::Host host = {&_steps, &_state};
    const auto s = EngineActorTimeline::Services(&host);
    ThreeMMActorSeekCommand command = {};
    const int ok = three_mm_actor_seek_next(&_sequence, &value, _track.Route(), _track.Events(), &command, &s);
    Assert(ok, "Unacknowledged seek command");
    (void)ok;
    EngineActorMotion::Publish(_state, value);
    return {static_cast<ActorSeekCommandKind>(command.kind), command.displayFrame != 0};
}
void ActorSeekEvaluator::Complete(bool success)
{
    auto value = EngineActorMotion::Core(_state);
    const int ok = three_mm_actor_seek_complete(&_sequence, &value, success);
    Assert(ok, "No seek command to complete");
    (void)ok;
    EngineActorMotion::Publish(_state, value);
}

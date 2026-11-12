/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_motion.h"
ASSERTNAME
static_assert(static_cast<int>(ActorFrameCommandKind::CheckEntrance) == THREE_MM_ACTOR_CHECK_ENTRANCE &&
                  static_cast<int>(ActorFrameCommandKind::ApplyEvent) == THREE_MM_ACTOR_APPLY_EVENT &&
                  static_cast<int>(ActorFrameCommandKind::Finished) == THREE_MM_ACTOR_FRAME_FINISHED,
              "actor frame command ABI");
void ActorFrameEvaluator::_SyncResult()
{
    _result.success = _sequence.result.success != 0;
    _result.aborted = _sequence.result.aborted != 0;
    _result.positionDirty = _sequence.result.positionDirty != 0;
    _result.soundInFrame = _sequence.result.soundInFrame != 0;
    _result.considerMotionSounds = _sequence.result.considerMotionSounds != 0;
    _result.positionBody = _sequence.result.positionBody != 0;
    _result.updatePose = _sequence.result.updatePose != 0;
    _result.selectCel = _sequence.result.selectCel != 0;
}
ActorFrameEvaluator::ActorFrameEvaluator(ActorFrameState &state, const ActorTrack &track,
                                         const ActorFrameInputs &inputs, bool dirty)
    : _state(state), _track(track)
{
    AssertPo(track.Route(), 0);
    AssertPo(track.Events(), 0);
    auto value = EngineActorMotion::Core(state);
    const auto services = EngineActorMotion::Services();
    const ThreeMMActorFrameInputs in = {inputs.firstFrame, inputs.step,         inputs.stepSucceeded, inputs.recording,
                                        inputs.rejoining,  inputs.displayFrame, inputs.suppressSounds};
    three_mm_actor_frame_init(&_sequence, &value, track.Route(), track.Events(), &in, dirty, &services);
    EngineActorMotion::Publish(state, value);
    _SyncResult();
}
bool ActorFrameEvaluator::FSubrouteEventsDone(const ActorTrack &track, LegacyLong index, RouteLocation location)
{
    AssertPo(track.Events(), 0);
    AssertIn(index, 0, track.Events()->IvMac() + 1);
    const auto point = EngineActorMotion::Location(location);
    const auto services = EngineActorMotion::Services();
    return three_mm_actor_subroute_events_done(track.Events(), index, &point, &services) != 0;
}
ActorFrameCommand ActorFrameEvaluator::Next()
{
    auto value = EngineActorMotion::Core(_state);
    const auto services = EngineActorMotion::Services();
    ThreeMMActorFrameCommand command = {};
    const int ok = three_mm_actor_frame_next(&_sequence, &value, _track.Events(), &command, &services);
    Assert(ok, "Unanswered entrance query");
    (void)ok;
    EngineActorMotion::Publish(_state, value);
    _SyncResult();
    return {static_cast<ActorFrameCommandKind>(command.kind), command.eventIndex};
}
void ActorFrameEvaluator::Complete(bool succeeded)
{
    auto value = EngineActorMotion::Core(_state);
    const auto services = EngineActorMotion::Services();
    const int ok = three_mm_actor_frame_complete(&_sequence, &value, _track.Events(), succeeded, &services);
    Assert(ok, "No frame command to complete");
    (void)ok;
    EngineActorMotion::Publish(_state, value);
    _SyncResult();
}

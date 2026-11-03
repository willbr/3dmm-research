/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef ACTOR_SEEK_H
#define ACTOR_SEEK_H
#include "actor_timeline.h"

enum class ActorSeekCommandKind
{
    Hide,
    ResetCostume, // Also clear recording mode; costume failure remains best-effort.
    ApplyQuickEffects,
    EvaluateFrame,
    Finished
};

struct ActorSeekCommand
{
    ActorSeekCommandKind kind;
    bool displayFrame = false;
};

// Outer seek policy over borrowed state/tables and action-step metadata. The
// host applies commands in order, acknowledges each with Complete, and stops
// after Finished. EvaluateFrame uses the frame sequencer and shares one dirty
// accumulator across the seek. Table frame caches may change during evaluation;
// their structure must remain stable. No application, Body or audio is owned.
class ActorSeekEvaluator
{
  public:
    ActorSeekEvaluator(ActorFrameState &state, const ActorTrack &track, LegacyLong firstFrame, LegacyLong lastFrame,
                       LegacyLong targetFrame, const ActorStepSource &steps);
    ActorSeekEvaluator(const ActorSeekEvaluator &) = delete;
    ActorSeekEvaluator &operator=(const ActorSeekEvaluator &) = delete;
    ActorSeekCommand Next();
    // Hide and ResetCostume acknowledgements are best-effort. A failed quick
    // effect stops the seek; failed frame evaluation accumulates failure while
    // still evaluating later frames, preserving the runtime recovery behavior.
    void Complete(bool success);
    bool Success() const
    {
        return _success;
    }

  private:
    enum class Phase
    {
        Initialize,
        Rewind,
        PrepareFrames,
        Frames,
        Done
    };
    ActorFrameState &_state;
    const ActorTrack &_track;
    ActorStepSource _steps;
    LegacyLong _firstFrame;
    LegacyLong _lastFrame;
    LegacyLong _targetFrame;
    Phase _phase = Phase::Initialize;
    bool _midPath = false;
    bool _waiting = false;
    bool _success = true;
    ActorSeekCommandKind _pending = ActorSeekCommandKind::Finished;
    ActorSeekCommand _Command(ActorSeekCommandKind kind, bool display = false);
    void _Rewind();
};
#endif

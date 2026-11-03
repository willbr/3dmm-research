/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef ACTOR_FRAME_H
#define ACTOR_FRAME_H
#include "actor_state.h"

struct ActorFrameInputs
{
    LegacyLong firstFrame = 0;
    BRS step = rZero; // Resolved, scaled distance for this frame.
    bool stepSucceeded = true;
    bool recording = false;
    bool rejoining = false;
    bool displayFrame = true;
    bool suppressSounds = false;
};

enum class ActorFrameCommandKind
{
    CheckEntrance,
    ApplyEvent,
    Finished
};

struct ActorFrameCommand
{
    ActorFrameCommandKind kind;
    LegacyLong eventIndex;
};

struct ActorFrameResult
{
    bool success = true;
    bool aborted = false;
    bool positionDirty = false;
    bool soundInFrame = false;
    bool considerMotionSounds = false; // Host also checks the current lifetime limit.
    bool positionBody = false;
    bool updatePose = false;
    bool selectCel = false;
};

// One-frame sequencer. Borrows valid runtime tables/state for its lifetime;
// owns no resources and calls no host services. Next returns ordered commands.
// Complete answers an entrance query, or acknowledges event application after
// the host updates state (without incrementing nextEvent). Event application
// may use ActorFrameState's pure operations and/or resource/effect adapters.
// Acknowledge each request before proceeding, and stop after Finished. Entrance
// queries may refresh event frame caches and host lifetime metadata. Tables
// must not be structurally edited while a frame is in progress.
class ActorFrameEvaluator
{
  public:
    ActorFrameEvaluator(ActorFrameState &state, const ActorTrack &track, const ActorFrameInputs &inputs,
                        bool positionDirty);
    ActorFrameEvaluator(const ActorFrameEvaluator &) = delete;
    ActorFrameEvaluator &operator=(const ActorFrameEvaluator &) = delete;
    ActorFrameCommand Next();
    void Complete(bool succeededOrDue);
    const ActorFrameResult &Result() const
    {
        return _result;
    }
    static bool FSubrouteEventsDone(const ActorTrack &track, LegacyLong index, RouteLocation location);

  private:
    enum class Phase
    {
        Entrances,
        Events,
        Done
    };
    enum class Pending
    {
        None,
        EntranceQuery,
        EntranceEvent,
        OrderedEntranceQuery,
        OrderedEntranceEvent,
        Event
    };
    ActorFrameState &_state;
    const ActorTrack &_track;
    ActorFrameInputs _inputs;
    ActorFrameResult _result;
    Phase _phase;
    Pending _pending = Pending::None;
    LegacyLong _scan = 0;
    LegacyLong _entranceScan;
    LegacyLong _entrance = ivNil;
    bool _freezeCel;
    bool _endRoute;
    RoutePoint _oldPosition;
    void _Finish();
    void _Abort();
};
#endif

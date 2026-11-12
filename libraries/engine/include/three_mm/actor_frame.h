/* Actor frame sequencing converted from engine actor_frame.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_FRAME_H
#define THREE_MM_ENGINE_ACTOR_FRAME_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMActorFrameInputs
    {
        ThreeMMI32 firstFrame, step;
        int stepSucceeded, recording, rejoining, displayFrame, suppressSounds;
    } ThreeMMActorFrameInputs;
    typedef struct ThreeMMActorFrameResult
    {
        int success, aborted, positionDirty, soundInFrame, considerMotionSounds, positionBody, updatePose, selectCel;
    } ThreeMMActorFrameResult;
    enum
    {
        THREE_MM_ACTOR_CHECK_ENTRANCE,
        THREE_MM_ACTOR_APPLY_EVENT,
        THREE_MM_ACTOR_FRAME_FINISHED
    };
    typedef struct ThreeMMActorFrameCommand
    {
        int kind;
        ThreeMMI32 eventIndex;
    } ThreeMMActorFrameCommand;
    /* Caller-owned sequencer storage. Implementation fields must not be modified.
     * State and valid runtime tables are borrowed, with no resource ownership. The
     * host may update actor state and event frame caches between Next and Complete,
     * but must not increment nextEvent or structurally change the tables. */
    typedef struct ThreeMMActorFrame
    {
        ThreeMMActorFrameInputs inputs;
        ThreeMMActorFrameResult result;
        int phase, pending;
        ThreeMMI32 scan, entranceScan, entrance;
        int freezeCel, endRoute;
        ThreeMMActorPoint oldPosition;
    } ThreeMMActorFrame;
    void three_mm_actor_frame_init(ThreeMMActorFrame *, ThreeMMActorState *, void *route, void *events,
                                   const ThreeMMActorFrameInputs *, int position_dirty,
                                   const ThreeMMActorMotionServices *);
    /* Returns zero for an unanswered entrance query, without changing state,
     * sequencer or output. Apply requests may be fetched repeatedly until Complete.
     * Stop requesting commands after Finished. */
    int three_mm_actor_frame_next(ThreeMMActorFrame *, ThreeMMActorState *, void *events, ThreeMMActorFrameCommand *,
                                  const ThreeMMActorMotionServices *);
    /* Zero means no command was pending; state and sequencer remain unchanged.
     * Entrance failures abort; ordinary failures retain the event cursor, mark
     * failure, and advance only the scan, preserving the legacy retry ordering. */
    int three_mm_actor_frame_complete(ThreeMMActorFrame *, ThreeMMActorState *, void *events, int succeeded_or_due,
                                      const ThreeMMActorMotionServices *);
    int three_mm_actor_subroute_events_done(void *events, ThreeMMI32 index, const ThreeMMActorLocation *,
                                            const ThreeMMActorMotionServices *);
#ifdef __cplusplus
}
#endif
#endif

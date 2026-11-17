/* Actor frame sequencing converted from engine actor_frame.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_FRAME_H
#define MM_ENGINE_ACTOR_FRAME_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMActorFrameInputs
    {
        MMI32 firstFrame, step;
        int stepSucceeded, recording, rejoining, displayFrame, suppressSounds;
    } MMActorFrameInputs;
    typedef struct MMActorFrameResult
    {
        int success, aborted, positionDirty, soundInFrame, considerMotionSounds, positionBody, updatePose, selectCel;
    } MMActorFrameResult;
    enum
    {
        MM_ACTOR_CHECK_ENTRANCE,
        MM_ACTOR_APPLY_EVENT,
        MM_ACTOR_FRAME_FINISHED
    };
    typedef struct MMActorFrameCommand
    {
        int kind;
        MMI32 eventIndex;
    } MMActorFrameCommand;
    /* Caller-owned sequencer storage. Implementation fields must not be modified.
     * State and valid runtime tables are borrowed, with no resource ownership. The
     * host may update actor state and event frame caches between Next and Complete,
     * but must not increment nextEvent or structurally change the tables. */
    typedef struct MMActorFrame
    {
        MMActorFrameInputs inputs;
        MMActorFrameResult result;
        int phase, pending;
        MMI32 scan, entranceScan, entrance;
        int freezeCel, endRoute;
        MMActorPoint oldPosition;
    } MMActorFrame;
    void mm_actor_frame_init(MMActorFrame *, MMActorState *, void *route, void *events,
                                   const MMActorFrameInputs *, int position_dirty,
                                   const MMActorMotionServices *);
    /* Returns zero for an unanswered entrance query, without changing state,
     * sequencer or output. Apply requests may be fetched repeatedly until Complete.
     * Stop requesting commands after Finished. */
    int mm_actor_frame_next(MMActorFrame *, MMActorState *, void *events, MMActorFrameCommand *,
                                  const MMActorMotionServices *);
    /* Zero means no command was pending; state and sequencer remain unchanged.
     * Entrance failures abort; ordinary failures retain the event cursor, mark
     * failure, and advance only the scan, preserving the legacy retry ordering. */
    int mm_actor_frame_complete(MMActorFrame *, MMActorState *, void *events, int succeeded_or_due,
                                      const MMActorMotionServices *);
    typedef struct MMActorFrameEffects
    {
        void *context;
        int (*event)(void *, const MMActorFrameCommand *, MMActorState *);
        int (*motion_sounds)(void *);
        void (*position_body)(void *);
        int (*select_cel)(void *);
        void (*update_pose)(void *);
    } MMActorFrameEffects;
    /* Deliver the sequencer's commands synchronously. Zero/one answers a
     * command, while a negative result stops immediately. Return 1 after the
     * last command and publish the result even if a frame effect failed.
     * A terminal error leaves the output unchanged and retains applied state. */
    int mm_actor_frame_run(MMActorState *, void *route, void *events, const MMActorFrameInputs *,
                           int position_dirty, const MMActorMotionServices *, const MMActorFrameEffects *,
                           MMActorFrameResult *);
    enum { MM_ACTOR_FRAME_STUDIO, MM_ACTOR_FRAME_PREVIEW };
    /* Publish frame flags and deliver display effects in order. Studio keeps
     * best-effort sound and display effects after ordinary frame failure.
     * Preview propagates sound/cel errors and selects cels when positioning.
     * Optional dirty is updated and cleared after positioning. Optional sound
     * is set only when this frame had a sound cue. */
    int mm_actor_frame_display(const MMActorFrameResult *, const MMActorState *, MMI32 last_frame, int profile,
                               int *position_dirty, int *sound, const MMActorFrameEffects *);
    int mm_actor_subroute_events_done(void *events, MMI32 index, const MMActorLocation *,
                                            const MMActorMotionServices *);
#ifdef __cplusplus
}
#endif
#endif

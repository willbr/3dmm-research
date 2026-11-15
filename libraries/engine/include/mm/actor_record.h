/* Actor recording converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_RECORD_H
#define MM_ENGINE_ACTOR_RECORD_H
#include "actor_subroute_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorRecording
{
    MMActorLocation insertion;
    MMU32 timestamp;
    int recording, rejoining, inserted;
    MMI32 gap;
    MMActorPoint raw;
} MMActorRecording;
typedef struct MMActorRecordResult
{
    int frame, route, written;
    void *context;
    /* Optional observer for native bool outputs. Runs at the original output
     * assignments, before scene seek/rollback, without mutation or reentry.
     * changed bit 1 writes frame, bit 2 writes route, preserving output aliasing. */
    void (*publish)(void *, int frame, int route, unsigned changed);
} MMActorRecordResult;
enum
{
    MM_RECORD_BAD_LIFETIME, MM_RECORD_UNFREEZE_FAILED,
    MM_RECORD_FREEZE_FAILED, MM_RECORD_STEP_FAILED
};
enum { MM_RECORD_FREEZE = 1, MM_RECORD_GROUND = 2 };
typedef struct MMActorRecordServices
{
    MMActorSubrouteEditServices subroute;
    int (*reserve_points)(void *route, MMU32);
    MMI32 (*distance)(void *context, const MMActorPoint *);
    void (*publish_record)(void *context, const MMActorRecording *);
    /* Temporary native undo transaction. May swap the owning route/event tables;
     * failure paths return immediately afterward and never reuse those handles.
     * Both state records are input/output, including on failed scene seeks. */
    void (*restore)(void *context, void *undo, MMActorState *, MMActorRecording *);
    int (*seek_scene)(void *context, MMActorState *, MMActorRecording *, MMI32 frame);
    void (*diagnostic)(void *context, int fault);
} MMActorRecordServices;
/* Runtime tables and recording invariants are borrowed. Callbacks cannot reenter
 * the operation. Distance is BRender's primitive scalar length; all threshold,
 * smoothing, sampling, ground, splice and failure decisions belong to C.
 * Timestamp differences wrap at 32 bits. Beginning preserves the old timestamp.
 * Move leaves result untouched before reservations/freeze transitions finish;
 * afterward written=1 and frame/route retain their values even after rollback. */
int mm_actor_record_valid(const MMActorRecording *, const MMActorPoint *, MMU32 now,
    const MMActorRecordServices *);
int mm_actor_record_step(const MMActorState *, MMI32 *, const MMActorTimelineServices *);
int mm_actor_record_begin(MMActorState *, MMActorRecording *, void *route, void *events,
    int replace, void *undo, const MMActorMotionServices *, const MMActorRecordServices *);
int mm_actor_record_move(MMActorState *, MMActorRecording *, MMActorSounds *,
    void *route, void *events, MMI32 actor, const MMActorPoint *, MMU32 flags, MMU32 now,
    MMActorRecordResult *, void *undo, const MMActorMotionServices *, const MMActorRecordServices *);
int mm_actor_record_end(MMActorState *, MMActorRecording *, MMActorSounds *,
    void *route, void *events, MMI32 actor, int replace, void *undo,
    const MMActorMotionServices *, const MMActorRecordServices *);
#ifdef __cplusplus
}
#endif
#endif

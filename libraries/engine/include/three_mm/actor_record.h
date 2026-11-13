/* Actor recording converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_RECORD_H
#define THREE_MM_ENGINE_ACTOR_RECORD_H
#include "actor_subroute_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorRecording
{
    ThreeMMActorLocation insertion;
    ThreeMMU32 timestamp;
    int recording, rejoining, inserted;
    ThreeMMI32 gap;
    ThreeMMActorPoint raw;
} ThreeMMActorRecording;
typedef struct ThreeMMActorRecordResult
{
    int frame, route, written;
    void *context;
    /* Optional observer for native bool outputs. Runs at the original output
     * assignments, before scene seek/rollback, without mutation or reentry.
     * changed bit 1 writes frame, bit 2 writes route, preserving output aliasing. */
    void (*publish)(void *, int frame, int route, unsigned changed);
} ThreeMMActorRecordResult;
enum
{
    THREE_MM_RECORD_BAD_LIFETIME, THREE_MM_RECORD_UNFREEZE_FAILED,
    THREE_MM_RECORD_FREEZE_FAILED, THREE_MM_RECORD_STEP_FAILED
};
enum { THREE_MM_RECORD_FREEZE = 1, THREE_MM_RECORD_GROUND = 2 };
typedef struct ThreeMMActorRecordServices
{
    ThreeMMActorSubrouteEditServices subroute;
    int (*reserve_points)(void *route, ThreeMMU32);
    ThreeMMI32 (*distance)(void *context, const ThreeMMActorPoint *);
    void (*publish_record)(void *context, const ThreeMMActorRecording *);
    /* Temporary native undo transaction. May swap the owning route/event tables;
     * failure paths return immediately afterward and never reuse those handles.
     * Both state records are input/output, including on failed scene seeks. */
    void (*restore)(void *context, void *undo, ThreeMMActorState *, ThreeMMActorRecording *);
    int (*seek_scene)(void *context, ThreeMMActorState *, ThreeMMActorRecording *, ThreeMMI32 frame);
    void (*diagnostic)(void *context, int fault);
} ThreeMMActorRecordServices;
/* Runtime tables and recording invariants are borrowed. Callbacks cannot reenter
 * the operation. Distance is BRender's primitive scalar length; all threshold,
 * smoothing, sampling, ground, splice and failure decisions belong to C.
 * Timestamp differences wrap at 32 bits. Beginning preserves the old timestamp.
 * Move leaves result untouched before reservations/freeze transitions finish;
 * afterward written=1 and frame/route retain their values even after rollback. */
int three_mm_actor_record_valid(const ThreeMMActorRecording *, const ThreeMMActorPoint *, ThreeMMU32 now,
    const ThreeMMActorRecordServices *);
int three_mm_actor_record_step(const ThreeMMActorState *, ThreeMMI32 *, const ThreeMMActorTimelineServices *);
int three_mm_actor_record_begin(ThreeMMActorState *, ThreeMMActorRecording *, void *route, void *events,
    int replace, void *undo, const ThreeMMActorMotionServices *, const ThreeMMActorRecordServices *);
int three_mm_actor_record_move(ThreeMMActorState *, ThreeMMActorRecording *, ThreeMMActorSounds *,
    void *route, void *events, ThreeMMI32 actor, const ThreeMMActorPoint *, ThreeMMU32 flags, ThreeMMU32 now,
    ThreeMMActorRecordResult *, void *undo, const ThreeMMActorMotionServices *, const ThreeMMActorRecordServices *);
int three_mm_actor_record_end(ThreeMMActorState *, ThreeMMActorRecording *, ThreeMMActorSounds *,
    void *route, void *events, ThreeMMI32 actor, int replace, void *undo,
    const ThreeMMActorMotionServices *, const ThreeMMActorRecordServices *);
#ifdef __cplusplus
}
#endif
#endif

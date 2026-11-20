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
    /* Optional output observer. Runs at the original output assignments,
     * before scene seek/rollback, without mutation or reentry.
     * changed bit 1 writes frame, bit 2 writes route, preserving output aliasing. */
    void (*publish)(void *, int frame, int route, unsigned changed);
} MMActorRecordResult;
enum
{
    MM_RECORD_BAD_LIFETIME, MM_RECORD_UNFREEZE_FAILED,
    MM_RECORD_FREEZE_FAILED, MM_RECORD_STEP_FAILED
};
enum { MM_RECORD_FREEZE = 1, MM_RECORD_GROUND = 2 };
struct MMActorOwnerServices;
struct MMActorTransactionServices;
struct MMPreviewActor;
struct MMPreviewActorServices;
typedef struct MMActorRecordServices
{
    MMActorSubrouteEditServices subroute;
    MMI32 (*distance)(void *context, const MMActorPoint *);
    /* Live actor identity and resources for restoring a recording snapshot.
     * Restoration may swap track tables. Callers return immediately afterward. */
    void *live_actor;
    const struct MMActorOwnerServices *owners;
    /* Private drafts advance their actor directly and are discarded on failure. */
    struct MMPreviewActor *draft;
    const struct MMPreviewActorServices *draft_playback;
    void (*publish_record)(void *context, const MMActorRecording *);
    /* Optional rollback for callers with borrowed evaluator tables. */
    void (*restore)(void *context, void *undo, MMActorState *, MMActorRecording *);
    int (*seek_scene)(void *context, MMActorState *, MMActorRecording *, MMI32 frame);
    void (*diagnostic)(void *context, int fault);
    /* Optional authored step for templates whose current cel has zero stride. */
    MMI32 stationary_step;
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

/* Run against the Studio owner bound in subroute.live. Use a working copy of
 * state and recording while effects publish intermediate state to the owner. */
int mm_actor_owner_record_begin(int replace, void *undo, const MMActorRecordServices *);
int mm_actor_owner_record_move(const MMActorPoint *, MMU32 flags, MMU32 now,
    MMActorRecordResult *, void *undo, const MMActorRecordServices *);
int mm_actor_owner_record_end(int replace, void *undo, const MMActorRecordServices *);

/* State for a record-tool gesture. The actor recording state above owns the
 * route splice. This record owns the gesture's rollback snapshot and cadence. */
typedef struct MMActorRecordGesture
{
    void *restore;
    MMU32 last_sample;
    MMI32 action;
    int replace, cycling, restore_default;
} MMActorRecordGesture;
enum
{
    MM_RECORD_GESTURE_SHIFT = 1,
    MM_RECORD_GESTURE_COMMAND = 2,
    MM_RECORD_FRAME_TICKS = 166,
    MM_RECORD_CEL_TICKS = 3 * MM_RECORD_FRAME_TICKS
};
enum
{
    MM_RECORD_GESTURE_IDLE,
    MM_RECORD_GESTURE_CEL,
    MM_RECORD_GESTURE_FRAME,
    MM_RECORD_GESTURE_POINT
};
typedef struct MMActorRecordGestureServices
{
    void *context;
    MMU32 (*clock_now)(void *);
    void (*hide_cursor)(void *);
    void (*recording_started)(void *, int replace);
} MMActorRecordGestureServices;
int mm_actor_record_gesture_start(MMActorRecordGesture *, unsigned modifiers,
    const MMActorRecordServices *, const struct MMActorTransactionServices *,
    const MMActorRecordGestureServices *);
int mm_actor_record_gesture_sample(MMActorRecordGesture *, const MMActorPoint *,
    unsigned modifiers, int ground, MMU32 now, const MMActorRecordServices *,
    const MMActorRecordGestureServices *);
/* End also restores the default mode after a temporary rerecord. An absent
 * actor still releases the snapshot and restores the mode. */
void mm_actor_record_gesture_end(MMActorRecordGesture *, const MMActorRecordServices *,
    const struct MMActorOwnerServices *);
void mm_actor_record_gesture_discard(MMActorRecordGesture *, const struct MMActorOwnerServices *);
void mm_actor_record_gesture_filter_axes(const MMActorRecordGesture *, int recording, MMActorPoint *);
#ifdef __cplusplus
}
#endif
#endif

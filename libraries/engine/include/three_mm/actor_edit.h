/* Actor event editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_EDIT_H
#define THREE_MM_ENGINE_ACTOR_EDIT_H
#include "actor_sound.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorEditRange { ThreeMMI32 first, last; } ThreeMMActorEditRange;
typedef struct ThreeMMActorEditServices
{
    void *context;
    void (*close_event_tag)(void *, ThreeMMI32 index);
    void (*delete_event)(void *, ThreeMMI32 index);
    int (*scene_frame)(void *, ThreeMMI32 *);
    void (*update_sound_frame)(void *);
    void (*publish)(void *, const ThreeMMActorState *);
    int (*insert_event)(void *, ThreeMMI32 index, ThreeMMU32 bytes, const void *, const ThreeMMActorEvent *);
    void (*duplicate_event_tag)(void *, ThreeMMI32 index);
    void (*write_payload)(void *, ThreeMMI32 index, const void *);
    void (*copy_payload)(void *, ThreeMMI32 destination, ThreeMMI32 source);
    /* Apply may update state, including on failure. Other callbacks observe it. */
    int (*apply_event)(void *, ThreeMMActorState *, ThreeMMI32 index);
    void (*dirty)(void *);
    void (*get_range)(void *, ThreeMMActorEditRange *);
    void (*invalidate_range)(void *);
    void (*publish_range)(void *, const ThreeMMActorEditRange *);
} ThreeMMActorEditServices;
/* Borrow valid runtime tables/state. Close tags before deletion; adjust only
 * cursors strictly after the deleted entry, then notify the current-scene sound
 * indicator. Callbacks may observe published state, but cannot mutate/re-enter it. */
void three_mm_actor_edit_remove(ThreeMMActorState *, void *events, ThreeMMI32 index, int update_state,
                                const ThreeMMActorMotionServices *, const ThreeMMActorEditServices *);
/* NULL payload reserves a row without duplicating tags or adjusting cursors. */
int three_mm_actor_edit_insert(ThreeMMActorState *, ThreeMMI32 index, ThreeMMU32 bytes, const void *payload,
                               const ThreeMMActorEvent *, int update_state, const ThreeMMActorEditServices *);
/* Merge retains legacy scan/early-return and cursor ordering. Optional result
 * storage is assigned before deleting the merged-away row. Costume replacement
 * closes the old reference and retains the replacement before removing its row. */
void three_mm_actor_edit_merge(ThreeMMActorState *, ThreeMMActorSounds *, void *events, ThreeMMI32 actor,
                               ThreeMMI32 first, ThreeMMI32 added, ThreeMMI32 *result,
                               const ThreeMMActorMotionServices *, const ThreeMMActorEditServices *);
/* Insert, merge, apply, dirty and lifetime-range decisions. Keeps BUG1870: Move
 * updates only subroute translation from the original delta, without replaying
 * accumulated moves or refreshing origin. Failed apply retains prior changes. */
int three_mm_actor_edit_add_apply(ThreeMMActorState *, ThreeMMActorSounds *, void *events, ThreeMMI32 actor,
                                   ThreeMMI32 kind, ThreeMMU32 bytes, const void *,
                                   const ThreeMMActorMotionServices *, const ThreeMMActorEditServices *);
#ifdef __cplusplus
}
#endif
#endif

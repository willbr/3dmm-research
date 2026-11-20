/* Actor event editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_EDIT_H
#define MM_ENGINE_ACTOR_EDIT_H
#include "actor_events.h"
#include "actor_sound.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorEditRange { MMI32 first, last; } MMActorEditRange;
typedef struct MMActorEditTables
{
    void *context;
    const MMTagServices *tags;
    void (*close_event_tag)(void *, MMI32 index, const MMTagServices *);
    void (*delete_event)(void *, MMI32 index);
    int (*insert_event)(void *, MMI32 index, MMU32 bytes, const void *, const MMActorEvent *);
    void (*duplicate_event_tag)(void *, MMI32 index, const MMTagServices *);
    void (*write_payload)(void *, MMI32 index, const void *);
    void (*copy_payload)(void *, MMI32 destination, MMI32 source);
} MMActorEditTables;
/* Borrow the event table and its tag services for the duration of the edit. */
MMActorEditTables mm_actor_edit_tables(MMActorEventTable *, const MMTagServices *);
typedef struct MMActorEditServices
{
    void *context;
    MMActorEditTables tables;
    /* Unbound actors can omit scene_frame and the corresponding notification. */
    int (*scene_frame)(void *, MMI32 *);
    void (*update_sound_frame)(void *);
    void (*publish)(void *, const MMActorState *);
    /* Apply may update state, including on failure. Other callbacks observe it. */
    int (*apply_event)(void *, MMActorState *, MMI32 index);
    void (*dirty)(void *);
    void (*get_range)(void *, MMActorEditRange *);
    void (*invalidate_range)(void *);
    void (*publish_range)(void *, const MMActorEditRange *);
} MMActorEditServices;
/* Borrow valid runtime tables/state. Close tags before deletion; adjust only
 * cursors strictly after the deleted entry, then notify the current-scene sound
 * indicator. Callbacks may observe published state, but cannot mutate/re-enter it. */
void mm_actor_edit_remove(MMActorState *, void *events, MMI32 index, int update_state,
                                const MMActorMotionServices *, const MMActorEditServices *);
/* NULL payload reserves a row without duplicating tags or adjusting cursors. */
int mm_actor_edit_insert(MMActorState *, MMI32 index, MMU32 bytes, const void *payload,
                               const MMActorEvent *, int update_state, const MMActorEditServices *);
/* Merge retains legacy scan/early-return and cursor ordering. Optional result
 * storage is assigned before deleting the merged-away row. Costume replacement
 * closes the old reference and retains the replacement before removing its row. */
void mm_actor_edit_merge(MMActorState *, MMActorSounds *, void *events, MMI32 actor,
                               MMI32 first, MMI32 added, MMI32 *result,
                               const MMActorMotionServices *, const MMActorEditServices *);
/* Insert, merge, apply, dirty and lifetime-range decisions. Keeps BUG1870: Move
 * updates only subroute translation from the original delta, without replaying
 * accumulated moves or refreshing origin. Failed apply retains prior changes. */
int mm_actor_edit_add_apply(MMActorState *, MMActorSounds *, void *events, MMI32 actor,
                                   MMI32 kind, MMU32 bytes, const void *,
                                   const MMActorMotionServices *, const MMActorEditServices *);
#ifdef __cplusplus
}
#endif
#endif

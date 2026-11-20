/* Actor action and fill editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_ACTION_EDIT_H
#define MM_ENGINE_ACTOR_ACTION_EDIT_H
#include "actor_sound_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorFillServices
{
    MMActorEditServices edits;
    void (*lock_events)(void *);
    void (*unlock_events)(void *);
    const void *(*payload_view)(void *, MMI32);
} MMActorFillServices;
/* Borrows valid runtime tables. Transform comparisons borrow native table views
 * under one lock, retaining their original aliasing during deletions. Action
 * fill compares actions independently of cel and preserves subroute-end freezes.
 * Flags are 1U << MMActorEventKind. Callbacks cannot re-enter these edits. */
void mm_actor_edit_transform_fill(MMActorState *, void *events, MMI32 kind,
    const void *payload, MMU32 bytes, MMI32 first, MMI32 compare, MMU32 flags,
    const MMActorMotionServices *, const MMActorFillServices *);
void mm_actor_edit_action_fill(MMActorState *, void *events, MMI32 first,
    MMI32 previous, MMI32 replacement, MMU32 flags,
    const MMActorMotionServices *, const MMActorEditServices *);
typedef struct MMActorActionEditServices
{
    void *context;
    MMActorSoundEditServices sounds;
    int (*action_flags)(void *, MMI32, MMU32 *);
    int (*text_template)(void *);
    void (*mark_life_dirty)(void *);
} MMActorActionEditServices;
/* Ordered partial edit: reserve, query metadata, remove old motion events, fill,
 * apply action, insert defaults, invalidate lifetime, freeze, then optional step.
 * Failures retain completed mutations. Static metadata is queried even when no
 * previous action event exists. Host metadata flags use the legacy static bit 8. */
int mm_actor_edit_set_action(MMActorState *, MMActorSounds *, void *events, MMI32 actor,
    MMI32 action, MMI32 cel, int freeze, MMU32 external_child,
    const MMActorMotionServices *, const MMActorActionEditServices *);
int mm_actor_edit_set_step(MMActorState *, MMActorSounds *, void *events, MMI32 actor,
    MMI32 step, const MMActorMotionServices *, const MMActorEditServices *);
#ifdef __cplusplus
}
#endif
#endif

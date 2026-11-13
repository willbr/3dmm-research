/* Actor action and fill editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_ACTION_EDIT_H
#define THREE_MM_ENGINE_ACTOR_ACTION_EDIT_H
#include "actor_sound_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorFillServices
{
    ThreeMMActorEditServices edits;
    void (*lock_events)(void *);
    void (*unlock_events)(void *);
    const void *(*payload_view)(void *, ThreeMMI32);
} ThreeMMActorFillServices;
/* Borrows valid runtime tables. Transform comparisons borrow native table views
 * under one lock, retaining their original aliasing during deletions. Action
 * fill compares actions independently of cel and preserves subroute-end freezes.
 * Flags are 1U << ThreeMMActorEventKind. Callbacks cannot re-enter these edits. */
void three_mm_actor_edit_transform_fill(ThreeMMActorState *, void *events, ThreeMMI32 kind,
    const void *payload, ThreeMMU32 bytes, ThreeMMI32 first, ThreeMMI32 compare, ThreeMMU32 flags,
    const ThreeMMActorMotionServices *, const ThreeMMActorFillServices *);
void three_mm_actor_edit_action_fill(ThreeMMActorState *, void *events, ThreeMMI32 first,
    ThreeMMI32 previous, ThreeMMI32 replacement, ThreeMMU32 flags,
    const ThreeMMActorMotionServices *, const ThreeMMActorEditServices *);
typedef struct ThreeMMActorActionEditServices
{
    void *context;
    ThreeMMActorSoundEditServices sounds;
    int (*action_flags)(void *, ThreeMMI32, ThreeMMU32 *);
    int (*text_template)(void *);
    void (*mark_life_dirty)(void *);
} ThreeMMActorActionEditServices;
/* Ordered partial edit: reserve, query metadata, remove old motion events, fill,
 * apply action, insert defaults, invalidate lifetime, freeze, then optional step.
 * Failures retain completed mutations. Static metadata is queried even when no
 * previous action event exists. Host metadata flags use the legacy static bit 8. */
int three_mm_actor_edit_set_action(ThreeMMActorState *, ThreeMMActorSounds *, void *events, ThreeMMI32 actor,
    ThreeMMI32 action, ThreeMMI32 cel, int freeze, ThreeMMU32 external_child,
    const ThreeMMActorMotionServices *, const ThreeMMActorActionEditServices *);
int three_mm_actor_edit_set_step(ThreeMMActorState *, ThreeMMActorSounds *, void *events, ThreeMMI32 actor,
    ThreeMMI32 step, const ThreeMMActorMotionServices *, const ThreeMMActorSoundEditServices *);
#ifdef __cplusplus
}
#endif
#endif

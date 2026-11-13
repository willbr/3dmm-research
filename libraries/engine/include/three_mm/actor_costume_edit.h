/* Actor costume editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_COSTUME_EDIT_H
#define THREE_MM_ENGINE_ACTOR_COSTUME_EDIT_H
#include "actor_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorCostumeEditServices
{
    void *context;
    ThreeMMActorEditServices edits;
    int (*reserve_events)(void *, ThreeMMU32 entries, ThreeMMU32 bytes);
    void *(*current)(void *, ThreeMMI32 body_set, int *custom); /* Borrowed. */
    void *(*fetch_material)(void *, const ThreeMMTag *); /* Owning or NULL. */
    void *(*fetch_custom)(void *, ThreeMMI32 costume); /* Owning or NULL. */
    ThreeMMI32 (*body_set)(void *, void *custom);
    void (*release)(void *, void *resource);
} ThreeMMActorCostumeEditServices;
/* Borrow valid tables/body and incoming costume. Preserve exact custom values,
 * pointer identity (including NULL equality), lookup/release order, transition
 * from current to incoming costume, and Add boundaries. Fetch may occur twice
 * for the transition event. Borrowed current material is never released. Host
 * callbacks may observe state but cannot mutate/re-enter the edit. */
void three_mm_actor_edit_costume_fill(ThreeMMActorState *, void *events, ThreeMMI32 first,
    const ThreeMMActorCostume *, const ThreeMMActorMotionServices *, const ThreeMMActorCostumeEditServices *);
/* Custom body set lookup/release precedes reservation; clicked set is used only
 * for ordinary materials. Failed apply retains prior fill deletions and state. */
int three_mm_actor_edit_set_costume(ThreeMMActorState *, ThreeMMActorSounds *, void *events, ThreeMMI32 actor,
    ThreeMMI32 clicked_set, const ThreeMMTag *, ThreeMMI32 costume, ThreeMMI32 custom,
    const ThreeMMActorMotionServices *, const ThreeMMActorCostumeEditServices *);
#ifdef __cplusplus
}
#endif
#endif

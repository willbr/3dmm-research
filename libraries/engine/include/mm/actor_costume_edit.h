/* Actor costume editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_COSTUME_EDIT_H
#define MM_ENGINE_ACTOR_COSTUME_EDIT_H
#include "actor_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorCostumeEditServices
{
    void *context;
    MMActorEditServices edits;
    void *(*current)(void *, MMI32 body_set, int *custom); /* Borrowed. */
    void *(*fetch_material)(void *, const MMTag *); /* Owning or NULL. */
    void *(*fetch_custom)(void *, MMI32 costume); /* Owning or NULL. */
    MMI32 (*body_set)(void *, void *custom);
    void (*release)(void *, void *resource);
} MMActorCostumeEditServices;
/* Borrow valid tables/body and incoming costume. Preserve exact custom values,
 * pointer identity (including NULL equality), lookup/release order, transition
 * from current to incoming costume, and Add boundaries. Fetch may occur twice
 * for the transition event. Borrowed current material is never released. Host
 * callbacks may observe state but cannot mutate/re-enter the edit. */
void mm_actor_edit_costume_fill(MMActorState *, void *events, MMI32 first,
    const MMActorCostume *, const MMActorMotionServices *, const MMActorCostumeEditServices *);
/* Custom body set lookup/release precedes reservation; clicked set is used only
 * for ordinary materials. Failed apply retains prior fill deletions and state. */
int mm_actor_edit_set_costume(MMActorState *, MMActorSounds *, void *events, MMI32 actor,
    MMI32 clicked_set, const MMTag *, MMI32 costume, MMI32 custom,
    const MMActorMotionServices *, const MMActorCostumeEditServices *);
#ifdef __cplusplus
}
#endif
#endif

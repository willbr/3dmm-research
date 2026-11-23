/* Actor copying converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_COPY_H
#define MM_ENGINE_ACTOR_COPY_H
#include "actor_create.h"
#include "movie_sound.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorCopyServices
{
    void *context;
    MMActorCreateServices create;
    int (*resolve_sound)(void *,MMActorSound *);
    MMI32 (*distance)(void *,const MMActorPoint *);
    void (*unknown_event)(void *);
    MMMovieSoundServices (*movie_sound)(void *scene);
} MMActorCopyServices;
/* Valid runtime state/tables and native actor payload layouts are required.
 * Creation returns initialized empty actor tables, using the source template.
 * Insert/merge use the shared C edit/tag policies. Full copy locks the source
 * payload across insertion, unlocks before merging or failure destruction, and
 * preserves successful source sound-tag rewrites even if the copy later fails.
 * Sound resolution uses the movie of the actor's source scene or an alternate
 * resolve_sound callback.
 * Result publication follows creation and release-before-clear on failure.
 * Callbacks cannot reenter/structurally mutate source tables; sound resolution
 * and its explicit payload write are the only source mutations.
 * postponed_tweaks selects legacy BUG1950 (normally disabled), which skips all
 * earlier Tweak/RotateHere events; otherwise current-frame events are retained. */
int mm_actor_copy(MMActorOwner *, int entire_scene, int postponed_tweaks,
                  MMActorOwnerResult *, const MMActorCopyServices *);
/* Route-only copy interpolates its first point and recomputes the distance to
 * the next node (epsilon 1 for zero length); full copy keeps the original node's
 * coordinates and subtracts its offset from the stored distance. */
int mm_actor_copy_route(const MMActorOwner *, int entire_scene,
                        MMActorOwnerResult *, const MMActorCopyServices *);
#ifdef __cplusplus
}
#endif
#endif

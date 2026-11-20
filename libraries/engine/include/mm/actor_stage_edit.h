/* Actor stage editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_STAGE_EDIT_H
#define MM_ENGINE_ACTOR_STAGE_EDIT_H
#include "actor_subroute_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_STAGE_INSERT_FAILED };
typedef struct MMActorStageEditServices
{
    MMActorSubrouteEditServices subroute;
    /* Placement already uses the shared C placement policy. Supplies its result,
     * cached route origin and the background camera yaw; no edits or reentry. */
    void (*placement)(void *context, MMActorPoint *, MMActorPoint *route_origin,
                      unsigned short *camera_yaw);
    void (*diagnostic)(void *context, int fault);
} MMActorStageEditServices;
/* Borrow valid runtime tables/state. Successful point reservation must make the
 * next single insertion infallible. Reservations precede placement/range edits.
 * Entrance extends caches early but defers range invalidation until success;
 * failures retain preceding mutations. Neighbor inheritance uses the shared C
 * subroute edits, then positioning, zero step and freeze in the native order. */
int mm_actor_stage_add(MMActorState *, MMActorSounds *, void *route, void *events,
    MMI32 actor, MMU32 external_child, const MMActorMotionServices *,
    const MMActorStageEditServices *);
/* Entire-subroute deletion is valid at its Add frame. Hide/default/rewind occur
 * before seek; empty tracks retain the invalid frame sentinel. Seek failure
 * retains partial state. Removal may expose a previous onstage subroute, which
 * then receives a Remove event. Offstage removal performs no reservation. */
int mm_actor_stage_delete_subroute(MMActorState *, void *route, void *events,
    const MMActorMotionServices *, const MMActorSubrouteEditServices *);
int mm_actor_stage_remove(MMActorState *, MMActorSounds *, void *route, void *events,
    MMI32 actor, const MMActorMotionServices *, const MMActorStageEditServices *);
int mm_actor_owner_stage_edit(struct MMActorOwner *, int entrance, MMU32 external_child,
    const MMActorStageEditServices *);
#ifdef __cplusplus
}
#endif
#endif

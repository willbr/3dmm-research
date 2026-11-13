/* Actor stage editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_STAGE_EDIT_H
#define THREE_MM_ENGINE_ACTOR_STAGE_EDIT_H
#include "actor_subroute_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { THREE_MM_STAGE_INSERT_FAILED };
typedef struct ThreeMMActorStageEditServices
{
    ThreeMMActorSubrouteEditServices subroute;
    int (*reserve_points)(void *route, ThreeMMU32 count);
    /* Placement already uses the shared C placement policy. Supplies its result,
     * cached route origin and the background camera yaw; no edits or reentry. */
    void (*placement)(void *context, ThreeMMActorPoint *, ThreeMMActorPoint *route_origin,
                      unsigned short *camera_yaw);
    void (*diagnostic)(void *context, int fault);
} ThreeMMActorStageEditServices;
/* Borrow valid runtime tables/state. Successful point reservation must make the
 * next single insertion infallible. Reservations precede placement/range edits.
 * Entrance extends caches early but defers range invalidation until success;
 * failures retain preceding mutations. Neighbor inheritance uses the shared C
 * subroute edits, then positioning, zero step and freeze in the native order. */
int three_mm_actor_stage_add(ThreeMMActorState *, ThreeMMActorSounds *, void *route, void *events,
    ThreeMMI32 actor, ThreeMMU32 external_child, const ThreeMMActorMotionServices *,
    const ThreeMMActorStageEditServices *);
/* Entire-subroute deletion is valid at its Add frame. Hide/default/rewind occur
 * before seek; empty tracks retain the invalid frame sentinel. Seek failure
 * retains partial state. Removal may expose a previous onstage subroute, which
 * then receives a Remove event. Offstage removal performs no reservation. */
int three_mm_actor_stage_delete_subroute(ThreeMMActorState *, void *route, void *events,
    const ThreeMMActorMotionServices *, const ThreeMMActorSubrouteEditServices *);
int three_mm_actor_stage_remove(ThreeMMActorState *, ThreeMMActorSounds *, void *route, void *events,
    ThreeMMI32 actor, const ThreeMMActorMotionServices *, const ThreeMMActorStageEditServices *);
#ifdef __cplusplus
}
#endif
#endif

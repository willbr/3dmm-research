/* Actor route editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_ROUTE_EDIT_H
#define MM_ENGINE_ACTOR_ROUTE_EDIT_H
#include "actor_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_ROUTE_BAD_ALIVE, MM_ROUTE_FREEZE_FAILED, MM_ROUTE_STEP_FAILED };
typedef struct MMActorRouteEditServices
{
    MMActorEditServices edits;
    int (*reserve_points)(void *route,MMU32 count);
    int (*insert_point)(void *route,MMI32,const MMActorRoutePoint *);
    void (*write_point)(void *route,MMI32,const MMActorRoutePoint *);
    void (*delete_points)(void *route,MMI32,MMI32 count);
    void (*clear_route)(void *route);
    void (*write_event)(void *events,MMI32,const MMActorEvent *);
    void (*swap_events)(void *events,MMI32,MMI32);
    void (*life_dirty)(void *context);
    void (*recording)(void *context,int enabled);
    void (*hide_body)(void *context);
    void (*default_costume)(void *context);
    void (*diagnostic)(void *context,int fault);
} MMActorRouteEditServices;
/* Install the storage operations for MMActorPath and MMActorEventTable. */
void mm_actor_route_edit_bind_tables(MMActorRouteEditServices *);
/* Valid runtime tables/indices are required. Point input remains unchanged on
 * failed insertion. Success may update its distance. Epsilon is one 16.16 unit;
 * scalar arithmetic wraps. Zero-distance nodes retain their subroute meaning. */
int mm_actor_route_edit_insert(void *route,MMI32,MMActorRoutePoint *,MMI32 prior_distance,
    const MMActorMotionServices *,const MMActorRouteEditServices *);
void mm_actor_route_edit_adjust_insert(void *route,void *events,MMI32 node,MMI32 first,
    const MMActorMotionServices *,const MMActorRouteEditServices *);
void mm_actor_route_edit_adjust_delete(MMActorState *,void *route,void *events,MMI32 node,MMI32 first,
    const MMActorMotionServices *,const MMActorRouteEditServices *);
MMI32 mm_actor_route_edit_find(void *events,MMI32 kind,MMI32 start,int backwards,
    const MMActorMotionServices *);
MMI32 mm_actor_route_edit_last(void *events,MMI32 first,MMI32 limit,
    const MMActorMotionServices *);
void mm_actor_route_edit_delete_add(MMActorState *,void *route,void *events,MMI32 first,MMI32 limit,
    const MMActorMotionServices *,const MMActorRouteEditServices *);
/* Retains write-length-before-interpolation and backward event scan ordering.
 * Truncation does not delete future events except through route-node adjustment. */
void mm_actor_route_edit_truncate(MMActorState *,void *route,void *events,MMI32 limit,
    const MMActorMotionServices *,const MMActorRouteEditServices *);
void mm_actor_route_edit_hide(MMActorState *,const MMActorRouteEditServices *);
void mm_actor_route_edit_rewind(MMActorState *,void *route,const MMActorMotionServices *,
    const MMActorRouteEditServices *);
void mm_actor_route_edit_reset(MMActorState *,void *route,const MMActorMotionServices *,
    const MMActorRouteEditServices *);
/* Forward deletion retains enabled retail BUG1870/BUG1932 behavior, selective
 * subroute boundaries, dirty/rewind ordering and absence of range invalidation.
 * Alive is assigned before reset on the dead path. The public operation adds
 * terminating freeze/step only when onstage, then preserves the latest tweak.
 * Callbacks may observe published state but cannot mutate/re-enter the edit. */
void mm_actor_route_edit_delete_forward_core(MMActorState *,void *route,void *events,int all,
    int *alive,MMI32 current,const MMActorMotionServices *,const MMActorRouteEditServices *);
void mm_actor_route_edit_delete_forward(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,int all,int *alive,MMI32 current,const MMActorMotionServices *,
    const MMActorRouteEditServices *);
#ifdef __cplusplus
}
#endif
#endif

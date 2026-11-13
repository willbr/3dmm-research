/* Actor route editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_ROUTE_EDIT_H
#define THREE_MM_ENGINE_ACTOR_ROUTE_EDIT_H
#include "actor_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { THREE_MM_ROUTE_BAD_ALIVE, THREE_MM_ROUTE_FREEZE_FAILED, THREE_MM_ROUTE_STEP_FAILED };
typedef struct ThreeMMActorRouteEditServices
{
    ThreeMMActorEditServices edits;
    int (*insert_point)(void *route,ThreeMMI32,const ThreeMMActorRoutePoint *);
    void (*write_point)(void *route,ThreeMMI32,const ThreeMMActorRoutePoint *);
    void (*delete_points)(void *route,ThreeMMI32,ThreeMMI32 count);
    void (*clear_route)(void *route);
    void (*write_event)(void *events,ThreeMMI32,const ThreeMMActorEvent *);
    void (*swap_events)(void *events,ThreeMMI32,ThreeMMI32);
    int (*reserve_events)(void *context,ThreeMMU32 entries,ThreeMMU32 bytes);
    void (*life_dirty)(void *context);
    void (*recording)(void *context,int enabled);
    void (*hide_body)(void *context);
    void (*default_costume)(void *context);
    void (*diagnostic)(void *context,int fault);
} ThreeMMActorRouteEditServices;
/* Valid runtime tables/indices are required. Point input remains unchanged on
 * failed insertion. Success may update its distance. Epsilon is one 16.16 unit;
 * scalar arithmetic wraps. Zero-distance nodes retain their subroute meaning. */
int three_mm_actor_route_edit_insert(void *route,ThreeMMI32,ThreeMMActorRoutePoint *,ThreeMMI32 prior_distance,
    const ThreeMMActorMotionServices *,const ThreeMMActorRouteEditServices *);
void three_mm_actor_route_edit_adjust_insert(void *route,void *events,ThreeMMI32 node,ThreeMMI32 first,
    const ThreeMMActorMotionServices *,const ThreeMMActorRouteEditServices *);
void three_mm_actor_route_edit_adjust_delete(ThreeMMActorState *,void *route,void *events,ThreeMMI32 node,ThreeMMI32 first,
    const ThreeMMActorMotionServices *,const ThreeMMActorRouteEditServices *);
ThreeMMI32 three_mm_actor_route_edit_find(void *events,ThreeMMI32 kind,ThreeMMI32 start,int backwards,
    const ThreeMMActorMotionServices *);
ThreeMMI32 three_mm_actor_route_edit_last(void *events,ThreeMMI32 first,ThreeMMI32 limit,
    const ThreeMMActorMotionServices *);
void three_mm_actor_route_edit_delete_add(ThreeMMActorState *,void *route,void *events,ThreeMMI32 first,ThreeMMI32 limit,
    const ThreeMMActorMotionServices *,const ThreeMMActorRouteEditServices *);
/* Retains write-length-before-interpolation and backward event scan ordering.
 * Truncation does not delete future events except through route-node adjustment. */
void three_mm_actor_route_edit_truncate(ThreeMMActorState *,void *route,void *events,ThreeMMI32 limit,
    const ThreeMMActorMotionServices *,const ThreeMMActorRouteEditServices *);
void three_mm_actor_route_edit_hide(ThreeMMActorState *,const ThreeMMActorRouteEditServices *);
void three_mm_actor_route_edit_rewind(ThreeMMActorState *,void *route,const ThreeMMActorMotionServices *,
    const ThreeMMActorRouteEditServices *);
void three_mm_actor_route_edit_reset(ThreeMMActorState *,void *route,const ThreeMMActorMotionServices *,
    const ThreeMMActorRouteEditServices *);
/* Forward deletion retains enabled retail BUG1870/BUG1932 behavior, selective
 * subroute boundaries, dirty/rewind ordering and absence of range invalidation.
 * Alive is assigned before reset on the dead path. The public operation adds
 * terminating freeze/step only when onstage, then preserves the latest tweak.
 * Callbacks may observe published state but cannot mutate/re-enter the edit. */
void three_mm_actor_route_edit_delete_forward_core(ThreeMMActorState *,void *route,void *events,int all,
    int *alive,ThreeMMI32 current,const ThreeMMActorMotionServices *,const ThreeMMActorRouteEditServices *);
void three_mm_actor_route_edit_delete_forward(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,int all,int *alive,ThreeMMI32 current,const ThreeMMActorMotionServices *,
    const ThreeMMActorRouteEditServices *);
#ifdef __cplusplus
}
#endif
#endif

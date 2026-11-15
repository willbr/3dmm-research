/* Actor copying converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_COPY_H
#define MM_ENGINE_ACTOR_COPY_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorCopyResult
{
    void *actor,*context;
    void (*publish)(void *,void *actor);
} MMActorCopyResult;
typedef struct MMActorCopyServices
{
    void *context;
    MMActorMotionServices motion;
    void *(*create_actor)(void *);
    void (*release_actor)(void *,void *actor);
    void *(*route)(void *,void *actor);
    void *(*events)(void *,void *actor);
    void (*lock_events)(void *,void *events);
    void (*unlock_events)(void *,void *events);
    const void *(*payload_view)(void *,void *events,MMI32 index);
    void (*write_payload)(void *,void *events,MMI32 index,const void *);
    int (*resolve_sound)(void *,MMActorSound *);
    int (*insert_event)(void *,void *actor,MMI32 index,MMU32 bytes,const void *,const MMActorEvent *);
    void (*merge_event)(void *,void *actor,MMI32 first,MMI32 added);
    int (*insert_point)(void *,void *route,MMI32,const MMActorRoutePoint *);
    void (*write_point)(void *,void *route,MMI32,const MMActorRoutePoint *);
    int (*resize_route)(void *,void *route,MMI32 count);
    int (*reserve_route)(void *,void *route,MMI32 additional);
    void (*copy_points)(void *,void *source,MMI32 first,void *destination,MMI32 to,MMI32 count);
    MMI32 (*distance)(void *,const MMActorPoint *);
    void (*rewind_actor)(void *,void *actor);
    void (*unknown_event)(void *);
} MMActorCopyServices;
/* Valid runtime state/tables and native actor payload layouts are required.
 * Creation returns initialized empty actor tables, using the source template.
 * Insert/merge use the shared C edit/tag policies. Full copy locks the source
 * payload across insertion, unlocks before merging or failure destruction, and
 * preserves successful source sound-tag rewrites even if the copy later fails.
 * Result publication follows creation and release-before-clear on failure.
 * Callbacks cannot reenter/structurally mutate source tables; sound resolution
 * and its explicit payload write are the only source mutations.
 * postponed_tweaks selects legacy BUG1950 (normally disabled), which skips all
 * earlier Tweak/RotateHere events; otherwise current-frame events are retained. */
int mm_actor_copy(const MMActorState *,void *route,void *events,int entire_scene,int postponed_tweaks,
    MMActorCopyResult *,const MMActorCopyServices *);
/* Route-only copy interpolates its first point and recomputes the distance to
 * the next node (epsilon 1 for zero length); full copy keeps the original node's
 * coordinates and subtracts its offset from the stored distance. */
int mm_actor_copy_route(const MMActorState *,void *route,int entire_scene,
    MMActorCopyResult *,const MMActorCopyServices *);
#ifdef __cplusplus
}
#endif
#endif

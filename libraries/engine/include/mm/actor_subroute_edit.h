/* Actor backward deletion and event propagation converted from actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_SUBROUTE_EDIT_H
#define MM_ENGINE_ACTOR_SUBROUTE_EDIT_H
#include "actor_route_edit.h"
#include "actor_action_edit.h"
#include "actor_pose.h"
#include "actor_timeline.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_SUBROUTE_BAD_LIFETIME, MM_SUBROUTE_BAD_HOLD, MM_SUBROUTE_OFFSTAGE_EVENT };
typedef struct MMActorSubrouteEditServices
{
    MMActorRouteEditServices route;
    MMActorActionEditServices action;
    MMActorTimelineServices timeline;
    MMActorLifetime lifetime;
    void (*pose_inputs)(void *,MMActorPoseInputs *);
    void (*locate_body)(void *,const MMActorBodyPose *);
    /* Temporary integration into the already C-owned seek policy. State is
     * input/output even on failure; callbacks retain partial owner-side flags. */
    int (*seek)(void *,MMActorState *,MMI32 frame);
    void (*diagnostic)(void *,int);
} MMActorSubrouteEditServices;
unsigned short mm_actor_edit_average_angle(unsigned short,unsigned short,MMI32 weight);
/* NULL distance writes all angles. Otherwise distance is a caller-supplied
 * BRender length; C owns axis selection and weight clamping to [6553,65536).
 * Add padding and translation are preserved. */
void mm_actor_edit_add_orientation(MMActorState *,void *events,unsigned short x,unsigned short y,
    unsigned short z,MMU32 axes,const MMI32 *distance,const MMActorMotionServices *,
    const MMActorEditServices *);
void mm_actor_subroute_save_orientation(MMActorState *,void *route,void *events,
    const MMActorMotionServices *,const MMActorSubrouteEditServices *);
void mm_actor_subroute_position_body(MMActorState *,void *route,void *events,const MMActorPoint *,
    const MMActorMotionServices *,const MMActorSubrouteEditServices *);
/* Retain BUG1870/BUG1866 ordering, including the second offset subtraction after
 * a matching held-frame location. Lifetime failure reports diagnostics but editing
 * continues. Optional alive is published before reset/lifetime/invalidation. */
void mm_actor_subroute_delete_back(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,int *alive,const MMActorMotionServices *,const MMActorSubrouteEditServices *);
/* Borrow valid subroutes/cursors. Copying a tagged payload retains its own tag
 * reference before merge/application, including on failure. This fixes the old
 * borrowed-copy ownership bug without changing logical event payloads. Previous
 * propagation keeps its costume-mask early return and prune/seek failure order. */
int mm_actor_subroute_from_previous(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,MMI32 limit,MMU32 mask,const MMActorMotionServices *,
    const MMActorSubrouteEditServices *);
int mm_actor_subroute_from_later(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,MMU32 external_child,const MMActorMotionServices *,const MMActorSubrouteEditServices *);
#ifdef __cplusplus
}
#endif
#endif

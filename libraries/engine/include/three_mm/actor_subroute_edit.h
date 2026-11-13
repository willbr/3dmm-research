/* Actor backward deletion and event propagation converted from actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_SUBROUTE_EDIT_H
#define THREE_MM_ENGINE_ACTOR_SUBROUTE_EDIT_H
#include "actor_route_edit.h"
#include "actor_action_edit.h"
#include "actor_pose.h"
#include "actor_timeline.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { THREE_MM_SUBROUTE_BAD_LIFETIME, THREE_MM_SUBROUTE_BAD_HOLD, THREE_MM_SUBROUTE_OFFSTAGE_EVENT };
typedef struct ThreeMMActorSubrouteFlags { int dirty,recording,rejoining; } ThreeMMActorSubrouteFlags;
typedef struct ThreeMMActorSubrouteEditServices
{
    ThreeMMActorRouteEditServices route;
    ThreeMMActorActionEditServices action;
    ThreeMMActorTimelineServices timeline;
    void (*get_flags)(void *,ThreeMMActorSubrouteFlags *);
    void (*set_dirty)(void *,int);
    void (*pose_inputs)(void *,ThreeMMActorPoseInputs *);
    void (*locate_body)(void *,const ThreeMMActorPoint *,const ThreeMMActorMatrix *);
    /* Temporary integration into the already C-owned seek policy. State is
     * input/output even on failure; callbacks retain partial owner-side flags. */
    int (*seek)(void *,ThreeMMActorState *,ThreeMMI32 frame);
    void (*diagnostic)(void *,int);
} ThreeMMActorSubrouteEditServices;
unsigned short three_mm_actor_edit_average_angle(unsigned short,unsigned short,ThreeMMI32 weight);
/* NULL distance writes all angles. Otherwise distance is a caller-supplied
 * BRender length; C owns axis selection and weight clamping to [6553,65536).
 * Add padding and translation are preserved. */
void three_mm_actor_edit_add_orientation(ThreeMMActorState *,void *events,unsigned short x,unsigned short y,
    unsigned short z,ThreeMMU32 axes,const ThreeMMI32 *distance,const ThreeMMActorMotionServices *,
    const ThreeMMActorEditServices *);
void three_mm_actor_subroute_save_orientation(ThreeMMActorState *,void *route,void *events,
    const ThreeMMActorMotionServices *,const ThreeMMActorSubrouteEditServices *);
void three_mm_actor_subroute_position_body(ThreeMMActorState *,void *route,void *events,const ThreeMMActorPoint *,
    const ThreeMMActorMotionServices *,const ThreeMMActorSubrouteEditServices *);
/* Lifetime failures publish partial caches/last frame and dirty=true. Recording
 * returns the cached last frame and keeps dirty=true without running metadata.
 * Optional independent output is untouched on non-recording failure. */
int three_mm_actor_subroute_lifetime(void *route,void *events,ThreeMMI32 *last,const ThreeMMActorSubrouteEditServices *);
/* Retain BUG1870/BUG1866 ordering, including the second offset subtraction after
 * a matching held-frame location. Lifetime failure reports diagnostics but editing
 * continues. Optional alive is published before reset/lifetime/invalidation. */
void three_mm_actor_subroute_delete_back(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,int *alive,const ThreeMMActorMotionServices *,const ThreeMMActorSubrouteEditServices *);
/* Borrow valid subroutes/cursors. Copying a tagged payload retains its own tag
 * reference before merge/application, including on failure. This fixes the old
 * borrowed-copy ownership bug without changing logical event payloads. Previous
 * propagation keeps its costume-mask early return and prune/seek failure order. */
int three_mm_actor_subroute_from_previous(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,ThreeMMI32 limit,ThreeMMU32 mask,const ThreeMMActorMotionServices *,
    const ThreeMMActorSubrouteEditServices *);
int three_mm_actor_subroute_from_later(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,ThreeMMU32 external_child,const ThreeMMActorMotionServices *,const ThreeMMActorSubrouteEditServices *);
#ifdef __cplusplus
}
#endif
#endif

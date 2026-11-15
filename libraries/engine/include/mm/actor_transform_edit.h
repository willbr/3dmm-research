/* Actor transform/time editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_TRANSFORM_EDIT_H
#define MM_ENGINE_ACTOR_TRANSFORM_EDIT_H
#include "actor_subroute_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_NORMAL_SIZE=1, MM_NORMAL_ROTATE=2 };
enum { MM_MOVE_GROUND=2, MM_MOVE_ORIENT=4, MM_MOVE_SCENE=8, MM_MOVE_SUBROUTE=16 };
enum { MM_TRANSFORM_ADD_FAILED, MM_TRANSFORM_MOVE_ROTATE_FAILED, MM_TRANSFORM_BAD_LIFETIME };
typedef struct MMActorTransformEditServices
{
    MMActorSubrouteEditServices subroute;
    MMActorFillServices fill;
    int (*scene_present)(void *);
    void (*normalise)(void *,MMActorMatrix *);
    MMI32 (*distance)(void *,const MMActorPoint *);
    void (*publish_origin)(void *,const MMActorPoint *);
    int (*seek_scene)(void *,MMActorState *,MMI32 frame);
    void (*scrollbars)(void *);
    void (*diagnostic)(void *,int fault);
} MMActorTransformEditServices;
typedef struct MMActorMoveResult
{
    int moved,written;
    void *context;
    void (*publish)(void *,int moved);
} MMActorMoveResult;
/* Borrow valid runtime tables and onstage state. Normalisation/distance are
 * BRender primitives. C owns matrix ordering, clipping/bounds, filling, event
 * sequencing and cache decisions. Callbacks cannot reenter/mutate the edit;
 * seek_scene is an explicit state transaction and may publish partial failure.
 * Scale/stretch upper bounds are exclusive. Rotation/revert retain native
 * diagnostic-and-continue behavior after a supposedly reserved insertion fails. */
int mm_actor_transform_rotate(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,unsigned short x,unsigned short y,unsigned short z,int forward,int world,
    const MMActorMotionServices *,const MMActorTransformEditServices *);
int mm_actor_transform_normalize(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,MMU32 flags,const MMActorMotionServices *,const MMActorTransformEditServices *);
int mm_actor_transform_scale(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,MMI32 factor,const MMActorMotionServices *,const MMActorTransformEditServices *);
int mm_actor_transform_pull(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,const MMActorStretch *,const MMActorMotionServices *,const MMActorTransformEditServices *);
int mm_actor_transform_tweak(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,const MMActorPoint *delta,MMU32 flags,const MMActorMotionServices *,const MMActorTransformEditServices *);
/* Origin is input/output. Moved is published before reservation/ground clipping
 * and reflects the requested delta, even if clipping prevents all movement. */
int mm_actor_transform_move(MMActorState *,MMActorSounds *,void *route,void *events,
    MMI32 actor,MMActorPoint *origin,const MMActorPoint *delta,MMU32 flags,MMActorMoveResult *,
    const MMActorMotionServices *,const MMActorTransformEditServices *);
/* Zero shift does nothing. Lifetime query failure reports a diagnostic and
 * continues; failed seeking retains preceding edits and does not update bars. */
int mm_actor_transform_time(MMActorState *,void *route,void *events,MMI32 shift,
    const MMActorMotionServices *,const MMActorTransformEditServices *);
#ifdef __cplusplus
}
#endif
#endif

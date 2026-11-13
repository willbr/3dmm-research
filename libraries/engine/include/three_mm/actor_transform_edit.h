/* Actor transform/time editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_TRANSFORM_EDIT_H
#define THREE_MM_ENGINE_ACTOR_TRANSFORM_EDIT_H
#include "actor_subroute_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { THREE_MM_NORMAL_SIZE=1, THREE_MM_NORMAL_ROTATE=2 };
enum { THREE_MM_MOVE_GROUND=2, THREE_MM_MOVE_ORIENT=4, THREE_MM_MOVE_SCENE=8, THREE_MM_MOVE_SUBROUTE=16 };
enum { THREE_MM_TRANSFORM_ADD_FAILED, THREE_MM_TRANSFORM_MOVE_ROTATE_FAILED, THREE_MM_TRANSFORM_BAD_LIFETIME };
typedef struct ThreeMMActorTransformEditServices
{
    ThreeMMActorSubrouteEditServices subroute;
    ThreeMMActorFillServices fill;
    int (*scene_present)(void *);
    void (*normalise)(void *,ThreeMMActorMatrix *);
    ThreeMMI32 (*distance)(void *,const ThreeMMActorPoint *);
    void (*publish_origin)(void *,const ThreeMMActorPoint *);
    int (*seek_scene)(void *,ThreeMMActorState *,ThreeMMI32 frame);
    void (*scrollbars)(void *);
    void (*diagnostic)(void *,int fault);
} ThreeMMActorTransformEditServices;
typedef struct ThreeMMActorMoveResult
{
    int moved,written;
    void *context;
    void (*publish)(void *,int moved);
} ThreeMMActorMoveResult;
/* Borrow valid runtime tables and onstage state. Normalisation/distance are
 * BRender primitives. C owns matrix ordering, clipping/bounds, filling, event
 * sequencing and cache decisions. Callbacks cannot reenter/mutate the edit;
 * seek_scene is an explicit state transaction and may publish partial failure.
 * Scale/stretch upper bounds are exclusive. Rotation/revert retain native
 * diagnostic-and-continue behavior after a supposedly reserved insertion fails. */
int three_mm_actor_transform_rotate(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,unsigned short x,unsigned short y,unsigned short z,int forward,int world,
    const ThreeMMActorMotionServices *,const ThreeMMActorTransformEditServices *);
int three_mm_actor_transform_normalize(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,ThreeMMU32 flags,const ThreeMMActorMotionServices *,const ThreeMMActorTransformEditServices *);
int three_mm_actor_transform_scale(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,ThreeMMI32 factor,const ThreeMMActorMotionServices *,const ThreeMMActorTransformEditServices *);
int three_mm_actor_transform_pull(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,const ThreeMMActorStretch *,const ThreeMMActorMotionServices *,const ThreeMMActorTransformEditServices *);
int three_mm_actor_transform_tweak(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,const ThreeMMActorPoint *delta,ThreeMMU32 flags,const ThreeMMActorMotionServices *,const ThreeMMActorTransformEditServices *);
/* Origin is input/output. Moved is published before reservation/ground clipping
 * and reflects the requested delta, even if clipping prevents all movement. */
int three_mm_actor_transform_move(ThreeMMActorState *,ThreeMMActorSounds *,void *route,void *events,
    ThreeMMI32 actor,ThreeMMActorPoint *origin,const ThreeMMActorPoint *delta,ThreeMMU32 flags,ThreeMMActorMoveResult *,
    const ThreeMMActorMotionServices *,const ThreeMMActorTransformEditServices *);
/* Zero shift does nothing. Lifetime query failure reports a diagnostic and
 * continues; failed seeking retains preceding edits and does not update bars. */
int three_mm_actor_transform_time(ThreeMMActorState *,void *route,void *events,ThreeMMI32 shift,
    const ThreeMMActorMotionServices *,const ThreeMMActorTransformEditServices *);
#ifdef __cplusplus
}
#endif
#endif

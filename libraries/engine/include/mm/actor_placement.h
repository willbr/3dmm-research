/* Actor placement converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_PLACEMENT_H
#define MM_ENGINE_ACTOR_PLACEMENT_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorPlacementServices
{
    void *context;
    void (*camera)(void *, MMActorMatrix *, MMI32 *hither, MMI32 *yon);
} MMActorPlacementServices;
/* In/out point is the background's authored placement. All-zero means absent:
 * only then query the camera, project 25 units ahead (or the clipping midpoint)
 * and put the actor on the floor. Authored heights are retained. Text adds ten
 * units in either case. Scalars and matrix use BRender's signed 16.16 format. */
void mm_actor_placement(MMActorPoint *, int text,
                             const MMActorPlacementServices *);
#ifdef __cplusplus
}
#endif
#endif

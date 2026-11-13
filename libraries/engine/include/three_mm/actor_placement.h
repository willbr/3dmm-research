/* Actor placement converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_PLACEMENT_H
#define THREE_MM_ENGINE_ACTOR_PLACEMENT_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorPlacementServices
{
    void *context;
    void (*camera)(void *, ThreeMMActorMatrix *, ThreeMMI32 *hither, ThreeMMI32 *yon);
} ThreeMMActorPlacementServices;
/* In/out point is the background's authored placement. All-zero means absent:
 * only then query the camera, project 25 units ahead (or the clipping midpoint)
 * and put the actor on the floor. Authored heights are retained. Text adds ten
 * units in either case. Scalars and matrix use BRender's signed 16.16 format. */
void three_mm_actor_placement(ThreeMMActorPoint *, int text,
                             const ThreeMMActorPlacementServices *);
#ifdef __cplusplus
}
#endif
#endif

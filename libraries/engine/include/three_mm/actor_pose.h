/* Actor pose evaluation converted from engine actor_pose.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_POSE_H
#define THREE_MM_ENGINE_ACTOR_POSE_H
#include "actor_math.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMActorPoseInputs
    {
        unsigned short restX, restY, restZ;
        int fixedPathOrientation;
    } ThreeMMActorPoseInputs;
    enum
    {
        THREE_MM_ACTOR_ROTATE_X = 1,
        THREE_MM_ACTOR_ROTATE_Y = 2,
        THREE_MM_ACTOR_ROTATE_Z = 4
    };
    /* Valid evaluated state required; non-current rotation also needs valid route/
     * event tables and an Add cursor. Preserve the
     * enabled retail BUG1899 orientation rule, weighted tangents, exclusive upper
     * normalization bound and exact matrix operation order. No resource/renderer
     * services are consulted. Position may alias state.position. */
    void three_mm_actor_pose_update(ThreeMMActorState *, void *route, void *events, const ThreeMMActorPoseInputs *,
                                    const ThreeMMActorPoint *position, ThreeMMActorMatrix *matrix,
                                    const ThreeMMActorMotionServices *);
    /* Matrix and each angle/axes output are independently optional. Single-point
     * routes leave the matrix unchanged and zero angle/axes outputs. Vector input
     * is read-only apart from caller-aliased outputs; banking remains zero. */
    void three_mm_actor_pose_orientation(const ThreeMMActorState *, void *route, void *events, ThreeMMActorMatrix *,
                                         unsigned short *x, unsigned short *y, unsigned short *z, ThreeMMU32 *axes,
                                         const ThreeMMActorMotionServices *);
    void three_mm_actor_pose_vector(ThreeMMU32 flags, const ThreeMMActorPoint *, ThreeMMActorMatrix *,
                                    unsigned short *x, unsigned short *y, unsigned short *z, ThreeMMU32 *axes);
#ifdef __cplusplus
}
#endif
#endif

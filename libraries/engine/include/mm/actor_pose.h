/* Actor pose evaluation converted from engine actor_pose.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_POSE_H
#define MM_ENGINE_ACTOR_POSE_H
#include "actor_math.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMActorPoseInputs
    {
        unsigned short restX, restY, restZ;
        int fixedPathOrientation;
    } MMActorPoseInputs;
    enum
    {
        MM_ACTOR_ROTATE_X = 1,
        MM_ACTOR_ROTATE_Y = 2,
        MM_ACTOR_ROTATE_Z = 4
    };
    /* Valid evaluated state required; non-current rotation also needs valid route/
     * event tables and an Add cursor. Preserve the
     * enabled retail BUG1899 orientation rule, weighted tangents, exclusive upper
     * normalization bound and exact matrix operation order. No resource/renderer
     * services are consulted. Position may alias state.position. */
    void mm_actor_pose_update(MMActorState *, void *route, void *events, const MMActorPoseInputs *,
                                    const MMActorPoint *position, MMActorMatrix *matrix,
                                    const MMActorMotionServices *);
    /* Matrix and each angle/axes output are independently optional. Single-point
     * routes leave the matrix unchanged and zero angle/axes outputs. Vector input
     * is read-only apart from caller-aliased outputs; banking remains zero. */
    void mm_actor_pose_orientation(const MMActorState *, void *route, void *events, MMActorMatrix *,
                                         unsigned short *x, unsigned short *y, unsigned short *z, MMU32 *axes,
                                         const MMActorMotionServices *);
    void mm_actor_pose_vector(MMU32 flags, const MMActorPoint *, MMActorMatrix *,
                                    unsigned short *x, unsigned short *y, unsigned short *z, MMU32 *axes);
#ifdef __cplusplus
}
#endif
#endif

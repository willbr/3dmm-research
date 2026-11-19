/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_motion.h"
#include "mm/actor_pose.h"
ASSERTNAME
static_assert(sizeof(BMAT34) == sizeof(MMActorMatrix) && offsetof(BMAT34, m) == 0, "pose matrix ABI");
static_assert(factnRotateX == MM_ACTOR_ROTATE_X && factnRotateY == MM_ACTOR_ROTATE_Y &&
                  factnRotateZ == MM_ACTOR_ROTATE_Z,
              "pose action flag ABI");
namespace
{
MMU32 *Axes(LegacyUlong *native, RoutePoint *vector, MMActorPoint *point, BMAT34 *matrix,
                 MMActorMatrix *core, MMU32 *fallback)
{
    if (!native)
        return nullptr;
    if (vector)
    {
        if (static_cast<void *>(native) == static_cast<void *>(&vector->dxr))
            return reinterpret_cast<MMU32 *>(&point->dxr);
        if (static_cast<void *>(native) == static_cast<void *>(&vector->dyr))
            return reinterpret_cast<MMU32 *>(&point->dyr);
        if (static_cast<void *>(native) == static_cast<void *>(&vector->dzr))
            return reinterpret_cast<MMU32 *>(&point->dzr);
    }
    if (matrix)
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 3; ++j)
                if (static_cast<void *>(native) == static_cast<void *>(&matrix->m[i][j]))
                    return reinterpret_cast<MMU32 *>(&core->m[i][j]);
    return fallback;
}
} // namespace
void ActorPoseEvaluator::Update(MMActorState &state, const ActorTrack &track, const ActorPoseInputs &inputs,
                                const RoutePoint *position, BMAT34 *matrix)
{
    AssertVarMem(matrix);
    MMActorMatrix output;
    Update(state, track, inputs, position, &output);
    std::memcpy(matrix, &output, sizeof(output));
}
void ActorPoseEvaluator::Update(MMActorState &state, const ActorTrack &track, const ActorPoseInputs &inputs,
                                const RoutePoint *position, MMActorMatrix *matrix)
{
    AssertVarMem(const_cast<RoutePoint *>(position));
    AssertVarMem(matrix);
    const auto services = EngineActorMotion::Services();
    const MMActorPoseInputs in = {inputs.restX, inputs.restY, inputs.restZ, inputs.fixedPathOrientation};
    mm_actor_pose_update(&state, track.Route(), track.Events(), &in, position, matrix, &services);
}
void ActorPoseEvaluator::RouteOrientation(const MMActorState &state, const ActorTrack &track, BMAT34 *matrix, BRA *x,
                                          BRA *y, BRA *z, LegacyUlong *axes)
{
    AssertNilOrVarMem(matrix);
    AssertNilOrVarMem(x);
    AssertNilOrVarMem(y);
    AssertNilOrVarMem(z);
    if (axes)
        *axes = 0;
    if (x)
        *x = 0;
    if (y)
        *y = 0;
    if (z)
        *z = 0;
    const auto services = EngineActorMotion::Services();
    MMActorMatrix output;
    if (matrix)
        output = EngineActorMotion::Matrix(*matrix);
    MMU32 mask = 0;
    auto flags = Axes(axes, nullptr, nullptr, matrix, &output, &mask);
    mm_actor_pose_orientation(&state, track.Route(), track.Events(), matrix ? &output : nullptr, x, y, z, flags,
                                    &services);
    if (matrix)
        std::memcpy(matrix, &output, sizeof(output));
    if (axes)
        *axes = *flags;
}
void ActorPoseEvaluator::ApplyVectorRotation(LegacyUlong actionFlags, RoutePoint *vector, BMAT34 *matrix, BRA *x,
                                             BRA *y, BRA *z, LegacyUlong *axes)
{
    AssertVarMem(vector);
    AssertNilOrVarMem(matrix);
    AssertNilOrVarMem(x);
    AssertNilOrVarMem(y);
    AssertNilOrVarMem(z);
    if (axes)
        *axes = 0;
    auto value = *vector;
    MMActorMatrix output;
    if (matrix)
        output = EngineActorMotion::Matrix(*matrix);
    MMU32 mask = 0;
    auto flags = Axes(axes, vector, &value, matrix, &output, &mask);
    mm_actor_pose_vector(actionFlags, &value, matrix ? &output : nullptr, x, y, z, flags);
    if (matrix)
        std::memcpy(matrix, &output, sizeof(output));
    if (axes)
        *axes = *flags;
}

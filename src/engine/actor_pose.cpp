/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_actor_motion.h"
#include "three_mm/actor_pose.h"
ASSERTNAME
static_assert(sizeof(BMAT34) == sizeof(ThreeMMActorMatrix) && offsetof(BMAT34, m) == 0, "pose matrix ABI");
static_assert(factnRotateX == THREE_MM_ACTOR_ROTATE_X && factnRotateY == THREE_MM_ACTOR_ROTATE_Y &&
                  factnRotateZ == THREE_MM_ACTOR_ROTATE_Z,
              "pose action flag ABI");
namespace
{
ThreeMMU32 *Axes(LegacyUlong *native, RoutePoint *vector, ThreeMMActorPoint *point, BMAT34 *matrix,
                 ThreeMMActorMatrix *core, ThreeMMU32 *fallback)
{
    if (!native)
        return nullptr;
    if (vector)
    {
        if (static_cast<void *>(native) == static_cast<void *>(&vector->dxr))
            return reinterpret_cast<ThreeMMU32 *>(&point->dxr);
        if (static_cast<void *>(native) == static_cast<void *>(&vector->dyr))
            return reinterpret_cast<ThreeMMU32 *>(&point->dyr);
        if (static_cast<void *>(native) == static_cast<void *>(&vector->dzr))
            return reinterpret_cast<ThreeMMU32 *>(&point->dzr);
    }
    if (matrix)
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 3; ++j)
                if (static_cast<void *>(native) == static_cast<void *>(&matrix->m[i][j]))
                    return reinterpret_cast<ThreeMMU32 *>(&core->m[i][j]);
    return fallback;
}
} // namespace
void ActorPoseEvaluator::Update(ActorFrameState &state, const ActorTrack &track, const ActorPoseInputs &inputs,
                                const RoutePoint *position, BMAT34 *matrix)
{
    AssertVarMem(const_cast<RoutePoint *>(position));
    AssertVarMem(matrix);
    state.position = *position;
    auto value = EngineActorMotion::Core(state);
    const auto point = EngineActorMotion::Point(*position);
    const auto services = EngineActorMotion::Services();
    const ThreeMMActorPoseInputs in = {inputs.restX, inputs.restY, inputs.restZ, inputs.fixedPathOrientation};
    ThreeMMActorMatrix output;
    auto destination = &output;
    if (matrix == &state.transform.bmat34Cur)
        destination = &value.transform.bmat34Cur;
    else if (matrix == &state.transform.bmat34Fwd)
        destination = &value.transform.bmat34Fwd;
    three_mm_actor_pose_update(&value, track.Route(), track.Events(), &in, &point, destination, &services);
    if (destination == &output)
        std::memcpy(matrix, &output, sizeof(output));
    EngineActorMotion::Publish(state, value);
}
void ActorPoseEvaluator::RouteOrientation(const ActorFrameState &state, const ActorTrack &track, BMAT34 *matrix, BRA *x,
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
    const auto value = EngineActorMotion::Core(state);
    const auto services = EngineActorMotion::Services();
    ThreeMMActorMatrix output;
    if (matrix)
        std::memcpy(&output, matrix, sizeof(output));
    ThreeMMU32 mask = 0;
    auto flags = Axes(axes, nullptr, nullptr, matrix, &output, &mask);
    three_mm_actor_pose_orientation(&value, track.Route(), track.Events(), matrix ? &output : nullptr, x, y, z, flags,
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
    auto value = EngineActorMotion::Point(*vector);
    ThreeMMActorMatrix output;
    if (matrix)
        std::memcpy(&output, matrix, sizeof(output));
    ThreeMMU32 mask = 0;
    auto flags = Axes(axes, vector, &value, matrix, &output, &mask);
    three_mm_actor_pose_vector(actionFlags, &value, matrix ? &output : nullptr, x, y, z, flags);
    if (matrix)
        std::memcpy(matrix, &output, sizeof(output));
    if (axes)
        *axes = *flags;
}

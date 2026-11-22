/* Actor pose evaluation converted from engine actor_pose.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_pose.h"
#include <stddef.h>
typedef struct PoseAdd
{
    MMActorPoint translation;
    unsigned short xa, ya, za, reserved;
} PoseAdd;
typedef char PoseAddWidth[(sizeof(PoseAdd) == 20) ? 1 : -1];
static MMI32 abs_word(MMI32 n)
{
    return n < 0 ? mm_fixed_sub(0, n) : n;
}
static MMI32 maximum(MMI32 a, MMI32 b)
{
    return a > b ? a : b;
}
static MMI32 max_abs(const MMActorPoint *p)
{
    return maximum(maximum(abs_word(p->dxr), abs_word(p->dyr)), abs_word(p->dzr));
}
static MMI32 fraction(MMI32 n)
{
    MMI32 half;
    MMU32 bits;
    half = n < 0 ? -(MMI32)((0U - (MMU32)n) >> 1) : n / 2;
    bits = (MMU32)half & 65535U;
    return bits < 32768U ? (MMI32)bits : (MMI32)bits - 65536L;
}
void mm_actor_pose_vector(MMU32 flags, const MMActorPoint *vector, MMActorMatrix *matrix,
                                unsigned short *x, unsigned short *y, unsigned short *z, MMU32 *axes)
{
    unsigned short yaw = 0, pitch = 0;
    MMI32 distance, rx, ry, rz, ex, ey, ez, horizontal;
    if (axes)
        *axes = 0;
    distance = mm_fixed_add(max_abs(vector), 65536L);
    rx = mm_fixed_div(vector->dxr, distance);
    ry = mm_fixed_div(vector->dyr, distance);
    rz = mm_fixed_div(vector->dzr, distance);
    ex = fraction(rx);
    ey = fraction(ry);
    ez = fraction(rz);
    if ((flags & MM_ACTOR_ROTATE_Y) && (vector->dxr || vector->dzr))
    {
        yaw = mm_angle_atan2(ex, ez);
        if (matrix)
            mm_matrix_rotate(matrix, MM_MATRIX_Y, yaw, 0);
        if (axes)
            *axes |= MM_ACTOR_ROTATE_Y;
    }
    if ((flags & MM_ACTOR_ROTATE_X) && vector->dyr)
    {
        horizontal = fraction(mm_fixed_add(abs_word(rx), abs_word(rz)));
        pitch = mm_angle_atan2(-ey, horizontal);
        if (matrix)
            mm_matrix_rotate(matrix, MM_MATRIX_X, pitch, 1);
        if (axes)
            *axes |= MM_ACTOR_ROTATE_X;
    }
    if (x)
        *x = pitch;
    if (y)
        *y = yaw;
    if (z)
        *z = 0;
}
static MMI32 bound(MMI32 n)
{
    return n < -65536L ? -65536L : n >= 65536L ? 65535L : n;
}
static void tangent(void *route, MMActorPoint *out, MMI32 node, MMI32 weight,
                    const MMActorMotionServices *s)
{
    MMActorRoutePoint a, b;
    MMActorPoint delta;
    MMI32 distance, second;
    s->route_point(route, node, &a);
    s->route_point(route, node + 1, &b);
    delta.dxr = mm_fixed_sub(b.xyz.dxr, a.xyz.dxr);
    delta.dyr = mm_fixed_sub(b.xyz.dyr, a.xyz.dyr);
    delta.dzr = mm_fixed_sub(b.xyz.dzr, a.xyz.dzr);
    distance = max_abs(&delta);
    /* Keep the original signed-component comparisons, not absolute comparisons. */
    if (delta.dxr == distance)
        second = maximum(abs_word(delta.dyr), abs_word(delta.dzr));
    else if (delta.dyr == distance)
        second = maximum(abs_word(delta.dxr), abs_word(delta.dzr));
    else
        second = maximum(abs_word(delta.dxr), abs_word(delta.dyr));
    /* Arithmetic right shift for the legacy signed-word result. */
    second = second < 0 ? -1 - ((-1 - second) / 2) : second / 2;
    distance = mm_fixed_add(distance, second);
    delta.dxr = bound(mm_fixed_div(delta.dxr, distance));
    delta.dyr = bound(mm_fixed_div(delta.dyr, distance));
    delta.dzr = bound(mm_fixed_div(delta.dzr, distance));
    if (weight != 65536L)
    {
        delta.dxr = mm_fixed_mul(weight, delta.dxr);
        delta.dyr = mm_fixed_mul(weight, delta.dyr);
        delta.dzr = mm_fixed_mul(weight, delta.dzr);
    }
    out->dxr = mm_fixed_add(out->dxr, delta.dxr);
    out->dyr = mm_fixed_add(out->dyr, delta.dyr);
    out->dzr = mm_fixed_add(out->dzr, delta.dzr);
}
void mm_actor_pose_orientation(const MMActorState *state, void *route, void *events,
                                     MMActorMatrix *matrix, unsigned short *x, unsigned short *y,
                                     unsigned short *z, MMU32 *axes, const MMActorMotionServices *s)
{
    MMI32 previous, first;
    MMActorPoint vector;
    MMActorEvent add;
    MMActorRoutePoint point;
    if (axes)
        *axes = 0;
    if (x)
        *x = 0;
    if (y)
        *y = 0;
    if (z)
        *z = 0;
    s->event(events, state->addEvent, &add);
    first = add.rtel.irpt;
    previous = state->location.dwrOffset > 0 || state->location.irpt == first ? state->location.irpt
                                                                              : state->location.irpt - 1;
    s->route_point(route, previous, &point);
    if (!point.dwr)
        --previous;
    if (previous < first)
        return;
    vector.dxr = vector.dyr = vector.dzr = 0;
    tangent(route, &vector, previous, 4 * 65536L, s);
    if (previous > first)
    {
        tangent(route, &vector, previous - 1, 2 * 65536L, s);
        if (previous - 1 > first)
            tangent(route, &vector, previous - 2, 65536L, s);
    }
    mm_actor_pose_vector(state->actionFlags, &vector, matrix, x, y, z, axes);
}
void mm_actor_pose_update(MMActorState *state, void *route, void *events,
                                const MMActorPoseInputs *inputs, const MMActorPoint *position,
                                MMActorMatrix *matrix, const MMActorMotionServices *s)
{
    MMActorMatrix scale, rotation;
    MMActorEvent add_event;
    MMActorRoutePoint first;
    PoseAdd add;
    int stretch;
    state->position = *position;
    stretch = state->transform.aevpull.rScaleX != 65536L || state->transform.aevpull.rScaleY != 65536L ||
              state->transform.aevpull.rScaleZ != 65536L || state->transform.rScaleStep != 65536L;
    mm_matrix_identity(&scale);
    mm_matrix_rotate(&scale, MM_MATRIX_X, inputs->restX, 0);
    mm_matrix_rotate(&scale, MM_MATRIX_Y, inputs->restY, 0);
    mm_matrix_rotate(&scale, MM_MATRIX_Z, inputs->restZ, 0);
    if (stretch)
    {
        mm_matrix_scale(&scale, state->transform.aevpull.rScaleX, state->transform.aevpull.rScaleY,
                              state->transform.aevpull.rScaleZ);
        if (state->transform.rScaleStep != 65536L)
            mm_matrix_scale(&scale, state->transform.rScaleStep, state->transform.rScaleStep,
                                  state->transform.rScaleStep);
    }
    if (state->useCurrentRotation)
        mm_matrix_multiply(matrix, &scale, &state->transform.bmat34Cur);
    else
    {
        rotation = state->transform.bmat34Fwd;
        s->event(events, state->addEvent, &add_event);
        s->route_point(route, add_event.rtel.irpt, &first);
        if (inputs->fixedPathOrientation || (state->location.irpt == add_event.rtel.irpt && !state->location.dwrOffset))
        {
            s->payload(events, state->addEvent, &add);
            mm_matrix_rotate(&rotation, MM_MATRIX_X, add.xa, 0);
            mm_matrix_rotate(&rotation, MM_MATRIX_Y, add.ya, 0);
            mm_matrix_rotate(&rotation, MM_MATRIX_Z, add.za, 0);
            state->transform.xaPath = add.xa;
            state->transform.yaPath = add.ya;
            state->transform.zaPath = add.za;
        }
        else
            mm_actor_pose_orientation(state, route, events, &rotation, &state->transform.xaPath,
                                            &state->transform.yaPath, &state->transform.zaPath, NULL, s);
        mm_matrix_multiply(matrix, &scale, &rotation);
        state->transform.bmat34Cur = rotation;
    }
}

void mm_actor_pose_translation(const MMActorState *state, MMActorPoint *position)
{
    position->dxr = mm_fixed_add(state->routeTranslation.dxr, state->position.dxr);
    position->dyr = mm_fixed_add(state->routeTranslation.dyr, state->position.dyr);
    position->dzr = mm_fixed_add(state->routeTranslation.dzr, state->position.dzr);
}

void mm_actor_pose_evaluate(MMActorState *state, void *route, void *events,
                            const MMActorPoseInputs *inputs, const MMActorPoint *position,
                            MMActorBodyPose *pose, const MMActorMotionServices *s)
{
    mm_actor_pose_update(state, route, events, inputs, position, &pose->orientation, s);
    mm_actor_pose_translation(state, &pose->translation);
}

void mm_actor_pose_root(const MMActorBodyPose *pose, MMActorMatrix *matrix)
{
    *matrix = pose->orientation;
    matrix->m[3][0] = mm_fixed_add(matrix->m[3][0], pose->translation.dxr);
    matrix->m[3][1] = mm_fixed_add(matrix->m[3][1], pose->translation.dyr);
    matrix->m[3][2] = mm_fixed_add(matrix->m[3][2], pose->translation.dzr);
}

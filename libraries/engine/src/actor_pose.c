/* Actor pose evaluation converted from engine actor_pose.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_pose.h"
#include <stddef.h>
typedef struct PoseAdd
{
    ThreeMMActorPoint translation;
    unsigned short xa, ya, za, reserved;
} PoseAdd;
typedef char PoseAddWidth[(sizeof(PoseAdd) == 20) ? 1 : -1];
static ThreeMMI32 abs_word(ThreeMMI32 n)
{
    return n < 0 ? three_mm_fixed_sub(0, n) : n;
}
static ThreeMMI32 maximum(ThreeMMI32 a, ThreeMMI32 b)
{
    return a > b ? a : b;
}
static ThreeMMI32 max_abs(const ThreeMMActorPoint *p)
{
    return maximum(maximum(abs_word(p->dxr), abs_word(p->dyr)), abs_word(p->dzr));
}
static ThreeMMI32 fraction(ThreeMMI32 n)
{
    ThreeMMI32 half;
    ThreeMMU32 bits;
    half = n < 0 ? -(ThreeMMI32)((0U - (ThreeMMU32)n) >> 1) : n / 2;
    bits = (ThreeMMU32)half & 65535U;
    return bits < 32768U ? (ThreeMMI32)bits : (ThreeMMI32)bits - 65536L;
}
void three_mm_actor_pose_vector(ThreeMMU32 flags, const ThreeMMActorPoint *vector, ThreeMMActorMatrix *matrix,
                                unsigned short *x, unsigned short *y, unsigned short *z, ThreeMMU32 *axes)
{
    unsigned short yaw = 0, pitch = 0;
    ThreeMMI32 distance, rx, ry, rz, ex, ey, ez, horizontal;
    if (axes)
        *axes = 0;
    distance = three_mm_fixed_add(max_abs(vector), 65536L);
    rx = three_mm_fixed_div(vector->dxr, distance);
    ry = three_mm_fixed_div(vector->dyr, distance);
    rz = three_mm_fixed_div(vector->dzr, distance);
    ex = fraction(rx);
    ey = fraction(ry);
    ez = fraction(rz);
    if ((flags & THREE_MM_ACTOR_ROTATE_Y) && (vector->dxr || vector->dzr))
    {
        yaw = three_mm_angle_atan2(ex, ez);
        if (matrix)
            three_mm_matrix_rotate(matrix, THREE_MM_MATRIX_Y, yaw, 0);
        if (axes)
            *axes |= THREE_MM_ACTOR_ROTATE_Y;
    }
    if ((flags & THREE_MM_ACTOR_ROTATE_X) && vector->dyr)
    {
        horizontal = fraction(three_mm_fixed_add(abs_word(rx), abs_word(rz)));
        pitch = three_mm_angle_atan2(-ey, horizontal);
        if (matrix)
            three_mm_matrix_rotate(matrix, THREE_MM_MATRIX_X, pitch, 1);
        if (axes)
            *axes |= THREE_MM_ACTOR_ROTATE_X;
    }
    if (x)
        *x = pitch;
    if (y)
        *y = yaw;
    if (z)
        *z = 0;
}
static ThreeMMI32 bound(ThreeMMI32 n)
{
    return n < -65536L ? -65536L : n >= 65536L ? 65535L : n;
}
static void tangent(void *route, ThreeMMActorPoint *out, ThreeMMI32 node, ThreeMMI32 weight,
                    const ThreeMMActorMotionServices *s)
{
    ThreeMMActorRoutePoint a, b;
    ThreeMMActorPoint delta;
    ThreeMMI32 distance, second;
    s->route_point(route, node, &a);
    s->route_point(route, node + 1, &b);
    delta.dxr = three_mm_fixed_sub(b.xyz.dxr, a.xyz.dxr);
    delta.dyr = three_mm_fixed_sub(b.xyz.dyr, a.xyz.dyr);
    delta.dzr = three_mm_fixed_sub(b.xyz.dzr, a.xyz.dzr);
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
    distance = three_mm_fixed_add(distance, second);
    delta.dxr = bound(three_mm_fixed_div(delta.dxr, distance));
    delta.dyr = bound(three_mm_fixed_div(delta.dyr, distance));
    delta.dzr = bound(three_mm_fixed_div(delta.dzr, distance));
    if (weight != 65536L)
    {
        delta.dxr = three_mm_fixed_mul(weight, delta.dxr);
        delta.dyr = three_mm_fixed_mul(weight, delta.dyr);
        delta.dzr = three_mm_fixed_mul(weight, delta.dzr);
    }
    out->dxr = three_mm_fixed_add(out->dxr, delta.dxr);
    out->dyr = three_mm_fixed_add(out->dyr, delta.dyr);
    out->dzr = three_mm_fixed_add(out->dzr, delta.dzr);
}
void three_mm_actor_pose_orientation(const ThreeMMActorState *state, void *route, void *events,
                                     ThreeMMActorMatrix *matrix, unsigned short *x, unsigned short *y,
                                     unsigned short *z, ThreeMMU32 *axes, const ThreeMMActorMotionServices *s)
{
    ThreeMMI32 previous, first;
    ThreeMMActorPoint vector;
    ThreeMMActorEvent add;
    ThreeMMActorRoutePoint point;
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
    three_mm_actor_pose_vector(state->actionFlags, &vector, matrix, x, y, z, axes);
}
void three_mm_actor_pose_update(ThreeMMActorState *state, void *route, void *events,
                                const ThreeMMActorPoseInputs *inputs, const ThreeMMActorPoint *position,
                                ThreeMMActorMatrix *matrix, const ThreeMMActorMotionServices *s)
{
    ThreeMMActorMatrix scale, rotation;
    ThreeMMActorEvent add_event;
    ThreeMMActorRoutePoint first;
    PoseAdd add;
    int stretch;
    state->position = *position;
    stretch = state->transform.aevpull.rScaleX != 65536L || state->transform.aevpull.rScaleY != 65536L ||
              state->transform.aevpull.rScaleZ != 65536L || state->transform.rScaleStep != 65536L;
    three_mm_matrix_identity(&scale);
    three_mm_matrix_rotate(&scale, THREE_MM_MATRIX_X, inputs->restX, 0);
    three_mm_matrix_rotate(&scale, THREE_MM_MATRIX_Y, inputs->restY, 0);
    three_mm_matrix_rotate(&scale, THREE_MM_MATRIX_Z, inputs->restZ, 0);
    if (stretch)
    {
        three_mm_matrix_scale(&scale, state->transform.aevpull.rScaleX, state->transform.aevpull.rScaleY,
                              state->transform.aevpull.rScaleZ);
        if (state->transform.rScaleStep != 65536L)
            three_mm_matrix_scale(&scale, state->transform.rScaleStep, state->transform.rScaleStep,
                                  state->transform.rScaleStep);
    }
    if (state->useCurrentRotation)
        three_mm_matrix_multiply(matrix, &scale, &state->transform.bmat34Cur);
    else
    {
        rotation = state->transform.bmat34Fwd;
        s->event(events, state->addEvent, &add_event);
        s->route_point(route, add_event.rtel.irpt, &first);
        if (inputs->fixedPathOrientation || (state->location.irpt == add_event.rtel.irpt && !state->location.dwrOffset))
        {
            s->payload(events, state->addEvent, &add);
            three_mm_matrix_rotate(&rotation, THREE_MM_MATRIX_X, add.xa, 0);
            three_mm_matrix_rotate(&rotation, THREE_MM_MATRIX_Y, add.ya, 0);
            three_mm_matrix_rotate(&rotation, THREE_MM_MATRIX_Z, add.za, 0);
            state->transform.xaPath = add.xa;
            state->transform.yaPath = add.ya;
            state->transform.zaPath = add.za;
        }
        else
            three_mm_actor_pose_orientation(state, route, events, &rotation, &state->transform.xaPath,
                                            &state->transform.yaPath, &state->transform.zaPath, NULL, s);
        three_mm_matrix_multiply(matrix, &scale, &rotation);
        state->transform.bmat34Cur = rotation;
    }
}

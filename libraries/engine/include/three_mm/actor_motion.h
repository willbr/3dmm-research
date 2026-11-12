/* Actor evaluation converted from engine actor_state.cpp and actor_route.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_MOTION_H
#define THREE_MM_ENGINE_ACTOR_MOTION_H
#include "actor_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Signed 16.16 scalar arithmetic. Add/subtract/multiply wrap to 32 bits;
     * multiply rounds toward negative infinity, division toward zero. Division
     * retains the engine's small-divisor reciprocal/saturation policy. */
    ThreeMMI32 three_mm_fixed_add(ThreeMMI32, ThreeMMI32);
    ThreeMMI32 three_mm_fixed_sub(ThreeMMI32, ThreeMMI32);
    ThreeMMI32 three_mm_fixed_mul(ThreeMMI32, ThreeMMI32);
    ThreeMMI32 three_mm_fixed_div(ThreeMMI32, ThreeMMI32);
    typedef struct ThreeMMActorPoint
    {
        ThreeMMI32 dxr, dyr, dzr;
    } ThreeMMActorPoint;
    typedef struct ThreeMMActorRoutePoint
    {
        ThreeMMActorPoint xyz;
        ThreeMMI32 dwr;
    } ThreeMMActorRoutePoint;
    typedef struct ThreeMMActorLocation
    {
        ThreeMMI32 irpt, dwrOffset, dnfrm;
    } ThreeMMActorLocation;
    typedef struct ThreeMMActorEvent
    {
        ThreeMMI32 aet, nfrm;
        ThreeMMActorLocation rtel;
    } ThreeMMActorEvent;
    typedef struct ThreeMMActorMatrix
    {
        ThreeMMI32 m[4][3];
    } ThreeMMActorMatrix;
    typedef struct ThreeMMActorStretch
    {
        ThreeMMI32 rScaleX, rScaleY, rScaleZ;
    } ThreeMMActorStretch;
    typedef struct ThreeMMActorTransform
    {
        ThreeMMActorMatrix bmat34Fwd;
        ThreeMMActorStretch aevpull;
        ThreeMMI32 rScaleStep;
        ThreeMMActorMatrix bmat34Cur;
        unsigned short xaPath, yaPath, zaPath;
    } ThreeMMActorTransform;
    typedef struct ThreeMMActorState
    {
        ThreeMMActorPoint routeTranslation, subrouteTranslation;
        int onStage, frozen, useCurrentRotation;
        ThreeMMI32 step, action, celCount, cel, frame, nextEvent, firstEventInFrame, actionEvent, addEvent;
        ThreeMMU32 actionFlags;
        ThreeMMActorLocation location;
        ThreeMMActorPoint position;
        ThreeMMActorTransform transform;
    } ThreeMMActorState;
    typedef struct ThreeMMActorMotionServices
    {
        ThreeMMU32 (*route_width)(void *);
        ThreeMMI32 (*route_count)(void *);
        void (*route_point)(void *, ThreeMMI32, ThreeMMActorRoutePoint *);
        ThreeMMU32 (*event_width)(void *);
        ThreeMMI32 (*event_count)(void *);
        void (*event)(void *, ThreeMMI32, ThreeMMActorEvent *);
        ThreeMMI32 (*event_size)(void *, ThreeMMI32);
        void (*payload)(void *, ThreeMMI32, void *);
    } ThreeMMActorMotionServices;
    /* Route operations borrow valid tables and locations. Each subroute terminates
     * at a zero-distance node. Clamping stops at the first Action/Step event;
     * arriving at the last node is distinct from attempting to advance beyond it.
     * Interpolation output may alias either input. No table writes or host effects. */
    void three_mm_actor_route_advance(void *route, void *events, ThreeMMI32 step, ThreeMMActorLocation *,
                                      ThreeMMI32 next_event, ThreeMMI32 frame, ThreeMMI32 first_frame, int clamp,
                                      int *end, const ThreeMMActorMotionServices *);
    void three_mm_actor_route_position(void *route, const ThreeMMActorLocation *, ThreeMMActorPoint *,
                                       const ThreeMMActorMotionServices *);
    void three_mm_actor_interpolate(const ThreeMMActorPoint *, const ThreeMMActorPoint *, ThreeMMI32,
                                    ThreeMMActorPoint *);
    void three_mm_actor_state_init(ThreeMMActorState *);
    void three_mm_actor_state_init_transform(ThreeMMActorState *);
    void three_mm_actor_state_rewind(ThreeMMActorState *, void *route, const ThreeMMActorMotionServices *);
    void three_mm_actor_state_origin(ThreeMMActorState *, const ThreeMMActorPoint *);
    enum
    {
        THREE_MM_ACTOR_APPLIED,
        THREE_MM_ACTOR_NEEDS_RUNTIME,
        THREE_MM_ACTOR_INVALID
    };
    /* Pure events validate layout before reading payloads. Zero-step locations are
     * checked before state changes. Runtime events defer payload checks to their
     * host. Invalid/NeedsRuntime leave state unchanged. Callbacks cannot mutate or
     * re-enter state/tables. Rewind preserves stage/frame/cached action metadata. */
    int three_mm_actor_state_apply(ThreeMMActorState *, void *route, void *events, ThreeMMI32 index,
                                   const ThreeMMActorPoint *origin, const ThreeMMActorMotionServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Actor evaluation converted from engine actor_state.cpp and actor_route.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_MOTION_H
#define MM_ENGINE_ACTOR_MOTION_H
#include "actor_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Signed 16.16 scalar arithmetic. Add/subtract/multiply wrap to 32 bits;
     * multiply rounds toward negative infinity, division toward zero. Division
     * retains the engine's small-divisor reciprocal/saturation policy. */
    MMI32 mm_fixed_add(MMI32, MMI32);
    MMI32 mm_fixed_sub(MMI32, MMI32);
    MMI32 mm_fixed_mul(MMI32, MMI32);
    MMI32 mm_fixed_div(MMI32, MMI32);
    typedef struct MMActorPoint
    {
        MMI32 dxr, dyr, dzr;
    } MMActorPoint;
    typedef struct MMActorRoutePoint
    {
        MMActorPoint xyz;
        MMI32 dwr;
    } MMActorRoutePoint;
    typedef struct MMActorLocation
    {
        MMI32 irpt, dwrOffset, dnfrm;
    } MMActorLocation;
    typedef struct MMActorEvent
    {
        MMI32 aet, nfrm;
        MMActorLocation rtel;
    } MMActorEvent;
    typedef struct MMActorMatrix
    {
        MMI32 m[4][3];
    } MMActorMatrix;
    typedef struct MMActorStretch
    {
        MMI32 rScaleX, rScaleY, rScaleZ;
    } MMActorStretch;
    typedef struct MMActorTransform
    {
        MMActorMatrix bmat34Fwd;
        MMActorStretch aevpull;
        MMI32 rScaleStep;
        MMActorMatrix bmat34Cur;
        unsigned short xaPath, yaPath, zaPath;
    } MMActorTransform;
    typedef struct MMActorState
    {
        MMActorPoint routeTranslation, subrouteTranslation;
        int onStage, frozen, useCurrentRotation;
        MMI32 step, action, celCount, cel, frame, nextEvent, firstEventInFrame, actionEvent, addEvent;
        MMU32 actionFlags;
        MMActorLocation location;
        MMActorPoint position;
        MMActorTransform transform;
    } MMActorState;
    typedef struct MMActorMotionServices
    {
        MMU32 (*route_width)(void *);
        MMI32 (*route_count)(void *);
        void (*route_point)(void *, MMI32, MMActorRoutePoint *);
        MMU32 (*event_width)(void *);
        MMI32 (*event_count)(void *);
        void (*event)(void *, MMI32, MMActorEvent *);
        MMI32 (*event_size)(void *, MMI32);
        void (*payload)(void *, MMI32, void *);
    } MMActorMotionServices;
    /* Route operations borrow valid tables and locations. Each subroute terminates
     * at a zero-distance node. Clamping stops at the first Action/Step event;
     * arriving at the last node is distinct from attempting to advance beyond it.
     * Interpolation output may alias either input. No table writes or host effects. */
    void mm_actor_route_advance(void *route, void *events, MMI32 step, MMActorLocation *,
                                      MMI32 next_event, MMI32 frame, MMI32 first_frame, int clamp,
                                      int *end, const MMActorMotionServices *);
    void mm_actor_route_position(void *route, const MMActorLocation *, MMActorPoint *,
                                       const MMActorMotionServices *);
    void mm_actor_interpolate(const MMActorPoint *, const MMActorPoint *, MMI32,
                                    MMActorPoint *);
    void mm_actor_state_init(MMActorState *);
    void mm_actor_state_init_transform(MMActorState *);
    void mm_actor_state_rewind(MMActorState *, void *route, const MMActorMotionServices *);
    void mm_actor_state_origin(MMActorState *, const MMActorPoint *);
    enum
    {
        MM_ACTOR_APPLIED,
        MM_ACTOR_NEEDS_RUNTIME,
        MM_ACTOR_INVALID
    };
    /* Pure events validate layout before reading payloads. Zero-step locations are
     * checked before state changes. Runtime events defer payload checks to their
     * host. Invalid/NeedsRuntime leave state unchanged. Callbacks cannot mutate or
     * re-enter state/tables. Rewind preserves stage/frame/cached action metadata. */
    int mm_actor_state_apply(MMActorState *, void *route, void *events, MMI32 index,
                                   const MMActorPoint *origin, const MMActorMotionServices *);
#ifdef __cplusplus
}
#endif
#endif

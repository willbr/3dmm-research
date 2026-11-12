/* Actor value evaluation converted from engine actor_state.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_motion.h"
#include <string.h>
void three_mm_actor_state_init_transform(ThreeMMActorState *s)
{
    unsigned i, j;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j)
            s->transform.bmat34Fwd.m[i][j] = s->transform.bmat34Cur.m[i][j] = i == j ? 65536L : 0;
    s->transform.aevpull.rScaleX = s->transform.aevpull.rScaleY = s->transform.aevpull.rScaleZ = 65536L;
    s->transform.rScaleStep = 65536L;
    s->transform.xaPath = s->transform.yaPath = s->transform.zaPath = 0;
}
void three_mm_actor_state_init(ThreeMMActorState *s)
{
    memset(s, 0, sizeof(*s));
    s->frame = 2147483647L;
    s->actionEvent = s->addEvent = -1;
    three_mm_actor_state_init_transform(s);
}
void three_mm_actor_state_rewind(ThreeMMActorState *state, void *route, const ThreeMMActorMotionServices *s)
{
    ThreeMMActorRoutePoint point;
    state->frozen = 0;
    state->action = state->cel = 0;
    state->location.irpt = state->location.dwrOffset = 0;
    state->location.dnfrm = -1;
    if (route && s->route_count(route) > 0)
    {
        s->route_point(route, 0, &point);
        state->position = point.xyz;
    }
    state->nextEvent = state->firstEventInFrame = 0;
    state->actionEvent = state->addEvent = -1;
    state->step = 0;
    three_mm_actor_state_init_transform(state);
}
void three_mm_actor_state_origin(ThreeMMActorState *s, const ThreeMMActorPoint *origin)
{
    s->routeTranslation.dxr = three_mm_fixed_add(origin->dxr, s->subrouteTranslation.dxr);
    s->routeTranslation.dyr = three_mm_fixed_add(origin->dyr, s->subrouteTranslation.dyr);
    s->routeTranslation.dzr = three_mm_fixed_add(origin->dzr, s->subrouteTranslation.dzr);
}
int three_mm_actor_state_apply(ThreeMMActorState *state, void *route, void *events, ThreeMMI32 index,
                               const ThreeMMActorPoint *origin, const ThreeMMActorMotionServices *s)
{
    ThreeMMActorEvent event;
    ThreeMMI32 size, scalar;
    ThreeMMActorRoutePoint point;
    ThreeMMActorPoint delta;
    if (!events || s->event_width(events) != 20 || index < 0 || index >= s->event_count(events))
        return THREE_MM_ACTOR_INVALID;
    s->event(events, index, &event);
    switch (event.aet)
    {
    case THREE_MM_ACTOR_ROTATE_FORWARD:
    case THREE_MM_ACTOR_ROTATE_HERE:
        size = 48;
        break;
    case THREE_MM_ACTOR_STRETCH:
    case THREE_MM_ACTOR_TWEAK:
    case THREE_MM_ACTOR_MOVE:
        size = 12;
        break;
    case THREE_MM_ACTOR_SIZE:
    case THREE_MM_ACTOR_STEP:
    case THREE_MM_ACTOR_FREEZE:
        size = 4;
        break;
    case THREE_MM_ACTOR_ACTION:
    case THREE_MM_ACTOR_ADD:
    case THREE_MM_ACTOR_REMOVE:
    case THREE_MM_ACTOR_COSTUME:
    case THREE_MM_ACTOR_SOUND:
        return THREE_MM_ACTOR_NEEDS_RUNTIME;
    default:
        return THREE_MM_ACTOR_INVALID;
    }
    if (s->event_size(events, index) != size)
        return THREE_MM_ACTOR_INVALID;
    switch (event.aet)
    {
    case THREE_MM_ACTOR_ROTATE_FORWARD:
        s->payload(events, index, &state->transform.bmat34Fwd);
        state->useCurrentRotation = 0;
        break;
    case THREE_MM_ACTOR_ROTATE_HERE:
        s->payload(events, index, &state->transform.bmat34Cur);
        state->useCurrentRotation = 1;
        break;
    case THREE_MM_ACTOR_STRETCH:
        s->payload(events, index, &state->transform.aevpull);
        break;
    case THREE_MM_ACTOR_SIZE:
        s->payload(events, index, &state->transform.rScaleStep);
        break;
    case THREE_MM_ACTOR_STEP:
        s->payload(events, index, &scalar);
        if (!scalar)
        {
            if (!route || s->route_width(route) != 16 || event.rtel.irpt < 0 ||
                event.rtel.irpt >= s->route_count(route) || event.rtel.dwrOffset < 0)
                return THREE_MM_ACTOR_INVALID;
            s->route_point(route, event.rtel.irpt, &point);
            if (event.rtel.dwrOffset > point.dwr)
                return THREE_MM_ACTOR_INVALID;
            state->location = event.rtel;
            three_mm_actor_route_position(route, &state->location, &state->position, s);
        }
        state->step = scalar;
        break;
    case THREE_MM_ACTOR_FREEZE:
        s->payload(events, index, &scalar);
        state->frozen = scalar != 0;
        break;
    case THREE_MM_ACTOR_TWEAK:
        if (event.rtel.dnfrm == state->location.dnfrm)
            s->payload(events, index, &state->position);
        break;
    case THREE_MM_ACTOR_MOVE:
        s->payload(events, index, &delta);
        state->subrouteTranslation.dxr = three_mm_fixed_add(delta.dxr, state->subrouteTranslation.dxr);
        state->subrouteTranslation.dyr = three_mm_fixed_add(delta.dyr, state->subrouteTranslation.dyr);
        state->subrouteTranslation.dzr = three_mm_fixed_add(delta.dzr, state->subrouteTranslation.dzr);
        three_mm_actor_state_origin(state, origin);
        break;
    }
    return THREE_MM_ACTOR_APPLIED;
}

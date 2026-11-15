/* Actor value evaluation converted from engine actor_state.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_motion.h"
#include <string.h>
void mm_actor_state_init_transform(MMActorState *s)
{
    unsigned i, j;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j)
            s->transform.bmat34Fwd.m[i][j] = s->transform.bmat34Cur.m[i][j] = i == j ? 65536L : 0;
    s->transform.aevpull.rScaleX = s->transform.aevpull.rScaleY = s->transform.aevpull.rScaleZ = 65536L;
    s->transform.rScaleStep = 65536L;
    s->transform.xaPath = s->transform.yaPath = s->transform.zaPath = 0;
}
void mm_actor_state_init(MMActorState *s)
{
    memset(s, 0, sizeof(*s));
    s->frame = 2147483647L;
    s->actionEvent = s->addEvent = -1;
    mm_actor_state_init_transform(s);
}
void mm_actor_state_rewind(MMActorState *state, void *route, const MMActorMotionServices *s)
{
    MMActorRoutePoint point;
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
    mm_actor_state_init_transform(state);
}
void mm_actor_state_origin(MMActorState *s, const MMActorPoint *origin)
{
    s->routeTranslation.dxr = mm_fixed_add(origin->dxr, s->subrouteTranslation.dxr);
    s->routeTranslation.dyr = mm_fixed_add(origin->dyr, s->subrouteTranslation.dyr);
    s->routeTranslation.dzr = mm_fixed_add(origin->dzr, s->subrouteTranslation.dzr);
}
int mm_actor_state_apply(MMActorState *state, void *route, void *events, MMI32 index,
                               const MMActorPoint *origin, const MMActorMotionServices *s)
{
    MMActorEvent event;
    MMI32 size, scalar;
    MMActorRoutePoint point;
    MMActorPoint delta;
    if (!events || s->event_width(events) != 20 || index < 0 || index >= s->event_count(events))
        return MM_ACTOR_INVALID;
    s->event(events, index, &event);
    switch (event.aet)
    {
    case MM_ACTOR_ROTATE_FORWARD:
    case MM_ACTOR_ROTATE_HERE:
        size = 48;
        break;
    case MM_ACTOR_STRETCH:
    case MM_ACTOR_TWEAK:
    case MM_ACTOR_MOVE:
        size = 12;
        break;
    case MM_ACTOR_SIZE:
    case MM_ACTOR_STEP:
    case MM_ACTOR_FREEZE:
        size = 4;
        break;
    case MM_ACTOR_ACTION:
    case MM_ACTOR_ADD:
    case MM_ACTOR_REMOVE:
    case MM_ACTOR_COSTUME:
    case MM_ACTOR_SOUND:
        return MM_ACTOR_NEEDS_RUNTIME;
    default:
        return MM_ACTOR_INVALID;
    }
    if (s->event_size(events, index) != size)
        return MM_ACTOR_INVALID;
    switch (event.aet)
    {
    case MM_ACTOR_ROTATE_FORWARD:
        s->payload(events, index, &state->transform.bmat34Fwd);
        state->useCurrentRotation = 0;
        break;
    case MM_ACTOR_ROTATE_HERE:
        s->payload(events, index, &state->transform.bmat34Cur);
        state->useCurrentRotation = 1;
        break;
    case MM_ACTOR_STRETCH:
        s->payload(events, index, &state->transform.aevpull);
        break;
    case MM_ACTOR_SIZE:
        s->payload(events, index, &state->transform.rScaleStep);
        break;
    case MM_ACTOR_STEP:
        s->payload(events, index, &scalar);
        if (!scalar)
        {
            if (!route || s->route_width(route) != 16 || event.rtel.irpt < 0 ||
                event.rtel.irpt >= s->route_count(route) || event.rtel.dwrOffset < 0)
                return MM_ACTOR_INVALID;
            s->route_point(route, event.rtel.irpt, &point);
            if (event.rtel.dwrOffset > point.dwr)
                return MM_ACTOR_INVALID;
            state->location = event.rtel;
            mm_actor_route_position(route, &state->location, &state->position, s);
        }
        state->step = scalar;
        break;
    case MM_ACTOR_FREEZE:
        s->payload(events, index, &scalar);
        state->frozen = scalar != 0;
        break;
    case MM_ACTOR_TWEAK:
        if (event.rtel.dnfrm == state->location.dnfrm)
            s->payload(events, index, &state->position);
        break;
    case MM_ACTOR_MOVE:
        s->payload(events, index, &delta);
        state->subrouteTranslation.dxr = mm_fixed_add(delta.dxr, state->subrouteTranslation.dxr);
        state->subrouteTranslation.dyr = mm_fixed_add(delta.dyr, state->subrouteTranslation.dyr);
        state->subrouteTranslation.dzr = mm_fixed_add(delta.dzr, state->subrouteTranslation.dzr);
        mm_actor_state_origin(state, origin);
        break;
    }
    return MM_ACTOR_APPLIED;
}

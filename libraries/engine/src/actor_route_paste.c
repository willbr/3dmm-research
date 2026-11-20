/* Actor route paste converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_route_paste.h"

static void publish(const MMActorState *state, const MMActorEditServices *edits)
{
    if (edits->publish)
        edits->publish(edits->context, state);
}

static int paste_route(MMActorOwner *actor, MMActorState *state, const MMActorPath *clipboard,
                       int legacy_static, const MMActorRoutePasteServices *s)
{
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    const MMActorRouteEditServices *route_edits = &s->subroute.route;
    const MMActorEditServices *edits = &route_edits->edits;
    MMActorPath *route = actor->track.route;
    MMActorEventTable *events = actor->track.events;
    MMActorRoutePoint point, current;
    MMActorPoint delta, position;
    MMActorEvent event;
    MMI32 removed = 0, added = mm_actor_path_count(clipboard) - 1;
    MMI32 i, prior, value;
    MMU32 flags = 0;

    if (added <= 0)
    {
        s->diagnostic(edits->context, MM_ROUTE_PASTE_EMPTY);
        return 0;
    }
    if (!route_edits->reserve_points(route, added) || !mm_actor_edit_reserve(edits, 1, 4))
        return 0;
    mm_actor_route_position(route, &state->location, &current.xyz, &motion);
    current.dwr = 1;
    if (state->location.dwrOffset)
    {
        prior = state->location.dwrOffset;
        ++state->location.irpt;
        state->location.dwrOffset = state->location.dnfrm = 0;
        publish(state, edits);
        if (!mm_actor_route_edit_insert(route, state->location.irpt, &current, prior, &motion, route_edits))
            return 0;
        mm_actor_route_position(route, &state->location, &state->position, &motion);
        publish(state, edits);
        mm_actor_route_edit_adjust_insert(route, events, state->location.irpt, 0, &motion, route_edits);
    }
    for (i = state->location.irpt + 1; i < mm_actor_path_count(route); ++i)
    {
        mm_actor_path_get(route, i, &point);
        ++removed;
        if (!point.dwr)
            break;
    }
    if (removed)
        route_edits->delete_points(route, state->location.irpt, removed);
    for (i = state->nextEvent; i < mm_actor_events_count(events); ++i)
    {
        mm_actor_events_get_fixed(events, i, &event);
        if (event.rtel.irpt > state->location.irpt + removed)
        {
            event.rtel.irpt += added - removed;
            route_edits->write_event(events, i, &event);
        }
        else
        {
            mm_actor_edit_remove(state, events, i, 0, &motion, edits);
            --i;
        }
    }
    mm_actor_path_get(clipboard, 0, &point);
    delta.dxr = mm_fixed_sub(current.xyz.dxr, point.xyz.dxr);
    delta.dyr = mm_fixed_sub(current.xyz.dyr, point.xyz.dyr);
    delta.dzr = mm_fixed_sub(current.xyz.dzr, point.xyz.dzr);
    for (i = 1; i <= added; ++i)
    {
        mm_actor_path_get(clipboard, i, &point);
        point.xyz.dxr = mm_fixed_add(point.xyz.dxr, delta.dxr);
        point.xyz.dyr = mm_fixed_add(point.xyz.dyr, delta.dyr);
        point.xyz.dzr = mm_fixed_add(point.xyz.dzr, delta.dzr);
        if (!route_edits->insert_point(route, state->location.irpt + i, &point))
            s->diagnostic(edits->context, MM_ROUTE_PASTE_INSERT_FAILED);
    }
    mm_actor_path_get(route, state->location.irpt + 1, &point);
    delta.dxr = mm_fixed_sub(point.xyz.dxr, current.xyz.dxr);
    delta.dyr = mm_fixed_sub(point.xyz.dyr, current.xyz.dyr);
    delta.dzr = mm_fixed_sub(point.xyz.dzr, current.xyz.dzr);
    current.dwr = s->distance(edits->context, &delta);
    if (!current.dwr)
        current.dwr = 1;
    route_edits->write_point(route, state->location.irpt, &current);
    if (legacy_static)
    {
        if (state->actionEvent < 0)
            s->diagnostic(edits->context, MM_ROUTE_PASTE_NO_ACTION);
        if (!s->subroute.action.action_flags(s->subroute.action.context, state->action, &flags))
            return 0;
    }
    if ((!legacy_static || (flags & 8)) &&
        !mm_actor_edit_set_step(state, &actor->sounds, events, actor->runtime.actor, -65536L,
                                &motion, &s->subroute.action.sounds.edits))
        return 0;
    event.aet = MM_ACTOR_FREEZE;
    event.nfrm = state->frame;
    event.rtel.irpt = state->location.irpt + added;
    event.rtel.dwrOffset = event.rtel.dnfrm = 0;
    value = 1;
    if (!mm_actor_edit_insert(state, state->nextEvent, sizeof(value), &value, &event, 1, edits))
        return 0;
    event.aet = MM_ACTOR_STEP;
    value = 0;
    if (!mm_actor_edit_insert(state, state->nextEvent, sizeof(value), &value, &event, 1, edits))
        return 0;
    publish(state, edits);
    route_edits->life_dirty(edits->context);
    edits->invalidate_range(edits->context);
    edits->dirty(edits->context);
    position = state->position;
    mm_actor_subroute_position_body(state, route, events, &position, &motion, &s->subroute);
    return 1;
}

int mm_actor_route_paste(MMActorOwner *actor, const MMActorPath *clipboard,
                        int legacy_static, const MMActorRoutePasteServices *s)
{
    MMActorState state = actor->runtime.state;
    int ok = paste_route(actor, &state, clipboard, legacy_static, s);
    actor->runtime.state = state;
    return ok;
}

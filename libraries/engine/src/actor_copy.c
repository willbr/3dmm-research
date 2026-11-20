/* Actor copying converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_copy.h"
#include <stddef.h>

static void publish(MMActorOwnerResult *out, void *actor)
{
    out->actor = actor;
    if (out->publish)
        out->publish(out->context, actor);
}

static MMActorOwner *create_copy(const MMActorOwner *source, MMActorOwnerResult *out, const MMActorCreateServices *s)
{
    void *actor = mm_actor_create(&source->runtime.template_tag, s);
    publish(out, actor);
    return actor ? s->owner.owner(s->owner.context, actor) : NULL;
}

static int fail(MMActorOwnerResult *out, const MMActorOwnerServices *s)
{
    s->release(s->context, out->actor);
    publish(out, NULL);
    return 0;
}

static int insert(MMActorOwner *target, MMActorEventTable *source, MMI32 i, MMI32 added,
                  const void *replacement, const MMActorEvent *event, const MMActorEditServices *edits)
{
    const void *payload;
    MMI32 bytes;
    int ok;
    mm_actor_events_lock(source);
    payload = mm_actor_events_payload(source, i, &bytes);
    ok = mm_actor_edit_insert(&target->runtime.state, added, bytes, replacement ? replacement : payload,
                              event, 0, edits);
    mm_actor_events_unlock(source);
    return ok;
}

int mm_actor_copy(MMActorOwner *source, int entire, int postponed,
                  MMActorOwnerResult *out, const MMActorCopyServices *s)
{
    const MMActorState state = source->runtime.state;
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    const MMActorPath *route = source->track.route;
    MMActorEventTable *events = source->track.events;
    const MMActorOwnerServices *owner = &s->create.owner;
    MMActorEvent event;
    MMActorSound sound;
    MMActorRoutePoint point;
    MMActorEditServices edits = {0};
    MMI32 i, limit, node, action[2], added;
    MMActorOwner *target = create_copy(source, out, &s->create);
    const void *replacement;
    if (!target)
        return 0;

    edits.tables = mm_actor_edit_tables(target->track.events, owner->tags);
    mm_actor_path_get(route, state.location.irpt, &point);
    point.dwr = mm_fixed_sub(point.dwr, state.location.dwrOffset);
    for (i = state.addEvent; i < state.nextEvent; ++i)
    {
        mm_actor_events_get_fixed(events, i, &event);
        replacement = NULL;
        switch (event.aet)
        {
        case MM_ACTOR_ACTION:
            mm_actor_events_get_payload(events, i, action);
            action[1] = state.cel;
            replacement = action;
            break;
        case MM_ACTOR_ADD: case MM_ACTOR_COSTUME: case MM_ACTOR_STRETCH:
        case MM_ACTOR_SIZE: case MM_ACTOR_ROTATE_FORWARD: case MM_ACTOR_FREEZE:
        case MM_ACTOR_STEP: case MM_ACTOR_MOVE:
            break;
        case MM_ACTOR_SOUND:
            mm_actor_events_get_payload(events, i, &sound);
            if (!s->resolve_sound(s->context, &sound))
                return fail(out, owner);
            mm_actor_events_set_payload(events, i, &sound);
            if (sound.cel == -1 && i >= state.firstEventInFrame)
                break;
            if (i > state.actionEvent && sound.cel != -1)
                break;
            continue;
        case MM_ACTOR_TWEAK: case MM_ACTOR_ROTATE_HERE:
            if (i >= (postponed ? state.nextEvent : state.firstEventInFrame))
                break;
            continue;
        case MM_ACTOR_REMOVE:
            continue;
        default:
            s->unknown_event(s->context);
            break;
        }
        event.rtel.irpt = event.rtel.dwrOffset = event.rtel.dnfrm = 0;
        added = mm_actor_events_count(target->track.events);
        if (!insert(target, events, i, added, replacement, &event, &edits))
            return fail(out, owner);
        mm_actor_edit_merge(&target->runtime.state, &target->sounds, target->track.events, target->runtime.actor,
                            0, added, NULL, &motion, &edits);
    }
    for (i = state.nextEvent; i < mm_actor_events_count(events); ++i)
    {
        mm_actor_events_get_fixed(events, i, &event);
        added = mm_actor_events_count(target->track.events);
        if (!entire && event.aet == MM_ACTOR_ADD && added > 0)
            break;
        event.rtel.irpt -= state.location.irpt;
        if (!event.rtel.irpt)
        {
            if (event.rtel.dwrOffset == state.location.dwrOffset)
            {
                event.rtel.dwrOffset = 0;
                event.rtel.dnfrm = event.rtel.dnfrm < state.location.dnfrm ? 0 : event.rtel.dnfrm - state.location.dnfrm;
            }
            else
                event.rtel.dwrOffset = point.dwr ? mm_fixed_sub(event.rtel.dwrOffset, state.location.dwrOffset) : 0;
        }
        if (!insert(target, events, i, added, NULL, &event, &edits))
            return fail(out, owner);
    }
    if (!mm_actor_path_insert(target->track.route, 0, &point))
        return fail(out, owner);
    limit = mm_actor_path_count(route);
    if (state.location.irpt + 1 < limit)
    {
        if (!entire)
            for (node = state.location.irpt; node < limit; ++node)
            {
                mm_actor_path_get(route, node, &point);
                if (!point.dwr)
                {
                    limit = node + 1;
                    break;
                }
            }
        i = limit - (state.location.irpt + 1);
        if (mm_array_set_count(&target->track.route->rows, 1 + i) != MM_STORAGE_OK)
            return fail(out, owner);
        mm_actor_path_copy(target->track.route, 1, route, state.location.irpt + 1, i);
    }
    else
    {
        point.dwr = 0;
        mm_actor_path_put(target->track.route, 0, &point);
    }
    mm_actor_owner_rewind(out->actor, owner);
    return 1;
}

int mm_actor_copy_route(const MMActorOwner *source, int entire, MMActorOwnerResult *out,
                        const MMActorCopyServices *s)
{
    const MMActorLocation location = source->runtime.state.location;
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    const MMActorOwnerServices *owner = &s->create.owner;
    MMActorPath *route = source->track.route;
    MMActorRoutePoint point, node, next;
    MMActorPoint delta;
    MMI32 i, limit, count;
    MMActorOwner *target = create_copy(source, out, &s->create);
    MMActorPath *path;
    if (!target)
        return 0;

    path = target->track.route;
    mm_actor_route_position(route, &location, &point.xyz, &motion);
    mm_actor_path_get(route, location.irpt, &node);
    point.dwr = 0;
    limit = mm_actor_path_count(route);
    if (!location.dwrOffset)
        point.dwr = node.dwr;
    else if (node.dwr > 0 && location.irpt < limit - 1)
    {
        mm_actor_path_get(route, location.irpt + 1, &next);
        delta.dxr = mm_fixed_sub(next.xyz.dxr, point.xyz.dxr);
        delta.dyr = mm_fixed_sub(next.xyz.dyr, point.xyz.dyr);
        delta.dzr = mm_fixed_sub(next.xyz.dzr, point.xyz.dzr);
        point.dwr = s->distance(s->context, &delta);
        if (!point.dwr)
            point.dwr = 1;
    }
    if (!mm_actor_path_insert(path, 0, &point))
        return fail(out, owner);
    if (entire || point.dwr)
    {
        if (!entire)
            for (i = location.irpt + 1; i < limit; ++i)
            {
                mm_actor_path_get(route, i, &point);
                if (!point.dwr)
                {
                    limit = i + 1;
                    break;
                }
            }
        count = limit - (location.irpt + 1);
        if (count > 0 && mm_array_reserve(&path->rows, count, 0) != MM_STORAGE_OK)
            return fail(out, owner);
        for (i = location.irpt + 1; i < limit; ++i)
        {
            mm_actor_path_get(route, i, &point);
            if (!mm_actor_path_insert(path, i - location.irpt, &point))
                return fail(out, owner);
        }
    }
    mm_actor_owner_rewind(out->actor, owner);
    return 1;
}

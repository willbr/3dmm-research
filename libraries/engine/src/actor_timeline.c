/* Actor timeline evaluation converted from engine actor_timeline.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_timeline.h"
#include "mm/wide.h"
#include <stddef.h>
static void publish(const MMActorState *state, const MMActorTimelineServices *s)
{
    if (s->publish)
        s->publish(s->context, state);
}
static void diagnostic(int fault, const MMActorTimelineServices *s)
{
    if (s->diagnostic)
        s->diagnostic(s->context, fault);
}
static int after(const MMActorLocation *a, const MMActorLocation *b)
{
    return a->irpt > b->irpt || (a->irpt == b->irpt && (a->dwrOffset > b->dwrOffset ||
                                                        (a->dwrOffset == b->dwrOffset && a->dnfrm > b->dnfrm)));
}
int mm_actor_step(MMI32 action, MMI32 cel, MMI32 *step, const MMActorTimelineServices *s)
{
    *step = 0;
    return s->resolve && s->resolve(s->context, action, cel, step);
}
int mm_actor_playback_step(const MMActorState *state, MMI32 *step,
                                 const MMActorTimelineServices *s)
{
    *step = 0;
    if (state->step == -65536L)
    {
        publish(state, s);
        if (!mm_actor_step(state->action, state->cel, step, s))
            return 0;
    }
    else
        *step = state->step;
    *step = mm_fixed_mul(*step, state->transform.rScaleStep);
    return 1;
}
static int entrance(void *events, MMI32 first, MMI32 *found, const MMActorMotionServices *s)
{
    MMActorEvent event;
    for (; first < s->event_count(events); ++first)
    {
        s->event(events, first, &event);
        if (event.aet == MM_ACTOR_ADD)
        {
            *found = first;
            return 1;
        }
    }
    *found = -1;
    return 0;
}
int mm_actor_stalled(void *events, MMI32 first, const MMActorLocation *p, MMI32 *last,
                           const MMActorTimelineServices *s)
{
    MMActorEvent event;
    MMI32 i, step;
    for (i = first; i < s->motion.event_count(events); ++i)
    {
        s->motion.event(events, i, &event);
        if (event.rtel.irpt > p->irpt || event.rtel.dwrOffset > p->dwrOffset)
        {
            if (last)
                *last = i;
            return 1;
        }
        if (event.rtel.irpt == p->irpt && event.rtel.dwrOffset == p->dwrOffset && event.aet == MM_ACTOR_STEP)
        {
            s->motion.payload(events, i, &step);
            if (step)
                return 0;
        }
    }
    return 0;
}
int mm_actor_lifetime(void *route, void *events, MMI32 first, int rejoining, MMI32 *last,
                            MMI32 bound, const MMActorTimelineServices *s)
{
    const MMActorMotionServices *m = &s->motion;
    MMI32 i, final_add = -1, next = 0, action = 0, cel = 0, scale = 65536L, step = 0, distance;
    MMI32 frozen = 0, payload[2];
    MMActorEvent event;
    MMActorLocation p;
    MMWide elapsed;
    int freeze_cel, end = 0, done = 0;
    p.irpt = p.dwrOffset = 0;
    p.dnfrm = -1;
    for (i = 0; i < m->event_count(events); ++i)
    {
        m->event(events, i, &event);
        if (event.aet == MM_ACTOR_ADD)
            final_add = i;
    }
    if (final_add < 0)
    {
        *last = first;
        return 1;
    }
    for (*last = first; p.irpt != m->route_count(route) || next != m->event_count(events); ++*last)
    {
        if (s->publish_frame)
            s->publish_frame(s->context, *last);
        elapsed = mm_wide_subtract(mm_wide_i32(*last), mm_wide_i32(first));
        if (mm_wide_compare(elapsed, mm_wide_i32(bound)) > 0 || *last == 2147483647L)
            return 0;
        freeze_cel = frozen;
        if (step == -65536L)
        {
            if (!mm_actor_step(action, cel, &distance, s))
                return 0;
        }
        else
            distance = step;
        distance = mm_fixed_mul(distance, scale);
        mm_actor_route_advance(route, events, distance, &p, next, *last, first, !rejoining, &end, m);
        for (i = next; i < m->event_count(events); ++i)
        {
            m->event(events, i, &event);
            if (after(&event.rtel, &p) && (event.aet != MM_ACTOR_ADD || (!end && !done)))
                goto end_frame;
            switch (event.aet)
            {
            case MM_ACTOR_ADD:
                freeze_cel = 1;
                action = cel = 0;
                if (event.nfrm > *last)
                    goto end_frame;
                p = event.rtel;
                done = end = 0;
                break;
            case MM_ACTOR_TWEAK:
            case MM_ACTOR_SOUND:
            case MM_ACTOR_REMOVE:
            case MM_ACTOR_COSTUME:
            case MM_ACTOR_ROTATE_FORWARD:
            case MM_ACTOR_ROTATE_HERE:
            case MM_ACTOR_STRETCH:
            case MM_ACTOR_MOVE:
                break;
            case MM_ACTOR_FREEZE:
                m->payload(events, i, payload);
                frozen = payload[0] != 0;
                break;
            case MM_ACTOR_SIZE:
                m->payload(events, i, &scale);
                break;
            case MM_ACTOR_ACTION:
                m->payload(events, i, payload);
                action = payload[0];
                cel = payload[1];
                frozen = 0;
                freeze_cel = 1;
                break;
            case MM_ACTOR_STEP:
                m->payload(events, i, &step);
                break;
            default:
                diagnostic(MM_ACTOR_UNKNOWN_EVENT, s);
                break;
            }
            if (after(&event.rtel, &p))
                goto end_frame;
            if (event.nfrm != *last)
            {
                event.nfrm = *last;
                s->put_event(events, i, &event);
            }
        }
    end_frame:
        if (!freeze_cel && !(end && mm_actor_subroute_events_done(events, i, &p, m)))
            cel = mm_fixed_add(cel, 1);
        if (end || (!step && (i == m->event_count(events) ||
                              (!(event.rtel.irpt == p.irpt && event.rtel.dwrOffset == p.dwrOffset) &&
                               mm_actor_stalled(events, next, &p, NULL, s)))))
        {
            done = 1;
            if (!entrance(events, i, &i, m))
            {
                if (end && step && *last > first)
                    --*last;
                return 1;
            }
            m->event(events, i, &event);
            p = event.rtel;
            action = cel = 0;
            p.dnfrm = mm_fixed_sub(p.dnfrm, 1);
        }
        next = i;
    }
    diagnostic(MM_ACTOR_LIFETIME_LOGIC, s);
    return 0;
}
int mm_actor_step_back(MMActorState *state, void *route, void *events, MMActorLocation *output,
                             int update, const MMActorTimelineServices *s)
{
    MMI32 cel = state->cel, frame = state->frame, step, distance;
    MMActorLocation saved = state->location;
    MMActorEvent add;
    MMActorRoutePoint point;
    s->motion.event(events, state->addEvent, &add);
    if (!state->frozen)
        state->cel = mm_fixed_sub(state->cel, 1);
    if (!state->step || (add.rtel.irpt == state->location.irpt && add.rtel.dwrOffset == state->location.dwrOffset))
    {
        state->frame = mm_fixed_sub(state->frame, 1);
        state->location.dnfrm = mm_fixed_sub(state->location.dnfrm, 1);
        goto done;
    }
    if (!mm_actor_playback_step(state, &step, s))
        goto fail;
    distance = mm_fixed_sub(state->location.dwrOffset, step);
    if (distance < 0)
    {
        while (distance < 0)
        {
            if (state->location.irpt <= 0)
            {
                publish(state, s);
                diagnostic(MM_ACTOR_CORRUPT_ROUTE, s);
                state->location.irpt = 0;
                distance = 0;
                break;
            }
            --state->location.irpt;
            s->motion.route_point(route, state->location.irpt, &point);
            distance = mm_fixed_add(point.dwr, distance);
        }
    }
    state->location.dwrOffset = distance;
    state->frame = mm_fixed_sub(state->frame, 1);
done:
    *output = state->location;
    if (!update)
    {
        state->cel = cel;
        state->frame = frame;
        state->location = saved;
    }
    return 1;
fail:
    state->cel = cel;
    state->frame = frame;
    state->location = saved;
    return 0;
}
MMActorQuickSeekResult mm_actor_quick_backup(MMActorState *state, void *route, void *events,
                                                        MMI32 last, MMI32 target,
                                                        const MMActorTimelineServices *s)
{
    MMActorQuickSeekResult result = {0, 0, 0};
    MMActorLocation probe;
    MMActorPoint old = state->position, point;
    MMI32 delta = mm_fixed_sub(state->frame, target);
    if (target >= last)
    {
        state->frame = target;
        state->location.dnfrm = mm_fixed_sub(state->location.dnfrm, delta);
        result.success = result.valid = 1;
        return result;
    }
    while (state->frame > target)
        if (!mm_actor_step_back(state, route, events, &state->location, 1, s))
            return result;
    mm_actor_route_position(route, &state->location, &state->position, &s->motion);
    state->frame = target;
    if (delta > 1 || old.dxr != state->position.dxr || old.dzr != state->position.dzr)
    {
        if (!mm_actor_step_back(state, route, events, &probe, 0, s))
            return result;
        mm_actor_route_position(route, &probe, &point, &s->motion);
        if (state->position.dxr == point.dxr && state->position.dzr == point.dzr)
        {
            result.success = 1;
            return result;
        }
    }
    result.success = result.valid = result.applyEffects = 1;
    return result;
}

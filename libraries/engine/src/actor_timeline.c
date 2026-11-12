/* Actor timeline evaluation converted from engine actor_timeline.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_timeline.h"
#include "three_mm/wide.h"
#include <stddef.h>
static void publish(const ThreeMMActorState *state, const ThreeMMActorTimelineServices *s)
{
    if (s->publish)
        s->publish(s->context, state);
}
static void diagnostic(int fault, const ThreeMMActorTimelineServices *s)
{
    if (s->diagnostic)
        s->diagnostic(s->context, fault);
}
static int after(const ThreeMMActorLocation *a, const ThreeMMActorLocation *b)
{
    return a->irpt > b->irpt || (a->irpt == b->irpt && (a->dwrOffset > b->dwrOffset ||
                                                        (a->dwrOffset == b->dwrOffset && a->dnfrm > b->dnfrm)));
}
int three_mm_actor_step(ThreeMMI32 action, ThreeMMI32 cel, ThreeMMI32 *step, const ThreeMMActorTimelineServices *s)
{
    *step = 0;
    return s->resolve && s->resolve(s->context, action, cel, step);
}
int three_mm_actor_playback_step(const ThreeMMActorState *state, ThreeMMI32 *step,
                                 const ThreeMMActorTimelineServices *s)
{
    *step = 0;
    if (state->step == -65536L)
    {
        publish(state, s);
        if (!three_mm_actor_step(state->action, state->cel, step, s))
            return 0;
    }
    else
        *step = state->step;
    *step = three_mm_fixed_mul(*step, state->transform.rScaleStep);
    return 1;
}
static int entrance(void *events, ThreeMMI32 first, ThreeMMI32 *found, const ThreeMMActorMotionServices *s)
{
    ThreeMMActorEvent event;
    for (; first < s->event_count(events); ++first)
    {
        s->event(events, first, &event);
        if (event.aet == THREE_MM_ACTOR_ADD)
        {
            *found = first;
            return 1;
        }
    }
    *found = -1;
    return 0;
}
int three_mm_actor_stalled(void *events, ThreeMMI32 first, const ThreeMMActorLocation *p, ThreeMMI32 *last,
                           const ThreeMMActorTimelineServices *s)
{
    ThreeMMActorEvent event;
    ThreeMMI32 i, step;
    for (i = first; i < s->motion.event_count(events); ++i)
    {
        s->motion.event(events, i, &event);
        if (event.rtel.irpt > p->irpt || event.rtel.dwrOffset > p->dwrOffset)
        {
            if (last)
                *last = i;
            return 1;
        }
        if (event.rtel.irpt == p->irpt && event.rtel.dwrOffset == p->dwrOffset && event.aet == THREE_MM_ACTOR_STEP)
        {
            s->motion.payload(events, i, &step);
            if (step)
                return 0;
        }
    }
    return 0;
}
int three_mm_actor_lifetime(void *route, void *events, ThreeMMI32 first, int rejoining, ThreeMMI32 *last,
                            ThreeMMI32 bound, const ThreeMMActorTimelineServices *s)
{
    const ThreeMMActorMotionServices *m = &s->motion;
    ThreeMMI32 i, final_add = -1, next = 0, action = 0, cel = 0, scale = 65536L, step = 0, distance;
    ThreeMMI32 frozen = 0, payload[2];
    ThreeMMActorEvent event;
    ThreeMMActorLocation p;
    ThreeMMWide elapsed;
    int freeze_cel, end = 0, done = 0;
    p.irpt = p.dwrOffset = 0;
    p.dnfrm = -1;
    for (i = 0; i < m->event_count(events); ++i)
    {
        m->event(events, i, &event);
        if (event.aet == THREE_MM_ACTOR_ADD)
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
        elapsed = three_mm_wide_subtract(three_mm_wide_i32(*last), three_mm_wide_i32(first));
        if (three_mm_wide_compare(elapsed, three_mm_wide_i32(bound)) > 0 || *last == 2147483647L)
            return 0;
        freeze_cel = frozen;
        if (step == -65536L)
        {
            if (!three_mm_actor_step(action, cel, &distance, s))
                return 0;
        }
        else
            distance = step;
        distance = three_mm_fixed_mul(distance, scale);
        three_mm_actor_route_advance(route, events, distance, &p, next, *last, first, !rejoining, &end, m);
        for (i = next; i < m->event_count(events); ++i)
        {
            m->event(events, i, &event);
            if (after(&event.rtel, &p) && (event.aet != THREE_MM_ACTOR_ADD || (!end && !done)))
                goto end_frame;
            switch (event.aet)
            {
            case THREE_MM_ACTOR_ADD:
                freeze_cel = 1;
                action = cel = 0;
                if (event.nfrm > *last)
                    goto end_frame;
                p = event.rtel;
                done = end = 0;
                break;
            case THREE_MM_ACTOR_TWEAK:
            case THREE_MM_ACTOR_SOUND:
            case THREE_MM_ACTOR_REMOVE:
            case THREE_MM_ACTOR_COSTUME:
            case THREE_MM_ACTOR_ROTATE_FORWARD:
            case THREE_MM_ACTOR_ROTATE_HERE:
            case THREE_MM_ACTOR_STRETCH:
            case THREE_MM_ACTOR_MOVE:
                break;
            case THREE_MM_ACTOR_FREEZE:
                m->payload(events, i, payload);
                frozen = payload[0] != 0;
                break;
            case THREE_MM_ACTOR_SIZE:
                m->payload(events, i, &scale);
                break;
            case THREE_MM_ACTOR_ACTION:
                m->payload(events, i, payload);
                action = payload[0];
                cel = payload[1];
                frozen = 0;
                freeze_cel = 1;
                break;
            case THREE_MM_ACTOR_STEP:
                m->payload(events, i, &step);
                break;
            default:
                diagnostic(THREE_MM_ACTOR_UNKNOWN_EVENT, s);
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
        if (!freeze_cel && !(end && three_mm_actor_subroute_events_done(events, i, &p, m)))
            cel = three_mm_fixed_add(cel, 1);
        if (end || (!step && (i == m->event_count(events) ||
                              (!(event.rtel.irpt == p.irpt && event.rtel.dwrOffset == p.dwrOffset) &&
                               three_mm_actor_stalled(events, next, &p, NULL, s)))))
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
            p.dnfrm = three_mm_fixed_sub(p.dnfrm, 1);
        }
        next = i;
    }
    diagnostic(THREE_MM_ACTOR_LIFETIME_LOGIC, s);
    return 0;
}
int three_mm_actor_step_back(ThreeMMActorState *state, void *route, void *events, ThreeMMActorLocation *output,
                             int update, const ThreeMMActorTimelineServices *s)
{
    ThreeMMI32 cel = state->cel, frame = state->frame, step, distance;
    ThreeMMActorLocation saved = state->location;
    ThreeMMActorEvent add;
    ThreeMMActorRoutePoint point;
    s->motion.event(events, state->addEvent, &add);
    if (!state->frozen)
        state->cel = three_mm_fixed_sub(state->cel, 1);
    if (!state->step || (add.rtel.irpt == state->location.irpt && add.rtel.dwrOffset == state->location.dwrOffset))
    {
        state->frame = three_mm_fixed_sub(state->frame, 1);
        state->location.dnfrm = three_mm_fixed_sub(state->location.dnfrm, 1);
        goto done;
    }
    if (!three_mm_actor_playback_step(state, &step, s))
        goto fail;
    distance = three_mm_fixed_sub(state->location.dwrOffset, step);
    if (distance < 0)
    {
        while (distance < 0)
        {
            if (state->location.irpt <= 0)
            {
                publish(state, s);
                diagnostic(THREE_MM_ACTOR_CORRUPT_ROUTE, s);
                state->location.irpt = 0;
                distance = 0;
                break;
            }
            --state->location.irpt;
            s->motion.route_point(route, state->location.irpt, &point);
            distance = three_mm_fixed_add(point.dwr, distance);
        }
    }
    state->location.dwrOffset = distance;
    state->frame = three_mm_fixed_sub(state->frame, 1);
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
ThreeMMActorQuickSeekResult three_mm_actor_quick_backup(ThreeMMActorState *state, void *route, void *events,
                                                        ThreeMMI32 last, ThreeMMI32 target,
                                                        const ThreeMMActorTimelineServices *s)
{
    ThreeMMActorQuickSeekResult result = {0, 0, 0};
    ThreeMMActorLocation probe;
    ThreeMMActorPoint old = state->position, point;
    ThreeMMI32 delta = three_mm_fixed_sub(state->frame, target);
    if (target >= last)
    {
        state->frame = target;
        state->location.dnfrm = three_mm_fixed_sub(state->location.dnfrm, delta);
        result.success = result.valid = 1;
        return result;
    }
    while (state->frame > target)
        if (!three_mm_actor_step_back(state, route, events, &state->location, 1, s))
            return result;
    three_mm_actor_route_position(route, &state->location, &state->position, &s->motion);
    state->frame = target;
    if (delta > 1 || old.dxr != state->position.dxr || old.dzr != state->position.dzr)
    {
        if (!three_mm_actor_step_back(state, route, events, &probe, 0, s))
            return result;
        three_mm_actor_route_position(route, &probe, &point, &s->motion);
        if (state->position.dxr == point.dxr && state->position.dzr == point.dzr)
        {
            result.success = 1;
            return result;
        }
    }
    result.success = result.valid = result.applyEffects = 1;
    return result;
}

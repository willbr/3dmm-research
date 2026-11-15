/* Actor route evaluation converted from engine actor_route.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_motion.h"
static int before(const MMActorLocation *a, const MMActorLocation *b, int equal)
{
    return a->irpt < b->irpt ||
           (a->irpt == b->irpt &&
            (a->dwrOffset < b->dwrOffset ||
             (a->dwrOffset == b->dwrOffset && (equal ? a->dnfrm <= b->dnfrm : a->dnfrm < b->dnfrm))));
}
void mm_actor_route_advance(void *route, void *events, MMI32 step, MMActorLocation *p, MMI32 next,
                                  MMI32 frame, MMI32 first, int clamp, int *end,
                                  const MMActorMotionServices *s)
{
    MMI32 distance, i;
    MMActorRoutePoint point;
    MMActorEvent event;
    int ended = 0;
    if (frame <= first)
    {
        p->irpt = 0;
        p->dwrOffset = 0;
        p->dnfrm = 0;
        goto done;
    }
    if (!step)
    {
        p->dnfrm = mm_fixed_add(p->dnfrm, 1);
        goto done;
    }
    s->route_point(route, p->irpt, &point);
    distance = mm_fixed_sub(point.dwr, p->dwrOffset);
    if (!distance)
    {
        p->dnfrm = mm_fixed_add(p->dnfrm, 1);
        ended = 1;
        goto done;
    }
    while (distance <= step && step > 0)
    {
        if (!distance)
            goto moved;
        p->irpt = mm_fixed_add(p->irpt, 1);
        p->dwrOffset = 0;
        step = mm_fixed_sub(step, distance);
        s->route_point(route, p->irpt, &point);
        distance = point.dwr;
    }
    p->dwrOffset = mm_fixed_add(p->dwrOffset, step);
moved:
    p->dnfrm = 0;
    if (clamp)
        for (i = next; i < s->event_count(events); ++i)
        {
            s->event(events, i, &event);
            if (before(&event.rtel, p, 1) && (event.aet == MM_ACTOR_STEP || event.aet == MM_ACTOR_ACTION))
            {
                *p = event.rtel;
                break;
            }
            if (before(p, &event.rtel, 0))
                break;
        }
done:
    if (end)
        *end = ended;
}
void mm_actor_interpolate(const MMActorPoint *a, const MMActorPoint *b, MMI32 fraction,
                                MMActorPoint *out)
{
    MMActorPoint result;
    result.dxr = mm_fixed_sub(b->dxr, a->dxr);
    result.dyr = mm_fixed_sub(b->dyr, a->dyr);
    result.dzr = mm_fixed_sub(b->dzr, a->dzr);
    result.dxr = mm_fixed_mul(fraction, result.dxr);
    result.dyr = mm_fixed_mul(fraction, result.dyr);
    result.dzr = mm_fixed_mul(fraction, result.dzr);
    result.dxr = mm_fixed_add(result.dxr, a->dxr);
    result.dyr = mm_fixed_add(result.dyr, a->dyr);
    result.dzr = mm_fixed_add(result.dzr, a->dzr);
    *out = result;
}
void mm_actor_route_position(void *route, const MMActorLocation *p, MMActorPoint *out,
                                   const MMActorMotionServices *s)
{
    MMActorRoutePoint a, b;
    s->route_point(route, p->irpt, &a);
    if (!p->dwrOffset || p->irpt + 1 == s->route_count(route))
    {
        *out = a.xyz;
        return;
    }
    s->route_point(route, p->irpt + 1, &b);
    mm_actor_interpolate(&a.xyz, &b.xyz, mm_fixed_div(p->dwrOffset, a.dwr), out);
}

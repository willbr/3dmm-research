/* Actor route evaluation converted from engine actor_route.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_motion.h"
static int before(const ThreeMMActorLocation *a, const ThreeMMActorLocation *b, int equal)
{
    return a->irpt < b->irpt ||
           (a->irpt == b->irpt &&
            (a->dwrOffset < b->dwrOffset ||
             (a->dwrOffset == b->dwrOffset && (equal ? a->dnfrm <= b->dnfrm : a->dnfrm < b->dnfrm))));
}
void three_mm_actor_route_advance(void *route, void *events, ThreeMMI32 step, ThreeMMActorLocation *p, ThreeMMI32 next,
                                  ThreeMMI32 frame, ThreeMMI32 first, int clamp, int *end,
                                  const ThreeMMActorMotionServices *s)
{
    ThreeMMI32 distance, i;
    ThreeMMActorRoutePoint point;
    ThreeMMActorEvent event;
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
        p->dnfrm = three_mm_fixed_add(p->dnfrm, 1);
        goto done;
    }
    s->route_point(route, p->irpt, &point);
    distance = three_mm_fixed_sub(point.dwr, p->dwrOffset);
    if (!distance)
    {
        p->dnfrm = three_mm_fixed_add(p->dnfrm, 1);
        ended = 1;
        goto done;
    }
    while (distance <= step && step > 0)
    {
        if (!distance)
            goto moved;
        p->irpt = three_mm_fixed_add(p->irpt, 1);
        p->dwrOffset = 0;
        step = three_mm_fixed_sub(step, distance);
        s->route_point(route, p->irpt, &point);
        distance = point.dwr;
    }
    p->dwrOffset = three_mm_fixed_add(p->dwrOffset, step);
moved:
    p->dnfrm = 0;
    if (clamp)
        for (i = next; i < s->event_count(events); ++i)
        {
            s->event(events, i, &event);
            if (before(&event.rtel, p, 1) && (event.aet == THREE_MM_ACTOR_STEP || event.aet == THREE_MM_ACTOR_ACTION))
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
void three_mm_actor_interpolate(const ThreeMMActorPoint *a, const ThreeMMActorPoint *b, ThreeMMI32 fraction,
                                ThreeMMActorPoint *out)
{
    ThreeMMActorPoint result;
    result.dxr = three_mm_fixed_sub(b->dxr, a->dxr);
    result.dyr = three_mm_fixed_sub(b->dyr, a->dyr);
    result.dzr = three_mm_fixed_sub(b->dzr, a->dzr);
    result.dxr = three_mm_fixed_mul(fraction, result.dxr);
    result.dyr = three_mm_fixed_mul(fraction, result.dyr);
    result.dzr = three_mm_fixed_mul(fraction, result.dzr);
    result.dxr = three_mm_fixed_add(result.dxr, a->dxr);
    result.dyr = three_mm_fixed_add(result.dyr, a->dyr);
    result.dzr = three_mm_fixed_add(result.dzr, a->dzr);
    *out = result;
}
void three_mm_actor_route_position(void *route, const ThreeMMActorLocation *p, ThreeMMActorPoint *out,
                                   const ThreeMMActorMotionServices *s)
{
    ThreeMMActorRoutePoint a, b;
    s->route_point(route, p->irpt, &a);
    if (!p->dwrOffset || p->irpt + 1 == s->route_count(route))
    {
        *out = a.xyz;
        return;
    }
    s->route_point(route, p->irpt + 1, &b);
    three_mm_actor_interpolate(&a.xyz, &b.xyz, three_mm_fixed_div(p->dwrOffset, a.dwr), out);
}

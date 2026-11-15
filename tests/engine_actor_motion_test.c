#include "mm/actor_motion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "actor motion failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
#define ONE 65536L
typedef struct Tables
{
    MMActorRoutePoint points[4];
    MMActorEvent events[13];
    MMI32 sizes[13], routes, count;
    MMU32 route_width, event_width;
    unsigned char bytes[13][64];
    int point_reads, payload_reads;
} Tables;
static MMU32 route_width(void *p)
{
    return ((Tables *)p)->route_width;
}
static MMI32 route_count(void *p)
{
    return ((Tables *)p)->routes;
}
static void route_point(void *p, MMI32 i, MMActorRoutePoint *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->routes);
    ++t->point_reads;
    *out = t->points[i];
}
static MMU32 event_width(void *p)
{
    return ((Tables *)p)->event_width;
}
static MMI32 event_count(void *p)
{
    return ((Tables *)p)->count;
}
static void event(void *p, MMI32 i, MMActorEvent *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->count);
    *out = t->events[i];
}
static MMI32 event_size(void *p, MMI32 i)
{
    return ((Tables *)p)->sizes[i];
}
static void payload(void *p, MMI32 i, void *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->count && t->sizes[i] >= 0 && t->sizes[i] <= 64);
    ++t->payload_reads;
    memcpy(out, t->bytes[i], (size_t)t->sizes[i]);
}
static MMActorMotionServices services = {route_width, route_count, route_point, event_width,
                                              event_count, event,       event_size,  payload};
static void init(Tables *t)
{
    memset(t, 0, sizeof(*t));
    t->routes = 4;
    t->route_width = 16;
    t->event_width = 20;
    t->points[0].dwr = 4 * ONE;
    t->points[1].xyz.dxr = 4 * ONE;
    t->points[1].dwr = 4 * ONE;
    t->points[2].xyz.dxr = 4 * ONE;
    t->points[2].xyz.dyr = 4 * ONE;
    t->points[3].xyz.dxr = 100 * ONE;
}
static int apply(Tables *t, MMActorState *s, MMI32 type, const void *p, MMI32 size)
{
    MMActorPoint origin = {10 * ONE, 20 * ONE, 30 * ONE};
    t->count = 1;
    t->events[0].aet = type;
    t->sizes[0] = size;
    if (size > 0)
        memcpy(t->bytes[0], p, (size_t)size);
    return mm_actor_state_apply(s, t, t, 0, &origin, &services);
}
static void arithmetic(void)
{
    CHECK(mm_fixed_add(2147483647L, 1) == (-2147483647L - 1));
    CHECK(mm_fixed_sub((-2147483647L - 1), 1) == 2147483647L);
    CHECK(mm_fixed_mul(-1, 1) == -1);
    CHECK(mm_fixed_mul(1, 1) == 0);
    CHECK(mm_fixed_mul(-ONE / 2, ONE / 2) == -ONE / 4);
    CHECK(mm_fixed_mul((-2147483647L - 1), -ONE) == (-2147483647L - 1));
    CHECK(mm_fixed_div(ONE, 2 * ONE) == ONE / 2);
    CHECK(mm_fixed_div(-ONE, 2 * ONE) == -ONE / 2);
    CHECK(mm_fixed_div(1, 0) == (-2147483647L - 1));
    CHECK(mm_fixed_div(-1, 0) == 2147483647L);
    CHECK(mm_fixed_div(ONE, 1) == 2147483647L);
    CHECK(mm_fixed_div(ONE, -1) == (-2147483647L - 1));
    CHECK(mm_fixed_div(3, 3) == 65535L); /* Reciprocal truncation is intentional. */
    CHECK(mm_fixed_div(ONE, ONE / 2) == 2 * ONE);
    CHECK(mm_fixed_div(2147483647L, 3) == 2147483647L);
}
static void routes(void)
{
    Tables t;
    MMActorLocation p = {99, 99, 99};
    MMActorPoint out, a = {2 * ONE, 4 * ONE, 6 * ONE}, b = {6 * ONE, 8 * ONE, 10 * ONE};
    int end = 1;
    init(&t);
    mm_actor_interpolate(&a, &b, ONE / 2, &a);
    CHECK(a.dxr == 4 * ONE && a.dzr == 8 * ONE);
    mm_actor_interpolate(&a, &b, ONE / 2, &b);
    CHECK(b.dxr == 5 * ONE && b.dzr == 9 * ONE);
    mm_actor_route_advance(&t, &t, 2 * ONE, &p, 0, -3, -3, 1, &end, &services);
    CHECK(!p.irpt && !p.dwrOffset && !p.dnfrm && !end && !t.point_reads);
    mm_actor_route_advance(&t, &t, 6 * ONE, &p, 0, -2, -3, 1, &end, &services);
    mm_actor_route_position(&t, &p, &out, &services);
    CHECK(p.irpt == 1 && out.dxr == 4 * ONE && out.dyr == 2 * ONE && !end);
    mm_actor_route_advance(&t, &t, 2 * ONE, &p, 0, -1, -3, 1, &end, &services);
    CHECK(p.irpt == 2 && !p.dwrOffset && !end);
    mm_actor_route_advance(&t, &t, ONE, &p, 0, 0, -3, 1, &end, &services);
    CHECK(end && p.dnfrm == 1);
    mm_actor_route_advance(&t, &t, 0, &p, 0, 1, -3, 1, &end, &services);
    CHECK(!end && p.dnfrm == 2);
    mm_actor_route_position(&t, &p, &out, &services);
    CHECK(out.dyr == 4 * ONE);
    t.count = 3;
    t.events[0].aet = MM_ACTOR_SOUND;
    t.events[0].rtel.dwrOffset = ONE;
    t.events[1].aet = MM_ACTOR_STEP;
    t.events[1].rtel.dwrOffset = 3 * ONE;
    t.events[2].aet = MM_ACTOR_ACTION;
    t.events[2].rtel.irpt = 1;
    t.events[2].rtel.dwrOffset = ONE;
    p.irpt = p.dwrOffset = p.dnfrm = 0;
    mm_actor_route_advance(&t, &t, 6 * ONE, &p, 0, 2, 1, 1, &end, &services);
    CHECK(!p.irpt && p.dwrOffset == 3 * ONE);
    mm_actor_route_advance(&t, &t, 3 * ONE, &p, 2, 3, 1, 1, &end, &services);
    CHECK(p.irpt == 1 && p.dwrOffset == ONE);
    p.irpt = p.dwrOffset = p.dnfrm = 0;
    mm_actor_route_advance(&t, &t, 100 * ONE, &p, 0, 2, 1, 0, NULL, &services);
    CHECK(p.irpt == 2 && !p.dwrOffset);
    p.irpt = 3;
    p.dwrOffset = ONE;
    mm_actor_route_position(&t, &p, &out, &services);
    CHECK(out.dxr == 100 * ONE);
    p.irpt = 0;
    p.dwrOffset = 2 * ONE;
    p.dnfrm = 0;
    mm_actor_route_advance(&t, &t, -ONE, &p, 3, 2, 1, 0, NULL, &services);
    CHECK(p.dwrOffset == ONE);
}
static void states(void)
{
    Tables t;
    MMActorState s, old;
    MMActorPoint delta = {ONE, -2 * ONE, 3 * ONE};
    MMActorMatrix matrix;
    MMActorStretch stretch = {2 * ONE, 3 * ONE, 4 * ONE};
    MMI32 value = 2 * ONE, type;
    int reads;
    init(&t);
    mm_actor_state_init(&s);
    CHECK(s.frame == 2147483647L && s.actionEvent == -1 && s.addEvent == -1 && !s.onStage && !s.frozen);
    CHECK(s.transform.bmat34Fwd.m[0][0] == ONE && !s.transform.bmat34Fwd.m[3][0] && s.transform.rScaleStep == ONE);
    s.onStage = 1;
    s.frame = 9;
    s.celCount = 6;
    s.actionFlags = 3;
    s.frozen = 1;
    s.action = s.cel = s.nextEvent = 4;
    s.position.dxr = 123;
    mm_actor_state_rewind(&s, NULL, &services);
    CHECK(s.onStage && s.frame == 9 && s.celCount == 6 && s.actionFlags == 3 && !s.frozen && !s.action && !s.cel &&
          !s.nextEvent);
    CHECK(s.position.dxr == 123 && s.location.dnfrm == -1);
    t.points[0].xyz.dxr = ONE;
    mm_actor_state_rewind(&s, &t, &services);
    CHECK(s.position.dxr == ONE);
    matrix = s.transform.bmat34Fwd;
    matrix.m[0][1] = ONE / 4;
    CHECK(apply(&t, &s, MM_ACTOR_ROTATE_HERE, &matrix, 48) == MM_ACTOR_APPLIED && s.useCurrentRotation);
    CHECK(apply(&t, &s, MM_ACTOR_ROTATE_FORWARD, &matrix, 48) == MM_ACTOR_APPLIED && !s.useCurrentRotation);
    CHECK(apply(&t, &s, MM_ACTOR_STRETCH, &stretch, 12) == MM_ACTOR_APPLIED &&
          s.transform.aevpull.rScaleY == 3 * ONE);
    CHECK(apply(&t, &s, MM_ACTOR_SIZE, &value, 4) == MM_ACTOR_APPLIED && s.transform.rScaleStep == 2 * ONE);
    CHECK(apply(&t, &s, MM_ACTOR_STEP, &value, 4) == MM_ACTOR_APPLIED && s.step == 2 * ONE &&
          s.location.dnfrm == -1);
    value = 0;
    t.events[0].rtel.dwrOffset = 2 * ONE;
    t.events[0].rtel.dnfrm = 3;
    CHECK(apply(&t, &s, MM_ACTOR_STEP, &value, 4) == MM_ACTOR_APPLIED && s.location.dnfrm == 3 &&
          s.position.dxr == 5 * ONE / 2);
    value = -7;
    CHECK(apply(&t, &s, MM_ACTOR_FREEZE, &value, 4) == MM_ACTOR_APPLIED && s.frozen);
    value = 0;
    CHECK(apply(&t, &s, MM_ACTOR_FREEZE, &value, 4) == MM_ACTOR_APPLIED && !s.frozen);
    CHECK(apply(&t, &s, MM_ACTOR_TWEAK, &delta, 12) == MM_ACTOR_APPLIED && s.position.dxr == ONE);
    t.events[0].rtel.dnfrm = 4;
    reads = t.payload_reads;
    CHECK(apply(&t, &s, MM_ACTOR_TWEAK, &delta, 12) == MM_ACTOR_APPLIED && t.payload_reads == reads);
    CHECK(apply(&t, &s, MM_ACTOR_MOVE, &delta, 12) == MM_ACTOR_APPLIED);
    CHECK(apply(&t, &s, MM_ACTOR_MOVE, &delta, 12) == MM_ACTOR_APPLIED);
    CHECK(s.subrouteTranslation.dxr == 2 * ONE && s.routeTranslation.dyr == 16 * ONE &&
          s.routeTranslation.dzr == 36 * ONE);
    old = s;
    for (type = 0; type < 13; ++type)
    {
        if (type == MM_ACTOR_ADD || type == MM_ACTOR_ACTION || type == MM_ACTOR_REMOVE ||
            type == MM_ACTOR_COSTUME || type == MM_ACTOR_SOUND)
        {
            t.events[0].aet = type;
            t.sizes[0] = -99;
            reads = t.payload_reads;
            CHECK(mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == MM_ACTOR_NEEDS_RUNTIME &&
                  reads == t.payload_reads);
        }
        else
        {
            t.events[0].aet = type;
            t.sizes[0] = -1;
            CHECK(mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == MM_ACTOR_INVALID);
        }
        CHECK(!memcmp(&s, &old, sizeof(s)));
    }
    CHECK(mm_actor_state_apply(&s, &t, NULL, 0, &delta, &services) == MM_ACTOR_INVALID);
    CHECK(mm_actor_state_apply(&s, &t, &t, -1, &delta, &services) == MM_ACTOR_INVALID);
    CHECK(mm_actor_state_apply(&s, &t, &t, 1, &delta, &services) == MM_ACTOR_INVALID);
    t.event_width = 19;
    CHECK(mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == MM_ACTOR_INVALID);
    t.event_width = 20;
    for (type = 0; type < 5; ++type)
    {
        t.events[0].aet = MM_ACTOR_STEP;
        t.sizes[0] = 4;
        memset(t.bytes[0], 0, 4);
        t.events[0].rtel.irpt = type == 0 ? -1 : type == 1 ? 4 : 0;
        t.events[0].rtel.dwrOffset = type == 2 ? -1 : type == 3 ? 5 * ONE : 0;
        if (type == 4)
            t.route_width = 15;
        CHECK(mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == MM_ACTOR_INVALID);
        CHECK(!memcmp(&s, &old, sizeof(s)));
    }
    CHECK(mm_actor_state_apply(&s, NULL, &t, 0, &delta, &services) == MM_ACTOR_INVALID);
    t.events[0].aet = 99;
    CHECK(mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == MM_ACTOR_INVALID);
    mm_actor_state_init_transform(&s);
    CHECK(s.transform.rScaleStep == ONE && s.transform.aevpull.rScaleY == ONE && s.frame == old.frame);
}
int main(void)
{
    arithmetic();
    routes();
    states();
    puts("actor motion passed");
    return 0;
}

#include "three_mm/actor_motion.h"
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
    ThreeMMActorRoutePoint points[4];
    ThreeMMActorEvent events[13];
    ThreeMMI32 sizes[13], routes, count;
    ThreeMMU32 route_width, event_width;
    unsigned char bytes[13][64];
    int point_reads, payload_reads;
} Tables;
static ThreeMMU32 route_width(void *p)
{
    return ((Tables *)p)->route_width;
}
static ThreeMMI32 route_count(void *p)
{
    return ((Tables *)p)->routes;
}
static void route_point(void *p, ThreeMMI32 i, ThreeMMActorRoutePoint *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->routes);
    ++t->point_reads;
    *out = t->points[i];
}
static ThreeMMU32 event_width(void *p)
{
    return ((Tables *)p)->event_width;
}
static ThreeMMI32 event_count(void *p)
{
    return ((Tables *)p)->count;
}
static void event(void *p, ThreeMMI32 i, ThreeMMActorEvent *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->count);
    *out = t->events[i];
}
static ThreeMMI32 event_size(void *p, ThreeMMI32 i)
{
    return ((Tables *)p)->sizes[i];
}
static void payload(void *p, ThreeMMI32 i, void *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->count && t->sizes[i] >= 0 && t->sizes[i] <= 64);
    ++t->payload_reads;
    memcpy(out, t->bytes[i], (size_t)t->sizes[i]);
}
static ThreeMMActorMotionServices services = {route_width, route_count, route_point, event_width,
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
static int apply(Tables *t, ThreeMMActorState *s, ThreeMMI32 type, const void *p, ThreeMMI32 size)
{
    ThreeMMActorPoint origin = {10 * ONE, 20 * ONE, 30 * ONE};
    t->count = 1;
    t->events[0].aet = type;
    t->sizes[0] = size;
    if (size > 0)
        memcpy(t->bytes[0], p, (size_t)size);
    return three_mm_actor_state_apply(s, t, t, 0, &origin, &services);
}
static void arithmetic(void)
{
    CHECK(three_mm_fixed_add(2147483647L, 1) == (-2147483647L - 1));
    CHECK(three_mm_fixed_sub((-2147483647L - 1), 1) == 2147483647L);
    CHECK(three_mm_fixed_mul(-1, 1) == -1);
    CHECK(three_mm_fixed_mul(1, 1) == 0);
    CHECK(three_mm_fixed_mul(-ONE / 2, ONE / 2) == -ONE / 4);
    CHECK(three_mm_fixed_mul((-2147483647L - 1), -ONE) == (-2147483647L - 1));
    CHECK(three_mm_fixed_div(ONE, 2 * ONE) == ONE / 2);
    CHECK(three_mm_fixed_div(-ONE, 2 * ONE) == -ONE / 2);
    CHECK(three_mm_fixed_div(1, 0) == (-2147483647L - 1));
    CHECK(three_mm_fixed_div(-1, 0) == 2147483647L);
    CHECK(three_mm_fixed_div(ONE, 1) == 2147483647L);
    CHECK(three_mm_fixed_div(ONE, -1) == (-2147483647L - 1));
    CHECK(three_mm_fixed_div(3, 3) == 65535L); /* Reciprocal truncation is intentional. */
    CHECK(three_mm_fixed_div(ONE, ONE / 2) == 2 * ONE);
    CHECK(three_mm_fixed_div(2147483647L, 3) == 2147483647L);
}
static void routes(void)
{
    Tables t;
    ThreeMMActorLocation p = {99, 99, 99};
    ThreeMMActorPoint out, a = {2 * ONE, 4 * ONE, 6 * ONE}, b = {6 * ONE, 8 * ONE, 10 * ONE};
    int end = 1;
    init(&t);
    three_mm_actor_interpolate(&a, &b, ONE / 2, &a);
    CHECK(a.dxr == 4 * ONE && a.dzr == 8 * ONE);
    three_mm_actor_interpolate(&a, &b, ONE / 2, &b);
    CHECK(b.dxr == 5 * ONE && b.dzr == 9 * ONE);
    three_mm_actor_route_advance(&t, &t, 2 * ONE, &p, 0, -3, -3, 1, &end, &services);
    CHECK(!p.irpt && !p.dwrOffset && !p.dnfrm && !end && !t.point_reads);
    three_mm_actor_route_advance(&t, &t, 6 * ONE, &p, 0, -2, -3, 1, &end, &services);
    three_mm_actor_route_position(&t, &p, &out, &services);
    CHECK(p.irpt == 1 && out.dxr == 4 * ONE && out.dyr == 2 * ONE && !end);
    three_mm_actor_route_advance(&t, &t, 2 * ONE, &p, 0, -1, -3, 1, &end, &services);
    CHECK(p.irpt == 2 && !p.dwrOffset && !end);
    three_mm_actor_route_advance(&t, &t, ONE, &p, 0, 0, -3, 1, &end, &services);
    CHECK(end && p.dnfrm == 1);
    three_mm_actor_route_advance(&t, &t, 0, &p, 0, 1, -3, 1, &end, &services);
    CHECK(!end && p.dnfrm == 2);
    three_mm_actor_route_position(&t, &p, &out, &services);
    CHECK(out.dyr == 4 * ONE);
    t.count = 3;
    t.events[0].aet = THREE_MM_ACTOR_SOUND;
    t.events[0].rtel.dwrOffset = ONE;
    t.events[1].aet = THREE_MM_ACTOR_STEP;
    t.events[1].rtel.dwrOffset = 3 * ONE;
    t.events[2].aet = THREE_MM_ACTOR_ACTION;
    t.events[2].rtel.irpt = 1;
    t.events[2].rtel.dwrOffset = ONE;
    p.irpt = p.dwrOffset = p.dnfrm = 0;
    three_mm_actor_route_advance(&t, &t, 6 * ONE, &p, 0, 2, 1, 1, &end, &services);
    CHECK(!p.irpt && p.dwrOffset == 3 * ONE);
    three_mm_actor_route_advance(&t, &t, 3 * ONE, &p, 2, 3, 1, 1, &end, &services);
    CHECK(p.irpt == 1 && p.dwrOffset == ONE);
    p.irpt = p.dwrOffset = p.dnfrm = 0;
    three_mm_actor_route_advance(&t, &t, 100 * ONE, &p, 0, 2, 1, 0, NULL, &services);
    CHECK(p.irpt == 2 && !p.dwrOffset);
    p.irpt = 3;
    p.dwrOffset = ONE;
    three_mm_actor_route_position(&t, &p, &out, &services);
    CHECK(out.dxr == 100 * ONE);
    p.irpt = 0;
    p.dwrOffset = 2 * ONE;
    p.dnfrm = 0;
    three_mm_actor_route_advance(&t, &t, -ONE, &p, 3, 2, 1, 0, NULL, &services);
    CHECK(p.dwrOffset == ONE);
}
static void states(void)
{
    Tables t;
    ThreeMMActorState s, old;
    ThreeMMActorPoint delta = {ONE, -2 * ONE, 3 * ONE};
    ThreeMMActorMatrix matrix;
    ThreeMMActorStretch stretch = {2 * ONE, 3 * ONE, 4 * ONE};
    ThreeMMI32 value = 2 * ONE, type;
    int reads;
    init(&t);
    three_mm_actor_state_init(&s);
    CHECK(s.frame == 2147483647L && s.actionEvent == -1 && s.addEvent == -1 && !s.onStage && !s.frozen);
    CHECK(s.transform.bmat34Fwd.m[0][0] == ONE && !s.transform.bmat34Fwd.m[3][0] && s.transform.rScaleStep == ONE);
    s.onStage = 1;
    s.frame = 9;
    s.celCount = 6;
    s.actionFlags = 3;
    s.frozen = 1;
    s.action = s.cel = s.nextEvent = 4;
    s.position.dxr = 123;
    three_mm_actor_state_rewind(&s, NULL, &services);
    CHECK(s.onStage && s.frame == 9 && s.celCount == 6 && s.actionFlags == 3 && !s.frozen && !s.action && !s.cel &&
          !s.nextEvent);
    CHECK(s.position.dxr == 123 && s.location.dnfrm == -1);
    t.points[0].xyz.dxr = ONE;
    three_mm_actor_state_rewind(&s, &t, &services);
    CHECK(s.position.dxr == ONE);
    matrix = s.transform.bmat34Fwd;
    matrix.m[0][1] = ONE / 4;
    CHECK(apply(&t, &s, THREE_MM_ACTOR_ROTATE_HERE, &matrix, 48) == THREE_MM_ACTOR_APPLIED && s.useCurrentRotation);
    CHECK(apply(&t, &s, THREE_MM_ACTOR_ROTATE_FORWARD, &matrix, 48) == THREE_MM_ACTOR_APPLIED && !s.useCurrentRotation);
    CHECK(apply(&t, &s, THREE_MM_ACTOR_STRETCH, &stretch, 12) == THREE_MM_ACTOR_APPLIED &&
          s.transform.aevpull.rScaleY == 3 * ONE);
    CHECK(apply(&t, &s, THREE_MM_ACTOR_SIZE, &value, 4) == THREE_MM_ACTOR_APPLIED && s.transform.rScaleStep == 2 * ONE);
    CHECK(apply(&t, &s, THREE_MM_ACTOR_STEP, &value, 4) == THREE_MM_ACTOR_APPLIED && s.step == 2 * ONE &&
          s.location.dnfrm == -1);
    value = 0;
    t.events[0].rtel.dwrOffset = 2 * ONE;
    t.events[0].rtel.dnfrm = 3;
    CHECK(apply(&t, &s, THREE_MM_ACTOR_STEP, &value, 4) == THREE_MM_ACTOR_APPLIED && s.location.dnfrm == 3 &&
          s.position.dxr == 5 * ONE / 2);
    value = -7;
    CHECK(apply(&t, &s, THREE_MM_ACTOR_FREEZE, &value, 4) == THREE_MM_ACTOR_APPLIED && s.frozen);
    value = 0;
    CHECK(apply(&t, &s, THREE_MM_ACTOR_FREEZE, &value, 4) == THREE_MM_ACTOR_APPLIED && !s.frozen);
    CHECK(apply(&t, &s, THREE_MM_ACTOR_TWEAK, &delta, 12) == THREE_MM_ACTOR_APPLIED && s.position.dxr == ONE);
    t.events[0].rtel.dnfrm = 4;
    reads = t.payload_reads;
    CHECK(apply(&t, &s, THREE_MM_ACTOR_TWEAK, &delta, 12) == THREE_MM_ACTOR_APPLIED && t.payload_reads == reads);
    CHECK(apply(&t, &s, THREE_MM_ACTOR_MOVE, &delta, 12) == THREE_MM_ACTOR_APPLIED);
    CHECK(apply(&t, &s, THREE_MM_ACTOR_MOVE, &delta, 12) == THREE_MM_ACTOR_APPLIED);
    CHECK(s.subrouteTranslation.dxr == 2 * ONE && s.routeTranslation.dyr == 16 * ONE &&
          s.routeTranslation.dzr == 36 * ONE);
    old = s;
    for (type = 0; type < 13; ++type)
    {
        if (type == THREE_MM_ACTOR_ADD || type == THREE_MM_ACTOR_ACTION || type == THREE_MM_ACTOR_REMOVE ||
            type == THREE_MM_ACTOR_COSTUME || type == THREE_MM_ACTOR_SOUND)
        {
            t.events[0].aet = type;
            t.sizes[0] = -99;
            reads = t.payload_reads;
            CHECK(three_mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == THREE_MM_ACTOR_NEEDS_RUNTIME &&
                  reads == t.payload_reads);
        }
        else
        {
            t.events[0].aet = type;
            t.sizes[0] = -1;
            CHECK(three_mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == THREE_MM_ACTOR_INVALID);
        }
        CHECK(!memcmp(&s, &old, sizeof(s)));
    }
    CHECK(three_mm_actor_state_apply(&s, &t, NULL, 0, &delta, &services) == THREE_MM_ACTOR_INVALID);
    CHECK(three_mm_actor_state_apply(&s, &t, &t, -1, &delta, &services) == THREE_MM_ACTOR_INVALID);
    CHECK(three_mm_actor_state_apply(&s, &t, &t, 1, &delta, &services) == THREE_MM_ACTOR_INVALID);
    t.event_width = 19;
    CHECK(three_mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == THREE_MM_ACTOR_INVALID);
    t.event_width = 20;
    for (type = 0; type < 5; ++type)
    {
        t.events[0].aet = THREE_MM_ACTOR_STEP;
        t.sizes[0] = 4;
        memset(t.bytes[0], 0, 4);
        t.events[0].rtel.irpt = type == 0 ? -1 : type == 1 ? 4 : 0;
        t.events[0].rtel.dwrOffset = type == 2 ? -1 : type == 3 ? 5 * ONE : 0;
        if (type == 4)
            t.route_width = 15;
        CHECK(three_mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == THREE_MM_ACTOR_INVALID);
        CHECK(!memcmp(&s, &old, sizeof(s)));
    }
    CHECK(three_mm_actor_state_apply(&s, NULL, &t, 0, &delta, &services) == THREE_MM_ACTOR_INVALID);
    t.events[0].aet = 99;
    CHECK(three_mm_actor_state_apply(&s, &t, &t, 0, &delta, &services) == THREE_MM_ACTOR_INVALID);
    three_mm_actor_state_init_transform(&s);
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

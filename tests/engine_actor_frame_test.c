#include "three_mm/actor_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ONE 65536L
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "actor frame failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Tables
{
    ThreeMMActorRoutePoint route[4];
    ThreeMMActorEvent events[8];
    ThreeMMActorSound sounds[8];
    ThreeMMI32 routes, count;
    int payloads;
} Tables;
static ThreeMMU32 route_width(void *p)
{
    (void)p;
    return 16;
}
static ThreeMMI32 route_count(void *p)
{
    return ((Tables *)p)->routes;
}
static void point(void *p, ThreeMMI32 i, ThreeMMActorRoutePoint *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->routes);
    *out = t->route[i];
}
static ThreeMMU32 event_width(void *p)
{
    (void)p;
    return 20;
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
    (void)p;
    (void)i;
    return sizeof(ThreeMMActorSound);
}
static void payload(void *p, ThreeMMI32 i, void *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->count && t->events[i].aet == THREE_MM_ACTOR_SOUND);
    ++t->payloads;
    memcpy(out, &t->sounds[i], sizeof(t->sounds[i]));
}
static ThreeMMActorMotionServices services = {route_width, route_count, point,      event_width,
                                              event_count, event,       event_size, payload};
static void init(Tables *t, ThreeMMActorState *s, ThreeMMActorFrameInputs *in)
{
    memset(t, 0, sizeof(*t));
    t->routes = 2;
    t->route[0].dwr = 10 * ONE;
    t->route[1].xyz.dxr = 10 * ONE;
    three_mm_actor_state_init(s);
    s->frame = 1;
    s->celCount = 4;
    memset(in, 0, sizeof(*in));
    in->firstFrame = 1;
    in->stepSucceeded = 1;
    in->displayFrame = 1;
}
static ThreeMMActorFrameCommand next(ThreeMMActorFrame *q, ThreeMMActorState *s, Tables *t, int kind, int index)
{
    ThreeMMActorFrameCommand out;
    CHECK(three_mm_actor_frame_next(q, s, t, &out, &services));
    CHECK(out.kind == kind && out.eventIndex == index);
    return out;
}
static void complete(ThreeMMActorFrame *q, ThreeMMActorState *s, Tables *t, int success)
{
    CHECK(three_mm_actor_frame_complete(q, s, t, success, &services));
}
static void entrances(void)
{
    Tables t;
    ThreeMMActorState s, old_state;
    ThreeMMActorFrameInputs in;
    ThreeMMActorFrame q, old;
    ThreeMMActorFrameCommand command = {99, 99};
    int i;
    init(&t, &s, &in);
    t.routes = 3;
    t.count = 3;
    s.frame = 3;
    for (i = 0; i < 3; ++i)
    {
        t.route[i].dwr = 0;
        t.route[i].xyz.dxr = i * 10 * ONE;
        t.events[i].aet = THREE_MM_ACTOR_ADD;
        t.events[i].nfrm = i == 2 ? 10 : i + 1;
        t.events[i].rtel.irpt = i;
    }
    three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    old = q;
    old_state = s;
    CHECK(!three_mm_actor_frame_complete(&q, &s, &t, 1, &services));
    CHECK(!memcmp(&q, &old, sizeof(q)) && !memcmp(&s, &old_state, sizeof(s)));
    next(&q, &s, &t, THREE_MM_ACTOR_CHECK_ENTRANCE, 0);
    old = q;
    old_state = s;
    CHECK(!three_mm_actor_frame_next(&q, &s, &t, &command, &services));
    CHECK(command.kind == 99 && command.eventIndex == 99 && !memcmp(&q, &old, sizeof(q)) &&
          !memcmp(&s, &old_state, sizeof(s)));
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 0);
    next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 0);
    s.onStage = 1;
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, THREE_MM_ACTOR_CHECK_ENTRANCE, 1);
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 1);
    s.location = t.events[1].rtel;
    s.position = t.route[1].xyz;
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, THREE_MM_ACTOR_CHECK_ENTRANCE, 2);
    complete(&q, &s, &t, 0);
    next(&q, &s, &t, THREE_MM_ACTOR_CHECK_ENTRANCE, 2);
    complete(&q, &s, &t, 0);
    next(&q, &s, &t, THREE_MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(s.nextEvent == 2 && s.cel == 1 && q.result.success && q.result.positionDirty && q.result.positionBody &&
          q.result.selectCel);
    init(&t, &s, &in);
    t.count = 1;
    t.events[0].aet = THREE_MM_ACTOR_ADD;
    three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, THREE_MM_ACTOR_CHECK_ENTRANCE, 0);
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 0);
    complete(&q, &s, &t, 0);
    next(&q, &s, &t, THREE_MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(q.result.aborted && !q.result.success && !q.result.positionBody && !q.result.selectCel && !s.nextEvent &&
          !s.cel);
    three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, THREE_MM_ACTOR_CHECK_ENTRANCE, 0);
    complete(&q, &s, &t, 0);
    next(&q, &s, &t, THREE_MM_ACTOR_CHECK_ENTRANCE, 0);
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 0);
    complete(&q, &s, &t, 0);
    CHECK(q.result.aborted && !q.result.success && !s.nextEvent);
}
static void sounds(void)
{
    Tables t;
    ThreeMMActorState s;
    ThreeMMActorFrameInputs in;
    ThreeMMActorFrame q;
    int display, suppress;
    for (display = 0; display < 2; ++display)
        for (suppress = 0; suppress < 2; ++suppress)
        {
            init(&t, &s, &in);
            s.onStage = 1;
            s.frozen = 1;
            in.displayFrame = display;
            in.suppressSounds = suppress;
            t.count = 2;
            t.events[0].aet = t.events[1].aet = THREE_MM_ACTOR_SOUND;
            t.sounds[0].cel = -1;
            t.sounds[1].cel = 2;
            three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
            if (display && !suppress)
            {
                next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 0);
                complete(&q, &s, &t, 1);
            }
            next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 1);
            complete(&q, &s, &t, 1);
            next(&q, &s, &t, THREE_MM_ACTOR_FRAME_FINISHED, -1);
            CHECK(s.nextEvent == 2 && !s.cel && q.result.success && !q.result.aborted);
            CHECK(q.result.soundInFrame == display && q.result.considerMotionSounds == (display && !suppress));
            CHECK(q.result.selectCel == display && !q.result.positionBody && !q.result.updatePose);
            CHECK(t.payloads == ((!display || suppress) ? 2 : 0));
        }
}
static void failures_and_latches(void)
{
    Tables t;
    ThreeMMActorState s;
    ThreeMMActorFrameInputs in;
    ThreeMMActorFrame q;
    int frozen;
    init(&t, &s, &in);
    s.onStage = 1;
    t.count = 2;
    t.events[0].aet = THREE_MM_ACTOR_ACTION;
    t.events[1].aet = THREE_MM_ACTOR_MOVE;
    three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 0);
    complete(&q, &s, &t, 0);
    CHECK(!q.result.success && !q.result.aborted && !s.nextEvent);
    /* Scan advances after an ordinary failure, but the command targets the
     * unchanged cursor. This intentionally preserves the native retry quirk. */
    next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 0);
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, THREE_MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(s.nextEvent == 1 && !s.cel && q.result.positionDirty && q.result.positionBody && !q.result.success);
    for (frozen = 0; frozen < 2; ++frozen)
    {
        init(&t, &s, &in);
        s.frozen = frozen;
        t.count = 1;
        t.events[0].aet = THREE_MM_ACTOR_FREEZE;
        three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
        next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 0);
        s.frozen = !frozen;
        complete(&q, &s, &t, 1);
        next(&q, &s, &t, THREE_MM_ACTOR_FRAME_FINISHED, -1);
        CHECK(s.cel == (frozen ? 0 : 1));
    }
}
static void modes_and_end(void)
{
    Tables t;
    ThreeMMActorState s;
    ThreeMMActorFrameInputs in;
    ThreeMMActorFrame q;
    int mode;
    for (mode = 0; mode < 3; ++mode)
    {
        init(&t, &s, &in);
        s.frame = 2;
        s.onStage = 1;
        in.step = 6 * ONE;
        t.route[0].dwr = 8 * ONE;
        t.route[1].xyz.dxr = 8 * ONE;
        in.recording = mode == 1;
        in.rejoining = mode == 2;
        t.count = 1;
        t.events[0].aet = THREE_MM_ACTOR_ACTION;
        t.events[0].rtel.dwrOffset = 2 * ONE;
        three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
        CHECK(s.position.dxr == (mode ? 6 : 2) * ONE && !s.useCurrentRotation);
        if (mode == 1)
        {
            next(&q, &s, &t, THREE_MM_ACTOR_FRAME_FINISHED, -1);
            CHECK(!s.nextEvent && s.cel == 1 && q.result.positionBody);
        }
        else
            next(&q, &s, &t, THREE_MM_ACTOR_APPLY_EVENT, 0);
    }
    init(&t, &s, &in);
    s.frame = 2;
    s.onStage = 1;
    in.step = ONE;
    in.displayFrame = 0;
    in.stepSucceeded = 0;
    t.route[1].xyz.dxr = 0;
    t.route[0].dwr = 8 * ONE;
    t.route[1].xyz.dyr = 8 * ONE;
    three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, THREE_MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(s.position.dyr == ONE && s.useCurrentRotation && q.result.positionDirty && q.result.updatePose &&
          !q.result.positionBody && !q.result.success);
    init(&t, &s, &in);
    t.routes = 1;
    t.route[0].dwr = 0;
    s.frame = 2;
    in.step = ONE;
    three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, THREE_MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(!s.cel && s.location.dnfrm == 1);
    t.count = 1;
    t.events[0].aet = THREE_MM_ACTOR_TWEAK;
    t.events[0].rtel.dnfrm = 5;
    CHECK(!three_mm_actor_subroute_events_done(&t, 0, &s.location, &services));
    three_mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, THREE_MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(s.cel == 1);
    t.events[0].aet = THREE_MM_ACTOR_ADD;
    CHECK(three_mm_actor_subroute_events_done(&t, 0, &s.location, &services));
    CHECK(three_mm_actor_subroute_events_done(&t, 1, &s.location, &services));
}
int main(void)
{
    entrances();
    sounds();
    failures_and_latches();
    modes_and_end();
    puts("actor frame passed");
    return 0;
}

#include "three_mm/actor_timeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ONE 65536L
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "actor timeline failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Tables
{
    ThreeMMActorRoutePoint route[4];
    ThreeMMActorEvent events[16];
    unsigned char bytes[16][64];
    ThreeMMI32 sizes[16], routes, count, cels[64];
    int calls, fail, puts, publishes, diagnostics, fault, observe;
    ThreeMMActorState view;
    ThreeMMI32 viewedFrame;
    int observeFrame;
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
    return ((Tables *)p)->sizes[i];
}
static void payload(void *p, ThreeMMI32 i, void *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->count);
    memcpy(out, t->bytes[i], (size_t)t->sizes[i]);
}
static void put_event(void *p, ThreeMMI32 i, const ThreeMMActorEvent *e)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->count);
    ++t->puts;
    t->events[i] = *e;
}
static int resolve(void *p, ThreeMMI32 action, ThreeMMI32 cel, ThreeMMI32 *step)
{
    Tables *t = (Tables *)p;
    CHECK(action == 0 && t->calls < 64 && *step == 0);
    t->cels[t->calls++] = cel;
    if (t->observeFrame)
        CHECK(t->viewedFrame == t->calls - 2);
    if (t->observe)
        CHECK(t->view.cel == cel);
    if (t->calls == t->fail)
    {
        *step = 123;
        return 0;
    }
    *step = (((cel % 4 + 4) % 4) + 1) * ONE;
    return 1;
}
static void publish(void *p, const ThreeMMActorState *s)
{
    Tables *t = (Tables *)p;
    ++t->publishes;
    t->view = *s;
}
static void publish_frame(void *p, ThreeMMI32 frame)
{
    ((Tables *)p)->viewedFrame = frame;
}
static void diagnostic(void *p, int fault)
{
    Tables *t = (Tables *)p;
    ++t->diagnostics;
    t->fault = fault;
}
static ThreeMMActorTimelineServices services(Tables *t)
{
    ThreeMMActorTimelineServices s;
    s.motion.route_width = route_width;
    s.motion.route_count = route_count;
    s.motion.route_point = point;
    s.motion.event_width = event_width;
    s.motion.event_count = event_count;
    s.motion.event = event;
    s.motion.event_size = event_size;
    s.motion.payload = payload;
    s.context = t;
    s.resolve = resolve;
    s.put_event = put_event;
    s.diagnostic = diagnostic;
    s.publish = publish;
    s.publish_frame = publish_frame;
    return s;
}
static void init(Tables *t)
{
    memset(t, 0, sizeof(*t));
    t->routes = 2;
    t->route[0].dwr = 6 * ONE;
    t->route[1].xyz.dxr = 6 * ONE;
}
static void row(Tables *t, ThreeMMI32 i, ThreeMMI32 type, ThreeMMI32 frame, ThreeMMI32 node, ThreeMMI32 offset,
                ThreeMMI32 dwell, const void *p, ThreeMMI32 size)
{
    t->events[i].aet = type;
    t->events[i].nfrm = frame;
    t->events[i].rtel.irpt = node;
    t->events[i].rtel.dwrOffset = offset;
    t->events[i].rtel.dnfrm = dwell;
    t->sizes[i] = size;
    if (size)
        memcpy(t->bytes[i], p, (size_t)size);
    if (i >= t->count)
        t->count = i + 1;
}
static void metadata(Tables *t, int freeze)
{
    ThreeMMI32 action[2] = {0, 1}, step = -ONE, scale = 2 * ONE, frozen = 1;
    init(t);
    row(t, 0, THREE_MM_ACTOR_ADD, -2, 0, 0, 0, NULL, 0);
    row(t, 1, THREE_MM_ACTOR_ACTION, -777, 0, 0, 0, action, 8);
    row(t, 2, THREE_MM_ACTOR_STEP, -777, 0, 0, 0, &step, 4);
    row(t, 3, THREE_MM_ACTOR_SIZE, -777, 0, 0, 0, &scale, 4);
    if (freeze)
        row(t, 4, THREE_MM_ACTOR_FREEZE, -777, 0, 4 * ONE, 0, &frozen, 4);
}
static void steps_and_lifetime(void)
{
    Tables t;
    ThreeMMActorTimelineServices s;
    ThreeMMActorState state;
    ThreeMMI32 last, step = 999, unchanged = -99;
    ThreeMMActorLocation location = {0, 0, 0};
    init(&t);
    s = services(&t);
    three_mm_actor_state_init(&state);
    state.step = -ONE;
    state.cel = 1;
    state.transform.rScaleStep = 2 * ONE;
    CHECK(three_mm_actor_playback_step(&state, &step, &s) && step == 4 * ONE && t.calls == 1);
    t.fail = 2;
    CHECK(!three_mm_actor_playback_step(&state, &step, &s) && step == 123);
    s.resolve = NULL;
    step = 999;
    CHECK(!three_mm_actor_step(0, 0, &step, &s) && !step);
    state.step = 3 * ONE;
    CHECK(three_mm_actor_playback_step(&state, &step, &s) && step == 6 * ONE && t.calls == 2);
    CHECK(three_mm_actor_lifetime(&t, &t, -5, 0, &last, -1, &s) && last == -5);
    metadata(&t, 0);
    s = services(&t);
    t.observeFrame = 1;
    CHECK(three_mm_actor_lifetime(&t, &t, -2, 0, &last, 100, &s) && last == 0);
    CHECK(t.calls == 3 && t.cels[0] == 1 && t.cels[1] == 2 && t.cels[2] == 3);
    CHECK(t.events[1].nfrm == -2 && t.events[2].nfrm == -2 && t.events[3].nfrm == -2 && t.puts == 3);
    metadata(&t, 1);
    s = services(&t);
    CHECK(three_mm_actor_lifetime(&t, &t, -2, 0, &last, 100, &s) && last == 0);
    CHECK(t.calls == 3 && t.cels[2] == 2 && t.events[4].nfrm == -1);
    metadata(&t, 1);
    s = services(&t);
    t.fail = 2;
    CHECK(!three_mm_actor_lifetime(&t, &t, -2, 0, &last, 100, &s) && last == 0);
    CHECK(t.events[1].nfrm == -2 && t.events[4].nfrm == -1);
    CHECK(!three_mm_actor_stalled(&t, 0, &location, &unchanged, &s) && unchanged == -99);
    CHECK(three_mm_actor_stalled(&t, 3, &location, &unchanged, &s) && unchanged == 4);
    metadata(&t, 0);
    s = services(&t);
    CHECK(!three_mm_actor_lifetime(&t, &t, -2, 0, &last, 0, &s) && last == -1 && !t.calls && t.puts == 3);
    CHECK(!three_mm_actor_lifetime(&t, &t, (-2147483647L - 1), 0, &last, -1, &s) && last == (-2147483647L - 1));
    CHECK(!three_mm_actor_lifetime(&t, &t, 2147483647L, 0, &last, 2147483647L, &s) && last == 2147483647L);
    /* Stalling uses separate node/offset comparisons, not lexicographic order. */
    location.irpt = 1;
    location.dwrOffset = 0;
    CHECK(three_mm_actor_stalled(&t, 4, &location, &unchanged, &s) == 0);
    row(&t, 4, THREE_MM_ACTOR_TWEAK, 0, 0, ONE, 0, NULL, 0);
    CHECK(three_mm_actor_stalled(&t, 4, &location, &unchanged, &s) && unchanged == 4);
}
static void straight(Tables *t, ThreeMMActorState *state)
{
    int i;
    init(t);
    t->routes = 4;
    for (i = 0; i < 4; ++i)
    {
        t->route[i].xyz.dxr = i * 4 * ONE;
        t->route[i].dwr = i == 3 ? 0 : 4 * ONE;
    }
    row(t, 0, THREE_MM_ACTOR_ADD, 1, 0, 0, 0, NULL, 0);
    three_mm_actor_state_init(state);
    state->frame = 10;
    state->cel = 3;
    state->step = -ONE;
    state->transform.rScaleStep = 2 * ONE;
    state->addEvent = 0;
    state->nextEvent = 1;
    state->location.irpt = 2;
    state->location.dwrOffset = 2 * ONE;
    state->position.dxr = 10 * ONE;
}
static void backwards(void)
{
    Tables t;
    ThreeMMActorTimelineServices s;
    ThreeMMActorState state, initial, after;
    ThreeMMActorLocation out = {99, 99, 99};
    ThreeMMActorQuickSeekResult quick;
    straight(&t, &state);
    s = services(&t);
    initial = state;
    t.observe = 1;
    CHECK(three_mm_actor_step_back(&state, &t, &t, &out, 0, &s));
    CHECK(out.irpt == 1 && !out.dwrOffset && t.cels[0] == 2 && t.publishes == 1 &&
          !memcmp(&state, &initial, sizeof(state)));
    t.fail = 2;
    out.irpt = 99;
    CHECK(!three_mm_actor_step_back(&state, &t, &t, &out, 0, &s) && out.irpt == 99 &&
          !memcmp(&state, &initial, sizeof(state)));
    straight(&t, &state);
    s = services(&t);
    t.fail = 2;
    CHECK(three_mm_actor_step_back(&state, &t, &t, &state.location, 1, &s));
    after = state;
    CHECK(state.frame == 9 && state.cel == 2 && state.location.irpt == 1 && !state.location.dwrOffset);
    CHECK(!three_mm_actor_step_back(&state, &t, &t, &state.location, 1, &s) && !memcmp(&state, &after, sizeof(state)));
    straight(&t, &state);
    s = services(&t);
    t.fail = 2;
    quick = three_mm_actor_quick_backup(&state, &t, &t, 20, 8, &s);
    CHECK(!quick.success && !quick.valid && !quick.applyEffects && !memcmp(&state, &after, sizeof(state)));
    straight(&t, &state);
    s = services(&t);
    state.cel = 9;
    state.step = ONE;
    state.transform.rScaleStep = ONE;
    quick = three_mm_actor_quick_backup(&state, &t, &t, 20, 8, &s);
    CHECK(quick.success && quick.valid && quick.applyEffects && state.frame == 8 && state.cel == 7 &&
          state.position.dxr == 8 * ONE && !t.calls);
    straight(&t, &state);
    s = services(&t);
    state.frame = 13;
    state.location.irpt = 3;
    state.location.dwrOffset = 0;
    state.location.dnfrm = 4;
    quick = three_mm_actor_quick_backup(&state, &t, &t, 9, 11, &s);
    CHECK(quick.success && quick.valid && !quick.applyEffects && state.frame == 11 && state.location.dnfrm == 2 &&
          !t.calls);
    straight(&t, &state);
    s = services(&t);
    state.step = 0;
    CHECK(three_mm_actor_step_back(&state, &t, &t, &state.location, 1, &s) && !t.calls && state.frame == 9 &&
          state.location.dnfrm == -1);
    straight(&t, &state);
    s = services(&t);
    state.location = t.events[0].rtel;
    CHECK(three_mm_actor_step_back(&state, &t, &t, &state.location, 1, &s) && !t.calls && state.location.dnfrm == -1);
    straight(&t, &state);
    s = services(&t);
    state.step = 2 * ONE;
    state.transform.rScaleStep = ONE;
    state.location.irpt = 0;
    state.location.dwrOffset = ONE;
    CHECK(three_mm_actor_step_back(&state, &t, &t, &state.location, 1, &s));
    CHECK(t.diagnostics == 1 && t.fault == THREE_MM_ACTOR_CORRUPT_ROUTE && !state.location.irpt &&
          !state.location.dwrOffset && state.frame == 9);
}
static void next(ThreeMMActorSeek *q, ThreeMMActorState *state, Tables *t, ThreeMMActorTimelineServices *s, int kind,
                 int display)
{
    ThreeMMActorSeekCommand out;
    CHECK(three_mm_actor_seek_next(q, state, t, t, &out, s) && out.kind == kind && out.displayFrame == display);
}
static void complete(ThreeMMActorSeek *q, ThreeMMActorState *state, int success)
{
    CHECK(three_mm_actor_seek_complete(q, state, success));
}
static void seek_protocol_and_forward(void)
{
    Tables t;
    ThreeMMActorTimelineServices s;
    ThreeMMActorState state, old_state;
    ThreeMMActorSeek q, old;
    ThreeMMActorSeekCommand out = {99, 99};
    init(&t);
    s = services(&t);
    row(&t, 0, THREE_MM_ACTOR_ADD, 1, 0, 0, 0, NULL, 0);
    three_mm_actor_state_init(&state);
    three_mm_actor_seek_init(&q, 1, 9, 3);
    old = q;
    old_state = state;
    CHECK(!three_mm_actor_seek_complete(&q, &state, 1) && !memcmp(&q, &old, sizeof(q)) &&
          !memcmp(&state, &old_state, sizeof(state)));
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_RESET_COSTUME, 0);
    CHECK(state.frame == 1 && state.location.dnfrm == -1);
    old = q;
    old_state = state;
    CHECK(!three_mm_actor_seek_next(&q, &state, &t, &t, &out, &s));
    CHECK(out.kind == 99 && out.displayFrame == 99 && !memcmp(&q, &old, sizeof(q)) &&
          !memcmp(&state, &old_state, sizeof(state)));
    complete(&q, &state, 0);
    CHECK(q.success);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FRAME, 0);
    state.nextEvent = 1;
    complete(&q, &state, 1);
    CHECK(state.frame == 2 && state.firstEventInFrame == 1);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FRAME, 0);
    complete(&q, &state, 0);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FRAME, 1);
    complete(&q, &state, 1);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(!q.success && state.frame == 3);
    three_mm_actor_seek_init(&q, 1, 9, 3);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(q.success);
    state.onStage = 1;
    state.frame = 9;
    three_mm_actor_seek_init(&q, 1, 9, -5);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_HIDE, 0);
    CHECK(state.frame == 9);
    state.onStage = 0;
    complete(&q, &state, 0);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_RESET_COSTUME, 0);
    complete(&q, &state, 0);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(q.success && state.frame == -5);
    three_mm_actor_seek_init(&q, 1, 9, -2);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(state.frame == -4); /* Inherited before-first forward behavior. */
    init(&t);
    s = services(&t);
    three_mm_actor_state_init(&state);
    three_mm_actor_seek_init(&q, 2147483647L, 2147483647L, 5);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_RESET_COSTUME, 0);
    complete(&q, &state, 0);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(q.success && state.frame == 5);
    state.frame = 1;
    three_mm_actor_seek_init(&q, 1, 9, 5);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(state.frame == 5 && q.success);
}
static void seek_quick_and_rewind(void)
{
    Tables t;
    ThreeMMActorTimelineServices s;
    ThreeMMActorState state, initial;
    ThreeMMActorSeek q;
    ThreeMMI32 step = 2 * ONE;
    int i;
    straight(&t, &state);
    s = services(&t);
    state.cel = 9;
    state.step = ONE;
    state.transform.rScaleStep = ONE;
    three_mm_actor_seek_init(&q, 1, 20, 8);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_QUICK_EFFECTS, 0);
    CHECK(state.frame == 8 && state.cel == 7 && state.position.dxr == 8 * ONE);
    complete(&q, &state, 0);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(!q.success);
    straight(&t, &state);
    s = services(&t);
    state.frame = 13;
    state.location.irpt = 3;
    state.location.dwrOffset = 0;
    state.location.dnfrm = 4;
    three_mm_actor_seek_init(&q, 1, 9, 11);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(q.success && state.frame == 11 && state.location.dnfrm == 2 && !t.calls);
    straight(&t, &state);
    s = services(&t);
    t.fail = 1;
    t.observe = 1;
    initial = state;
    three_mm_actor_seek_init(&q, 1, 20, 8);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(!q.success && !memcmp(&state, &initial, sizeof(state)) && t.calls == 1 && t.publishes == 1);
    /* A vertical transition invalidates quick effects and replays the subroute. */
    straight(&t, &state);
    s = services(&t);
    t.routes = 3;
    for (i = 0; i < 3; ++i)
        memset(&t.route[i], 0, sizeof(t.route[i]));
    t.route[0].dwr = t.route[1].dwr = 4 * ONE;
    t.route[1].xyz.dyr = 4 * ONE;
    t.route[2].xyz.dxr = t.route[2].xyz.dyr = 4 * ONE;
    row(&t, 1, THREE_MM_ACTOR_STEP, 1, 0, 0, 0, &step, 4);
    state.frame = 4;
    state.cel = 4;
    state.step = step;
    state.transform.rScaleStep = ONE;
    state.location.irpt = 1;
    state.location.dwrOffset = 2 * ONE;
    state.position.dxr = 2 * ONE;
    state.position.dyr = 4 * ONE;
    state.nextEvent = 2;
    three_mm_actor_seek_init(&q, 1, 20, 3);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_RESET_COSTUME, 0);
    CHECK(state.frame == 1 && !state.nextEvent && state.location.dnfrm == -1);
    complete(&q, &state, 0);
    for (i = 1; i <= 3; ++i)
    {
        next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FRAME, i == 3);
        CHECK(state.frame == i);
        state.nextEvent = 2;
        state.actionEvent = 0;
        complete(&q, &state, 1);
    }
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(q.success && state.frame == 3);
    /* Midpath rewind chooses the latest entrance not after the target. */
    straight(&t, &state);
    s = services(&t);
    row(&t, 1, THREE_MM_ACTOR_ADD, 8, 1, 0, 0, NULL, 0);
    row(&t, 2, THREE_MM_ACTOR_ADD, 10, 2, 0, 0, NULL, 0);
    state.frame = 13;
    state.nextEvent = 3;
    three_mm_actor_seek_init(&q, 1, 20, 9);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_RESET_COSTUME, 0);
    CHECK(state.frame == 8 && state.nextEvent == 1 && state.firstEventInFrame == 1 && state.addEvent == 1 &&
          state.location.irpt == 1 && state.location.dnfrm == -1);
    complete(&q, &state, 1);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FRAME, 0);
    state.nextEvent = 2;
    complete(&q, &state, 1);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FRAME, 1);
    complete(&q, &state, 1);
    next(&q, &state, &t, &s, THREE_MM_ACTOR_SEEK_FINISHED, 0);
    CHECK(q.success);
}
int main(void)
{
    steps_and_lifetime();
    backwards();
    seek_protocol_and_forward();
    seek_quick_and_rewind();
    puts("actor timeline and seek passed");
    return 0;
}

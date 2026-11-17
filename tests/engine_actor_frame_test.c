#include "mm/actor_frame.h"
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
    MMActorRoutePoint route[4];
    MMActorEvent events[8];
    MMActorSound sounds[8];
    MMI32 routes, count;
    int payloads;
} Tables;
static MMU32 route_width(void *p)
{
    (void)p;
    return 16;
}
static MMI32 route_count(void *p)
{
    return ((Tables *)p)->routes;
}
static void point(void *p, MMI32 i, MMActorRoutePoint *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->routes);
    *out = t->route[i];
}
static MMU32 event_width(void *p)
{
    (void)p;
    return 20;
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
    (void)p;
    (void)i;
    return sizeof(MMActorSound);
}
static void payload(void *p, MMI32 i, void *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->count && t->events[i].aet == MM_ACTOR_SOUND);
    ++t->payloads;
    memcpy(out, &t->sounds[i], sizeof(t->sounds[i]));
}
static MMActorMotionServices services = {route_width, route_count, point,      event_width,
                                              event_count, event,       event_size, payload};
static void init(Tables *t, MMActorState *s, MMActorFrameInputs *in)
{
    memset(t, 0, sizeof(*t));
    t->routes = 2;
    t->route[0].dwr = 10 * ONE;
    t->route[1].xyz.dxr = 10 * ONE;
    mm_actor_state_init(s);
    s->frame = 1;
    s->celCount = 4;
    memset(in, 0, sizeof(*in));
    in->firstFrame = 1;
    in->stepSucceeded = 1;
    in->displayFrame = 1;
}
static MMActorFrameCommand next(MMActorFrame *q, MMActorState *s, Tables *t, int kind, int index)
{
    MMActorFrameCommand out;
    CHECK(mm_actor_frame_next(q, s, t, &out, &services));
    CHECK(out.kind == kind && out.eventIndex == index);
    return out;
}
static void complete(MMActorFrame *q, MMActorState *s, Tables *t, int success)
{
    CHECK(mm_actor_frame_complete(q, s, t, success, &services));
}
static void entrances(void)
{
    Tables t;
    MMActorState s, old_state;
    MMActorFrameInputs in;
    MMActorFrame q, old;
    MMActorFrameCommand command = {99, 99};
    int i;
    init(&t, &s, &in);
    t.routes = 3;
    t.count = 3;
    s.frame = 3;
    for (i = 0; i < 3; ++i)
    {
        t.route[i].dwr = 0;
        t.route[i].xyz.dxr = i * 10 * ONE;
        t.events[i].aet = MM_ACTOR_ADD;
        t.events[i].nfrm = i == 2 ? 10 : i + 1;
        t.events[i].rtel.irpt = i;
    }
    mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    old = q;
    old_state = s;
    CHECK(!mm_actor_frame_complete(&q, &s, &t, 1, &services));
    CHECK(!memcmp(&q, &old, sizeof(q)) && !memcmp(&s, &old_state, sizeof(s)));
    next(&q, &s, &t, MM_ACTOR_CHECK_ENTRANCE, 0);
    old = q;
    old_state = s;
    CHECK(!mm_actor_frame_next(&q, &s, &t, &command, &services));
    CHECK(command.kind == 99 && command.eventIndex == 99 && !memcmp(&q, &old, sizeof(q)) &&
          !memcmp(&s, &old_state, sizeof(s)));
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 0);
    next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 0);
    s.onStage = 1;
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, MM_ACTOR_CHECK_ENTRANCE, 1);
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 1);
    s.location = t.events[1].rtel;
    s.position = t.route[1].xyz;
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, MM_ACTOR_CHECK_ENTRANCE, 2);
    complete(&q, &s, &t, 0);
    next(&q, &s, &t, MM_ACTOR_CHECK_ENTRANCE, 2);
    complete(&q, &s, &t, 0);
    next(&q, &s, &t, MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(s.nextEvent == 2 && s.cel == 1 && q.result.success && q.result.positionDirty && q.result.positionBody &&
          q.result.selectCel);
    init(&t, &s, &in);
    t.count = 1;
    t.events[0].aet = MM_ACTOR_ADD;
    mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, MM_ACTOR_CHECK_ENTRANCE, 0);
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 0);
    complete(&q, &s, &t, 0);
    next(&q, &s, &t, MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(q.result.aborted && !q.result.success && !q.result.positionBody && !q.result.selectCel && !s.nextEvent &&
          !s.cel);
    mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, MM_ACTOR_CHECK_ENTRANCE, 0);
    complete(&q, &s, &t, 0);
    next(&q, &s, &t, MM_ACTOR_CHECK_ENTRANCE, 0);
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 0);
    complete(&q, &s, &t, 0);
    CHECK(q.result.aborted && !q.result.success && !s.nextEvent);
}
static void sounds(void)
{
    Tables t;
    MMActorState s;
    MMActorFrameInputs in;
    MMActorFrame q;
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
            t.events[0].aet = t.events[1].aet = MM_ACTOR_SOUND;
            t.sounds[0].cel = -1;
            t.sounds[1].cel = 2;
            mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
            if (display && !suppress)
            {
                next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 0);
                complete(&q, &s, &t, 1);
            }
            next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 1);
            complete(&q, &s, &t, 1);
            next(&q, &s, &t, MM_ACTOR_FRAME_FINISHED, -1);
            CHECK(s.nextEvent == 2 && !s.cel && q.result.success && !q.result.aborted);
            CHECK(q.result.soundInFrame == display && q.result.considerMotionSounds == (display && !suppress));
            CHECK(q.result.selectCel == display && !q.result.positionBody && !q.result.updatePose);
            CHECK(t.payloads == ((!display || suppress) ? 2 : 0));
        }
}
static void failures_and_latches(void)
{
    Tables t;
    MMActorState s;
    MMActorFrameInputs in;
    MMActorFrame q;
    int frozen;
    init(&t, &s, &in);
    s.onStage = 1;
    t.count = 2;
    t.events[0].aet = MM_ACTOR_ACTION;
    t.events[1].aet = MM_ACTOR_MOVE;
    mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 0);
    complete(&q, &s, &t, 0);
    CHECK(!q.result.success && !q.result.aborted && !s.nextEvent);
    /* Scan advances after an ordinary failure, but the command targets the
     * unchanged cursor. This intentionally preserves the native retry quirk. */
    next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 0);
    complete(&q, &s, &t, 1);
    next(&q, &s, &t, MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(s.nextEvent == 1 && !s.cel && q.result.positionDirty && q.result.positionBody && !q.result.success);
    for (frozen = 0; frozen < 2; ++frozen)
    {
        init(&t, &s, &in);
        s.frozen = frozen;
        t.count = 1;
        t.events[0].aet = MM_ACTOR_FREEZE;
        mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
        next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 0);
        s.frozen = !frozen;
        complete(&q, &s, &t, 1);
        next(&q, &s, &t, MM_ACTOR_FRAME_FINISHED, -1);
        CHECK(s.cel == (frozen ? 0 : 1));
    }
}
static void modes_and_end(void)
{
    Tables t;
    MMActorState s;
    MMActorFrameInputs in;
    MMActorFrame q;
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
        t.events[0].aet = MM_ACTOR_ACTION;
        t.events[0].rtel.dwrOffset = 2 * ONE;
        mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
        CHECK(s.position.dxr == (mode ? 6 : 2) * ONE && !s.useCurrentRotation);
        if (mode == 1)
        {
            next(&q, &s, &t, MM_ACTOR_FRAME_FINISHED, -1);
            CHECK(!s.nextEvent && s.cel == 1 && q.result.positionBody);
        }
        else
            next(&q, &s, &t, MM_ACTOR_APPLY_EVENT, 0);
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
    mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(s.position.dyr == ONE && s.useCurrentRotation && q.result.positionDirty && q.result.updatePose &&
          !q.result.positionBody && !q.result.success);
    init(&t, &s, &in);
    t.routes = 1;
    t.route[0].dwr = 0;
    s.frame = 2;
    in.step = ONE;
    mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(!s.cel && s.location.dnfrm == 1);
    t.count = 1;
    t.events[0].aet = MM_ACTOR_TWEAK;
    t.events[0].rtel.dnfrm = 5;
    CHECK(!mm_actor_subroute_events_done(&t, 0, &s.location, &services));
    mm_actor_frame_init(&q, &s, &t, &t, &in, 0, &services);
    next(&q, &s, &t, MM_ACTOR_FRAME_FINISHED, -1);
    CHECK(s.cel == 1);
    t.events[0].aet = MM_ACTOR_ADD;
    CHECK(mm_actor_subroute_events_done(&t, 0, &s.location, &services));
    CHECK(mm_actor_subroute_events_done(&t, 1, &s.location, &services));
}
typedef struct EffectLog
{
    char calls[8];
    unsigned count;
    int motion_status, cel_status;
} EffectLog;

static int effect_event(void *context, const MMActorFrameCommand *command, MMActorState *state)
{
    EffectLog *log = (EffectLog *)context;
    CHECK(command->kind == MM_ACTOR_APPLY_EVENT && command->eventIndex == 0);
    log->calls[log->count++] = 'E';
    state->onStage = 1;
    return -2;
}

static int effect_sound(void *context)
{
    EffectLog *log = (EffectLog *)context;
    log->calls[log->count++] = 'S';
    return log->motion_status;
}

static void effect_body(void *context)
{
    EffectLog *log = (EffectLog *)context;
    log->calls[log->count++] = 'B';
}

static int effect_cel(void *context)
{
    EffectLog *log = (EffectLog *)context;
    log->calls[log->count++] = 'C';
    return log->cel_status;
}

static void effect_pose(void *context)
{
    EffectLog *log = (EffectLog *)context;
    log->calls[log->count++] = 'P';
}

static void display_effects(void)
{
    Tables t;
    MMActorState s;
    MMActorFrameInputs in;
    MMActorFrameResult result;
    MMActorFrameEffects effects;
    EffectLog log;
    int dirty, sound = 0;
    memset(&log, 0, sizeof(log));
    effects.context = &log;
    effects.event = effect_event;
    effects.motion_sounds = effect_sound;
    effects.position_body = effect_body;
    effects.select_cel = effect_cel;
    effects.update_pose = effect_pose;
    init(&t, &s, &in);
    t.count = 1;
    t.events[0].aet = MM_ACTOR_TWEAK;
    memset(&result, 0, sizeof(result));
    result.success = 99;
    CHECK(mm_actor_frame_run(&s, &t, &t, &in, 0, &services, &effects, &result) == -2);
    CHECK(result.success == 99 && s.onStage && !s.nextEvent && log.count == 1 && log.calls[0] == 'E');

    result.success = 0;
    result.positionDirty = result.positionBody = result.selectCel = 1;
    result.soundInFrame = result.considerMotionSounds = 1;
    log.count = 0;
    log.motion_status = -1;
    log.cel_status = 1;
    CHECK(!mm_actor_frame_display(&result, &s, 1, MM_ACTOR_FRAME_STUDIO, &dirty, &sound, &effects));
    CHECK(!dirty && sound && log.count == 3 && !memcmp(log.calls, "SBC", 3));
    log.count = 0;
    CHECK(!mm_actor_frame_display(&result, &s, 1, MM_ACTOR_FRAME_PREVIEW, &dirty, 0, &effects));
    CHECK(dirty && !log.count);

    result.success = 1;
    CHECK(mm_actor_frame_display(&result, &s, 1, MM_ACTOR_FRAME_PREVIEW, 0, 0, &effects) == -1);
    CHECK(log.count == 1 && log.calls[0] == 'S');
    log.count = 0;
    log.motion_status = 1;
    log.cel_status = -2;
    CHECK(mm_actor_frame_display(&result, &s, 1, MM_ACTOR_FRAME_PREVIEW, 0, 0, &effects) == -2);
    CHECK(log.count == 3 && !memcmp(log.calls, "SBC", 3));

    log.count = 0;
    result.positionBody = result.considerMotionSounds = result.positionDirty = 0;
    log.cel_status = 0;
    CHECK(mm_actor_frame_display(&result, &s, 1, MM_ACTOR_FRAME_PREVIEW, 0, 0, &effects) == 1 && !log.count);
    CHECK(!mm_actor_frame_display(&result, &s, 1, MM_ACTOR_FRAME_STUDIO, 0, 0, &effects));
    CHECK(log.count == 1 && log.calls[0] == 'C');
    result.selectCel = 0;
    result.updatePose = 1;
    CHECK(mm_actor_frame_display(&result, &s, 1, MM_ACTOR_FRAME_STUDIO, 0, 0, &effects));
    CHECK(log.count == 2 && log.calls[1] == 'P');
}

int main(void)
{
    entrances();
    sounds();
    failures_and_latches();
    modes_and_end();
    display_effects();
    puts("actor frame passed");
    return 0;
}

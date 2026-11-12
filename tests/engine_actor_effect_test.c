#include "three_mm/actor_effect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ONE 65536L
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "actor effect failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef union Payload
{
    ThreeMMActorEntrance add;
    ThreeMMActorCostume costume;
    ThreeMMI32 action[2];
    ThreeMMI32 scalar;
} Payload;
typedef struct Fixture
{
    ThreeMMActorRoutePoint route[2];
    ThreeMMActorEvent event;
    Payload data;
    ThreeMMI32 bytes;
    ThreeMMActorState state, published, observed[16];
    int fail, count_writes, writes, calls, operations[16], body_costume, saved_costume, references;
    int resource;
} Fixture;
static ThreeMMU32 width(void *p) { (void)p; return 16; }
static ThreeMMI32 count(void *p) { (void)p; return 2; }
static void point(void *p, ThreeMMI32 i, ThreeMMActorRoutePoint *out)
{
    CHECK(i >= 0 && i < 2);
    *out = ((Fixture *)p)->route[i];
}
static ThreeMMU32 event_width(void *p) { (void)p; return 20; }
static ThreeMMI32 event_count(void *p) { (void)p; return 1; }
static void event(void *p, ThreeMMI32 i, ThreeMMActorEvent *out)
{
    CHECK(i == 0);
    *out = ((Fixture *)p)->event;
}
static ThreeMMI32 size(void *p, ThreeMMI32 i) { CHECK(i == 0); return ((Fixture *)p)->bytes; }
static void payload(void *p, ThreeMMI32 i, void *out)
{
    Fixture *f = (Fixture *)p;
    CHECK(i == 0 && f->bytes >= 0 && (unsigned)f->bytes <= sizeof(f->data));
    memcpy(out, &f->data, (size_t)f->bytes);
}
static void write_event(void *p, ThreeMMI32 i, const ThreeMMActorEvent *value)
{
    Fixture *f = (Fixture *)p;
    CHECK(i == 0);
    ++f->writes;
    f->event = *value;
}
static void publish(void *p, const ThreeMMActorState *state) { ((Fixture *)p)->published = *state; }
static void run(void *p, const ThreeMMActorEffectCommand *c, ThreeMMActorEffectReply *r)
{
    Fixture *f = (Fixture *)p;
    int ok = c->operation != f->fail;
    CHECK(f->calls < 16);
    f->observed[f->calls] = f->published;
    f->operations[f->calls++] = c->operation;
    r->success = ok;
    switch (c->operation)
    {
    case THREE_MM_EFFECT_SET_ACTION_CEL:
        CHECK(c->first == 7 && c->second == 3);
        break;
    case THREE_MM_EFFECT_CEL_COUNT:
        CHECK(c->first == 7);
        if (f->count_writes)
            r->value = ok ? 19 : -71;
        break;
    case THREE_MM_EFFECT_ACTION_FLAGS:
        CHECK(c->first == 7);
        r->flags = 0x81234567U;
        break;
    case THREE_MM_EFFECT_SAVE_COSTUME:
        f->saved_costume = f->body_costume;
        break;
    case THREE_MM_EFFECT_DEFAULT_COSTUME:
        f->body_costume = 2;
        break;
    case THREE_MM_EFFECT_RESTORE_COSTUME:
        f->body_costume = f->saved_costume;
        break;
    case THREE_MM_EFFECT_FETCH_CUSTOM:
    case THREE_MM_EFFECT_FETCH_MATERIAL:
        CHECK(c->first == 23 && c->tag && c->tag->sid == 9 && c->tag->number == 44);
        if (ok)
        {
            r->resource = &f->resource;
            ++f->references;
        }
        break;
    case THREE_MM_EFFECT_APPLY_CUSTOM:
    case THREE_MM_EFFECT_APPLY_MATERIAL:
        CHECK(c->resource == &f->resource && c->first == 12 && f->references == 1);
        break;
    case THREE_MM_EFFECT_RELEASE_CUSTOM:
    case THREE_MM_EFFECT_RELEASE_MATERIAL:
        CHECK(c->resource == &f->resource && f->references == 1);
        --f->references;
        break;
    case THREE_MM_EFFECT_ENQUEUE_SOUND:
        CHECK(c->first == 0);
        break;
    }
}
static const ThreeMMActorMotionServices motion = {width, count, point, event_width, event_count, event, size, payload};
static void init(Fixture *f, int kind)
{
    memset(f, 0, sizeof(*f));
    three_mm_actor_state_init(&f->state);
    f->state.frame = 31;
    f->state.action = 4;
    f->state.cel = 6;
    f->state.celCount = 11;
    f->state.actionFlags = 55;
    f->state.position.dxr = -ONE;
    f->state.location.dnfrm = 99;
    f->state.useCurrentRotation = 1;
    f->event.aet = kind;
    f->event.nfrm = -17;
    f->event.rtel.dwrOffset = ONE;
    f->event.rtel.dnfrm = 5;
    f->route[0].xyz.dxr = 2 * ONE;
    f->route[0].xyz.dyr = 4 * ONE;
    f->route[0].dwr = 2 * ONE;
    f->route[1].xyz.dxr = 6 * ONE;
    f->route[1].xyz.dyr = 8 * ONE;
    f->fail = -1;
    f->count_writes = 1;
    f->body_costume = 1;
    if (kind == THREE_MM_ACTOR_ACTION)
    {
        f->data.action[0] = 7;
        f->data.action[1] = 3;
        f->bytes = 8;
    }
    if (kind == THREE_MM_ACTOR_ADD)
    {
        f->data.add.translation.dxr = 3 * ONE;
        f->data.add.translation.dyr = -2 * ONE;
        f->data.add.x = 16384;
        f->data.add.y = 8192;
        f->bytes = 20;
        f->state.action = 7;
        f->state.cel = 3;
    }
    if (kind == THREE_MM_ACTOR_COSTUME)
    {
        f->data.costume.body_set = 12;
        f->data.costume.costume = 23;
        f->data.costume.tag.sid = 9;
        f->data.costume.tag.number = 44;
        f->bytes = sizeof(f->data.costume);
    }
}
static int apply_mode(Fixture *f, int body, int metadata)
{
    const ThreeMMActorPoint origin = {8 * ONE, 4 * ONE, -ONE};
    ThreeMMActorEffectServices effects;
    effects.context = f;
    effects.write_event = write_event;
    effects.run = run;
    effects.publish = publish;
    return three_mm_actor_effect_apply(&f->state, f, f, 0, &origin, body, metadata, &motion, &effects);
}
static int apply(Fixture *f, int body) { return apply_mode(f, body, 0); }
static void action_tests(void)
{
    Fixture f;
    ThreeMMActorState before;
    int fail, same, writes, ok, expected;
    for (same = 0; same < 2; ++same)
        for (writes = 0; writes < 2; ++writes)
            for (fail = -1; fail <= THREE_MM_EFFECT_ACTION_FLAGS; ++fail)
            {
                init(&f, THREE_MM_ACTOR_ACTION);
                f.count_writes = writes;
                if (same) f.state.action = 7;
                before = f.state;
                f.fail = fail;
                ok = apply(&f, 1);
                CHECK(ok == (fail < THREE_MM_EFFECT_SET_ACTION_CEL));
                CHECK(f.event.nfrm == 31 && f.writes == 1);
                expected = same ? 0 : 1;
                if (!same) CHECK(f.operations[0] == THREE_MM_EFFECT_CLEAR_SOUNDS);
                CHECK(f.operations[expected] == THREE_MM_EFFECT_SET_ACTION_CEL);
                if (fail == THREE_MM_EFFECT_SET_ACTION_CEL)
                {
                    CHECK(memcmp(&before, &f.state, sizeof(before)) == 0);
                    CHECK(f.calls == expected + 1);
                    continue;
                }
                CHECK(f.state.action == 7 && f.state.cel == 3);
                CHECK(f.operations[expected + 1] == THREE_MM_EFFECT_CEL_COUNT);
                CHECK(f.observed[expected + 1].action == 7 && f.observed[expected + 1].cel == 3);
                CHECK(f.state.celCount == (writes ? (fail == THREE_MM_EFFECT_CEL_COUNT ? -71 : 19) : 11));
                if (fail == THREE_MM_EFFECT_CEL_COUNT)
                {
                    CHECK(f.calls == expected + 2 && f.state.actionEvent == -1 && f.state.location.dnfrm == 99);
                    continue;
                }
                CHECK(f.operations[expected + 2] == THREE_MM_EFFECT_ACTION_FLAGS && f.calls == expected + 3);
                CHECK(f.state.actionEvent == 0 && f.state.location.dnfrm == 5);
                CHECK(f.state.position.dxr == 4 * ONE && f.state.position.dyr == 6 * ONE);
                CHECK(f.observed[expected + 2].actionEvent == 0 && f.observed[expected + 2].position.dxr == 4 * ONE);
                CHECK(f.state.actionFlags == (fail == THREE_MM_EFFECT_ACTION_FLAGS ? 55U : 0x81234567U));
            }
}
static void entrance_tests(void)
{
    Fixture f;
    ThreeMMActorState before;
    int fail, stage, ok;
    for (stage = 0; stage < 2; ++stage)
        for (fail = -1; fail <= THREE_MM_EFFECT_SHOW; ++fail)
        {
            init(&f, THREE_MM_ACTOR_ADD);
            f.state.onStage = stage;
            f.state.transform.aevpull.rScaleX = 3 * ONE;
            f.state.transform.rScaleStep = 2 * ONE;
            before = f.state;
            f.fail = fail;
            ok = apply(&f, 1);
            CHECK(ok == (fail != THREE_MM_EFFECT_SAVE_COSTUME && fail != THREE_MM_EFFECT_DEFAULT_COSTUME &&
                         fail != THREE_MM_EFFECT_SET_ACTION_CEL));
            CHECK(f.operations[0] == THREE_MM_EFFECT_SAVE_COSTUME);
            if (!ok)
            {
                CHECK(memcmp(&before, &f.state, sizeof(before)) == 0);
                CHECK(f.calls == (fail == THREE_MM_EFFECT_SAVE_COSTUME ? 1 :
                                 fail == THREE_MM_EFFECT_DEFAULT_COSTUME ? 2 : 4));
                CHECK(f.body_costume == (fail == THREE_MM_EFFECT_DEFAULT_COSTUME ? 2 : 1));
                if (fail == THREE_MM_EFFECT_SET_ACTION_CEL)
                    CHECK(f.operations[3] == THREE_MM_EFFECT_RESTORE_COSTUME);
                continue;
            }
            CHECK(f.calls == (stage ? 4 : 5));
            CHECK(f.operations[1] == THREE_MM_EFFECT_DEFAULT_COSTUME && f.operations[2] == THREE_MM_EFFECT_SET_ACTION_CEL);
            CHECK(f.operations[3] == THREE_MM_EFFECT_CLEAR_SOUNDS);
            CHECK(f.state.onStage && f.state.useCurrentRotation && f.state.addEvent == 0 && f.state.firstEventInFrame == 0);
            CHECK(f.state.routeTranslation.dxr == 11 * ONE && f.state.routeTranslation.dyr == 2 * ONE);
            CHECK(f.state.position.dxr == 4 * ONE && f.state.position.dyr == 6 * ONE);
            CHECK(f.state.transform.rScaleStep == ONE && f.state.transform.aevpull.rScaleX == ONE);
            CHECK(f.state.transform.xaPath == 16384 && f.state.transform.yaPath == 8192);
            if (!stage)
            {
                CHECK(f.operations[4] == THREE_MM_EFFECT_SHOW && !f.observed[4].onStage);
                CHECK(f.observed[4].addEvent == 0 && f.observed[4].position.dxr == 4 * ONE);
            }
        }
}
static void costume_tests(void)
{
    Fixture f;
    int custom, fail, fetch, install, release, ok;
    for (custom = 0; custom < 2; ++custom)
        for (fail = -1; fail <= THREE_MM_EFFECT_RELEASE_MATERIAL; ++fail)
        {
            init(&f, THREE_MM_ACTOR_COSTUME);
            f.data.costume.custom = custom ? -1 : 0;
            f.fail = fail;
            fetch = custom ? THREE_MM_EFFECT_FETCH_CUSTOM : THREE_MM_EFFECT_FETCH_MATERIAL;
            install = custom ? THREE_MM_EFFECT_APPLY_CUSTOM : THREE_MM_EFFECT_APPLY_MATERIAL;
            release = custom ? THREE_MM_EFFECT_RELEASE_CUSTOM : THREE_MM_EFFECT_RELEASE_MATERIAL;
            ok = apply(&f, 1);
            CHECK(ok == (fail != fetch));
            CHECK(f.references == 0 && f.operations[0] == fetch && f.calls == (ok ? 3 : 1));
            if (ok) CHECK(f.operations[1] == install && f.operations[2] == release);
        }
}
int main(void)
{
    Fixture f;
    ThreeMMActorState before;
    int stage, body, fail, kind;
    action_tests();
    entrance_tests();
    costume_tests();
    init(&f, THREE_MM_ACTOR_ADD);
    CHECK(apply_mode(&f, 0, 1) && f.calls == 0 && f.state.onStage && f.state.addEvent == 0);
    CHECK(f.state.routeTranslation.dxr == 11 * ONE && f.state.position.dxr == 4 * ONE);
    for (fail = -1; fail <= THREE_MM_EFFECT_ACTION_FLAGS; ++fail)
    {
        init(&f, THREE_MM_ACTOR_ACTION);
        f.fail = fail;
        CHECK(apply_mode(&f, 0, 1) == (fail != THREE_MM_EFFECT_CEL_COUNT && fail != THREE_MM_EFFECT_ACTION_FLAGS));
        CHECK(f.operations[0] == THREE_MM_EFFECT_CEL_COUNT);
        CHECK(f.calls == (fail == THREE_MM_EFFECT_CEL_COUNT ? 1 : 2));
        if (f.calls == 2) CHECK(f.operations[1] == THREE_MM_EFFECT_ACTION_FLAGS);
    }
    for (kind = 0; kind < 3; ++kind)
    {
        init(&f, kind == 0 ? THREE_MM_ACTOR_COSTUME : kind == 1 ? THREE_MM_ACTOR_SOUND : THREE_MM_ACTOR_REMOVE);
        f.state.onStage = 1;
        CHECK(apply_mode(&f, 0, 1) && f.calls == 0);
        CHECK(f.state.onStage == (kind != 2));
    }
    for (stage = 0; stage < 2; ++stage)
        for (body = 0; body < 2; ++body)
        {
            init(&f, THREE_MM_ACTOR_REMOVE);
            f.state.onStage = stage;
            CHECK(apply(&f, body) && !f.state.onStage && f.calls == (stage && body));
            if (f.calls) CHECK(f.operations[0] == THREE_MM_EFFECT_HIDE && f.observed[0].onStage);
        }
    for (fail = 0; fail < 2; ++fail)
    {
        init(&f, THREE_MM_ACTOR_SOUND);
        f.fail = fail ? THREE_MM_EFFECT_ENQUEUE_SOUND : -1;
        CHECK(apply(&f, 1) == !fail && f.calls == 1 && f.operations[0] == THREE_MM_EFFECT_ENQUEUE_SOUND);
    }
    init(&f, THREE_MM_ACTOR_FREEZE);
    f.bytes = 4;
    f.data.scalar = -1;
    CHECK(apply(&f, 1) && f.state.frozen && f.calls == 0 && f.writes == 1);
    CHECK(apply(&f, 1) && f.writes == 1);
    for (kind = THREE_MM_ACTOR_ADD; kind <= THREE_MM_ACTOR_COSTUME; ++kind)
    {
        init(&f, kind);
        f.bytes = 1;
        before = f.state;
        CHECK(!apply(&f, 1) && !f.calls && f.writes == 1);
        CHECK(memcmp(&before, &f.state, sizeof(before)) == 0);
    }
    init(&f, 99);
    CHECK(!apply(&f, 1) && f.calls == 0 && f.writes == 1);
    puts("actor event effects passed");
    return 0;
}

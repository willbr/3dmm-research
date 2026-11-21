#include "mm/actor_effect.h"
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
    MMActorEntrance add;
    MMActorCostume costume;
    MMI32 action[2];
    MMI32 scalar;
} Payload;
typedef struct Fixture
{
    MMActorRoutePoint route[2];
    MMActorEvent event;
    Payload data;
    MMI32 bytes, action_limit;
    MMActorState state, published, observed[16];
    int fail, count_writes, writes, calls, operations[16], body_costume, saved_costume, references;
    int resource;
} Fixture;
static MMU32 width(void *p) { (void)p; return 16; }
static MMI32 count(void *p) { (void)p; return 2; }
static void point(void *p, MMI32 i, MMActorRoutePoint *out)
{
    CHECK(i >= 0 && i < 2);
    *out = ((Fixture *)p)->route[i];
}
static MMU32 event_width(void *p) { (void)p; return 20; }
static MMI32 event_count(void *p) { (void)p; return 1; }
static void event(void *p, MMI32 i, MMActorEvent *out)
{
    CHECK(i == 0);
    *out = ((Fixture *)p)->event;
}
static MMI32 size(void *p, MMI32 i) { CHECK(i == 0); return ((Fixture *)p)->bytes; }
static void payload(void *p, MMI32 i, void *out)
{
    Fixture *f = (Fixture *)p;
    CHECK(i == 0 && f->bytes >= 0 && (unsigned)f->bytes <= sizeof(f->data));
    memcpy(out, &f->data, (size_t)f->bytes);
}
static void write_event(void *p, MMI32 i, const MMActorEvent *value)
{
    Fixture *f = (Fixture *)p;
    CHECK(i == 0);
    ++f->writes;
    f->event = *value;
}
static void publish(void *p, const MMActorState *state) { ((Fixture *)p)->published = *state; }
enum
{
    MM_EFFECT_CLEAR_SOUNDS, MM_EFFECT_SET_ACTION_CEL, MM_EFFECT_CEL_COUNT, MM_EFFECT_ACTION_FLAGS,
    MM_EFFECT_SAVE_COSTUME, MM_EFFECT_DEFAULT_COSTUME, MM_EFFECT_RESTORE_COSTUME, MM_EFFECT_SHOW, MM_EFFECT_HIDE,
    MM_EFFECT_FETCH_CUSTOM, MM_EFFECT_FETCH_MATERIAL, MM_EFFECT_APPLY_CUSTOM, MM_EFFECT_APPLY_MATERIAL,
    MM_EFFECT_RELEASE_CUSTOM, MM_EFFECT_RELEASE_MATERIAL, MM_EFFECT_ENQUEUE_SOUND
};
static int record(Fixture *f, int operation)
{
    CHECK(f->calls < 16);
    f->observed[f->calls] = f->published;
    f->operations[f->calls++] = operation;
    return operation != f->fail;
}
static void clear(void *p) { record((Fixture *)p, MM_EFFECT_CLEAR_SOUNDS); }
static int select_cel(void *p, MMI32 action, MMI32 cel)
{
    CHECK(action == 7 && cel == 3);
    return record((Fixture *)p, MM_EFFECT_SET_ACTION_CEL);
}
static int cel_count(void *p, MMI32 action, MMI32 *out)
{
    Fixture *f = (Fixture *)p;
    int ok = record(f, MM_EFFECT_CEL_COUNT);
    CHECK(action == 7);
    if (f->count_writes)
        *out = ok ? 19 : -71;
    return ok;
}
static int action_flags(void *p, MMI32 action, MMU32 *out)
{
    CHECK(action == 7);
    *out = 0x81234567U;
    return record((Fixture *)p, MM_EFFECT_ACTION_FLAGS);
}
static int save_costume(void *p)
{
    Fixture *f = (Fixture *)p;
    f->saved_costume = f->body_costume;
    return record(f, MM_EFFECT_SAVE_COSTUME);
}
static int default_costume(void *p)
{
    Fixture *f = (Fixture *)p;
    f->body_costume = 2;
    return record(f, MM_EFFECT_DEFAULT_COSTUME);
}
static void restore_costume(void *p)
{
    Fixture *f = (Fixture *)p;
    f->body_costume = f->saved_costume;
    record(f, MM_EFFECT_RESTORE_COSTUME);
}
static void visibility(void *p, int shown) { record((Fixture *)p, shown ? MM_EFFECT_SHOW : MM_EFFECT_HIDE); }
static void *resource(Fixture *f, int operation)
{
    if (!record(f, operation))
        return NULL;
    ++f->references;
    return &f->resource;
}
static void *fetch_custom(void *p, MMI32 costume)
{
    CHECK(costume == 23);
    return resource((Fixture *)p, MM_EFFECT_FETCH_CUSTOM);
}
static void *fetch_material(void *p, const MMTag *tag)
{
    CHECK(tag && tag->sid == 9 && tag->number == 44);
    return resource((Fixture *)p, MM_EFFECT_FETCH_MATERIAL);
}
static void apply_custom(void *p, void *resource)
{
    Fixture *f = (Fixture *)p;
    CHECK(resource == &f->resource && f->references == 1);
    record(f, MM_EFFECT_APPLY_CUSTOM);
}
static void apply_material(void *p, MMI32 set, void *resource)
{
    Fixture *f = (Fixture *)p;
    CHECK(set == 12 && resource == &f->resource && f->references == 1);
    record(f, MM_EFFECT_APPLY_MATERIAL);
}
static void release_resource(void *p, void *resource, int custom)
{
    Fixture *f = (Fixture *)p;
    CHECK(resource == &f->resource && f->references == 1);
    --f->references;
    record(f, custom ? MM_EFFECT_RELEASE_CUSTOM : MM_EFFECT_RELEASE_MATERIAL);
}
static int enqueue(void *p, MMI32 event)
{
    CHECK(event == 0);
    return record((Fixture *)p, MM_EFFECT_ENQUEUE_SOUND);
}
static MMI32 action_count(void *p) { return ((Fixture *)p)->action_limit; }

static const MMActorMotionServices motion = {width, count, point, event_width, event_count, event, size, payload};
static void init(Fixture *f, int kind)
{
    memset(f, 0, sizeof(*f));
    mm_actor_state_init(&f->state);
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
    if (kind == MM_ACTOR_ACTION)
    {
        f->data.action[0] = 7;
        f->data.action[1] = 3;
        f->bytes = 8;
    }
    if (kind == MM_ACTOR_ADD)
    {
        f->data.add.translation.dxr = 3 * ONE;
        f->data.add.translation.dyr = -2 * ONE;
        f->data.add.x = 16384;
        f->data.add.y = 8192;
        f->bytes = 20;
        f->state.action = 7;
        f->state.cel = 3;
    }
    if (kind == MM_ACTOR_COSTUME)
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
    const MMActorPoint origin = {8 * ONE, 4 * ONE, -ONE};
    MMActorEffectServices effects = {0};
    effects.context = f;
    effects.write_event = write_event;
    effects.publish = publish;
    effects.action_count = f->action_limit ? action_count : NULL;
    effects.select_cel = select_cel;
    effects.cel_count = cel_count;
    effects.action_flags = action_flags;
    effects.save_costume = save_costume;
    effects.default_costume = default_costume;
    effects.restore_costume = restore_costume;
    effects.visibility = visibility;
    effects.fetch_custom = fetch_custom;
    effects.fetch_material = fetch_material;
    effects.apply_custom = apply_custom;
    effects.apply_material = apply_material;
    effects.release_resource = release_resource;
    effects.clear_sounds = clear;
    effects.enqueue_sound = enqueue;
    return mm_actor_effect_apply(&f->state, f, f, 0, &origin, body, metadata, &motion, &effects);
}
static int apply(Fixture *f, int body) { return apply_mode(f, body, 0); }
static void action_tests(void)
{
    Fixture f;
    MMActorState before;
    int fail, same, writes, ok, expected;
    for (same = 0; same < 2; ++same)
        for (writes = 0; writes < 2; ++writes)
            for (fail = -1; fail <= MM_EFFECT_ACTION_FLAGS; ++fail)
            {
                init(&f, MM_ACTOR_ACTION);
                f.count_writes = writes;
                if (same) f.state.action = 7;
                before = f.state;
                f.fail = fail;
                ok = apply(&f, 1);
                CHECK(ok == (fail < MM_EFFECT_SET_ACTION_CEL));
                CHECK(f.event.nfrm == 31 && f.writes == 1);
                expected = same ? 0 : 1;
                if (!same) CHECK(f.operations[0] == MM_EFFECT_CLEAR_SOUNDS);
                CHECK(f.operations[expected] == MM_EFFECT_SET_ACTION_CEL);
                if (fail == MM_EFFECT_SET_ACTION_CEL)
                {
                    CHECK(memcmp(&before, &f.state, sizeof(before)) == 0);
                    CHECK(f.calls == expected + 1);
                    continue;
                }
                CHECK(f.state.action == 7 && f.state.cel == 3);
                CHECK(f.operations[expected + 1] == MM_EFFECT_CEL_COUNT);
                CHECK(f.observed[expected + 1].action == 7 && f.observed[expected + 1].cel == 3);
                CHECK(f.state.celCount == (writes ? (fail == MM_EFFECT_CEL_COUNT ? -71 : 19) : 11));
                if (fail == MM_EFFECT_CEL_COUNT)
                {
                    CHECK(f.calls == expected + 2 && f.state.actionEvent == -1 && f.state.location.dnfrm == 99);
                    continue;
                }
                CHECK(f.operations[expected + 2] == MM_EFFECT_ACTION_FLAGS && f.calls == expected + 3);
                CHECK(f.state.actionEvent == 0 && f.state.location.dnfrm == 5);
                CHECK(f.state.position.dxr == 4 * ONE && f.state.position.dyr == 6 * ONE);
                CHECK(f.observed[expected + 2].actionEvent == 0 && f.observed[expected + 2].position.dxr == 4 * ONE);
                CHECK(f.state.actionFlags == (fail == MM_EFFECT_ACTION_FLAGS ? 55U : 0x81234567U));
            }
    init(&f, MM_ACTOR_ACTION);
    before = f.state;
    f.action_limit = 7;
    CHECK(!apply(&f, 1) && f.calls == 1 && f.operations[0] == MM_EFFECT_CLEAR_SOUNDS);
    CHECK(!memcmp(&before, &f.state, sizeof(before)));
    CHECK(!apply_mode(&f, 0, 1) && f.calls == 1 && f.state.action == 7 && f.state.celCount == 11);
}
static void entrance_tests(void)
{
    Fixture f;
    MMActorState before;
    int fail, stage, ok;
    for (stage = 0; stage < 2; ++stage)
        for (fail = -1; fail <= MM_EFFECT_SHOW; ++fail)
        {
            init(&f, MM_ACTOR_ADD);
            f.state.onStage = stage;
            f.state.transform.aevpull.rScaleX = 3 * ONE;
            f.state.transform.rScaleStep = 2 * ONE;
            before = f.state;
            f.fail = fail;
            ok = apply(&f, 1);
            CHECK(ok == (fail != MM_EFFECT_SAVE_COSTUME && fail != MM_EFFECT_DEFAULT_COSTUME &&
                         fail != MM_EFFECT_SET_ACTION_CEL));
            CHECK(f.operations[0] == MM_EFFECT_SAVE_COSTUME);
            if (!ok)
            {
                CHECK(memcmp(&before, &f.state, sizeof(before)) == 0);
                CHECK(f.calls == (fail == MM_EFFECT_SAVE_COSTUME ? 1 :
                                 fail == MM_EFFECT_DEFAULT_COSTUME ? 2 : 4));
                CHECK(f.body_costume == (fail == MM_EFFECT_DEFAULT_COSTUME ? 2 : 1));
                if (fail == MM_EFFECT_SET_ACTION_CEL)
                    CHECK(f.operations[3] == MM_EFFECT_RESTORE_COSTUME);
                continue;
            }
            CHECK(f.calls == (stage ? 4 : 5));
            CHECK(f.operations[1] == MM_EFFECT_DEFAULT_COSTUME && f.operations[2] == MM_EFFECT_SET_ACTION_CEL);
            CHECK(f.operations[3] == MM_EFFECT_CLEAR_SOUNDS);
            CHECK(f.state.onStage && f.state.useCurrentRotation && f.state.addEvent == 0 && f.state.firstEventInFrame == 0);
            CHECK(f.state.routeTranslation.dxr == 11 * ONE && f.state.routeTranslation.dyr == 2 * ONE);
            CHECK(f.state.position.dxr == 4 * ONE && f.state.position.dyr == 6 * ONE);
            CHECK(f.state.transform.rScaleStep == ONE && f.state.transform.aevpull.rScaleX == ONE);
            CHECK(f.state.transform.xaPath == 16384 && f.state.transform.yaPath == 8192);
            if (!stage)
            {
                CHECK(f.operations[4] == MM_EFFECT_SHOW && !f.observed[4].onStage);
                CHECK(f.observed[4].addEvent == 0 && f.observed[4].position.dxr == 4 * ONE);
            }
        }
}
static void costume_tests(void)
{
    Fixture f;
    int custom, fail, fetch, install, release, ok;
    for (custom = 0; custom < 2; ++custom)
        for (fail = -1; fail <= MM_EFFECT_RELEASE_MATERIAL; ++fail)
        {
            init(&f, MM_ACTOR_COSTUME);
            f.data.costume.custom = custom ? -1 : 0;
            f.fail = fail;
            fetch = custom ? MM_EFFECT_FETCH_CUSTOM : MM_EFFECT_FETCH_MATERIAL;
            install = custom ? MM_EFFECT_APPLY_CUSTOM : MM_EFFECT_APPLY_MATERIAL;
            release = custom ? MM_EFFECT_RELEASE_CUSTOM : MM_EFFECT_RELEASE_MATERIAL;
            ok = apply(&f, 1);
            CHECK(ok == (fail != fetch));
            CHECK(f.references == 0 && f.operations[0] == fetch && f.calls == (ok ? 3 : 1));
            if (ok) CHECK(f.operations[1] == install && f.operations[2] == release);
        }
}
int main(void)
{
    Fixture f;
    MMActorState before;
    int stage, body, fail, kind;
    action_tests();
    entrance_tests();
    costume_tests();
    init(&f, MM_ACTOR_ADD);
    CHECK(apply_mode(&f, 0, 1) && f.calls == 0 && f.state.onStage && f.state.addEvent == 0);
    CHECK(f.state.routeTranslation.dxr == 11 * ONE && f.state.position.dxr == 4 * ONE);
    for (fail = -1; fail <= MM_EFFECT_ACTION_FLAGS; ++fail)
    {
        init(&f, MM_ACTOR_ACTION);
        f.fail = fail;
        CHECK(apply_mode(&f, 0, 1) == (fail != MM_EFFECT_CEL_COUNT && fail != MM_EFFECT_ACTION_FLAGS));
        CHECK(f.operations[0] == MM_EFFECT_CEL_COUNT);
        CHECK(f.calls == (fail == MM_EFFECT_CEL_COUNT ? 1 : 2));
        if (f.calls == 2) CHECK(f.operations[1] == MM_EFFECT_ACTION_FLAGS);
    }
    for (kind = 0; kind < 3; ++kind)
    {
        init(&f, kind == 0 ? MM_ACTOR_COSTUME : kind == 1 ? MM_ACTOR_SOUND : MM_ACTOR_REMOVE);
        f.state.onStage = 1;
        CHECK(apply_mode(&f, 0, 1) && f.calls == 0);
        CHECK(f.state.onStage == (kind != 2));
    }
    for (stage = 0; stage < 2; ++stage)
        for (body = 0; body < 2; ++body)
        {
            init(&f, MM_ACTOR_REMOVE);
            f.state.onStage = stage;
            CHECK(apply(&f, body) && !f.state.onStage && f.calls == (stage && body));
            if (f.calls) CHECK(f.operations[0] == MM_EFFECT_HIDE && f.observed[0].onStage);
        }
    for (fail = 0; fail < 2; ++fail)
    {
        init(&f, MM_ACTOR_SOUND);
        f.fail = fail ? MM_EFFECT_ENQUEUE_SOUND : -1;
        CHECK(apply(&f, 1) == !fail && f.calls == 1 && f.operations[0] == MM_EFFECT_ENQUEUE_SOUND);
    }
    init(&f, MM_ACTOR_FREEZE);
    f.bytes = 4;
    f.data.scalar = -1;
    CHECK(apply(&f, 1) && f.state.frozen && f.calls == 0 && f.writes == 1);
    CHECK(apply(&f, 1) && f.writes == 1);
    for (kind = MM_ACTOR_ADD; kind <= MM_ACTOR_COSTUME; ++kind)
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

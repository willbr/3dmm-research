#include "mm/actor_effect.h"
#include "mm/actor_route_paste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "route paste check %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define ONE 65536L

typedef struct Fixture
{
    MMActorOwner actor;
    MMActorPath *clipboard;
    MMActorRoutePasteServices services;
    int tag_refs, dirty, invalidated, located;
    int fail_growth, failed_allocations, point_inserts, fail_point_at, event_inserts, fail_event_at;
    int reserves, fail_reserve_at, applied, fail_apply, flag_queries, fail_flags, flags, zero_distance, diagnostics;
} Fixture;

static void *resize(void *context, void *ptr, MMU32 old, MMU32 size, int priority)
{
    Fixture *f = (Fixture *)context;
    (void)priority;
    if (f->fail_growth && size > old)
    {
        ++f->failed_allocations;
        return NULL;
    }
    return realloc(ptr, size);
}

static void release(void *context, void *ptr, MMU32 bytes)
{
    (void)context;
    (void)bytes;
    free(ptr);
}

static int insert_point(void *storage, MMI32 index, const MMActorRoutePoint *point)
{
    MMActorPath *path = (MMActorPath *)storage;
    Fixture *f = (Fixture *)path->storage.allocator.context;
    if (f->fail_point_at == ++f->point_inserts)
        return 0;
    return mm_actor_path_insert(path, index, point);
}

static int insert_event(void *storage, MMI32 index, MMU32 bytes, const void *payload, const MMActorEvent *event)
{
    MMActorEventTable *events = (MMActorEventTable *)storage;
    Fixture *f = (Fixture *)events->storage.allocator.context;
    if (f->fail_event_at == ++f->event_inserts)
        return 0;
    return mm_actor_events_insert(events, index, bytes, payload, event);
}

static int reserve_events(void *storage, MMU32 count, MMU32 bytes)
{
    MMActorEventTable *events = (MMActorEventTable *)storage;
    Fixture *f = (Fixture *)events->storage.allocator.context;
    CHECK(count == 1 && bytes == 4);
    if (f->fail_reserve_at == ++f->reserves)
        return 0;
    return mm_actor_events_reserve(events, count, bytes);
}

static int apply_event(void *context, MMActorState *state, MMI32 index)
{
    Fixture *f = (Fixture *)context;
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    if (++f->applied == f->fail_apply)
        return 0;
    return mm_actor_state_apply(state, f->actor.track.route, f->actor.track.events, index,
                                &f->actor.runtime.origin, &motion) == MM_ACTOR_APPLIED;
}

static void retain_tag(void *cache) { ++((Fixture *)cache)->tag_refs; }
static void close_tag(void *cache) { CHECK(--((Fixture *)cache)->tag_refs >= 0); }

static const MMTagServices tags = {
    NULL, NULL, retain_tag, close_tag, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void publish(void *context, const MMActorState *state) { ((Fixture *)context)->actor.runtime.state = *state; }
static void life_dirty(void *context) { ((Fixture *)context)->actor.runtime.life_dirty = 1; }
static void invalidate(void *context) { ++((Fixture *)context)->invalidated; }
static void dirty(void *context) { ++((Fixture *)context)->dirty; }

static void get_range(void *context, MMActorEditRange *range)
{
    const MMActorRuntime *r = &((Fixture *)context)->actor.runtime;
    range->first = r->first;
    range->last = r->last;
}

static void pose_inputs(void *context, MMActorPoseInputs *inputs)
{
    (void)context;
    memset(inputs, 0, sizeof(*inputs));
}

static void locate_body(void *context, const MMActorBodyPose *pose)
{
    Fixture *f = (Fixture *)context;
    (void)pose;
    CHECK(f->actor.runtime.life_dirty && f->invalidated);
    ++f->located;
}

static MMI32 distance(void *context, const MMActorPoint *v)
{
    Fixture *f = (Fixture *)context;
    CHECK(v->dyr == 0 && v->dzr == 0);
    return f->zero_distance ? 0 : (v->dxr < 0 ? -v->dxr : v->dxr);
}

static int action_flags(void *context, MMI32 action, MMU32 *flags)
{
    Fixture *f = (Fixture *)context;
    CHECK(action == 0);
    ++f->flag_queries;
    *flags = f->flags;
    return !f->fail_flags;
}

static void diagnostic(void *context, int fault)
{
    Fixture *f = (Fixture *)context;
    CHECK(fault >= MM_ROUTE_PASTE_EMPTY && fault <= MM_ROUTE_PASTE_NO_ACTION);
    f->diagnostics |= 1 << fault;
}

static void append_point(MMActorPath *path, MMI32 x, MMI32 length)
{
    MMActorRoutePoint point = {{0, 0, 0}, 0};
    point.xyz.dxr = x * ONE;
    point.dwr = length * ONE;
    CHECK(mm_actor_path_insert(path, mm_actor_path_count(path), &point));
}

static void append_event(Fixture *f, MMI32 kind, MMI32 node, MMI32 frame, const void *payload, MMU32 size)
{
    MMActorEventTable *events = f->actor.track.events;
    MMActorEvent event = {0};
    event.aet = kind;
    event.nfrm = frame;
    event.rtel.irpt = node;
    CHECK(mm_actor_events_insert(events, mm_actor_events_count(events), size, payload, &event));
}

static void seed(Fixture *f, int clipboard_rows)
{
    MMActorCostume costume = {0};
    MMActorEntrance entrance = {0};
    MMI32 action[2] = {0, 0}, step = 0;
    const int x[] = {0, 4, 10, 20, 30}, lengths[] = {4, 6, 0, 10, 0};
    int i;
    for (i = 0; i < 5; ++i)
        append_point(f->actor.track.route, x[i], lengths[i]);
    for (i = 0; i < clipboard_rows; ++i)
        append_point(f->clipboard, 100 + i * 4, i + 1 == clipboard_rows ? 0 : 4);

    costume.tag.cache = f;
    append_event(f, MM_ACTOR_ADD, 0, 1, &entrance, sizeof(entrance));
    append_event(f, MM_ACTOR_ACTION, 0, 1, action, sizeof(action));
    append_event(f, MM_ACTOR_STEP, 0, 2, &step, sizeof(step));
    append_event(f, MM_ACTOR_COSTUME, 1, 3, &costume, sizeof(costume));
    append_event(f, MM_ACTOR_STEP, 2, 4, &step, sizeof(step));
    append_event(f, MM_ACTOR_ADD, 3, 8, &entrance, sizeof(entrance));
    append_event(f, MM_ACTOR_ACTION, 3, 8, action, sizeof(action));
    append_event(f, MM_ACTOR_COSTUME, 3, 8, &costume, sizeof(costume));
    append_event(f, MM_ACTOR_STEP, 4, 10, &step, sizeof(step));
    f->tag_refs = 2;
}

static void init(Fixture *f, int clipboard_rows)
{
    MMStorageAllocator allocator = {NULL, resize, release};
    MMActorState *state;
    MMActorSubrouteEditServices *s;
    MMActorEditServices *edits;

    memset(f, 0, sizeof(*f));
    allocator.context = f;
    mm_actor_owner_init(&f->actor);
    CHECK(mm_actor_track_create(&f->actor.track, &allocator, 0, &tags));
    f->clipboard = mm_actor_path_create(NULL, 0, 0);
    CHECK(f->clipboard);
    seed(f, clipboard_rows);

    f->actor.runtime.first = 1;
    f->actor.runtime.last = 10;
    f->actor.runtime.actor = 23;
    state = &f->actor.runtime.state;
    state->frame = 3;
    state->onStage = 1;
    state->firstEventInFrame = state->nextEvent = 3;
    state->addEvent = 0;
    state->actionEvent = 1;
    state->celCount = 4;

    s = &f->services.subroute;
    edits = &s->route.edits;
    edits->tables = mm_actor_edit_tables(f->actor.track.events, &tags);
    edits->tables.reserve_events = reserve_events;
    edits->tables.insert_event = insert_event;
    edits->context = f;
    edits->publish = publish;
    edits->apply_event = apply_event;
    edits->dirty = dirty;
    edits->invalidate_range = invalidate;
    edits->get_range = get_range;
    mm_actor_route_edit_bind_tables(&s->route);
    s->route.insert_point = insert_point;
    s->route.life_dirty = life_dirty;
    s->action.context = f;
    s->action.action_flags = action_flags;
    s->action.sounds.edits = *edits;
    s->pose_inputs = pose_inputs;
    s->locate_body = locate_body;
    f->flags = 8;
    f->services.distance = distance;
    f->services.diagnostic = diagnostic;
}

static void dispose(Fixture *f)
{
    mm_actor_track_dispose(&f->actor.track, &tags);
    mm_actor_sounds_dispose(&f->actor.sounds);
    mm_actor_path_release(f->clipboard);
    CHECK(!f->tag_refs);
}

static int paste(Fixture *f, int legacy)
{
    f->services.legacy_static_mode = legacy;
    return mm_actor_route_paste(&f->actor, f->clipboard, &f->services);
}

static MMActorRoutePoint point_at(const MMActorPath *path, MMI32 index)
{
    MMActorRoutePoint point;
    CHECK(mm_actor_path_get(path, index, &point));
    return point;
}

static MMActorEvent event_at(const Fixture *f, MMI32 index)
{
    MMActorEvent event;
    CHECK(mm_actor_events_get_fixed(f->actor.track.events, index, &event));
    return event;
}

static MMI32 later_node(const Fixture *f)
{
    MMI32 i;
    for (i = 0; i < mm_actor_events_count(f->actor.track.events); ++i)
    {
        const MMActorEvent event = event_at(f, i);
        if (event.aet == MM_ACTOR_ADD && event.nfrm == 8)
            return event.rtel.irpt;
    }
    return -1;
}

static void test_routes(void)
{
    Fixture f;
    MMActorEvent event;
    MMI32 i, scalar;
    init(&f, 3);
    CHECK(paste(&f, 0) && mm_actor_path_count(f.actor.track.route) == 5 && later_node(&f) == 3);
    CHECK(f.tag_refs == 1 && f.actor.runtime.state.step == -ONE && f.actor.runtime.life_dirty);
    CHECK(f.dirty == 2 && f.invalidated == 1 && f.located == 1);
    CHECK(point_at(f.actor.track.route, 0).xyz.dxr == 0 && point_at(f.actor.track.route, 0).dwr == 4 * ONE);
    CHECK(point_at(f.actor.track.route, 1).xyz.dxr == 4 * ONE && point_at(f.actor.track.route, 2).xyz.dxr == 8 * ONE);
    CHECK(mm_actor_path_count(f.clipboard) == 3);
    for (i = 0; i < 3; ++i)
    {
        const MMActorRoutePoint source = point_at(f.clipboard, i);
        CHECK(source.xyz.dxr == (100 + 4 * i) * ONE && source.dwr == (i == 2 ? 0 : 4 * ONE));
    }
    i = f.actor.runtime.state.nextEvent;
    event = event_at(&f, i);
    CHECK(event.aet == MM_ACTOR_STEP && event.rtel.irpt == 2 && event.nfrm == 3);
    CHECK(!event.rtel.dwrOffset && !event.rtel.dnfrm);
    CHECK(mm_actor_events_get_payload(f.actor.track.events, i, &scalar) && !scalar);
    CHECK(event_at(&f, i + 1).aet == MM_ACTOR_FREEZE);
    CHECK(mm_actor_events_get_payload(f.actor.track.events, i + 1, &scalar) && scalar == 1);
    dispose(&f);

    init(&f, 3);
    f.actor.runtime.state.location.dwrOffset = f.actor.runtime.state.position.dxr = 2 * ONE;
    CHECK(paste(&f, 0) && mm_actor_path_count(f.actor.track.route) == 6);
    CHECK(f.actor.runtime.state.location.irpt == 1 && !f.actor.runtime.state.location.dwrOffset);
    CHECK(point_at(f.actor.track.route, 0).dwr == 2 * ONE && point_at(f.actor.track.route, 1).xyz.dxr == 2 * ONE);
    CHECK(point_at(f.actor.track.route, 2).xyz.dxr == 6 * ONE && later_node(&f) == 4);
    dispose(&f);

    init(&f, 2);
    CHECK(paste(&f, 0) && mm_actor_path_count(f.actor.track.route) == 4 && later_node(&f) == 2 && f.tag_refs == 1);
    dispose(&f);

    init(&f, 3);
    f.zero_distance = 1;
    CHECK(paste(&f, 0) && point_at(f.actor.track.route, 0).dwr == 1);
    dispose(&f);
}

static void test_failures(void)
{
    Fixture f;
    int i;
    for (i = 0; i < 2; ++i)
    {
        init(&f, i);
        CHECK(!paste(&f, 0) && f.diagnostics == (1 << MM_ROUTE_PASTE_EMPTY));
        CHECK(!f.reserves && !f.point_inserts && f.tag_refs == 2 && mm_actor_path_count(f.actor.track.route) == 5);
        dispose(&f);
    }
    init(&f, 3);
    CHECK(mm_storage_resize_exact(&f.actor.track.route->storage, 0, 5 * MM_ACTOR_ROUTE_BYTES,
                                  MM_STORAGE_NORMAL) == MM_STORAGE_OK);
    f.fail_growth = 1;
    CHECK(!paste(&f, 0) && f.failed_allocations && !f.reserves && f.tag_refs == 2 && !f.point_inserts);
    dispose(&f);

    init(&f, 3);
    f.fail_reserve_at = 1;
    CHECK(!paste(&f, 0) && !f.point_inserts && f.tag_refs == 2);
    dispose(&f);

    init(&f, 3);
    f.fail_point_at = 1;
    f.actor.runtime.state.location.dwrOffset = 2 * ONE;
    CHECK(!paste(&f, 0) && f.actor.runtime.state.location.irpt == 1 && !f.actor.runtime.state.location.dwrOffset);
    CHECK(mm_actor_path_count(f.actor.track.route) == 5 && f.tag_refs == 2);
    dispose(&f);

    init(&f, 3);
    f.fail_point_at = 1;
    CHECK(paste(&f, 0) && f.diagnostics == (1 << MM_ROUTE_PASTE_INSERT_FAILED) && f.located == 1);
    dispose(&f);

    for (i = 1; i <= 3; ++i)
    {
        init(&f, 3);
        f.fail_event_at = i;
        CHECK(!paste(&f, 0) && f.dirty == (i > 1));
        CHECK(f.tag_refs == 1 && !f.located && !f.actor.runtime.life_dirty && !f.invalidated);
        CHECK(mm_actor_path_count(f.actor.track.route) == 5);
        dispose(&f);
    }
    init(&f, 3);
    f.fail_reserve_at = 2;
    CHECK(!paste(&f, 0) && f.tag_refs == 1 && !f.event_inserts);
    dispose(&f);

    init(&f, 3);
    f.fail_apply = 1;
    CHECK(!paste(&f, 0) && f.tag_refs == 1 && f.event_inserts == 1 && !f.located);
    dispose(&f);
}

static void test_static(void)
{
    Fixture f;
    init(&f, 3);
    CHECK(paste(&f, 1) && f.flag_queries == 1 && f.actor.runtime.state.step == -ONE && f.event_inserts == 3);
    dispose(&f);

    init(&f, 3);
    f.flags = 0;
    CHECK(paste(&f, 1) && f.flag_queries == 1 && !f.actor.runtime.state.step && f.event_inserts == 2 && f.dirty == 1);
    dispose(&f);

    init(&f, 3);
    f.fail_flags = 1;
    CHECK(!paste(&f, 1) && f.flag_queries == 1 && f.tag_refs == 1 && !f.event_inserts && !f.located);
    dispose(&f);

    init(&f, 3);
    f.fail_flags = 1;
    f.actor.runtime.state.actionEvent = -1;
    CHECK(!paste(&f, 1) && f.diagnostics == (1 << MM_ROUTE_PASTE_NO_ACTION) && f.flag_queries == 1);
    dispose(&f);
}

int main(void)
{
    test_routes();
    test_failures();
    test_static();
    puts("actor route paste C89 checks passed");
    return 0;
}

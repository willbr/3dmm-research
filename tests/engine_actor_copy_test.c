#include "mm/actor_copy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "copy check %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define ONE 65536L

typedef struct Fixture
{
    MMActorOwner source, target;
    MMActorOwnerResult result;
    MMActorCopyServices services;
    int releases, publications, template_refs, tag_refs, resolves, distances, unknown, rewinds;
    int fail_create, fail_resolve, fail_alloc, allocation_failures;
    int event_capacity, route_capacity, events_before_failure, points_before_failure;
} Fixture;

static MMActorOwner *owner(void *context, void *actor)
{
    (void)context;
    return (MMActorOwner *)actor;
}

static void *create(void *context)
{
    Fixture *f = (Fixture *)context;
    if (f->fail_create)
        return NULL;
    mm_actor_owner_init(&f->target);
    return &f->target;
}

static void *fetch_template(void *context, const MMTag *tag)
{
    Fixture *f = (Fixture *)context;
    CHECK(tag->sid == 7 && tag->number == 18);
    ++f->template_refs;
    return &f->template_refs;
}

static int action_flags(void *context, void *object, MMI32 action, MMU32 *flags)
{
    Fixture *f = (Fixture *)context;
    CHECK(object == &f->template_refs && action == 0 && f->rewinds == 1);
    *flags = 8;
    return 1;
}

static void release(void *context, void *object)
{
    Fixture *f = (Fixture *)context;
    if (object == &f->template_refs)
    {
        CHECK(--f->template_refs >= 0);
        return;
    }
    CHECK(object == &f->target && f->result.actor == object);
    CHECK(!f->source.track.events->storage.buffers[0].locks);
    if (f->fail_alloc)
    {
        CHECK(mm_actor_events_count(f->target.track.events) == f->events_before_failure);
        CHECK(mm_actor_path_count(f->target.track.route) == f->points_before_failure);
    }
    mm_actor_owner_dispose(object, &f->services.create.owner);
    ++f->releases;
}

static void retain_cache(void *cache) { ++((Fixture *)cache)->tag_refs; }
static void release_cache(void *cache) { CHECK(--((Fixture *)cache)->tag_refs >= 0); }

static const MMTagServices tags = {
    NULL, retain_cache, release_cache, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void set_recording(void *context, int enabled)
{
    Fixture *f = (Fixture *)context;
    CHECK(!enabled && !f->source.track.events->storage.buffers[0].locks);
    CHECK(f->rewinds == f->publications);
    if (f->rewinds)
    {
        CHECK(f->result.actor == &f->target && mm_actor_path_count(f->target.track.route));
        CHECK(f->target.runtime.recording.recording);
    }
    ++f->rewinds;
    f->target.runtime.recording.recording = enabled;
}

static void route_services(void *context, void *actor, MMActorRouteEditServices *s)
{
    Fixture *f = (Fixture *)context;
    CHECK(actor == &f->target);
    memset(s, 0, sizeof(*s));
    s->edits.context = f;
    s->recording = set_recording;
}

static void *resize(void *context, void *ptr, MMU32 previous, MMU32 size, int priority)
{
    Fixture *f = (Fixture *)context;
    (void)priority;
    if (f->fail_alloc && size > previous)
    {
        CHECK(f->result.actor == &f->target);
        CHECK(f->source.track.events->storage.buffers[0].locks == (f->event_capacity >= 0 ? 1U : 0U));
        ++f->allocation_failures;
        return NULL;
    }
    return realloc(ptr, size);
}

static void free_block(void *context, void *ptr, MMU32 bytes)
{
    (void)context;
    (void)bytes;
    free(ptr);
}

static void publish(void *context, void *actor)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->result.actor == actor);
    ++f->publications;
    if (!actor)
        return;
    CHECK(actor == &f->target && f->rewinds == 1);
    f->target.runtime.recording.recording = 1;
    /* Tight capacities trigger real storage failures after the factory has returned. */
    if (f->event_capacity >= 0)
    {
        CHECK(mm_storage_resize_exact(&f->target.track.events->storage, 1,
              f->event_capacity * sizeof(MMGroupLocation), MM_STORAGE_NORMAL) == MM_STORAGE_OK);
        f->fail_alloc = 1;
    }
    if (f->route_capacity >= 0)
    {
        CHECK(mm_storage_resize_exact(&f->target.track.route->storage, 0,
              f->route_capacity * sizeof(MMActorRoutePoint), MM_STORAGE_NORMAL) == MM_STORAGE_OK);
        f->fail_alloc = 1;
    }
}

static int resolve(void *context, MMActorSound *sound)
{
    Fixture *f = (Fixture *)context;
    CHECK(!f->source.track.events->storage.buffers[0].locks);
    if (++f->resolves == f->fail_resolve)
        return 0;
    sound->tag.number = 99;
    return 1;
}

static MMI32 distance(void *context, const MMActorPoint *v)
{
    Fixture *f = (Fixture *)context;
    ++f->distances;
    CHECK(!v->dyr && !v->dzr);
    return v->dxr < 0 ? -v->dxr : v->dxr;
}

static void unknown(void *context) { ++((Fixture *)context)->unknown; }

static void append(Fixture *f, int kind, int node, int frame, const void *payload, MMU32 bytes)
{
    MMActorEventTable *events = f->source.track.events;
    MMActorEvent event = {0};
    event.aet = kind;
    event.nfrm = frame;
    event.rtel.irpt = node;
    CHECK(mm_actor_events_insert(events, mm_actor_events_count(events), bytes, payload, &event));
}

static MMActorEvent event_at(const MMActorOwner *actor, MMU32 index)
{
    MMActorEvent event;
    CHECK(mm_actor_events_get_fixed(actor->track.events, index, &event));
    return event;
}

static MMActorRoutePoint point_at(const MMActorOwner *actor, MMU32 index)
{
    MMActorRoutePoint point;
    CHECK(mm_actor_path_get(actor->track.route, index, &point));
    return point;
}

static MMActorSound sound_at(const MMActorOwner *actor, MMU32 index)
{
    MMActorSound sound;
    CHECK(mm_actor_events_get_payload(actor->track.events, index, &sound));
    return sound;
}

static void set_location(Fixture *f, MMU32 index, MMI32 node, MMI32 offset, MMI32 hold)
{
    MMActorEvent event = event_at(&f->source, index);
    event.rtel.irpt = node;
    event.rtel.dwrOffset = offset;
    event.rtel.dnfrm = hold;
    CHECK(mm_actor_events_set_fixed(f->source.track.events, index, &event));
}

static void init(Fixture *f)
{
    MMActorCopyServices *s;
    MMActorState *state;
    MMActorSound sound = {0};
    MMActorRoutePoint point = {0};
    MMI32 action[2] = {0, 1}, scalar = ONE;
    unsigned char blank[48] = {0};
    int i;

    memset(f, 0, sizeof(*f));
    s = &f->services;
    s->context = f;
    s->resolve_sound = resolve;
    s->distance = distance;
    s->unknown_event = unknown;
    s->create.owner.context = f;
    s->create.owner.tags = &tags;
    s->create.owner.owner = owner;
    s->create.owner.create = create;
    s->create.owner.release = release;
    s->create.owner.release_template = release;
    s->create.owner.route_services = route_services;
    s->create.fetch_template = fetch_template;
    s->create.action_flags = action_flags;
    s->create.track_allocator.context = f;
    s->create.track_allocator.resize = resize;
    s->create.track_allocator.release = free_block;
    f->result.context = f;
    f->result.publish = publish;
    f->event_capacity = f->route_capacity = -1;

    mm_actor_owner_init(&f->source);
    f->source.runtime.template_tag.sid = 7;
    f->source.runtime.template_tag.number = 18;
    f->source.runtime.template_object = &f->template_refs;
    f->template_refs = 1;
    CHECK(mm_actor_track_create(&f->source.track, NULL, 0, &tags));
    for (i = 0; i < 4; ++i)
    {
        point.xyz.dxr = i * 10 * ONE;
        point.dwr = i == 1 || i == 3 ? 0 : 10 * ONE;
        CHECK(mm_actor_path_insert(f->source.track.route, i, &point));
    }
    append(f, MM_ACTOR_ADD, 0, 1, blank, 20);
    append(f, MM_ACTOR_ACTION, 0, 1, action, sizeof(action));
    sound.tag.cache = f;
    sound.tag.number = 10;
    sound.cel = -1;
    append(f, MM_ACTOR_SOUND, 0, 2, &sound, sizeof(sound));
    append(f, MM_ACTOR_TWEAK, 0, 2, blank, 12);
    append(f, MM_ACTOR_ROTATE_HERE, 0, 3, blank, 48);
    sound.cel = 2;
    append(f, MM_ACTOR_SOUND, 0, 3, &sound, sizeof(sound));
    f->tag_refs = 2;
    append(f, MM_ACTOR_REMOVE, 0, 3, NULL, 0);
    append(f, MM_ACTOR_STEP, 0, 4, &scalar, sizeof(scalar));
    set_location(f, 7, 0, 7 * ONE, 1);
    append(f, MM_ACTOR_ADD, 2, 8, blank, 20);
    append(f, MM_ACTOR_ACTION, 2, 8, action, sizeof(action));
    append(f, MM_ACTOR_STEP, 3, 9, &scalar, sizeof(scalar));
    state = &f->source.runtime.state;
    state->cel = 3;
    state->addEvent = 0;
    state->nextEvent = 7;
    state->firstEventInFrame = 4;
    state->actionEvent = 1;
    state->location.dwrOffset = 5 * ONE;
    state->location.dnfrm = 2;
}

static void dispose(Fixture *f)
{
    if (f->result.actor)
        release(f, f->result.actor);
    mm_actor_owner_dispose(&f->source, &f->services.create.owner);
    CHECK(!f->template_refs && !f->tag_refs);
}

static int full(Fixture *f, int entire, int postponed)
{
    return mm_actor_copy(&f->source, entire, postponed, &f->result, &f->services);
}

static int path(Fixture *f, int entire)
{
    return mm_actor_copy_route(&f->source, entire, &f->result, &f->services);
}

static void rewound(const Fixture *f)
{
    CHECK(f->result.actor == &f->target && f->rewinds == 2 && f->publications == 1);
    CHECK(f->target.runtime.state.location.dnfrm == -1 && f->target.runtime.state.actionFlags == 8);
    CHECK(f->target.runtime.state.position.dxr == point_at(&f->target, 0).xyz.dxr);
    CHECK(!f->target.runtime.state.nextEvent && !f->target.runtime.recording.recording);
}

static void failed(Fixture *f)
{
    CHECK(!f->result.actor && f->releases == 1 && f->publications == 2 && f->rewinds == 1);
    CHECK(f->template_refs == 1 && f->tag_refs == 2 && !f->source.track.events->storage.buffers[0].locks);
    if (f->fail_alloc)
        CHECK(f->allocation_failures);
    dispose(f);
}

static void test_full(void)
{
    Fixture f;
    MMI32 action[2];
    MMActorEvent event;
    int hold;

    init(&f);
    CHECK(full(&f, 0, 0));
    rewound(&f);
    CHECK(mm_actor_events_count(f.target.track.events) == 5 && mm_actor_path_count(f.target.track.route) == 2);
    CHECK(!point_at(&f.target, 0).xyz.dxr && point_at(&f.target, 0).dwr == 5 * ONE);
    CHECK(f.resolves == 2 && sound_at(&f.source, 2).tag.number == 99 && sound_at(&f.source, 5).tag.number == 99);
    CHECK(f.tag_refs == 3 && f.template_refs == 2 && sound_at(&f.target, 3).cel == 2);
    CHECK(mm_actor_events_get_payload(f.target.track.events, 1, action) && action[1] == 3);
    CHECK(mm_actor_events_get_payload(f.source.track.events, 1, action) && action[1] == 1);
    event = event_at(&f.target, 0);
    CHECK(!event.rtel.irpt && !event.rtel.dnfrm && event.nfrm == 1);
    event = event_at(&f.target, 4);
    CHECK(event.rtel.dwrOffset == 2 * ONE && event.rtel.dnfrm == 1);
    dispose(&f);

    init(&f);
    CHECK(full(&f, 1, 0) && mm_actor_path_count(f.target.track.route) == 4);
    CHECK(mm_actor_events_count(f.target.track.events) == 8);
    CHECK(mm_actor_events_get_payload(f.target.track.events, 6, action) && action[1] == 1);
    CHECK(event_at(&f.target, 5).rtel.irpt == 2);
    dispose(&f);

    init(&f);
    CHECK(full(&f, 0, 1) && mm_actor_events_count(f.target.track.events) == 4);
    dispose(&f);

    for (hold = 1; hold <= 4; hold += 3)
    {
        init(&f);
        set_location(&f, 7, 0, 5 * ONE, hold);
        CHECK(full(&f, 0, 0));
        event = event_at(&f.target, 4);
        CHECK(!event.rtel.dwrOffset && event.rtel.dnfrm == (hold == 1 ? 0 : 2));
        dispose(&f);
    }

    init(&f);
    f.source.runtime.state.location.dwrOffset = 10 * ONE;
    CHECK(full(&f, 0, 0) && !event_at(&f.target, 4).rtel.dwrOffset);
    dispose(&f);

    init(&f);
    f.source.runtime.state.nextEvent = f.source.runtime.state.addEvent = 0;
    CHECK(full(&f, 0, 0) && mm_actor_events_count(f.target.track.events) == 8);
    dispose(&f);

    init(&f);
    event = event_at(&f.source, 3);
    event.aet = 99;
    CHECK(mm_actor_events_set_fixed(f.source.track.events, 3, &event));
    CHECK(full(&f, 0, 0) && f.unknown == 1);
    dispose(&f);

    init(&f);
    f.source.runtime.state.location.irpt = 1;
    f.source.runtime.state.location.dwrOffset = 0;
    set_location(&f, 7, 1, 0, 1);
    CHECK(full(&f, 0, 0) && mm_actor_path_count(f.target.track.route) == 1);
    dispose(&f);

    init(&f);
    f.source.runtime.state.location.irpt = 3;
    f.source.runtime.state.location.dwrOffset = 0;
    f.source.runtime.state.addEvent = 8;
    f.source.runtime.state.nextEvent = 11;
    CHECK(full(&f, 0, 0) && mm_actor_path_count(f.target.track.route) == 1);
    CHECK(!point_at(&f.target, 0).dwr && !f.resolves);
    dispose(&f);
}

static void test_merge(void)
{
    Fixture f;
    MMActorCostume costume = {0};
    init(&f);
    costume.tag.cache = &f;
    costume.tag.number = 31;
    append(&f, MM_ACTOR_COSTUME, 3, 9, &costume, sizeof(costume));
    costume.tag.number = 32;
    append(&f, MM_ACTOR_COSTUME, 3, 9, &costume, sizeof(costume));
    f.tag_refs += 2;
    f.source.runtime.state.addEvent = 11;
    f.source.runtime.state.nextEvent = 13;
    f.source.runtime.state.location.irpt = 3;
    f.source.runtime.state.location.dwrOffset = 0;
    CHECK(full(&f, 0, 0) && mm_actor_events_count(f.target.track.events) == 1);
    CHECK(mm_actor_events_get_payload(f.target.track.events, 0, &costume));
    CHECK(costume.tag.number == 32 && f.tag_refs == 5);
    dispose(&f);
}

static void test_route(void)
{
    Fixture f;
    init(&f);
    CHECK(path(&f, 0));
    rewound(&f);
    CHECK(mm_actor_path_count(f.target.track.route) == 2 && !mm_actor_events_count(f.target.track.events));
    CHECK(point_at(&f.target, 0).xyz.dxr == 5 * ONE && point_at(&f.target, 0).dwr == 5 * ONE);
    CHECK(!f.resolves && f.distances == 1);
    dispose(&f);

    init(&f);
    CHECK(path(&f, 1) && mm_actor_path_count(f.target.track.route) == 4);
    dispose(&f);

    init(&f);
    f.source.runtime.state.location.dwrOffset = 0;
    CHECK(path(&f, 0) && !f.distances && point_at(&f.target, 0).dwr == 10 * ONE);
    dispose(&f);

    init(&f);
    f.source.runtime.state.location.irpt = 1;
    f.source.runtime.state.location.dwrOffset = 0;
    CHECK(path(&f, 0) && mm_actor_path_count(f.target.track.route) == 1);
    dispose(&f);

    init(&f);
    f.source.runtime.state.location.dwrOffset = 10 * ONE;
    CHECK(path(&f, 0) && point_at(&f.target, 0).dwr == 1);
    dispose(&f);
}

static void test_failures(void)
{
    Fixture f;
    int i;
    for (i = 0; i < 5; ++i)
    {
        init(&f);
        f.event_capacity = f.events_before_failure = i;
        CHECK(!full(&f, 0, 0));
        failed(&f);
    }
    for (i = 1; i <= 2; ++i)
    {
        init(&f);
        f.fail_resolve = i;
        CHECK(!full(&f, 0, 0));
        CHECK(sound_at(&f.source, 2).tag.number == (i == 1 ? 10U : 99U));
        CHECK(sound_at(&f.source, 5).tag.number == 10U);
        failed(&f);
    }
    for (i = 0; i < 2; ++i)
    {
        init(&f);
        f.route_capacity = f.points_before_failure = i;
        f.events_before_failure = 5;
        CHECK(!full(&f, 0, 0));
        CHECK(sound_at(&f.source, 2).tag.number == 99 && sound_at(&f.source, 5).tag.number == 99);
        failed(&f);

        init(&f);
        f.route_capacity = f.points_before_failure = i;
        CHECK(!path(&f, 0));
        failed(&f);
    }
    for (i = 0; i < 2; ++i)
    {
        init(&f);
        f.fail_create = 1;
        CHECK(!(i ? path(&f, 0) : full(&f, 0, 0)));
        CHECK(!f.result.actor && !f.releases && f.publications == 1 && f.template_refs == 1);
        dispose(&f);
    }
}

int main(void)
{
    test_full();
    test_merge();
    test_route();
    test_failures();
    puts("actor copy C89 checks passed");
    return 0;
}

#include "mm/actor_effect.h"
#include "mm/actor_paste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "paste check at %d: %s\n", __LINE__, #c); exit(1); } } while (0)

typedef struct Fixture
{
    MMActorOwner actor;
    MMActorPasteServices services;
    int old_template, new_template, body, source_refs, target_refs, scene;
    int fail, same_cache, text, new_text, old_refs, new_refs, released;
    int ensured, saved, fetched, bound, placed, dirty, exists, sounds, children, yaw, sound_updates;
    int diagnostics[4];
} Fixture;

static int is_text(void *context, void *object)
{
    Fixture *f = (Fixture *)context;
    CHECK(object == &f->old_template || object == &f->new_template);
    return object == &f->old_template ? f->text : f->new_text;
}

static int ensure(void *context, void *scene, void **cache)
{
    Fixture *f = (Fixture *)context;
    CHECK(scene == &f->scene && !f->bound && f->actor.runtime.first == 10);
    ++f->ensured;
    *cache = f->same_cache ? &f->source_refs : &f->target_refs;
    return f->fail != 1;
}

static void retain_cache(void *cache) { ++*(int *)cache; }
static void release_cache(void *cache) { CHECK(--*(int *)cache >= 0); }

static const MMTagServices tags = {
    NULL, NULL, retain_cache, release_cache, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static MMActorOwner *owner(void *context, void *actor)
{
    (void)context;
    return (MMActorOwner *)actor;
}

static void redirect_tag(MMTag *tag, void *cache, MMU32 number)
{
    retain_cache(cache);
    release_cache(tag->cache);
    tag->cache = cache;
    tag->number = number;
}

static int save_template(void *context, MMTag *tag, void *cache)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->source_refs == 2 && !f->target_refs && cache == &f->target_refs);
    ++f->saved;
    if (f->fail == 2)
        return 0;
    redirect_tag(tag, cache, 77);
    return 1;
}

static void *fetch_template(void *context, const MMTag *tag)
{
    Fixture *f = (Fixture *)context;
    CHECK(tag->cache == &f->target_refs && tag->number == 77);
    CHECK(f->old_refs == 1 && f->source_refs == 1 && f->target_refs == 1);
    ++f->fetched;
    if (f->fail == 3)
        return NULL;
    ++f->new_refs;
    return &f->new_template;
}

static void release(void *context, void *object)
{
    Fixture *f = (Fixture *)context;
    const MMActorRuntime *r = &f->actor.runtime;
    if (object == &f->body)
    {
        CHECK(r->body == object && r->scene == &f->scene);
        return;
    }
    CHECK(r->template_object == object);
    if (object == &f->old_template)
    {
        if (r->template_tag.cache)
        {
            CHECK(r->template_tag.cache == &f->target_refs);
            CHECK(!f->source_refs && f->new_refs == 1 && !f->bound);
            ++f->released;
        }
        CHECK(--f->old_refs == 0);
    }
    else
    {
        CHECK(object == &f->new_template);
        CHECK(--f->new_refs == 0);
    }
}

static void *create_body(void *context, void *object)
{
    Fixture *f = (Fixture *)context;
    const MMActorRuntime *r = &f->actor.runtime;
    CHECK(r->template_object == object && r->scene == &f->scene && !r->body && r->life_dirty);
    CHECK(r->state.frame == mm_fixed_sub(r->first, 1));
    return &f->body;
}

static void bind_body(void *context, void *body, void *scene)
{
    Fixture *f = (Fixture *)context;
    CHECK(scene == &f->scene && body == &f->body && f->actor.runtime.body == body);
    ++f->bound;
}

static void placement(void *context, MMActorPoint *point)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->bound == 1 && f->actor.runtime.body == &f->body);
    ++f->placed;
    point->dxr = 100;
    point->dyr = 200;
    point->dzr = -300;
}

static int sound_exists(void *context, const MMTag *tag)
{
    Fixture *f = (Fixture *)context;
    CHECK(tag->cache == &f->source_refs);
    ++f->exists;
    return tag->number != 2;
}

static int save_sound(void *context, void *scene, MMTag *tag)
{
    Fixture *f = (Fixture *)context;
    CHECK(scene == &f->scene);
    ++f->sounds;
    if (tag->number == 3)
        return 0;
    redirect_tag(tag, &f->target_refs, tag->number + 100);
    return 1;
}

static int sound_child(void *context, void *scene, MMU32 number, MMU32 *child)
{
    Fixture *f = (Fixture *)context;
    CHECK(scene == &f->scene && number >= 100);
    ++f->children;
    *child = number + 1000;
    return number != 104;
}

static int scene_frame(void *context, MMI32 *frame)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->actor.runtime.scene == &f->scene);
    *frame = 21;
    return 1;
}

static void update_sound_frame(void *context)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->source_refs == 3 && f->actor.runtime.state.nextEvent == 4);
    CHECK(mm_actor_events_count(f->actor.track.events) == 4);
    ++f->sound_updates;
}

static unsigned short camera_yaw(void *context, void *scene)
{
    Fixture *f = (Fixture *)context;
    CHECK(scene == &f->scene);
    ++f->yaw;
    return 0x1234;
}

static void dirty(void *context, void *scene)
{
    Fixture *f = (Fixture *)context;
    CHECK(scene == &f->scene && f->placed);
    ++f->dirty;
}

static void diagnostic(void *context, int which)
{
    Fixture *f = (Fixture *)context;
    CHECK(which >= 0 && which < 4);
    ++f->diagnostics[which];
}

static void append(Fixture *f, MMI32 kind, MMI32 frame, const void *payload, MMU32 bytes)
{
    MMActorEventTable *events = f->actor.track.events;
    MMActorEvent event = {0};
    event.aet = kind;
    event.nfrm = frame;
    CHECK(mm_actor_events_insert(events, mm_actor_events_count(events), bytes, payload, &event));
}

static void init(Fixture *f)
{
    MMActorPasteServices *s;
    MMActorRuntime *r;
    MMActorRoutePoint point = {{1, 2, 3}, 0};
    MMActorEntrance entrance = {0};

    memset(f, 0, sizeof(*f));
    s = &f->services;
    s->context = s->edits.context = s->create.owner.context = f;
    s->create.owner.tags = &tags;
    s->create.owner.owner = owner;
    s->create.owner.release = release;
    s->create.fetch_template = fetch_template;
    s->create.create_body = create_body;
    s->create.bind_body = bind_body;
    s->edits.scene_frame = scene_frame;
    s->edits.update_sound_frame = update_sound_frame;
    s->is_text = is_text;
    s->ensure_autosave = ensure;
    s->save_template = save_template;
    s->placement = placement;
    s->sound_exists = sound_exists;
    s->save_sound = save_sound;
    s->sound_child = sound_child;
    s->camera_yaw = camera_yaw;
    s->dirty = dirty;
    s->diagnostic = diagnostic;

    mm_actor_owner_init(&f->actor);
    CHECK(mm_actor_track_create(&f->actor.track, NULL, 0, &tags));
    f->source_refs = f->old_refs = 1;
    r = &f->actor.runtime;
    r->template_object = &f->old_template;
    r->template_tag.sid = 0;
    r->template_tag.cache = &f->source_refs;
    r->first = 10;
    r->last = 99;
    r->state.frame = 33;
    r->state.routeTranslation.dxr = 999;
    r->state.subrouteTranslation.dxr = 555;
    r->origin.dxr = 777;
    CHECK(mm_actor_path_insert(f->actor.track.route, 0, &point));
    entrance.translation.dxr = 4;
    entrance.translation.dyr = 5;
    entrance.translation.dzr = 6;
    entrance.y = 321;
    entrance.reserved = 0xabcd;
    append(f, MM_ACTOR_ADD, 10, &entrance, sizeof(entrance));
}

static void dispose(Fixture *f)
{
    mm_actor_owner_dispose(&f->actor, &f->services.create.owner);
    CHECK(!f->source_refs && !f->target_refs && !f->old_refs && !f->new_refs);
}

static void add_sound(Fixture *f, MMU32 number, MMI32 sid)
{
    MMActorSound sound = {0};
    sound.tag.sid = sid;
    sound.tag.number = number;
    sound.child = 55;
    if (!sid)
    {
        sound.tag.cache = &f->source_refs;
        retain_cache(sound.tag.cache);
    }
    append(f, MM_ACTOR_SOUND, 10 + mm_actor_events_count(f->actor.track.events), &sound, sizeof(sound));
}

static int paste(Fixture *f, MMI32 frame, int migrate)
{
    return mm_actor_paste(&f->actor, frame, &f->scene, migrate, &f->services);
}

static MMActorEvent event_at(Fixture *f, MMU32 index)
{
    MMActorEvent event;
    CHECK(mm_actor_events_get_fixed(f->actor.track.events, index, &event));
    return event;
}

static MMActorEntrance entrance_at(Fixture *f)
{
    MMActorEntrance entrance;
    CHECK(mm_actor_events_get_payload(f->actor.track.events, 0, &entrance));
    return entrance;
}

static MMActorSound sound_at(Fixture *f, MMU32 index)
{
    MMActorSound sound;
    CHECK(mm_actor_events_get_payload(f->actor.track.events, index, &sound));
    return sound;
}

static void positioned(const MMActorRuntime *r)
{
    CHECK(r->origin.dxr == 95 && r->origin.dyr == 193 && r->origin.dzr == -309);
    CHECK(!r->state.subrouteTranslation.dxr && !r->state.subrouteTranslation.dyr && !r->state.subrouteTranslation.dzr);
    CHECK(r->last == 99);
}

static void test_templates(void)
{
    Fixture f;
    int fail, mode;
    for (mode = 0; mode < 16; ++mode)
    {
        int migrate = mode & 1;
        int transfer;
        init(&f);
        f.same_cache = (mode >> 1) & 1;
        f.text = (mode >> 2) & 1;
        f.new_text = (mode >> 3) & 1;
        transfer = migrate && f.text && !f.same_cache;
        CHECK(paste(&f, 20, migrate));
        positioned(&f.actor.runtime);
        CHECK(f.actor.runtime.first == 20 && f.actor.runtime.state.frame == 19 && event_at(&f, 0).nfrm == 20);
        CHECK(f.actor.runtime.state.routeTranslation.dxr == 95 && f.actor.runtime.state.routeTranslation.dyr == 193);
        CHECK(f.actor.runtime.state.routeTranslation.dzr == -309);
        CHECK(f.ensured == (migrate && f.text) && f.saved == transfer && f.fetched == transfer && f.released == transfer);
        CHECK(f.source_refs == !transfer && f.target_refs == transfer);
        CHECK(f.old_refs == !transfer && f.new_refs == transfer);
        CHECK(f.yaw == (transfer ? f.new_text : f.text));
        CHECK(entrance_at(&f).y == (f.yaw ? 0x1234 : 321));
        CHECK(entrance_at(&f).reserved == 0xabcd && f.dirty == 1);
        dispose(&f);
    }

    for (fail = 1; fail <= 3; ++fail)
    {
        init(&f);
        f.text = 1;
        f.fail = fail;
        CHECK(!paste(&f, 20, 1) && !f.bound && !f.placed && !f.dirty);
        CHECK(f.actor.runtime.first == 10 && f.actor.runtime.state.frame == 33);
        CHECK(f.source_refs == 1 && !f.target_refs && f.old_refs == 1 && !f.new_refs);
        CHECK(f.actor.runtime.template_object == &f.old_template && f.actor.runtime.template_tag.cache == &f.source_refs);
        dispose(&f);
    }
}

static void test_frames(void)
{
    Fixture f;
    MMActorEvent event;

    init(&f);
    event = event_at(&f, 0);
    event.aet = MM_ACTOR_REMOVE;
    CHECK(mm_actor_events_set_fixed(f.actor.track.events, 0, &event));
    CHECK(!paste(&f, 20, 1) && f.bound == 1 && f.placed == 1 && !f.dirty);
    CHECK(f.actor.runtime.first == 20 && f.actor.runtime.state.frame == 19 && f.actor.runtime.origin.dxr == 777);
    CHECK(event_at(&f, 0).nfrm == 10);
    dispose(&f);

    init(&f);
    CHECK(mm_actor_events_delete(f.actor.track.events, 0));
    CHECK(!paste(&f, 20, 1) && f.diagnostics[MM_PASTE_EMPTY] == 1 && f.bound && !f.dirty);
    dispose(&f);

    init(&f);
    CHECK(paste(&f, 10, 1) && event_at(&f, 0).nfrm == 10 && f.dirty == 1);
    dispose(&f);

    init(&f);
    event = event_at(&f, 0);
    event.nfrm = (-2147483647L - 1L);
    CHECK(mm_actor_events_set_fixed(f.actor.track.events, 0, &event));
    CHECK(paste(&f, 2147483647L, 1) && event_at(&f, 0).nfrm == 2147483647L);
    dispose(&f);

    init(&f);
    CHECK(paste(&f, (-2147483647L - 1L), 1) && f.actor.runtime.state.frame == 2147483647L);
    dispose(&f);
}

static void test_sounds(void)
{
    Fixture f;
    const MMActorState *state;
    MMActorSound sound;

    init(&f);
    add_sound(&f, 2, 0);
    add_sound(&f, 3, 0);
    add_sound(&f, 1, 0);
    add_sound(&f, 8, 7);
    f.actor.runtime.state.nextEvent = 5;
    f.actor.runtime.state.firstEventInFrame = 3;
    CHECK(paste(&f, 20, 1));
    positioned(&f.actor.runtime);
    CHECK(mm_actor_events_count(f.actor.track.events) == 3 && f.sound_updates == 1);
    CHECK(f.exists == 3 && f.sounds == 2 && f.children == 1);
    CHECK(f.diagnostics[MM_PASTE_MISSING_SOUND] == 1 && f.diagnostics[MM_PASTE_SAVE_SOUND_FAILED] == 1);
    state = &f.actor.runtime.state;
    CHECK(state->nextEvent == 3 && state->firstEventInFrame == 1 && state->routeTranslation.dxr == 95);
    CHECK(event_at(&f, 1).nfrm == 23 && event_at(&f, 2).nfrm == 24);
    sound = sound_at(&f, 1);
    CHECK(sound.tag.number == 101 && sound.tag.cache == &f.target_refs && sound.child == 1101);
    CHECK(sound_at(&f, 2).tag.number == 8 && f.source_refs == 1 && f.target_refs == 1);
    dispose(&f);

    /* Child failure leaves the retimed row with its original tag and the tail untouched. */
    init(&f);
    f.text = f.same_cache = 1;
    add_sound(&f, 1, 0);
    add_sound(&f, 4, 0);
    add_sound(&f, 1, 0);
    CHECK(!paste(&f, 20, 1) && !f.dirty && !f.yaw && !f.sound_updates);
    positioned(&f.actor.runtime);
    CHECK(event_at(&f, 1).nfrm == 21 && event_at(&f, 2).nfrm == 22 && event_at(&f, 3).nfrm == 13);
    CHECK(sound_at(&f, 1).tag.number == 101 && sound_at(&f, 2).tag.number == 4);
    sound = sound_at(&f, 2);
    CHECK(sound.tag.cache == &f.source_refs && sound.child == 55);
    CHECK(f.actor.runtime.state.routeTranslation.dxr == 999 && entrance_at(&f).y == 321);
    CHECK(f.source_refs == 2 && f.target_refs == 2);
    /* Repair the test's synthetic references after observing the legacy partial failure. */
    retain_cache(&f.source_refs);
    release_cache(&f.target_refs);
    dispose(&f);
}

int main(void)
{
    test_templates();
    test_frames();
    test_sounds();
    puts("C89 actor paste checks passed");
    return 0;
}

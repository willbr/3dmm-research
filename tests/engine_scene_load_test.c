#include "mm/scene_load.h"
#define SCENE_SOUND_FIXTURE_NO_PACKET_HELPERS
#include "scene_sound_fixture.h"
typedef union Payload {
    void *align;
    unsigned char bytes[160];
} Payload;
typedef struct LoadEvent
{
    unsigned char fixed[8];
    int bytes;
    Payload payload;
} LoadEvent;
typedef struct LoadObject
{
    int type, count, refs;
    LoadEvent events[8];
} LoadObject;
typedef struct Loader
{
    Fixture packets;
    MMSceneLoadServices s;
    MMSceneOwner owner;
    MMSceneState metadata;
    LoadObject wire[2], *scene;
    int attempt, fail, live, swap, names, published, log[512], used, locks;
    int actor_calls, resolve_calls, actor_fail, resolve_fail, no_thumbnail, bad_kind;
} Loader;
static void note(Loader *f, int value)
{
    CHECK(f->used < 512);
    f->log[f->used++] = value;
}
static int step(Loader *f)
{
    return ++f->attempt != f->fail;
}
static void *new_object(Loader *f, int type)
{
    LoadObject *p;
    if (!step(f))
        return NULL;
    p = (LoadObject *)calloc(1, sizeof(*p));
    CHECK(p);
    p->type = type;
    p->refs = 1;
    ++f->live;
    return p;
}
static void *new_scene(void *p, void *movie)
{
    Loader *f = (Loader *)p;
    f->scene = (LoadObject *)new_object(f, 200);
    if (f->scene)
        mm_scene_owner_init(&f->owner, movie);
    return f->scene;
}
static MMSceneOwner *owner_state(void *p, void *scene)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene);
    return &f->owner;
}
static void *new_slot(void *p, int type)
{
    return new_object((Loader *)p, type);
}
static void publish_owner(void *p, void *scene, const MMSceneOwner *o)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene && o == &f->owner);
}
static void reset(void *p, void *scene)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene);
    note(f, 1);
}
static void release_object(void *p, void *value)
{
    Loader *f = (Loader *)p;
    LoadObject *o = (LoadObject *)value;
    CHECK(o && o->refs == 1);
    note(f, 100 + o->type);
    if (o == f->scene)
        mm_scene_owner_dispose(&f->owner, o, &f->s.owners);
    o->refs = 0;
    --f->live;
    if (o->type < 100 || o->type >= 200)
        free(o);
}
static MMI32 group_count(void *p, void *g)
{
    (void)p;
    return ((LoadObject *)g)->count;
}
static MMI32 group_kind(void *p, void *g, MMI32 i)
{
    MMI32 kind;
    (void)p;
    memcpy(&kind, ((LoadObject *)g)->events[i].fixed + 4, 4);
    return kind;
}
static void *group_payload(void *p, void *g, MMI32 i, MMI32 *bytes)
{
    LoadEvent *e = &((LoadObject *)g)->events[i];
    (void)p;
    *bytes = e->bytes;
    return e->payload.bytes;
}
static void *group_object(void *p, void *g, MMI32 i)
{
    void *object;
    (void)p;
    memcpy(&object, ((LoadObject *)g)->events[i].payload.bytes, sizeof(object));
    return object;
}
static MMI32 sound_count(void *p, void *sound)
{
    (void)p;
    return ((MMSceneSound *)sound)->ctagc;
}
static MMI32 sound_style(void *p, void *sound)
{
    (void)p;
    return ((MMSceneSound *)sound)->sty;
}
static int opened(void *p, void *sound, MMI32 i)
{
    (void)p;
    return at((MMSceneSound *)sound, i)->tag.cache != NULL;
}
static void close_tag(void *p, void *sound, MMI32 i)
{
    Loader *f = (Loader *)p;
    Record *r = at((MMSceneSound *)sound, i);
    note(f, 10 + (int)r->child);
    if (!r->tag.sid)
    {
        CHECK(r->tag.cache == &f->packets && f->packets.refs[r->tag.number] > 0);
        --f->packets.refs[r->tag.number];
        r->tag.cache = NULL;
    }
}
static void free_sound(void *p, void *sound)
{
    Loader *f = (Loader *)p;
    note(f, 20);
    raw_free(&f->packets, sound);
}
static void diagnostic(void *p, int issue)
{
    (void)p;
    (void)issue;
    CHECK(0);
}
static int metadata(void *p, MMU32 number, MMSceneState *out)
{
    Loader *f = (Loader *)p;
    CHECK(number == 42 && out->name.unit == 1);
    if (!step(f))
        return 0;
    out->firstFrame = 3;
    out->lastFrame = 19;
    out->transition = 3;
    return 1;
}
static void publish_metadata(void *p, void *scene, const MMSceneState *value)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene && f->owner.owned[2] && f->owner.owned[3]);
    CHECK(value->currentFrame == 2);
    f->metadata = *value;
    f->published = 1;
    note(f, 2);
}
static int child(void *p, MMU32 scene, int kind, MMU32 id, MMU32 *number)
{
    Loader *f = (Loader *)p;
    CHECK(scene == 42 && f->published);
    if (!step(f) || (kind == MM_SCENE_THUMBNAIL && f->no_thumbnail))
        return 0;
    if (kind == MM_SCENE_FRAMES)
        CHECK(!id);
    else if (kind == MM_SCENE_STARTS)
        CHECK(id == 1);
    else if (kind == MM_SCENE_ACTORS || kind == MM_SCENE_TEXTS)
        CHECK(id == 7);
    *number = (MMU32)kind + 100;
    return 1;
}
static void *thumbnail(void *p, MMU32 number)
{
    CHECK(number == 106);
    return new_object((Loader *)p, 6);
}
static void *read_group(void *p, MMU32 number, int start, int *swap)
{
    Loader *f = (Loader *)p;
    LoadObject *g = &f->wire[start];
    CHECK(number == (MMU32)(100 + start));
    if (!step(f))
        return NULL;
    CHECK(!g->refs);
    g->refs = 1;
    ++f->live;
    *swap = f->swap;
    return g;
}
static void fixed(void *p, void *g, MMI32 i, unsigned char *bytes)
{
    (void)p;
    memcpy(bytes, ((LoadObject *)g)->events[i].fixed, 8);
}
static void *read_object(void *p, MMU32 number, int kind, void *scene)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene);
    CHECK(number == (kind == MM_SCENE_ADD_ACTOR ? 102U : 103U));
    return new_object(f, 300 + kind);
}
static int insert(void *p, void *g, MMI32 i, MMI32 frame, int kind, MMI32 bytes, const void *payload)
{
    Loader *f = (Loader *)p;
    LoadObject *group = (LoadObject *)g;
    LoadEvent *e;
    CHECK(i == group->count && i < 8 && bytes >= 0 && bytes <= 160);
    CHECK(frame == 3);
    if (!step(f))
        return 0;
    e = &group->events[i];
    memcpy(e->fixed, &frame, 4);
    memcpy(e->fixed + 4, &kind, 4);
    e->bytes = bytes;
    memcpy(e->payload.bytes, payload, (size_t)bytes);
    ++group->count;
    return 1;
}
static int insert_tag(void *p, void *g, MMI32 i, MMI32 frame, const MMTag *tag)
{
    CHECK(tag->sid == 1 && !tag->cache && tag->type == 23 && tag->number == 9);
    return insert(p, g, i, frame, MM_SCENE_SET_BACKGROUND, sizeof(*tag), tag);
}
static int open_tag(void *p, MMSceneSound *sound, MMI32 i)
{
    Loader *f = (Loader *)p;
    Record *r = at(sound, i);
    if (!step(f))
        return 0;
    CHECK(r->child == (MMU32)i + 1 && !r->tag.cache);
    if (!r->tag.sid)
    {
        r->tag.cache = &f->packets;
        ++f->packets.refs[r->tag.number];
    }
    note(f, 30 + i);
    return 1;
}
static void load_name(void *p, void *scene, MMU32 number)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene && number == 42 && ((LoadObject *)f->owner.owned[0])->count == 3 &&
          ((LoadObject *)f->owner.owned[1])->count == 4);
    ++f->names;
    note(f, 3);
}
static void *packet_allocate(void *p, MMI32 bytes)
{
    Loader *f = (Loader *)p;
    if (!step(f))
        return NULL;
    return allocate(&f->packets, bytes);
}
static void wire_word(void *p, MMU32 value, int swap)
{
    unsigned char *b = (unsigned char *)p, native[4];
    unsigned i;
    memcpy(native, &value, 4);
    for (i = 0; i < 4; ++i)
        b[i] = native[swap ? 3 - i : i];
}
static void wire_event(Loader *f, int start, int kind, const MMU32 *words, int count)
{
    LoadObject *g = &f->wire[start];
    LoadEvent *e = &g->events[g->count++];
    int i;
    wire_word(e->fixed, 3, f->swap);
    wire_word(e->fixed + 4, (MMU32)kind, f->swap);
    e->bytes = count * 4;
    for (i = 0; i < count; ++i)
        wire_word(e->payload.bytes + i * 4, words[i], f->swap);
}
static void init_loader(Loader *f, int swap)
{
    MMSceneOwnerServices *o;
    MMSceneLoadServices *s;
    MMU32 sound[] = {80, 2, 0, 3, 1, 0, 0, 23, 1, 2, 3, 0, 23, 2, 3, 0, 0, 23, 3}, camera[] = {0x12345678U},
               pause[] = {3, 230}, background[] = {1, 999, 23, 9}, id[] = {7};
    memset(f, 0, sizeof(*f));
    init(&f->packets);
    f->swap = swap;
    f->wire[0].type = 100;
    f->wire[1].type = 101;
    wire_event(f, 0, 1, sound, 19);
    wire_event(f, 0, 3, camera, 1);
    wire_event(f, 0, 5, pause, 2);
    wire_event(f, 1, 4, background, 4);
    wire_event(f, 1, 0, id, 1);
    wire_event(f, 1, 2, id, 1);
    wire_event(f, 1, 3, camera, 1);
    s = &f->s;
    s->context = f;
    s->packets = f->packets.s;
    s->packets.allocate = packet_allocate;
    s->metadata = metadata;
    s->publish_metadata = publish_metadata;
    s->child = child;
    s->thumbnail = thumbnail;
    s->read_group = read_group;
    s->fixed = fixed;
    s->object = read_object;
    s->insert = insert;
    s->insert_tag = insert_tag;
    s->open_tag = open_tag;
    s->load_name = load_name;
    o = &s->owners;
    o->context = f;
    o->pointer_bytes = sizeof(void *);
    o->sound_header_bytes = 16;
    o->sound_tag_bytes = sizeof(Record);
    o->create_scene = new_scene;
    o->create_slot = new_slot;
    o->state = owner_state;
    o->publish = publish_owner;
    o->reset_cursor = reset;
    o->release = release_object;
    o->count = group_count;
    o->kind = group_kind;
    o->data = group_payload;
    o->object = group_object;
    o->diagnostic = diagnostic;
    o->sound.context = f;
    o->sound.count = sound_count;
    o->sound.style = sound_style;
    o->sound.open = opened;
    o->sound.close = close_tag;
    o->sound.free = free_sound;
    o->sound.diagnostic = diagnostic;
}
static void check_clean(Loader *f)
{
    CHECK(!f->live);
    CHECK(!f->wire[0].refs && !f->wire[1].refs);
    clean(&f->packets);
}
static void test_load(void)
{
    Loader f;
    LoadObject *scene, *frames;
    int swap, fail, attempts, j, last;
    for (swap = 0; swap < 2; ++swap)
    {
        init_loader(&f, swap);
        scene = (LoadObject *)mm_scene_load(&f, 42, 1, &f.s);
        CHECK(scene && f.names == 1);
        attempts = f.attempt;
        frames = (LoadObject *)f.owner.owned[0];
        CHECK(frames->events[0].bytes == (int)(16 + 3 * sizeof(Record)));
        CHECK(!memcmp(frames->events[1].payload.bytes, f.wire[0].events[1].payload.bytes, 4));
        CHECK(!memcmp(frames->events[2].payload.bytes, f.wire[0].events[2].payload.bytes, 8));
        CHECK(f.packets.refs[1] == 1 && f.packets.refs[3] == 1 && !f.packets.refs[2]);
        release_object(&f, scene);
        check_clean(&f);
        for (fail = 1; fail <= attempts; ++fail)
        {
            init_loader(&f, swap);
            f.fail = fail;
            scene = (LoadObject *)mm_scene_load(&f, 42, 1, &f.s);
            CHECK((scene != NULL) == (fail == 5 || fail == 6));
            if (scene)
            {
                CHECK(f.names == 1);
                release_object(&f, scene);
            }
            else
                CHECK(!f.names);
            check_clean(&f);
        }
        /* Failed second open closes only tag zero, before packet and wire release. */
        init_loader(&f, swap);
        f.fail = 12;
        CHECK(!mm_scene_load(&f, 42, 1, &f.s));
        last = -1;
        for (j = 0; j < f.used; ++j)
            if (f.log[j] == 11)
                last = j;
        CHECK(last >= 0 && f.log[last + 1] == 20 && f.log[last + 2] == 200);
        check_clean(&f);
        /* Failed insertion closes all newly opened tags in reverse order. */
        init_loader(&f, swap);
        f.fail = 14;
        CHECK(!mm_scene_load(&f, 42, 1, &f.s));
        last = -1;
        for (j = 0; j < f.used; ++j)
            if (f.log[j] == 13)
                last = j;
        CHECK(last >= 0 && f.log[last + 1] == 12 && f.log[last + 2] == 11 && f.log[last + 3] == 20 &&
              f.log[last + 4] == 200);
        check_clean(&f);
        init_loader(&f, swap);
        f.no_thumbnail = 1;
        scene = (LoadObject *)mm_scene_load(&f, 42, 1, &f.s);
        CHECK(scene && !f.owner.owned[6]);
        release_object(&f, scene);
        check_clean(&f);
    }
    init_loader(&f, 0);
    CHECK(!mm_scene_load(&f, 42, 3, &f.s) && !f.attempt);
    check_clean(&f);
}
static int resolve_actor(void *p, MMI32 i, MMU32 scene)
{
    Loader *f = (Loader *)p;
    CHECK(scene == 42 && i == f->actor_calls && !f->locks);
    ++f->actor_calls;
    return f->actor_calls != f->actor_fail;
}
static void lock_group(void *p, void *g)
{
    Loader *f = (Loader *)p;
    CHECK(g == f->owner.owned[0] && !f->locks);
    ++f->locks;
}
static void unlock_group(void *p, void *g)
{
    Loader *f = (Loader *)p;
    CHECK(g == f->owner.owned[0] && f->locks == 1);
    --f->locks;
}
static MMSceneSound *resolve_sound(void *p, void *g, MMI32 i)
{
    MMI32 bytes;
    return (MMSceneSound *)group_payload(p, g, i, &bytes);
}
static int resolve_record(void *p, void *record, MMU32 scene)
{
    Loader *f = (Loader *)p;
    Record *r = (Record *)record;
    CHECK(scene == 42 && f->locks == 1 && !r->tag.sid);
    ++f->resolve_calls;
    if (f->resolve_calls == f->resolve_fail)
        return 0;
    r->tag.number += 4;
    return 1;
}
static void test_resolve(void)
{
    Loader f;
    MMSceneResolveServices s;
    LoadObject *scene, *actors;
    int fail;
    for (fail = 0; fail < 5; ++fail)
    {
        init_loader(&f, 0);
        scene = (LoadObject *)mm_scene_load(&f, 42, 1, &f.s);
        CHECK(scene);
        actors = (LoadObject *)f.owner.owned[2];
        actors->count = 2;
        memset(&s, 0, sizeof(s));
        s.context = &f;
        s.owner = &f.owner;
        s.count = group_count;
        s.kind = group_kind;
        s.actor = resolve_actor;
        s.lock = lock_group;
        s.unlock = unlock_group;
        s.sound = resolve_sound;
        s.packets = f.packets.s;
        s.resolve = resolve_record;
        f.actor_fail = fail <= 2 ? fail : 0;
        f.resolve_fail = fail >= 3 ? fail - 2 : 0;
        CHECK(mm_scene_resolve_sounds(42, &s) == (fail == 0));
        CHECK(!f.locks);
        if (fail == 1 || fail == 2)
            CHECK(!f.resolve_calls);
        else
            CHECK(f.actor_calls == 2 && f.resolve_calls == (fail == 3 ? 1 : 2));
        /* Failed resolution retains just the completed tag mutation prefix. */
        {
            MMSceneSound *p = resolve_sound(&f, f.owner.owned[0], 0);
            int i;
            CHECK(at(p, 0)->tag.number == (fail == 0 || fail == 4 ? 5U : 1U));
            CHECK(at(p, 1)->tag.number == 2);
            CHECK(at(p, 2)->tag.number == (fail == 0 ? 7U : 3U));
            for (i = 0; i < p->ctagc; ++i)
                if (at(p, i)->tag.number >= 4)
                    at(p, i)->tag.number -= 4;
        }
        actors->count = 0;
        release_object(&f, scene);
        check_clean(&f);
    }
}
int main(void)
{
    test_load();
    test_resolve();
    puts("C89 Scene load and resolve checks passed");
    return 0;
}

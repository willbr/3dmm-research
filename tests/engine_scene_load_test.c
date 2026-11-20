#include "mm/scene_load.h"
#include "mm/text_store.h"
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
    LoadObject wire[2], background_cache, *scene;
    int attempt, fail, live, swap, names, published, log[512], used;
    int actor_calls, resolve_calls, actor_fail, resolve_fail, no_thumbnail, bad_kind;
    int track_allocations, fail_open, fail_track;
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
static void *resize_track(void *p, void *data, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Loader *f = p;
    int fresh = !data;
    void *replacement;
    (void)old_bytes;
    if (priority == MM_STORAGE_SPEED || f->fail_track || !step(f))
        return NULL;
    replacement = realloc(data, (size_t)bytes);
    CHECK(replacement);
    f->track_allocations += fresh;
    return replacement;
}
static void release_track(void *p, void *data, MMU32 bytes)
{
    Loader *f = p;
    (void)bytes;
    CHECK(f->track_allocations > 0);
    --f->track_allocations;
    free(data);
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
    MMStorageAllocator allocator = {NULL, resize_track, release_track};
    allocator.context = f;
    f->scene = (LoadObject *)new_object(f, 200);
    if (f->scene)
        mm_scene_owner_init(&f->owner, movie, &allocator);
    return f->scene;
}
static MMSceneOwner *owner_state(void *p, void *scene)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene);
    return &f->owner;
}
static void release_object(void *p, void *value)
{
    Loader *f = (Loader *)p;
    LoadObject *o = (LoadObject *)value;
    CHECK(o && o->refs == 1);
    note(f, 100 + o->type);
    if (o == f->scene)
        mm_scene_owner_dispose(&f->owner, &f->s.owners);
    o->refs = 0;
    --f->live;
    if (o->type < 100 || o->type >= 200)
        free(o);
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
static int metadata(void *p, MMU32 number, MMMovieSessionScene *out)
{
    Loader *f = (Loader *)p;
    CHECK(number == 42);
    if (!step(f))
        return MM_MOVIE_BAD_FILE;
    out->first_frame = 3;
    out->last_frame = 19;
    out->transition = 3;
    out->number = number;
    return MM_MOVIE_OK;
}
static void publish_metadata(void *p, void *scene, const MMSceneState *value)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene && !mm_scene_member_count(&f->owner, MM_SCENE_ACTORS) &&
          !mm_scene_member_count(&f->owner, MM_SCENE_TEXTS));
    CHECK(value->currentFrame == 2 && value->name.unit == 1);
    f->metadata = *value;
    f->published = 1;
    note(f, 2);
}
static int child(void *p, MMU32 scene, MMU32 id, MMU32 type, MMU32 *number)
{
    Loader *f = (Loader *)p;
    CHECK(scene == 42 && f->published);
    if (!step(f) || (type == MM_SCENE_THUMBNAIL_CHUNK && f->no_thumbnail))
        return 0;
    CHECK(type == MM_SCENE_THUMBNAIL_CHUNK ? id == 0 :
          (type == MM_SCENE_ACTOR_CHUNK || type == MM_TEXT_BOX) && id == 7);
    *number = type == MM_SCENE_THUMBNAIL_CHUNK ? (100 + MM_SCENE_THUMBNAIL) :
              type == MM_SCENE_ACTOR_CHUNK ? (100 + MM_SCENE_ACTORS) : (100 + MM_SCENE_TEXTS);
    return 1;
}
static void *thumbnail(void *p, MMU32 number)
{
    CHECK(number == 100 + MM_SCENE_THUMBNAIL);
    return new_object((Loader *)p, MM_SCENE_THUMBNAIL);
}
static int read_group(void *p, MMU32 number, int start, MMMovieSceneGroup *out)
{
    Loader *f = (Loader *)p;
    LoadObject *g = &f->wire[start];
    CHECK(number == 42 && f->published);
    if (!step(f) || !step(f))
        return MM_MOVIE_BAD_FILE;
    CHECK(!g->refs);
    g->refs = 1;
    ++f->live;
    out->handle = g;
    out->count = (MMU32)g->count;
    return MM_MOVIE_OK;
}
static void read_event(void *p, const MMMovieSceneGroup *g, MMU32 i, MMMovieSceneEvent *out)
{
    Loader *f = (Loader *)p;
    LoadEvent *event = &((LoadObject *)g->handle)->events[i];
    const unsigned short endian = 1;
    out->big_endian = (*(const unsigned char *)&endian == 0) != f->swap;
    out->data = event->fixed;
    out->bytes = sizeof(event->fixed);
    out->frame = (MMI32)mm_movie_scene_event_word(out, 0);
    out->kind = (MMI32)mm_movie_scene_event_word(out, 4);
    out->data = event->payload.bytes;
    out->bytes = event->bytes;
}
static void release_group(void *p, MMMovieSceneGroup *g)
{
    release_object(p, g->handle);
    g->handle = NULL;
}
static void *read_object(void *p, MMU32 number, int kind, void *scene)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene);
    CHECK(number == (kind == MM_SCENE_ADD_ACTOR ? 100 + MM_SCENE_ACTORS : 100 + MM_SCENE_TEXTS));
    return new_object(f, 300 + kind);
}
static void *retain_background_cache(void *p)
{
    Loader *f = (Loader *)p;
    CHECK(!f->background_cache.refs);
    f->background_cache.type = 103;
    ++f->background_cache.refs;
    ++f->live;
    return &f->background_cache;
}
static int open_tag(void *p, MMSceneSound *sound, MMI32 i)
{
    Loader *f = (Loader *)p;
    Record *r = at(sound, i);
    if (!step(f) || f->fail_open == i + 1)
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
    CHECK(scene == f->scene && number == 42 &&
          f->owner.tracks[MM_SCENE_FRAMES].rows.count == 3 && f->owner.tracks[MM_SCENE_STARTS].rows.count == 4);
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
               pause[] = {3, 230}, background[] = {0, 999, 23, 9}, id[] = {7};
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
    s->read.context = f;
    s->read.header = metadata;
    s->read.child = child;
    s->read.group = read_group;
    s->read.event = read_event;
    s->read.close = release_group;
    s->publish_metadata = publish_metadata;
    s->thumbnail = thumbnail;
    s->object = read_object;
    s->retain_background_cache = retain_background_cache;
    s->open_tag = open_tag;
    s->load_name = load_name;
    o = &s->owners;
    o->context = f;
    o->sound_tag_bytes = sizeof(Record);
    o->create_scene = new_scene;
    o->state = owner_state;
    o->release = release_object;
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
    CHECK(!f->live && !f->track_allocations);
    CHECK(!f->wire[0].refs && !f->wire[1].refs);
    clean(&f->packets);
}
static void test_load(void)
{
    Loader f;
    LoadObject *scene;
    MMSceneTrack *frames;
    MMSceneTrackEvent event;
    MMTag tag;
    MMI32 bytes;
    int swap, fail, attempts, j, last;
    for (swap = 0; swap < 2; ++swap)
    {
        init_loader(&f, swap);
        scene = (LoadObject *)mm_scene_load(&f, 42, 1, &f.s);
        CHECK(scene && f.names == 1);
        attempts = f.attempt;
        frames = &f.owner.tracks[MM_SCENE_FRAMES];
        CHECK(mm_scene_track_payload(frames, 0, &bytes) && bytes == (int)(16 + 3 * sizeof(Record)));
        CHECK(mm_scene_track_get(frames, 0, &event) && event.frame == 3 && event.kind == MM_SCENE_PLAY_SOUND);
        CHECK(!memcmp(mm_scene_track_payload(frames, 1, NULL), f.wire[0].events[1].payload.bytes, 4));
        CHECK(!memcmp(mm_scene_track_payload(frames, 2, NULL), f.wire[0].events[2].payload.bytes, 8));
        memcpy(&tag, mm_scene_track_payload(&f.owner.tracks[MM_SCENE_STARTS], 0, NULL), sizeof(tag));
        CHECK(tag.sid == 0 && tag.cache == &f.background_cache && tag.type == 23 && tag.number == 9);
        CHECK(f.packets.refs[1] == 1 && f.packets.refs[3] == 1 && !f.packets.refs[2]);
        release_object(&f, scene);
        check_clean(&f);
        for (fail = 1; fail <= attempts; ++fail)
        {
            init_loader(&f, swap);
            f.fail = fail;
            scene = (LoadObject *)mm_scene_load(&f, 42, 1, &f.s);
            CHECK((scene != NULL) == (fail == 3 || fail == 4));
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
        f.fail_open = 2;
        CHECK(!mm_scene_load(&f, 42, 1, &f.s));
        last = -1;
        for (j = 0; j < f.used; ++j)
            if (f.log[j] == 11)
                last = j;
        CHECK(last >= 0 && f.log[last + 1] == 20 && f.log[last + 2] == 200);
        check_clean(&f);
        /* Failed insertion closes all newly opened tags in reverse order. */
        init_loader(&f, swap);
        f.fail_track = 1;
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
        CHECK(scene && !f.owner.owned[MM_SCENE_THUMBNAIL]);
        release_object(&f, scene);
        check_clean(&f);
    }
    init_loader(&f, 0);
    CHECK(!mm_scene_load(&f, 42, 3, &f.s) && !f.attempt);
    check_clean(&f);
}
static int resolve_actor(void *p, void *actor, MMU32 scene)
{
    Loader *f = (Loader *)p;
    CHECK(scene == 42 && actor == &f->wire[f->actor_calls]);
    CHECK(!f->owner.tracks[MM_SCENE_FRAMES].storage.buffers[0].locks);
    ++f->actor_calls;
    return f->actor_calls != f->actor_fail;
}
static int resolve_record(void *p, void *record, MMU32 scene)
{
    Loader *f = (Loader *)p;
    Record *r = (Record *)record;
    CHECK(scene == 42 && f->owner.tracks[MM_SCENE_FRAMES].storage.buffers[0].locks == 1 && !r->tag.sid);
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
    LoadObject *scene;
    MMSceneTrack *frames = &f.owner.tracks[MM_SCENE_FRAMES];
    MMSceneSound *sound;
    int fail, i;
    for (fail = 0; fail < 6; ++fail)
    {
        init_loader(&f, 0);
        scene = (LoadObject *)mm_scene_load(&f, 42, 1, &f.s);
        CHECK(scene);
        CHECK(mm_scene_member_append(&f.owner, MM_SCENE_ACTORS, &f.wire[0]));
        CHECK(mm_scene_member_append(&f.owner, MM_SCENE_ACTORS, &f.wire[1]));
        memset(&s, 0, sizeof(s));
        s.context = &f;
        s.owner = &f.owner;
        s.actor = resolve_actor;
        s.packets = f.packets.s;
        s.resolve = resolve_record;
        f.actor_fail = fail <= 2 ? fail : 0;
        f.resolve_fail = fail == 3 || fail == 4 ? fail - 2 : 0;
        sound = (MMSceneSound *)mm_scene_track_payload(frames, 0, NULL);
        CHECK(sound && sound->ctagc == 3);
        if (fail == 5)
            sound->ctagc = 1000;
        CHECK(mm_scene_resolve_sounds(42, &s) == (fail == 0));
        CHECK(!frames->storage.buffers[0].locks);
        if (fail == 1 || fail == 2 || fail == 5)
            CHECK(!f.resolve_calls);
        else
            CHECK(f.actor_calls == 2 && f.resolve_calls == (fail == 3 ? 1 : 2));
        sound->ctagc = 3;
        /* Failed resolution retains just the completed tag mutation prefix. */
        CHECK(at(sound, 0)->tag.number == (fail == 0 || fail == 4 ? 5U : 1U));
        CHECK(at(sound, 1)->tag.number == 2);
        CHECK(at(sound, 2)->tag.number == (fail == 0 ? 7U : 3U));
        for (i = 0; i < sound->ctagc; ++i)
            if (at(sound, i)->tag.number >= 4)
                at(sound, i)->tag.number -= 4;
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

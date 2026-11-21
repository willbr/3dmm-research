#include "mm/scene_load.h"
#include "mm/movie_sound.h"
#include "mm/text.h"
#include "mm/text_store.h"
#define SCENE_SOUND_FIXTURE_NO_PACKET_HELPERS
#include "scene_sound_fixture.h"
#include "chunk_seed_fixture.h"
#include "tag_copy_fixture.h"
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
typedef struct Loader Loader;
typedef struct LoadFile
{
    TagCopyFile archive;
    Loader *loader;
} LoadFile;
struct Loader
{
    Fixture packets;
    MMSceneLoadServices s;
    MMSceneOwner owner;
    LoadObject wire[2], *scene;
    unsigned title_unit;
    int attempt, fail, live, swap, names, title_mode, bad_titles, log[512], used;
    int actor_calls, resolve_calls, actor_fail, resolve_refs, no_thumbnail, bad_kind;
    ChunkIndexFixture archive;
    LoadFile source, destination;
    MMTagServices load_tags, movie_tags;
    int cache_refs, released_tag, cross_file;
    int track_allocations, fail_track;
};
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
        CHECK(mm_scene_owner_init(&f->owner, movie, f->title_unit, &allocator));
    return f->scene;
}
static MMSceneOwner *owner_state(void *p, void *scene)
{
    Loader *f = (Loader *)p;
    CHECK(scene == f->scene);
    return &f->owner;
}
static void release_cache(Loader *f)
{
    CHECK(f->cache_refs > 0);
    --f->cache_refs;
}
static void release_object(void *p, void *value)
{
    Loader *f = (Loader *)p;
    LoadObject *o = (LoadObject *)value;
    if (value == f)
    {
        release_cache(f);
        return;
    }
    CHECK(o && o->refs == 1);
    note(f, 100 + o->type);
    if (o == f->scene)
        mm_scene_owner_dispose(&f->owner, &f->s.owners);
    o->refs = 0;
    --f->live;
    if (o->type < 100 || o->type >= 200)
        free(o);
}
static void close_tag(void *p, MMSceneSoundTag *r)
{
    Loader *f = (Loader *)p;
    CHECK(r->tag.cache == (r->tag.sid ? NULL : f));
    mm_tag_close(&r->tag, &f->load_tags);
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
static int child(void *p, MMU32 scene, MMU32 id, MMU32 type, MMU32 *number)
{
    Loader *f = (Loader *)p;
    const MMSceneState *data = &f->owner.data;
    CHECK(scene == 42 && f->scene && data->currentFrame == 2 && data->firstFrame == 3 &&
          data->lastFrame == 19 && data->transition == 3 && data->name.unit == f->title_unit && !data->name.count);
    CHECK(!f->owner.runtime.next_event && !f->names);
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
    CHECK(number == 42 && f->owner.data.currentFrame == 2 && !f->names);
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
static void *cache_file(void *p)
{
    Loader *f = p;
    return f->cross_file ? &f->destination : &f->source;
}
static MMChunkEndpoint bind_file(void *p, MMChunkFileCopy *binding)
{
    LoadFile *file = p;
    return tag_copy_file_endpoint(&file->archive, binding);
}
static void retain_cache(void *p)
{
    ++((Loader *)p)->cache_refs;
}
static void release_tag_cache(void *p)
{
    Loader *f = p;
    release_cache(f);
    f->released_tag = 1;
}
static void publish_tag(void *p, const MMTag *tag)
{
    Loader *f = p;
    if (f->released_tag)
    {
        CHECK(!tag->cache);
        note(f, 10 + (int)tag->number);
        f->released_tag = 0;
    }
}
static void missing_chunk(void *p)
{
    Loader *f = p;
    CHECK(f->cache_refs == 1);
    note(f, 40);
}
static int read_title(void *p, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written)
{
    Loader *f = ((LoadFile *)p)->loader;
    static const unsigned char name[] = {3, 3, 5, 'S', 'c', 'e', 'n', 'e', 0};
    unsigned short wide[] = {0, 5, 'S', 'c', 0x2014, 'n', 'e', 0};
    const void *bytes = f->title_unit == 2 ? (const void *)wide : name;
    size_t size = f->title_unit == 2 ? sizeof(wide) : sizeof(name);
    CHECK(type == MM_SCENE_CHUNK && number == 42 && capacity >= size &&
          f->owner.tracks[MM_SCENE_FRAMES].rows.count == 3 && f->owner.tracks[MM_SCENE_STARTS].rows.count == 4);
    ++f->names;
    note(f, 3);
    if (f->title_mode == 1)
        return 0;
    wide[0] = (unsigned short)f->s.titles.encoding;
    memcpy(wire, bytes, size);
    *written = f->title_mode == 2 ? 1 : (MMU32)size;
    return 1;
}
static void bad_title(void *p)
{
    ++((Loader *)p)->bad_titles;
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
    MMU32 sound[] = {80, 2, 0, 3, 1, 0, 0, MM_SOUND_CHUNK, 1, 2, 3, 0, MM_SOUND_CHUNK, 2,
                    3, 0, 0, MM_SOUND_CHUNK, 3}, camera[] = {0x12345678U},
               pause[] = {3, 230}, background[] = {0, 999, 23, 9}, id[] = {7};
    memset(f, 0, sizeof(*f));
    init(&f->packets);
    f->swap = swap;
    f->title_unit = 1;
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
    f->source.loader = f;
    tag_copy_file_init(&f->source.archive);
    tag_copy_file_init(&f->destination.archive);
    f->load_tags.context = f;
    f->load_tags.file = cache_file;
    f->load_tags.bind_file = bind_file;
    f->load_tags.retain = retain_cache;
    f->load_tags.release = release_tag_cache;
    f->load_tags.missing_chunk = missing_chunk;
    f->load_tags.publish = publish_tag;
    s->source_file = &f->source;
    s->title_file = &f->source;
    s->resources = f;
    s->tags = &f->load_tags;
    s->titles.context = f;
    s->titles.encoding = MM_TEXT_WINDOWS;
    s->titles.read = read_title;
    s->titles.bad_string = bad_title;
    s->thumbnail = thumbnail;
    s->object = read_object;
    o = &s->owners;
    o->context = f;
    o->create_scene = new_scene;
    o->state = owner_state;
    o->release = release_object;
    o->diagnostic = diagnostic;
    o->sound.context = f;
    o->sound.close = close_tag;
    o->sound.free = free_sound;
    o->sound.diagnostic = diagnostic;
}
static void check_clean(Loader *f)
{
    CHECK(!f->live && !f->track_allocations && !f->cache_refs && !f->released_tag);
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
    MMSceneSoundTag sound_tag;
    MMSceneSound *packet;
    MMI32 bytes;
    int swap, fail, attempts, j, last;
    for (swap = 0; swap < 2; ++swap)
    {
        init_loader(&f, swap);
        scene = (LoadObject *)mm_scene_load(&f, 42, &f.s);
        CHECK(scene && f.names == 1);
        CHECK(f.owner.data.name.count == 5 && !memcmp(mm_movie_title_text(&f.owner.data.name), "Scene", 6));
        attempts = f.attempt;
        frames = &f.owner.tracks[MM_SCENE_FRAMES];
        packet = mm_scene_track_payload(frames, 0, &bytes);
        CHECK(packet && bytes == (int)(16 + 3 * sizeof(Record)));
        CHECK(mm_scene_track_get(frames, 0, &event) && event.frame == 3 && event.kind == MM_SCENE_PLAY_SOUND);
        CHECK(!memcmp(mm_scene_track_payload(frames, 1, NULL), f.wire[0].events[1].payload.bytes, 4));
        CHECK(!memcmp(mm_scene_track_payload(frames, 2, NULL), f.wire[0].events[2].payload.bytes, 8));
        memcpy(&tag, mm_scene_track_payload(&f.owner.tracks[MM_SCENE_STARTS], 0, NULL), sizeof(tag));
        CHECK(tag.sid == 0 && tag.cache == &f && tag.type == 23 && tag.number == 9);
        CHECK(f.owner.owned[MM_SCENE_BACKGROUND_CACHE] == &f && f.cache_refs == 3);
        for (j = 0; j < packet->ctagc; ++j)
        {
            mm_scene_sound_get_tag(packet, j, &sound_tag);
            CHECK(sound_tag.tag.sid == (j == 1 ? 3 : 0) && sound_tag.tag.cache == (j == 1 ? NULL : &f));
        }
        release_object(&f, scene);
        check_clean(&f);
        for (fail = 1; fail <= attempts; ++fail)
        {
            init_loader(&f, swap);
            f.fail = fail;
            scene = (LoadObject *)mm_scene_load(&f, 42, &f.s);
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
        /* A missing local chunk after a successful copy releases the open tag before the packet and group. */
        init_loader(&f, swap);
        f.cross_file = 1;
        fixture_chunk_add(&f.source.archive.index, MM_SOUND_CHUNK, 1);
        CHECK(!mm_scene_load(&f, 42, &f.s));
        CHECK(f.destination.archive.index.count == 1 &&
              f.destination.archive.index.rows[0].entry.type == MM_SOUND_CHUNK);
        last = -1;
        for (j = 0; j < f.used; ++j)
            if (f.log[j] == 40)
                last = j;
        CHECK(last >= 0 && last + 3 < f.used &&
              f.log[last + 1] == 10 + (int)f.destination.archive.index.rows[0].entry.number &&
              f.log[last + 2] == 20 && f.log[last + 3] == 200);
        check_clean(&f);
        /* Failed insertion closes all newly opened tags in reverse order. */
        init_loader(&f, swap);
        f.fail_track = 1;
        CHECK(!mm_scene_load(&f, 42, &f.s));
        last = -1;
        for (j = 0; j < f.used; ++j)
            if (f.log[j] == 13)
                last = j;
        CHECK(last >= 0 && last + 3 < f.used && f.log[last + 1] == 11 &&
              f.log[last + 2] == 20 && f.log[last + 3] == 200);
        check_clean(&f);
        init_loader(&f, swap);
        f.no_thumbnail = 1;
        scene = (LoadObject *)mm_scene_load(&f, 42, &f.s);
        CHECK(scene && !f.owner.owned[MM_SCENE_THUMBNAIL]);
        release_object(&f, scene);
        check_clean(&f);
    }
    for (j = 1; j <= 2; ++j)
    {
        init_loader(&f, 0);
        f.title_mode = j;
        scene = (LoadObject *)mm_scene_load(&f, 42, &f.s);
        CHECK(scene && f.names == 1 && f.bad_titles == (j == 2) && !f.owner.data.name.count);
        release_object(&f, scene);
        check_clean(&f);
    }
    {
        const unsigned short one = 1, expected[] = {'S', 'c', 0x2014, 'n', 'e', 0};
        init_loader(&f, 0);
        f.title_unit = 2;
        f.s.titles.encoding = *(const unsigned char *)&one ? MM_TEXT_UTF16_LE : MM_TEXT_UTF16_BE;
        scene = (LoadObject *)mm_scene_load(&f, 42, &f.s);
        CHECK(scene && f.owner.data.name.count == 5);
        CHECK(!memcmp(mm_movie_title_text(&f.owner.data.name), expected, sizeof(expected)));
        release_object(&f, scene);
        check_clean(&f);
    }
}
static int resolve_actor(void *p, void *actor, MMU32 scene)
{
    Loader *f = (Loader *)p;
    CHECK(scene == 42 && actor == &f->wire[f->actor_calls]);
    CHECK(!f->owner.tracks[MM_SCENE_FRAMES].storage.buffers[0].locks);
    ++f->actor_calls;
    return f->actor_calls != f->actor_fail;
}
static void read_resolve_movie(void *p, MMMovieSoundState *state)
{
    Loader *f = p;
    CHECK(f->owner.tracks[MM_SCENE_FRAMES].storage.buffers[0].locks == 1 && f->actor_calls == 2);
    ++f->resolve_calls;
    state->cache = f;
    state->movie = 0;
    state->scene = 0;
}
static void *resolve_file(void *p)
{
    return &((Loader *)p)->archive;
}
static MMChunkEdit resolve_edit(void *p)
{
    return fixture_chunk_edit(&((Loader *)p)->archive);
}
static void retain_resolve_cache(void *p)
{
    Loader *f = p;
    CHECK(f->resolve_refs == 2);
    ++f->resolve_refs;
}
static void release_resolve_cache(void *p)
{
    Loader *f = p;
    CHECK(f->resolve_refs == 3);
    --f->resolve_refs;
}
static MMMovieSoundServices movie_sound(void *p)
{
    Loader *f = p;
    MMMovieSoundServices s = {0};
    s.context = f;
    s.read = read_resolve_movie;
    s.tags = &f->movie_tags;
    s.edit = resolve_edit;
    return s;
}
static void test_resolve(void)
{
    Loader f;
    MMSceneResolveServices s;
    LoadObject *scene;
    MMSceneTrack *frames = &f.owner.tracks[MM_SCENE_FRAMES];
    MMSceneSound *sound;
    MMSceneSoundTag tag;
    int fail;
    for (fail = 0; fail < 6; ++fail)
    {
        init_loader(&f, 0);
        scene = (LoadObject *)mm_scene_load(&f, 42, &f.s);
        CHECK(scene);
        CHECK(mm_scene_member_append(&f.owner, MM_SCENE_ACTORS, &f.wire[0]));
        CHECK(mm_scene_member_append(&f.owner, MM_SCENE_ACTORS, &f.wire[1]));
        memset(&s, 0, sizeof(s));
        s.context = &f;
        s.owner = &f.owner;
        s.actor = resolve_actor;
        s.movie_sound = movie_sound;
        f.actor_fail = fail <= 2 ? fail : 0;
        f.resolve_refs = 2;
        f.movie_tags.file = resolve_file;
        f.movie_tags.retain = retain_resolve_cache;
        f.movie_tags.release = release_resolve_cache;
        if (fail != 3)
            fixture_chunk_seed_link(&f.archive, MM_SOUND_SCENE, 42, MM_SOUND_CHUNK, 5, 1);
        if (fail != 4)
            fixture_chunk_seed_link(&f.archive, MM_SOUND_SCENE, 42, MM_SOUND_CHUNK, 7, 3);
        sound = (MMSceneSound *)mm_scene_track_payload(frames, 0, NULL);
        CHECK(sound && sound->ctagc == 3);
        if (fail == 5)
            sound->ctagc = 1000;
        CHECK(mm_scene_resolve_sounds(42, &s) == (fail == 0));
        CHECK(!frames->storage.buffers[0].locks && f.resolve_refs == 2);
        if (fail == 1 || fail == 2 || fail == 5)
            CHECK(!f.resolve_calls);
        else
            CHECK(f.actor_calls == 2 && f.resolve_calls == (fail == 3 ? 1 : 2));
        sound->ctagc = 3;
        /* Failed resolution retains just the completed tag mutation prefix. */
        mm_scene_sound_get_tag(sound, 0, &tag);
        CHECK(tag.tag.number == (fail == 0 || fail == 4 ? 5U : 1U));
        mm_scene_sound_get_tag(sound, 1, &tag);
        CHECK(tag.tag.number == 2);
        mm_scene_sound_get_tag(sound, 2, &tag);
        CHECK(tag.tag.number == (fail == 0 ? 7U : 3U));
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

#include "mm/scene_store.h"
#include "mm/scene_data.h"
#include "mm/movie_sound.h"
#include "scene_sound_fixture.h"
typedef union StorePayload {
    void *pointer;
    MMTag tag;
    unsigned char bytes[192];
} StorePayload;
typedef struct StoreEvent
{
    unsigned char fixed[8];
    int bytes;
    StorePayload payload;
    MMSceneSound *sound;
} StoreEvent;
typedef struct StoreGroup
{
    int type, count, live;
    StoreEvent events[8];
} StoreGroup;
typedef struct StoreFixture
{
    Fixture packets;
    MMSceneStoreServices s;
    MMSceneCollectServices collect;
    MMSceneOwner owner;
    StoreGroup runtime[2], temp[2], wire[2];
    int attempts, fail, created, live, deleted, thumb_deleted, with_thumb, thumb, updated, old_thumb;
    int written, metadata, diagnostics, missing_background, empty_events, actor_error, no_background;
    int missing_actor, missing_frames;
    int log[512], used, adopted, adoptions, objects, swap, collected, tag_limit;
    MMU32 actor_id, text_id;
    MMTag tags[24];
} StoreFixture;
static void note(StoreFixture *f, int n)
{
    CHECK(f->used < 512);
    f->log[f->used++] = n;
}
static int step(StoreFixture *f, int n)
{
    note(f, n);
    return ++f->attempts != f->fail;
}
static MMI32 count(void *p, void *g)
{
    (void)p;
    return ((StoreGroup *)g)->count;
}
static void fixed(void *p, void *g, MMI32 i, unsigned char *b)
{
    (void)p;
    memcpy(b, ((StoreGroup *)g)->events[i].fixed, 8);
}
static void *data(void *p, void *g, MMI32 i, MMI32 *bytes)
{
    StoreEvent *e = &((StoreGroup *)g)->events[i];
    (void)p;
    *bytes = e->bytes;
    return e->sound ? (void *)e->sound : e->payload.bytes;
}
static void release_group(void *p, void *g)
{
    StoreFixture *f = (StoreFixture *)p;
    StoreGroup *group = (StoreGroup *)g;
    CHECK(group && group->live == 1);
    group->live = 0;
    --f->live;
    note(f, 100 + group->type);
}
static void free_raw(void *p, void *value)
{
    StoreFixture *f = (StoreFixture *)p;
    note(f, 30);
    raw_free(&f->packets, value);
}
static void *packet_allocate(void *p, MMI32 bytes)
{
    StoreFixture *f = (StoreFixture *)p;
    return step(f, 31) ? allocate(&f->packets, bytes) : NULL;
}
static void *packet_data(void *p, void *g, MMI32 i)
{
    MMI32 bytes;
    return data(p, g, i, &bytes);
}
static MMI32 packet_size(void *p, void *g, MMI32 i)
{
    MMI32 bytes;
    data(p, g, i, &bytes);
    return bytes;
}
static void release_packet(void *p, MMSceneSound *sound)
{
    StoreFixture *f = (StoreFixture *)p;
    note(f, 32);
    drop(&f->packets, sound);
}
static int add_scene(void *p, MMU32 *number)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(*number == 0xffffffffUL);
    *number = 42;
    return step(f, 1);
}
static int add_scene_unchanged(void *p, MMU32 *number)
{
    (void)p;
    CHECK(*number == 0xffffffffUL);
    return 0;
}
static void delete_scene(void *p, MMU32 number)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(number == 42);
    ++f->deleted;
    note(f, 90);
}
static void *new_group(void *p)
{
    StoreFixture *f = (StoreFixture *)p;
    StoreGroup *g = &f->temp[f->created];
    CHECK(f->created < 2);
    if (!step(f, 2 + f->created))
        return NULL;
    g->type = 10 + f->created++;
    g->live = 1;
    ++f->live;
    return g;
}
static int insert(void *p, void *g, MMI32 i, MMI32 frame, int kind, MMI32 bytes, const void *value)
{
    StoreFixture *f = (StoreFixture *)p;
    StoreGroup *group = (StoreGroup *)g;
    StoreEvent *e;
    CHECK(i == group->count && bytes <= 192 && frame == 3);
    if (!step(f, 40 + kind))
        return 0;
    e = &group->events[i];
    memcpy(e->fixed, &frame, 4);
    memcpy(e->fixed + 4, &kind, 4);
    e->bytes = bytes;
    memcpy(e->payload.bytes, value, (size_t)bytes);
    ++group->count;
    return 1;
}
static int store_entry(void *p, MMU32 row, MMChunkEntry *entry)
{
    StoreFixture *f = (StoreFixture *)p;
    memset(entry, 0, sizeof(*entry));
    if (row == 0)
    {
        entry->type = MM_SOUND_CHUNK;
        entry->number = 1;
    }
    else
    {
        CHECK(row == 1);
        entry->type = MM_SCENE_CHUNK;
        entry->number = 42;
        entry->child_count = f->adopted ? 3 : 2;
    }
    return 1;
}
static int store_child(void *p, MMU32 row, MMU32 i, MMChunkChild *out)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(row == 1 && i < (f->adopted ? 3U : 2U));
    if (i == 0)
    {
        out->type = MM_SCENE_ACTOR_CHUNK;
        out->number = 77;
        out->slot = 1;
    }
    else
    {
        out->type = MM_SOUND_CHUNK;
        out->number = 1;
        out->slot = f->adopted && i == 1 ? 1 : 3;
    }
    return 1;
}
static MMChunkAccess store_chunks(void *p)
{
    MMChunkAccess chunks = {NULL, 2, store_entry, store_child};
    chunks.context = p;
    return chunks;
}
static int adopt_sound(void *p, MMU32 scene, MMU32 number, MMU32 child)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(scene == 42 && number == 1 && child == 1);
    if (!step(f, 70))
        return 0;
    f->adopted |= 1 << child;
    ++f->adoptions;
    return 1;
}
static int add_child(void *p, MMU32 scene, int kind, MMU32 id, MMU32 *number)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(scene == 42);
    if (kind == MM_SCENE_ADD_ACTOR)
        CHECK(id == f->actor_id++);
    else
        CHECK(kind == MM_SCENE_ADD_TEXT && id == f->text_id++);
    *number = 80 + (MMU32)kind * 4 + id;
    return step(f, 71 + kind);
}
static void *object(void *p, void *g, MMI32 i)
{
    (void)p;
    return ((StoreGroup *)g)->events[i].payload.pointer;
}
static int write_object(void *p, void *value, int kind, MMU32 number, MMU32 scene)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(value == f && scene == 42 && number >= 80);
    ++f->objects;
    return step(f, 75 + kind);
}
static int save_background(void *p, void *g, MMI32 i, MMTag *tag)
{
    StoreFixture *f = (StoreFixture *)p;
    MMTag *input = &((StoreGroup *)g)->events[i].payload.tag;
    input->number += 10;
    *tag = *input;
    return step(f, 80);
}
static int write_events(void *p, MMU32 scene, void *frames, void *starts)
{
    StoreFixture *f = (StoreFixture *)p;
    MMU32 n;
    StoreGroup *a = (StoreGroup *)frames, *b = (StoreGroup *)starts;
    CHECK(scene == 42 && a->count == 4 && b->count == 6);
    CHECK(a->events[0].bytes == 96 && b->events[0].bytes == 4 && b->events[1].bytes == 4);
    memcpy(&n, b->events[0].payload.bytes, 4);
    CHECK(n == 0);
    memcpy(&n, b->events[1].payload.bytes, 4);
    CHECK(n == 0);
    memcpy(&n, b->events[2].payload.bytes, 4);
    CHECK(n == 1);
    memcpy(&n, b->events[3].payload.bytes, 4);
    CHECK(n == 1);
    memcpy(&n, b->events[4].payload.bytes + 4, 4);
    CHECK(!n);
    memcpy(&n, b->events[4].payload.bytes + 12, 4);
    CHECK(n == 19);
    CHECK(f->objects == 4 && f->actor_id == 2 && f->text_id == 2 && f->adoptions == 1);
    f->written = 1;
    return step(f, 81);
}
static void update_thumbnail(void *p)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(f->written && !f->updated);
    f->updated = 1;
    note(f, 82);
    f->owner.owned[MM_SCENE_THUMBNAIL] = f->with_thumb ? &f->thumb : NULL;
}
static MMI32 thumbnail_size(void *p, void *thumbnail)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(thumbnail == &f->thumb && f->updated);
    return 65;
}
static int add_thumbnail(void *p, MMI32 bytes, MMU32 *number)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(bytes == 65);
    *number = 55;
    return step(f, 83);
}
static int write_thumbnail(void *p, void *thumbnail)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(thumbnail == &f->thumb);
    return step(f, 84);
}
static int adopt_thumbnail(void *p, MMU32 scene, MMU32 number)
{
    CHECK(scene == 42 && number == 55);
    return step((StoreFixture *)p, 85);
}
static void delete_thumbnail(void *p, MMU32 number)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(number == 55);
    ++f->thumb_deleted;
    note(f, 86);
}
static void clear_loner(void *p, MMU32 number)
{
    CHECK(number == 55);
    note((StoreFixture *)p, 87);
}
static int write_metadata(void *p, MMU32 scene)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(scene == 42 && f->updated);
    f->metadata = 1;
    return step(f, 88);
}
static void diagnostic(void *p, int start)
{
    StoreFixture *f = (StoreFixture *)p;
    (void)start;
    ++f->diagnostics;
}
static void event(StoreGroup *g, int kind, const void *value, int bytes)
{
    StoreEvent *e = &g->events[g->count++];
    MMI32 frame = 3;
    memcpy(e->fixed, &frame, 4);
    memcpy(e->fixed + 4, &kind, 4);
    e->bytes = bytes;
    memcpy(e->payload.bytes, value, (size_t)bytes);
}
static void init_store(StoreFixture *f)
{
    MMSceneStoreServices *s;
    MMSceneStoreGroups *g;
    MMSceneSound *sound;
    MMI32 pause[] = {3, 230}, camera = 0x12345678, frame = 3, kind = 1;
    MMTag tag;
    void *ptr;
    int i;
    memset(f, 0, sizeof(*f));
    init(&f->packets);
    s = &f->s;
    s->context = f;
    s->owner = &f->owner;
    g = &s->groups;
    g->context = f;
    g->count = count;
    g->fixed = fixed;
    g->data = data;
    g->release = release_group;
    g->free_raw = free_raw;
    s->values.context = f;
    s->values.packets = f->packets.s;
    s->values.packets.allocate = packet_allocate;
    s->values.packets.group_data = packet_data;
    s->values.packets.group_size = packet_size;
    s->values.release = release_packet;
    s->add_scene = add_scene;
    s->delete_scene = delete_scene;
    s->new_group = new_group;
    s->insert = insert;
    s->chunks = store_chunks;
    s->adopt_sound = adopt_sound;
    s->add_child = add_child;
    s->object = object;
    s->write_object = write_object;
    s->save_background = save_background;
    s->write_events = write_events;
    s->update_thumbnail = update_thumbnail;
    s->thumbnail_size = thumbnail_size;
    s->add_thumbnail = add_thumbnail;
    s->write_thumbnail = write_thumbnail;
    s->adopt_thumbnail = adopt_thumbnail;
    s->delete_thumbnail = delete_thumbnail;
    s->clear_loner = clear_loner;
    s->write_metadata = write_metadata;
    s->diagnostic = diagnostic;
    f->owner.owned[0] = &f->runtime[0];
    f->owner.owned[1] = &f->runtime[1];
    f->owner.owned[6] = &f->old_thumb;
    f->with_thumb = 1;
    f->packets.input[3].tag.sid = 0;
    for (i = 0; i < 2; ++i)
    {
        sound = make(&f->packets, 4);
        f->runtime[0].events[i].sound = sound;
        f->runtime[0].events[i].bytes = 16 + 4 * sizeof(Record);
        memcpy(f->runtime[0].events[i].fixed, &frame, 4);
        memcpy(f->runtime[0].events[i].fixed + 4, &kind, 4);
        ++f->runtime[0].count;
    }
    event(&f->runtime[0], 3, &camera, 4);
    event(&f->runtime[0], 5, pause, 8);
    ptr = f;
    event(&f->runtime[1], 0, &ptr, sizeof(ptr));
    event(&f->runtime[1], 2, &ptr, sizeof(ptr));
    event(&f->runtime[1], 0, &ptr, sizeof(ptr));
    event(&f->runtime[1], 2, &ptr, sizeof(ptr));
    tag.sid = 0;
    tag.cache = &f->packets;
    tag.type = 23;
    tag.number = 9;
    event(&f->runtime[1], 4, &tag, sizeof(tag));
    event(&f->runtime[1], 3, &camera, 4);
}
static void clean_store(StoreFixture *f)
{
    int i;
    CHECK(!f->live);
    for (i = 0; i < 2; ++i)
        if (f->runtime[0].events[i].sound)
            drop(&f->packets, f->runtime[0].events[i].sound);
    clean(&f->packets);
}
static void test_store(void)
{
    StoreFixture f;
    MMU32 number;
    int attempts, fail, j, last, background_called;
    init_store(&f);
    CHECK(mm_scene_store(&number, &f.s) && number == 42 && !f.deleted && f.metadata);
    attempts = f.attempts;
    CHECK(f.log[f.used - 2] == 110 && f.log[f.used - 1] == 111);
    CHECK(f.packets.refs[1] == 2 && f.packets.refs[3] == 2 && f.packets.refs[4] == 2);
    clean_store(&f);
    for (fail = 1; fail <= attempts; ++fail)
    {
        init_store(&f);
        f.fail = fail;
        CHECK(!mm_scene_store(&number, &f.s) && number == 42 && f.deleted == 1);
        last = -1;
        for (j = 0; j < f.used; ++j)
            if (f.log[j] == 90)
                last = j;
        CHECK(last >= 0);
        if (f.created == 2)
            CHECK(f.log[last + 1] == 111 && f.log[last + 2] == 110);
        else if (f.created == 1)
            CHECK(f.log[last + 1] == 110);
        if (f.thumb_deleted)
            CHECK(f.log[last - 1] == 86);
        CHECK(f.packets.refs[1] == 2 && f.packets.refs[3] == 2 && f.packets.refs[4] == 2);
        background_called = 0;
        for (j = 0; j < f.used; ++j)
            if (f.log[j] == 80)
                background_called = 1;
        CHECK(f.runtime[1].events[4].payload.tag.number == (background_called ? 19U : 9U));
        clean_store(&f);
    }
    init_store(&f);
    f.with_thumb = 0;
    CHECK(mm_scene_store(&number, &f.s) && f.updated && f.metadata);
    clean_store(&f);
    init_store(&f);
    f.s.add_scene = add_scene_unchanged;
    CHECK(!mm_scene_store(&number, &f.s) && number == 0xffffffffUL && !f.deleted);
    clean_store(&f);
}
static void wire_word(void *p, MMU32 value, int swap)
{
    unsigned char b[4];
    int i;
    memcpy(b, &value, 4);
    for (i = 0; i < 4; ++i)
        ((unsigned char *)p)[i] = b[swap ? 3 - i : i];
}
static void wire_event(StoreFixture *f, int start, int kind, const MMU32 *words, int n)
{
    StoreGroup *g = &f->wire[start];
    StoreEvent *e = &g->events[g->count++];
    int i;
    e->bytes = n * 4;
    wire_word(e->fixed, 3, f->swap);
    wire_word(e->fixed + 4, (MMU32)kind, f->swap);
    for (i = 0; i < n; ++i)
        wire_word(e->payload.bytes + i * 4, words[i], f->swap);
}
static int collect_entry(void *p, MMU32 row, MMChunkEntry *entry)
{
    StoreFixture *f = (StoreFixture *)p;
    memset(entry, 0, sizeof(*entry));
    if (row == 0)
    {
        entry->type = MM_SCENE_ACTOR_CHUNK;
        entry->number = 77;
        entry->child_count = 1;
    }
    else if (row == 1)
    {
        entry->type = 0x47474145UL;
        entry->number = 37;
    }
    else
    {
        CHECK(row == 2);
        entry->type = MM_SCENE_CHUNK;
        entry->number = 42;
        entry->child_count = !f->missing_actor;
    }
    return 1;
}
static int collect_link(void *p, MMU32 row, MMU32 i, MMChunkChild *out)
{
    (void)p;
    CHECK((row == 0 || row == 2) && i == 0);
    out->type = row == 0 ? 0x47474145UL : MM_SCENE_ACTOR_CHUNK;
    out->number = row == 0 ? 37 : 77;
    out->slot = row == 0 ? 0 : 7;
    return 1;
}
static void collect_access(void *p, MMChunkAccess *out)
{
    MMChunkAccess chunks = {NULL, 3, collect_entry, collect_link};
    chunks.context = p;
    *out = chunks;
}
static int actor_payload(void *p, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    StoreFixture *f = (StoreFixture *)p;
    unsigned char bytes[76] = {0};
    MMMemoryStream memory;
    size_t size;
    const unsigned short marker = 1;
    const unsigned char *bo = (const unsigned char *)&marker;
    bytes[0] = bo[f->swap];
    bytes[1] = bo[!f->swap];
    if (entry->type == MM_SCENE_ACTOR_CHUNK)
    {
        CHECK(entry->number == 77);
        size = MM_ACTOR_HEADER_BYTES;
        wire_word(bytes + 28, 4, f->swap);
        wire_word(bytes + 36, 0x544d504cUL, f->swap);
        wire_word(bytes + 40, 65, f->swap);
    }
    else
    {
        CHECK(entry->type == 0x47474145UL && entry->number == 37);
        if (f->actor_error || !step(f, 7))
            return MM_MOVIE_IO;
        size = f->empty_events ? 20 : sizeof(bytes);
        wire_word(bytes + 4, !f->empty_events, f->swap);
        wire_word(bytes + 8, f->empty_events ? 0 : 48, f->swap);
        wire_word(bytes + 12, 0xffffffffUL, f->swap);
        wire_word(bytes + 16, MM_ACTOR_EVENT_BYTES, f->swap);
        wire_word(bytes + 20, MM_ACTOR_COSTUME, f->swap);
        wire_word(bytes + 52, 8, f->swap);
        wire_word(bytes + 60, 0x4d54524cUL, f->swap);
        wire_word(bytes + 64, 9, f->swap);
        wire_word(bytes + 72, 48, f->swap);
    }
    CHECK(mm_memory_stream_init(&memory, bytes, size));
    return receive(out, &memory.stream);
}
static int read_group(void *p, MMU32 scene, int start, MMMovieSceneGroup *out)
{
    StoreFixture *f = (StoreFixture *)p;
    StoreGroup *g = &f->wire[start];
    CHECK(scene == 42);
    if (!start && f->missing_frames)
        return MM_MOVIE_ASSET_NOT_FOUND;
    if (!step(f, 4 + start))
        return MM_MOVIE_IO;
    CHECK(!g->live);
    g->live = 1;
    ++f->live;
    out->handle = g;
    out->count = g->count;
    return MM_MOVIE_OK;
}
static void read_event(void *p, const MMMovieSceneGroup *group, MMU32 i, MMMovieSceneEvent *out)
{
    const unsigned short one = 1;
    StoreFixture *f = (StoreFixture *)p;
    StoreGroup *g = (StoreGroup *)group->handle;
    StoreEvent *e = &g->events[i];
    CHECK(g->live && i < group->count);
    out->big_endian = (*(const unsigned char *)&one == 0) != f->swap;
    out->data = e->fixed;
    out->frame = (MMI32)mm_movie_scene_event_word(out, 0);
    out->kind = (MMI32)mm_movie_scene_event_word(out, 4);
    out->bytes = e->bytes;
    out->data = e->payload.bytes;
}
static void close_group(void *p, MMMovieSceneGroup *group)
{
    release_group(p, group->handle);
}
static int collect_tag(void *p, const MMTag *tag)
{
    StoreFixture *f = (StoreFixture *)p;
    if (!step(f, 8))
        return 0;
    CHECK(f->collected < 24);
    f->tags[f->collected++] = *tag;
    return 1;
}
static int background(void *p, MMTag *tag)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(!tag->cache);
    tag->number += 10;
    f->tags[f->collected++] = *tag;
    return step(f, 9);
}
static int collect_child(void *p, const MMTag *tag, MMU32 child, MMU32 type)
{
    CHECK(tag->number == 29 && child == 0x12345678U && type == 23);
    return step((StoreFixture *)p, 10);
}
static void collect_diagnostic(void *p, int start, int missing)
{
    StoreFixture *f = (StoreFixture *)p;
    (void)start;
    ++f->diagnostics;
    f->missing_background += missing;
}
static void init_collect(StoreFixture *f, int swap)
{
    MMSceneCollectServices *s;
    MMU32 child[] = {7}, bg[] = {1, 999, 23, 9}, camera[] = {0x12345678U},
               sound[] = {80, 2, 0, 2, 1, 0, 999, 23, 1, 2, 3, 999, 23, 2}, pause[] = {3, 230};
    memset(f, 0, sizeof(*f));
    init(&f->packets);
    f->swap = swap;
    s = &f->collect;
    s->context = f;
    s->read.context = f;
    s->read.group = read_group;
    s->read.event = read_event;
    s->read.close = close_group;
    s->packets = f->packets.s;
    s->packets.allocate = packet_allocate;
    s->camera_type = 23;
    s->graph.context = f;
    s->graph.access = collect_access;
    s->graph.payload = actor_payload;
    s->free_sound = free_raw;
    s->insert_tag = collect_tag;
    s->background = background;
    s->insert_child = collect_child;
    s->diagnostic = collect_diagnostic;
    f->wire[0].type = 20;
    f->wire[1].type = 21;
    wire_event(f, 1, 0, child, 1);
    wire_event(f, 1, 4, bg, 4);
    bg[3] = 19;
    wire_event(f, 1, 4, bg, 4);
    wire_event(f, 1, 2, child, 1);
    wire_event(f, 0, 3, camera, 1);
    wire_event(f, 0, 1, sound, 14);
    wire_event(f, 0, 5, pause, 2);
}
static void test_collect(void)
{
    StoreFixture f;
    int attempts, fail, swap;
    MMU32 child = 7;
    for (swap = 0; swap < 2; ++swap)
    {
        init_collect(&f, swap);
        CHECK(mm_scene_collect_tags(42, &f.collect));
        attempts = f.attempts;
        CHECK(f.collected == 6 && !f.live && !f.diagnostics && f.tags[4].cache == NULL && f.tags[5].cache == NULL);
        CHECK(f.tags[0].sid == 4 && f.tags[0].number == 65 && f.tags[1].sid == 8 && f.tags[1].number == 9);
        clean(&f.packets);
        for (fail = 1; fail <= attempts; ++fail)
        {
            init_collect(&f, swap);
            f.fail = fail;
            CHECK(!mm_scene_collect_tags(42, &f.collect));
            CHECK(!f.live && f.collected < 6);
            clean(&f.packets);
        }
        init_collect(&f, swap);
        f.missing_actor = 1;
        CHECK(!mm_scene_collect_tags(42, &f.collect) && !f.live && !f.collected);
        clean(&f.packets);
        init_collect(&f, swap);
        f.missing_frames = 1;
        CHECK(!mm_scene_collect_tags(42, &f.collect) && !f.live && f.collected == 4);
        clean(&f.packets);
        init_collect(&f, swap);
        f.empty_events = 1;
        CHECK(mm_scene_collect_tags(42, &f.collect) && f.collected == 5 && !f.live);
        clean(&f.packets);
        init_collect(&f, swap);
        f.actor_error = 1;
        CHECK(!mm_scene_collect_tags(42, &f.collect) && !f.live && !f.collected && f.log[f.used - 1] == 121);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[1].count = 1;
        CHECK(!mm_scene_collect_tags(42, &f.collect) && f.missing_background == 1 && !f.live);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[1].events[0].bytes = 0;
        CHECK(!mm_scene_collect_tags(42, &f.collect) && !f.live);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[1].events[1].bytes = 20;
        CHECK(!mm_scene_collect_tags(42, &f.collect) && !f.live && f.collected == 2);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[0].events[0].bytes = 0;
        CHECK(!mm_scene_collect_tags(42, &f.collect) && !f.live && f.collected == 4);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[0].events[1].bytes = 16;
        CHECK(!mm_scene_collect_tags(42, &f.collect) && !f.live);
        clean(&f.packets);
        init_collect(&f, swap);
        wire_event(&f, 1, 3, &child, 1);
        wire_event(&f, 0, 0, &child, 1);
        CHECK(mm_scene_collect_tags(42, &f.collect) && f.diagnostics == 2 && !f.live);
        clean(&f.packets);
    }
}
int main(void)
{
    test_store();
    test_collect();
    puts("C89 Scene store and collect checks passed");
    return 0;
}

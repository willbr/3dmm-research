#include "three_mm/scene_store.h"
#include "scene_sound_fixture.h"
typedef union StorePayload {
    void *pointer;
    ThreeMMTag tag;
    unsigned char bytes[192];
} StorePayload;
typedef struct StoreEvent
{
    unsigned char fixed[8];
    int bytes;
    StorePayload payload;
    ThreeMMSceneSound *sound;
} StoreEvent;
typedef struct StoreGroup
{
    int type, count, live;
    StoreEvent events[8];
} StoreGroup;
typedef struct StoreFixture
{
    Fixture packets;
    ThreeMMSceneStoreServices s;
    ThreeMMSceneCollectServices collect;
    ThreeMMSceneOwner owner;
    StoreGroup runtime[2], temp[2], wire[2], list;
    int attempts, fail, created, live, deleted, thumb_deleted, with_thumb, thumb, updated, old_thumb;
    int written, metadata, diagnostics, missing_background, empty_list, list_error, no_background;
    int log[512], used, adopted, adoptions, objects, swap, collected, tag_limit;
    ThreeMMU32 actor_id, text_id;
    ThreeMMTag tags[24];
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
static ThreeMMI32 count(void *p, void *g)
{
    (void)p;
    return ((StoreGroup *)g)->count;
}
static void fixed(void *p, void *g, ThreeMMI32 i, unsigned char *b)
{
    (void)p;
    memcpy(b, ((StoreGroup *)g)->events[i].fixed, 8);
}
static void *data(void *p, void *g, ThreeMMI32 i, ThreeMMI32 *bytes)
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
static void *packet_allocate(void *p, ThreeMMI32 bytes)
{
    StoreFixture *f = (StoreFixture *)p;
    return step(f, 31) ? allocate(&f->packets, bytes) : NULL;
}
static void *packet_data(void *p, void *g, ThreeMMI32 i)
{
    ThreeMMI32 bytes;
    return data(p, g, i, &bytes);
}
static ThreeMMI32 packet_size(void *p, void *g, ThreeMMI32 i)
{
    ThreeMMI32 bytes;
    data(p, g, i, &bytes);
    return bytes;
}
static void release_packet(void *p, ThreeMMSceneSound *sound)
{
    StoreFixture *f = (StoreFixture *)p;
    note(f, 32);
    drop(&f->packets, sound);
}
static int add_scene(void *p, ThreeMMU32 *number)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(*number == 0xffffffffUL);
    *number = 42;
    return step(f, 1);
}
static int add_scene_unchanged(void *p, ThreeMMU32 *number)
{
    (void)p;
    CHECK(*number == 0xffffffffUL);
    return 0;
}
static void delete_scene(void *p, ThreeMMU32 number)
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
static int insert(void *p, void *g, ThreeMMI32 i, ThreeMMI32 frame, int kind, ThreeMMI32 bytes, const void *value)
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
static int sound_child(void *p, ThreeMMU32 scene, ThreeMMU32 child)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(scene == 42);
    note(f, 50 + (int)child);
    return child == 3 || (f->adopted & (1 << child));
}
static int sound_exists(void *p, ThreeMMU32 number)
{
    StoreFixture *f = (StoreFixture *)p;
    note(f, 60 + (int)number);
    return number != 4;
}
static int adopt_sound(void *p, ThreeMMU32 scene, ThreeMMU32 number, ThreeMMU32 child)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(scene == 42 && number == 1 && child == 1);
    if (!step(f, 70))
        return 0;
    f->adopted |= 1 << child;
    ++f->adoptions;
    return 1;
}
static int add_child(void *p, ThreeMMU32 scene, int kind, ThreeMMU32 id, ThreeMMU32 *number)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(scene == 42);
    if (kind == THREE_MM_SCENE_ADD_ACTOR)
        CHECK(id == f->actor_id++);
    else
        CHECK(kind == THREE_MM_SCENE_ADD_TEXT && id == f->text_id++);
    *number = 80 + (ThreeMMU32)kind * 4 + id;
    return step(f, 71 + kind);
}
static void *object(void *p, void *g, ThreeMMI32 i)
{
    (void)p;
    return ((StoreGroup *)g)->events[i].payload.pointer;
}
static int write_object(void *p, void *value, int kind, ThreeMMU32 number, ThreeMMU32 scene)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(value == f && scene == 42 && number >= 80);
    ++f->objects;
    return step(f, 75 + kind);
}
static int save_background(void *p, void *g, ThreeMMI32 i, ThreeMMTag *tag)
{
    StoreFixture *f = (StoreFixture *)p;
    ThreeMMTag *input = &((StoreGroup *)g)->events[i].payload.tag;
    input->number += 10;
    *tag = *input;
    return step(f, 80);
}
static int write_events(void *p, ThreeMMU32 scene, void *frames, void *starts)
{
    StoreFixture *f = (StoreFixture *)p;
    ThreeMMU32 n;
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
    f->owner.owned[THREE_MM_SCENE_THUMBNAIL] = f->with_thumb ? &f->thumb : NULL;
}
static ThreeMMI32 thumbnail_size(void *p, void *thumbnail)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(thumbnail == &f->thumb && f->updated);
    return 65;
}
static int add_thumbnail(void *p, ThreeMMI32 bytes, ThreeMMU32 *number)
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
static int adopt_thumbnail(void *p, ThreeMMU32 scene, ThreeMMU32 number)
{
    CHECK(scene == 42 && number == 55);
    return step((StoreFixture *)p, 85);
}
static void delete_thumbnail(void *p, ThreeMMU32 number)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(number == 55);
    ++f->thumb_deleted;
    note(f, 86);
}
static void clear_loner(void *p, ThreeMMU32 number)
{
    CHECK(number == 55);
    note((StoreFixture *)p, 87);
}
static int write_metadata(void *p, ThreeMMU32 scene)
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
    ThreeMMI32 frame = 3;
    memcpy(e->fixed, &frame, 4);
    memcpy(e->fixed + 4, &kind, 4);
    e->bytes = bytes;
    memcpy(e->payload.bytes, value, (size_t)bytes);
}
static void init_store(StoreFixture *f)
{
    ThreeMMSceneStoreServices *s;
    ThreeMMSceneStoreGroups *g;
    ThreeMMSceneSound *sound;
    ThreeMMI32 pause[] = {3, 230}, camera = 0x12345678, frame = 3, kind = 1;
    ThreeMMTag tag;
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
    s->sound_child = sound_child;
    s->sound_exists = sound_exists;
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
    ThreeMMU32 number;
    int attempts, fail, j, last, background_called;
    init_store(&f);
    CHECK(three_mm_scene_store(&number, &f.s) && number == 42 && !f.deleted && f.metadata);
    attempts = f.attempts;
    CHECK(f.log[f.used - 2] == 110 && f.log[f.used - 1] == 111);
    CHECK(f.packets.refs[1] == 2 && f.packets.refs[3] == 2 && f.packets.refs[4] == 2);
    clean_store(&f);
    for (fail = 1; fail <= attempts; ++fail)
    {
        init_store(&f);
        f.fail = fail;
        CHECK(!three_mm_scene_store(&number, &f.s) && number == 42 && f.deleted == 1);
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
    CHECK(three_mm_scene_store(&number, &f.s) && f.updated && f.metadata);
    clean_store(&f);
    init_store(&f);
    f.s.add_scene = add_scene_unchanged;
    CHECK(!three_mm_scene_store(&number, &f.s) && number == 0xffffffffUL && !f.deleted);
    clean_store(&f);
}
static void wire_word(void *p, ThreeMMU32 value, int swap)
{
    unsigned char b[4];
    int i;
    memcpy(b, &value, 4);
    for (i = 0; i < 4; ++i)
        ((unsigned char *)p)[i] = b[swap ? 3 - i : i];
}
static void wire_event(StoreFixture *f, int start, int kind, const ThreeMMU32 *words, int n)
{
    StoreGroup *g = &f->wire[start];
    StoreEvent *e = &g->events[g->count++];
    int i;
    e->bytes = n * 4;
    wire_word(e->fixed, 3, f->swap);
    wire_word(e->fixed + 4, (ThreeMMU32)kind, f->swap);
    for (i = 0; i < n; ++i)
        wire_word(e->payload.bytes + i * 4, words[i], f->swap);
}
static void *read_group(void *p, ThreeMMU32 scene, int start, int *swap)
{
    StoreFixture *f = (StoreFixture *)p;
    StoreGroup *g = &f->wire[start];
    CHECK(scene == 42);
    if (!step(f, 4 + start))
        return NULL;
    CHECK(!g->live);
    g->live = 1;
    ++f->live;
    *swap = f->swap;
    return g;
}
static int actor_child(void *p, ThreeMMU32 scene, ThreeMMU32 id, ThreeMMU32 *number)
{
    CHECK(scene == 42 && id == 7);
    *number = 77;
    return step((StoreFixture *)p, 6);
}
static void *actor_tags(void *p, ThreeMMU32 number, int *error)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(number == 77);
    *error = f->list_error || !step(f, 7);
    if (f->empty_list)
        return NULL;
    f->list.live = 1;
    ++f->live;
    return &f->list;
}
static void list_tag(void *p, void *list, ThreeMMI32 i, ThreeMMTag *tag)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(list == &f->list);
    *tag = f->packets.input[i].tag;
}
static int collect_tag(void *p, const ThreeMMTag *tag)
{
    StoreFixture *f = (StoreFixture *)p;
    if (!step(f, 8))
        return 0;
    CHECK(f->collected < 24);
    f->tags[f->collected++] = *tag;
    return 1;
}
static int background(void *p, ThreeMMTag *tag)
{
    StoreFixture *f = (StoreFixture *)p;
    CHECK(!tag->cache);
    tag->number += 10;
    f->tags[f->collected++] = *tag;
    return step(f, 9);
}
static int collect_child(void *p, const ThreeMMTag *tag, ThreeMMU32 child, ThreeMMU32 type)
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
    ThreeMMSceneCollectServices *s;
    ThreeMMU32 child[] = {7}, bg[] = {1, 999, 23, 9}, camera[] = {0x12345678U},
               sound[] = {80, 2, 0, 2, 1, 0, 999, 23, 1, 2, 3, 999, 23, 2}, pause[] = {3, 230};
    memset(f, 0, sizeof(*f));
    init(&f->packets);
    f->swap = swap;
    s = &f->collect;
    s->context = f;
    s->groups.context = f;
    s->groups.count = count;
    s->groups.fixed = fixed;
    s->groups.data = data;
    s->groups.release = release_group;
    s->groups.free_raw = free_raw;
    s->packets = f->packets.s;
    s->packets.allocate = packet_allocate;
    s->camera_type = 23;
    s->read_group = read_group;
    s->actor_child = actor_child;
    s->actor_tags = actor_tags;
    s->list_tag = list_tag;
    s->insert_tag = collect_tag;
    s->background = background;
    s->insert_child = collect_child;
    s->diagnostic = collect_diagnostic;
    f->wire[0].type = 20;
    f->wire[1].type = 21;
    f->list.type = 22;
    f->list.count = 2;
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
    ThreeMMU32 child = 7;
    for (swap = 0; swap < 2; ++swap)
    {
        init_collect(&f, swap);
        CHECK(three_mm_scene_collect_tags(42, &f.collect));
        attempts = f.attempts;
        CHECK(f.collected == 6 && !f.live && !f.diagnostics && f.tags[4].cache == NULL && f.tags[5].cache == NULL);
        clean(&f.packets);
        for (fail = 1; fail <= attempts; ++fail)
        {
            init_collect(&f, swap);
            f.fail = fail;
            CHECK(!three_mm_scene_collect_tags(42, &f.collect));
            CHECK(!f.live && f.collected < 6);
            clean(&f.packets);
        }
        init_collect(&f, swap);
        f.empty_list = 1;
        CHECK(three_mm_scene_collect_tags(42, &f.collect) && f.collected == 4 && !f.live);
        clean(&f.packets);
        init_collect(&f, swap);
        f.list_error = 1;
        CHECK(!three_mm_scene_collect_tags(42, &f.collect) && f.log[f.used - 2] == 122 && f.log[f.used - 1] == 121 &&
              !f.live);
        clean(&f.packets);
        init_collect(&f, swap);
        f.list_error = f.empty_list = 1;
        CHECK(!three_mm_scene_collect_tags(42, &f.collect) && !f.live && !f.collected);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[1].count = 1;
        CHECK(!three_mm_scene_collect_tags(42, &f.collect) && f.missing_background == 1 && !f.live);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[1].events[0].bytes = 0;
        CHECK(!three_mm_scene_collect_tags(42, &f.collect) && !f.live);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[1].events[1].bytes = 20;
        CHECK(!three_mm_scene_collect_tags(42, &f.collect) && !f.live && f.collected == 2);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[0].events[0].bytes = 0;
        CHECK(!three_mm_scene_collect_tags(42, &f.collect) && !f.live && f.collected == 4);
        clean(&f.packets);
        init_collect(&f, swap);
        f.wire[0].events[1].bytes = 16;
        CHECK(!three_mm_scene_collect_tags(42, &f.collect) && !f.live);
        clean(&f.packets);
        init_collect(&f, swap);
        wire_event(&f, 1, 3, &child, 1);
        wire_event(&f, 0, 0, &child, 1);
        CHECK(three_mm_scene_collect_tags(42, &f.collect) && f.diagnostics == 2 && !f.live);
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

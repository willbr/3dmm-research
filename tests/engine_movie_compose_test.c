/* Composition ownership, partial application, import remapping and C storage. */
#include "mm/movie_compose.h"
#include "mm/movie_rollcall.h"
#include "mm/scene_data.h"
#include "movie_scene_archive_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
enum
{
    TEMP = 1,
    SAVE,
    CHILD,
    READ_TRANS,
    READ_THUMB,
    THUMB,
    CLONE,
    DELETE_ROOT,
    READ_TABLE,
    APPEND_ACTOR,
    ADJUST,
    ADOPT,
    SOUNDS,
    READ_SCENE,
    START,
    ADD_ACTORS,
    ACTOR_TOOLS,
    TEXT_TOOLS,
    DELETE_CHILD,
    WRITE_TRANS,
    SET_TRANS,
    DIRTY,
    NUKED,
    INVALIDATE,
    ROLL_CHANGED,
    CLOSE_SCENE,
    CLOSE_TAG,
    REBIND_MATERIALS,
    LINK_SOUNDS
};
typedef struct Test Test;
typedef struct Object
{
    Test *test;
    int id, refs;
} Object;
struct Test
{
    MMMovieComposeServices s;
    MMMovieState data;
    MMMovieScenes scenes;
    MMCompositeMovie composite;
    ChunkIndexFixture archives[2];
    Object resources[3], thumbs[3], open, read, tags[3];
    int fail, failAt, calls[40], events[256], n, badRef;
    int withActors, actorIds[2], adjusted[8], adjustCount;
    int deleted[8], deletedCount, adopted, removedActors, closed, diagnostic[4];
    int memoryCalls, allocFailAt, blocks, openTransition, fileTransition;
    int readingSource, actorBlocks, closedTags[2], tagCloses;
};
static void note(Test *t, int operation)
{
    t->events[t->n++] = operation;
}
static int fails(Test *t, int operation)
{
    note(t, operation);
    ++t->calls[operation];
    return t->fail == operation && t->calls[operation] == t->failAt;
}
static int position(Test *t, int operation)
{
    int i;
    for (i = 0; i < t->n; ++i)
        if (t->events[i] == operation)
            return i;
    return -1;
}
static void retain(void *p)
{
    Object *o = p;
    note(o->test, 100 + o->id);
    ++o->refs;
}
static void release(void *p)
{
    Object *o = p;
    note(o->test, 200 + o->id);
    if (--o->refs < 0)
        o->test->badRef = 1;
}
static int temporary(void *p)
{
    return !fails(p, TEMP);
}
static int save(void *p)
{
    return !fails(p, SAVE);
}
static void *file(void *p)
{
    return p;
}
static ChunkIndexFixture *archive(void *p)
{
    Object *o = p;
    if (o->id != 1 && o->id != 2)
        abort();
    return &o->test->archives[o->id - 1];
}
static MMChunkAccess chunks(void *p)
{
    Object *o = p;
    MMChunkAccess access = fixture_chunk_access(archive(p));
    if (fails(o->test, CHILD))
        access.count = 0;
    return access;
}
static MMChunkEdit edit_archive(void *p)
{
    return fixture_chunk_edit(archive(p));
}
static int read_transition(void *p, MMU32 number, MMI32 *out)
{
    Object *o = p;
    if (fails(o->test, READ_TRANS))
        return 0;
    *out = (MMI32)number;
    return 1;
}
static MMI32 transition(void *p)
{
    return ((Object *)p)->test->openTransition;
}
static void *read_thumbnail(void *p, MMU32 number)
{
    Object *o = p;
    Test *t = o->test;
    (void)number;
    if (fails(t, READ_THUMB))
        return 0;
    ++t->thumbs[0].refs;
    return &t->thumbs[0];
}
static void *thumbnail(void *p)
{
    Test *t = ((Object *)p)->test;
    return fails(t, THUMB) ? NULL : &t->thumbs[1];
}
static int clone(void *p, MMU32 number, void *destination, MMU32 *out)
{
    Object *o = p;
    const MMChunkAccess source = fixture_chunk_access(archive(p));
    MMChunkEntry entry;
    MMU32 position;
    if (fails(o->test, CLONE))
        return 0;
    if (mm_chunk_find(&source, MM_MOVIE_CHUNK, number, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&source, position, &entry) != MM_CHUNK_OK)
        abort();
    *out = number + 100;
    fixture_scene_seed_movie(archive(destination), *out, *out * 10, entry.child_count);
    return 1;
}
static int link_sounds(void *file, MMU32 number)
{
    Object *o = file;
    CHECK(number == 120);
    return !fails(o->test, LINK_SOUNDS);
}
static void erase_entry(void *p, const MMChunkEntry *entry)
{
    Test *t = p;
    if (entry->type == MM_MOVIE_CHUNK)
    {
        note(t, DELETE_ROOT);
        t->deleted[t->deletedCount++] = (int)entry->number;
    }
}
static int read_table(void *p, MMU32 number, MMRollCallTable *table)
{
    Object *o = p;
    Test *t = o->test;
    MMRollCallActor row;
    char name[144];
    int i;
    (void)number;
    if (fails(t, READ_TABLE))
        return 0;
    t->readingSource = 1;
    for (i = 0; t->withActors && i < 2; ++i)
    {
        memset(name, 'A' + i, sizeof(name));
        memset(&row, 0, sizeof(row));
        row.data.actor_id = i * 2 + 5;
        row.data.reference_count = 3;
        row.cache = &t->tags[i];
        if (!mm_rollcall_table_add(table, name, sizeof(name), &row))
            abort();
        ++t->tags[i].refs;
    }
    t->readingSource = 0;
    return 1;
}
static void *resize_actors(void *p, void *data, MMU32 before, MMU32 size, int priority)
{
    Test *t = p;
    int fresh = !data;
    void *result;
    (void)before;
    (void)priority;
    if (!t->readingSource && t->fail == APPEND_ACTOR && t->data.actors.count == (MMU32)t->failAt)
        return NULL;
    result = realloc(data, size);
    if (result && fresh)
        ++t->actorBlocks;
    return result;
}
static void free_actors(void *p, void *data, MMU32 size)
{
    Test *t = p;
    (void)size;
    --t->actorBlocks;
    free(data);
}
static void close_tag(MMTag *tag)
{
    Object *o = tag->cache;
    Test *t = o->test;
    t->closedTags[t->tagCloses++] = o->id;
    note(o->test, CLOSE_TAG);
    release(o);
    tag->cache = 0;
}
static MMU32 graph_count(void *p, MMU32 type, MMU32 number)
{
    Test *t = p;
    if (!t->withActors)
        return 0;
    if (type == 0x4d564945U)
        return 2;
    if (type == 0x5343454eU || (type == 0x41435452U && number == 100))
        return 1;
    return 0;
}
static int graph_child(void *p, MMU32 type, MMU32 number, MMU32 i, MMChunkChild *out)
{
    if (i >= graph_count(p, type, number))
        return 0;
    out->slot = i;
    if (type == 0x4d564945U)
    {
        out->type = 0x5343454eU;
        out->number = 11 + i;
    }
    else
    {
        out->type = 0x41435452U;
        out->number = type == 0x41435452U && number == 100 ? 101 : 100;
    }
    return 1;
}
static int adjust(void *p, MMU32 number, MMI32 offset)
{
    Object *o = p;
    Test *t = o->test;
    if (fails(t, ADJUST))
        return 0;
    t->actorIds[number - 100] += offset;
    t->adjusted[t->adjustCount++] = (int)number;
    return 1;
}
static int rebind_materials(void *source, void *destination, MMU32 actor)
{
    Test *t = ((Object *)destination)->test;
    return source == &t->resources[1] && (actor == 100 || actor == 101) && !fails(t, REBIND_MATERIALS);
}
static int insert_link(void *p, MMU32 root, const MMChunkChild *child)
{
    Test *t = p;
    if (root != 10 || child->type != MM_SCENE_CHUNK)
        abort();
    if (fails(t, ADOPT))
        return 0;
    ++t->adopted;
    return 1;
}
static int sounds(void *p, void *f, MMU32 number)
{
    (void)f;
    (void)number;
    return !fails(p, SOUNDS);
}
static void *read_scene(void *p, void *resources, MMU32 number, const struct MMMovieSession *session)
{
    Test *t = p;
    (void)resources;
    (void)number;
    if (session)
        abort();
    if (fails(t, READ_SCENE))
        return 0;
    t->read.refs = 1;
    return &t->read;
}
static int start(void *p)
{
    return !fails(((Object *)p)->test, START);
}
static int add_actors(void *p)
{
    return !fails(((Object *)p)->test, ADD_ACTORS);
}
static void remove_actors(void *p)
{
    ++((Object *)p)->test->removedActors;
}
static void actor_tools(void *p)
{
    fails(p, ACTOR_TOOLS);
}
static void text_tools(void *p)
{
    fails(p, TEXT_TOOLS);
}
static void erase_link(void *p, MMU32 root, const MMChunkChild *child)
{
    if (root != 10 || child->type != MM_SCENE_CHUNK)
        abort();
    fails(p, DELETE_CHILD);
}
static void bad_archive_edit(void *p, int issue)
{
    (void)p;
    fprintf(stderr, "unexpected archive issue %d\n", issue);
    abort();
}
static int write_transition(void *p, MMU32 number, MMI32 value)
{
    Object *o = p;
    (void)number;
    if (fails(o->test, WRITE_TRANS))
        return 0;
    o->test->fileTransition = value;
    return 1;
}
static void set_transition(void *p, MMI32 value)
{
    Object *o = p;
    fails(o->test, SET_TRANS);
    o->test->openTransition = value;
}
static void dirty(void *p)
{
    fails(p, DIRTY);
}
static void nuked(void *p)
{
    fails(p, NUKED);
}
static void invalidate(void *p)
{
    fails(p, INVALIDATE);
}
static void changed(void *p)
{
    fails(p, ROLL_CHANGED);
}
static void diagnostic(void *p, int code)
{
    ++((Test *)p)->diagnostic[code];
}
static void close_scene(void *p)
{
    Object *o = p;
    fails(o->test, CLOSE_SCENE);
    ++o->test->closed;
    release(o);
}
static void *resize(void *p, void *old, MMU32 before, MMU32 bytes, int priority)
{
    Test *t = p;
    void *result;
    (void)before;
    (void)priority;
    ++t->memoryCalls;
    if (t->allocFailAt && t->memoryCalls >= t->allocFailAt)
        return 0;
    result = realloc(old, bytes);
    if (result && !old)
        ++t->blocks;
    return result;
}
static void free_buffer(void *p, void *memory, MMU32 bytes)
{
    Test *t = p;
    (void)bytes;
    --t->blocks;
    free(memory);
}
static void object(Object *o, Test *t, int id, int refs)
{
    o->test = t;
    o->id = id;
    o->refs = refs;
}
static void init(Test *t)
{
    MMMovieComposeServices *s = &t->s;
    MMStorageAllocator actor_allocator = {NULL, resize_actors, free_actors};
    int i;
    memset(t, 0, sizeof(*t));
    t->failAt = 1;
    for (i = 0; i < 3; ++i)
    {
        object(&t->resources[i], t, 1 + i, 1);
        object(&t->thumbs[i], t, 4 + i, i == 1);
        object(&t->tags[i], t, 20 + i, i == 2);
    }
    object(&t->open, t, 13, 1);
    object(&t->read, t, 14, 0);
    actor_allocator.context = t;
    if (mm_rollcall_table_init(&t->data.actors, sizeof(MMRollCallActor), 1, 0x0303, 0,
                               &actor_allocator) != MM_STORAGE_OK)
        abort();
    t->data.actors.storage.min_grow[0] = 0;
    t->actorIds[0] = 5;
    t->actorIds[1] = 7;
    t->data.resources = &t->resources[0];
    t->data.movieNumber = 10;
    t->data.actorIdLimit = 10;
    t->data.sceneCount = 2;
    fixture_scene_seed_movie(&t->archives[0], 10, 100, 2);
    fixture_scene_seed_movie(&t->archives[1], 20, 200, 1);
    fixture_scene_seed_movie(&t->archives[1], 30, 300, 0);
    t->archives[0].observer = t;
    t->archives[0].before_insert = insert_link;
    t->archives[0].before_unlink = erase_link;
    t->archives[0].before_delete = erase_entry;
    t->scenes.index = 1;
    t->scenes.open = &t->open;
    t->openTransition = 4;
    s->context = t;
    s->data = &t->data;
    s->scenes = &t->scenes;
    s->temporary_before_save = 1;
    s->use_temporary = temporary;
    s->autosave = save;
    s->retain = retain;
    s->release = release;
    s->read_transition = read_transition;
    s->transition = transition;
    s->read_thumbnail = read_thumbnail;
    s->thumbnail = thumbnail;
    s->clone_movie = clone;
    s->read_rollcall = read_table;
    s->close_tag = close_tag;
    s->adjust_actor = adjust;
    s->graph.context = t;
    s->graph.children = graph_count;
    s->graph.child = graph_child;
    s->adopt_sounds = sounds;
    s->start_scene = start;
    s->add_actors = add_actors;
    s->remove_actors = remove_actors;
    s->actor_tools = actor_tools;
    s->text_tools = text_tools;
    s->write_transition = write_transition;
    s->set_transition = set_transition;
    s->dirty = dirty;
    s->scene_nuked = nuked;
    s->invalidate = invalidate;
    s->rollcall_changed = changed;
    s->diagnostic = diagnostic;
    s->scene_control.context = t;
    s->scene_control.state = &t->scenes;
    s->scene_control.close = close_scene;
    s->scene_control.file = file;
    s->scene_control.chunks = chunks;
    s->scene_control.edit = edit_archive;
    s->scene_control.read = read_scene;
    s->scene_control.archive_issue = bad_archive_edit;
    s->allocator.context = t;
    s->allocator.resize = resize;
    s->allocator.release = free_buffer;
}
static int add_external(Test *t, MMU32 root, MMI32 scene_count)
{
    MMMovieState *data = &t->data;
    MMI32 index = (MMI32)t->composite.scene_count;
    int ok;
    data->resources = &t->resources[1];
    data->movieNumber = root;
    data->actorIdLimit = 5;
    data->sceneCount = scene_count;
    t->scenes.index = 0;
    ok = mm_movie_compose_add(&t->composite, &index, &t->s);
    data->resources = &t->resources[0];
    data->movieNumber = 10;
    data->actorIdLimit = 10;
    data->sceneCount = 2;
    t->scenes.index = 1;
    return ok;
}
static int apply_setup(Test *t, int external, MMI32 base_limit)
{
    MMI32 index = 0;
    MMU32 i;
    t->data.actorIdLimit = base_limit;
    if (!mm_movie_compose_add(&t->composite, &index, &t->s) || (external && !add_external(t, 20, 1)))
        return 0;
    for (i = 0; i < t->composite.scene_count; ++i)
        if (!mm_composite_set_transition(&t->composite, i, 6 + (MMI32)i))
            return 0;
    memset(t->calls, 0, sizeof(t->calls));
    t->n = 0;
    return 1;
}
static int cleanup_tags(Test *t)
{
    MMTag tag;
    MMRollCallActor row;
    MMU32 i;
    CHECK(t->actorBlocks == 2);
    for (i = 1; i < t->data.actors.count; ++i)
    {
        CHECK(mm_rollcall_table_extra(&t->data.actors, i, &row));
        tag = mm_movie_rollcall_tag(&row);
        close_tag(&tag);
    }
    CHECK(!t->tags[0].refs && !t->tags[1].refs && t->tags[2].refs == 1 && !t->badRef && !t->blocks);
    CHECK(mm_rollcall_table_dispose(&t->data.actors) == MM_STORAGE_OK && !t->actorBlocks);
    return 0;
}
static int seed_destination(Test *t)
{
    MMRollCallActor row;
    memset(&row, 0, sizeof(row));
    row.data.actor_id = 1;
    row.data.reference_count = 2;
    row.cache = &t->tags[2];
    return mm_rollcall_table_add(&t->data.actors, "Base", 4, &row);
}
static int adding(void)
{
    Test t;
    MMComposeScene scene;
    MMI32 index;
    int i;
    const int faults[] = {0, TEMP, SAVE, CHILD, READ_TRANS, READ_THUMB, THUMB};
    for (i = 0; i < (int)(sizeof(faults) / sizeof(faults[0])); ++i)
    {
        init(&t);
        t.fail = faults[i];
        index = 0;
        CHECK(mm_movie_compose_add(&t.composite, &index, &t.s) == !i);
        CHECK(t.events[0] == TEMP && (i == 1 || t.events[1] == SAVE));
        if (!i)
        {
            CHECK(index == 2 && t.composite.scene_count == 2 && t.composite.movie_count == 1 &&
                  t.resources[0].refs == 2);
            CHECK(t.thumbs[0].refs == 1 && t.thumbs[1].refs == 2 &&
                  mm_composite_scene(&t.composite, 1, &scene) && scene.transition == 4);
            mm_composite_empty(&t.composite, release);
        }
        else
            CHECK(!index && !t.composite.scene_count && !t.composite.movie_count);
        CHECK(t.resources[0].refs == 1 && !t.thumbs[0].refs && t.thumbs[1].refs == 1 && !t.badRef && !t.blocks);
    }
    for (i = 1; i <= 2; ++i)
    {
        init(&t);
        t.allocFailAt = i;
        index = 0;
        CHECK(!mm_movie_compose_add(&t.composite, &index, &t.s) && !index && !t.blocks);
        CHECK(!t.composite.scene_count && !t.composite.movie_count && !t.calls[CHILD] && t.resources[0].refs == 1);
    }
    init(&t);
    CHECK(apply_setup(&t, 0, 10));
    t.fail = THUMB;
    index = 1;
    CHECK(!mm_movie_compose_add(&t.composite, &index, &t.s) && index == 1);
    CHECK(t.composite.scene_count == 2 && t.composite.movie_count == 1 && t.blocks == 2 &&
          t.thumbs[0].refs == 1 && t.thumbs[1].refs == 2);
    CHECK(mm_composite_scene(&t.composite, 0, &scene) && scene.transition == 6);
    CHECK(mm_composite_scene(&t.composite, 1, &scene) && scene.transition == 7);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.blocks);
    init(&t);
    t.s.temporary_before_save = 0;
    t.data.sceneCount = 0;
    index = 0;
    CHECK(mm_movie_compose_add(&t.composite, &index, &t.s) && !index && !t.calls[TEMP] && t.calls[SAVE] == 1);
    CHECK(!t.composite.scene_count && t.composite.movie_count == 1);
    mm_composite_empty(&t.composite, release);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.badRef && !t.blocks && t.resources[0].refs == 1);
    return 0;
}
static int merging(void)
{
    Test t;
    int i, offset;
    MMRollCallActor row;
    const void *name;
    MMU32 count;
    for (offset = -3; offset <= 10; offset += 13)
    {
        init(&t);
        CHECK(seed_destination(&t));
        t.withActors = 1;
        CHECK(mm_movie_compose_merge(10, offset, &t.s));
        CHECK(t.data.actors.count == 3 && !t.blocks);
        CHECK(mm_rollcall_table_extra(&t.data.actors, 1, &row));
        CHECK(row.data.actor_id == 5 + offset && !row.data.reference_count);
        CHECK(mm_rollcall_table_name(&t.data.actors, 1, &name, &count) && count == 144 &&
              ((const char *)name)[143] == 'A');
        CHECK(t.adjustCount == (offset > 0 ? 2 : 0) && t.actorIds[0] == 5 + (offset > 0 ? offset : 0));
        if (offset > 0)
            CHECK(t.adjusted[0] == 100 && t.adjusted[1] == 101 && t.actorIds[1] == 17);
        CHECK(!cleanup_tags(&t));
    }
    for (i = 1; i <= 2; ++i)
    {
        init(&t);
        CHECK(seed_destination(&t));
        t.withActors = 1;
        t.fail = APPEND_ACTOR;
        t.failAt = i;
        CHECK(!mm_movie_compose_merge(10, 10, &t.s) && t.data.actors.count == 1);
        CHECK(t.tagCloses == 2 && t.closedTags[0] == (i == 1 ? 21 : 20) &&
              t.closedTags[1] == (i == 1 ? 20 : 21) && !t.adjustCount);
        CHECK(!cleanup_tags(&t));
    }
    init(&t);
    CHECK(seed_destination(&t));
    t.withActors = 1;
    t.fail = ADJUST;
    t.failAt = 2;
    CHECK(!mm_movie_compose_merge(10, 10, &t.s) && t.data.actors.count == 1 && t.actorIds[0] == 15 &&
          t.actorIds[1] == 7);
    CHECK(t.tagCloses == 2 && t.closedTags[0] == 21 && t.closedTags[1] == 20 && !cleanup_tags(&t));
    init(&t);
    CHECK(seed_destination(&t));
    t.withActors = 1;
    t.allocFailAt = 2;
    CHECK(!mm_movie_compose_merge(10, 10, &t.s) && !t.adjustCount && t.data.actors.count == 1 && !t.blocks);
    CHECK(!cleanup_tags(&t));
    init(&t);
    CHECK(seed_destination(&t));
    t.withActors = 1;
    t.allocFailAt = 1;
    CHECK(mm_movie_compose_merge(10, 10, &t.s) && !t.adjustCount && t.data.actors.count == 3 && !t.blocks);
    CHECK(!cleanup_tags(&t));
    init(&t);
    CHECK(seed_destination(&t));
    t.fail = READ_TABLE;
    CHECK(!mm_movie_compose_merge(10, 10, &t.s) && t.data.actors.count == 1 && !t.tagCloses && !cleanup_tags(&t));
    return 0;
}
static int applying(void)
{
    Test t;
    const MMChunkChild background = {0x424b4744UL, 2, 0};
    MMChunkEdit edit;
    MMChunkEntry asset;
    MMChunkAccess index;
    MMU32 row;
    int i;
    const int failures[] = {0, CLONE, READ_TABLE, CHILD, ADOPT, SOUNDS, WRITE_TRANS};
    for (i = 0; i < (int)(sizeof(failures) / sizeof(failures[0])); ++i)
    {
        init(&t);
        CHECK(apply_setup(&t, 1, 10));
        t.fail = failures[i];
        CHECK(mm_movie_compose_apply(&t.composite, &t.s) == !i);
        CHECK(t.resources[0].refs == 2 && t.resources[1].refs == 2 && !t.read.refs && t.blocks == 2 && !t.badRef);
        CHECK(t.deletedCount == (i == 1 ? 0 : 1));
        CHECK(t.archives[0].count == 3U + t.adopted);
        if (t.deletedCount)
            CHECK(t.deleted[0] == 120);
        if (!i)
        {
            CHECK(t.data.sceneCount == 3 && t.data.actorIdLimit == 15 && t.scenes.index == 1 && t.openTransition == 7);
            CHECK(t.calls[ACTOR_TOOLS] == 1 && t.calls[TEXT_TOOLS] == 1 && t.calls[DIRTY] == 1 &&
                  t.calls[ROLL_CHANGED] == 1);
            CHECK(position(&t, DIRTY) < position(&t, INVALIDATE) &&
                  position(&t, INVALIDATE) < position(&t, ROLL_CHANGED));
        }
        else
            CHECK(t.data.sceneCount == 2 && t.data.actorIdLimit == 10 && !t.calls[DIRTY]);
        if (i >= 3 && i <= 5)
            CHECK(t.openTransition == 7);
        if (i == 5)
            CHECK(t.adopted == 1 && !t.calls[READ_SCENE]);
        mm_composite_empty(&t.composite, release);
        CHECK(!t.blocks && t.resources[0].refs == 1 && t.resources[1].refs == 1);
    }
    init(&t);
    CHECK(apply_setup(&t, 1, 10));
    t.allocFailAt = t.memoryCalls + 1;
    CHECK(!mm_movie_compose_apply(&t.composite, &t.s) && !t.calls[CLONE] && !t.deletedCount && t.blocks == 2);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.blocks);
    for (i = 0; i < 2; ++i)
    {
        init(&t);
        CHECK(apply_setup(&t, 1, 0) && seed_destination(&t));
        t.withActors = 1;
        t.s.rebind_actor_materials = rebind_materials;
        t.fail = i ? REBIND_MATERIALS : 0;
        t.failAt = 2;
        CHECK(mm_movie_compose_apply(&t.composite, &t.s) == !i);
        mm_composite_empty(&t.composite, release);
        CHECK(t.calls[REBIND_MATERIALS] == 2 && !t.adjustCount && t.deletedCount == 1 && t.deleted[0] == 120);
        CHECK(t.data.actors.count == (i ? 1U : 3U) && t.adopted == !i && !cleanup_tags(&t));
    }
    init(&t);
    CHECK(apply_setup(&t, 1, 10));
    t.s.link_imported_sounds = link_sounds;
    t.fail = LINK_SOUNDS;
    CHECK(!mm_movie_compose_apply(&t.composite, &t.s));
    CHECK(t.calls[LINK_SOUNDS] == 1 && position(&t, CLONE) < position(&t, LINK_SOUNDS) &&
          position(&t, LINK_SOUNDS) < position(&t, DELETE_ROOT));
    CHECK(!t.calls[READ_TABLE] && !t.adopted && t.deletedCount == 1 && t.deleted[0] == 120);
    mm_composite_empty(&t.composite, release);
    init(&t);
    CHECK(apply_setup(&t, 1, 2147483647));
    CHECK(mm_movie_compose_apply(&t.composite, &t.s) && t.data.actorIdLimit == -2147483644);
    mm_composite_empty(&t.composite, release);
    init(&t);
    CHECK(apply_setup(&t, 1, 10) && add_external(&t, 30, 0));
    t.fail = READ_TABLE;
    t.failAt = 2;
    CHECK(!mm_movie_compose_apply(&t.composite, &t.s) && t.deletedCount == 2 && t.deleted[0] == 130 &&
          t.deleted[1] == 120);
    CHECK(t.resources[0].refs == 2 && t.resources[1].refs == 3);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.blocks && t.resources[0].refs == 1 && t.resources[1].refs == 1);
    for (i = READ_SCENE; i <= ADD_ACTORS; ++i)
    {
        init(&t);
        CHECK(apply_setup(&t, 1, 10));
        t.fail = i;
        CHECK(mm_movie_compose_apply(&t.composite, &t.s) && t.diagnostic[MM_COMPOSE_IMPORT_ACTORS] == 1);
        CHECK(t.calls[ACTOR_TOOLS] == 1 && t.calls[TEXT_TOOLS] == 1 && !t.read.refs);
        CHECK(t.calls[ADD_ACTORS] == (i == ADD_ACTORS));
        mm_composite_empty(&t.composite, release);
    }
    init(&t);
    fixture_chunk_add(&t.archives[0], background.type, background.number);
    edit = edit_archive(&t.resources[0]);
    t.archives[0].before_insert = NULL;
    CHECK(mm_chunk_adopt(&edit, MM_SCENE_CHUNK, 100, &background, 1) == MM_CHUNK_OK);
    t.archives[0].before_insert = insert_link;
    CHECK(apply_setup(&t, 1, 10));
    CHECK(mm_composite_remove_scene(&t.composite, 0) && mm_composite_move_scene(&t.composite, 2, 0));
    CHECK(mm_movie_compose_apply(&t.composite, &t.s) && t.scenes.index == 0 && t.data.sceneCount == 2 &&
          t.removedActors == 1);
    CHECK(t.calls[DELETE_CHILD] == 1 && t.open.refs == 1);
    CHECK(t.archives[0].count == 4 && fixture_scene_slot(&t.archives[0], 101) == 0 &&
          fixture_scene_slot(&t.archives[0], 1200) == 1 && fixture_scene_slot(&t.archives[0], 100) == -1);
    index = chunks(&t.resources[0]);
    CHECK(mm_chunk_find(&index, background.type, background.number, &row) == MM_CHUNK_OK &&
          mm_chunk_at(&index, row, &asset) == MM_CHUNK_OK && (asset.flags & MM_CHUNK_LONER) && !asset.parent_count);
    mm_composite_empty(&t.composite, release);
    init(&t);
    CHECK(apply_setup(&t, 0, 10));
    CHECK(mm_composite_remove_scene(&t.composite, 0) && mm_composite_remove_scene(&t.composite, 0));
    t.fail = START;
    t.allocFailAt = t.memoryCalls + 1; /* A local-only apply needs no clone table. */
    CHECK(mm_movie_compose_apply(&t.composite, &t.s) && !t.data.sceneCount && !t.scenes.open &&
          t.scenes.index == -1);
    CHECK(t.removedActors == 2 && t.diagnostic[MM_COMPOSE_REMOVE_ACTORS] == 1 && t.closed == 1 &&
          t.calls[NUKED] == 1);
    CHECK(!t.open.refs && !t.read.refs && !t.badRef);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.blocks);
    return 0;
}
static int emptying(void)
{
    Test t;
    MMComposeScene scene;
    int start;
    init(&t);
    CHECK(apply_setup(&t, 1, 10));
    CHECK(mm_composite_move_scene(&t.composite, 0, 3));
    CHECK(mm_composite_scene(&t.composite, 2, &scene) && scene.number == 100);
    CHECK(mm_composite_remove_scene(&t.composite, 0) && !mm_composite_remove_scene(&t.composite, 2));
    CHECK(mm_composite_scene(&t.composite, 2, &scene) && scene.number == 101 && scene.deleted);
    CHECK(!mm_composite_move_scene(&t.composite, 3, 0) && !mm_composite_set_transition(&t.composite, 3, 0));
    start = t.n;
    mm_composite_empty(&t.composite, release);
    CHECK(t.events[start] == 205 && t.events[start + 1] == 204 && t.events[start + 2] == 205 &&
          t.events[start + 3] == 202 && t.events[start + 4] == 201);
    CHECK(!t.composite.scene_count && !t.composite.movie_count && t.thumbs[1].refs == 1 &&
          t.resources[0].refs == 1 && t.resources[1].refs == 1 && !t.badRef && !t.blocks);
    mm_composite_empty(&t.composite, release);
    return 0;
}
int main(void)
{
    if (adding() || merging() || applying() || emptying())
        return 1;
    puts("Movie composition, ownership, rollback and shared actor remapping passed");
    return 0;
}

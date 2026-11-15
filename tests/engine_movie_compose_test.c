/* Composition ownership, partial application, import remapping and C storage. */
#include "three_mm/movie_compose.h"
#include "three_mm/movie_rollcall.h"
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
    NEW_SCENES,
    NEW_MOVIES,
    APPEND_MOVIE,
    CHILD,
    READ_TRANS,
    READ_THUMB,
    INSERT,
    DUP_LIST,
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
    CHANGE_CHILD,
    WRITE_TRANS,
    SET_TRANS,
    DIRTY,
    NUKED,
    INVALIDATE,
    ROLL_CHANGED,
    CLOSE_SCENE,
    ERASE_ROW,
    CLOSE_TAG
};
typedef struct Test Test;
typedef struct Object
{
    Test *test;
    int id, refs;
} Object;
typedef struct List
{
    Object object;
    int movies, count;
    ThreeMMComposeScene scenes[8];
    ThreeMMComposeMovie descriptors[4];
} List;
typedef struct Table
{
    Object object;
    int count;
    ThreeMMRollCallActor rows[8];
} Table;
struct Test
{
    ThreeMMMovieComposeServices s;
    ThreeMMMovieState data;
    ThreeMMMovieScenes scenes;
    ThreeMMCompositeMovie composite;
    Object resources[3], thumbs[3], open, read, tags[3];
    List sceneList, movieList, copy;
    Table destination, source;
    int fail, failAt, calls[40], events[256], n, badRef;
    int withActors, actorIds[2], adjusted[8], adjustCount;
    int deleted[8], deletedCount, adopted, removedActors, closed, diagnostic[4];
    int memoryCalls, allocFailAt, blocks, openTransition, fileTransition;
};
static Test *active;
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
static void *new_list(int movies)
{
    List *l = movies ? &active->movieList : &active->sceneList;
    if (fails(active, movies ? NEW_MOVIES : NEW_SCENES))
        return 0;
    l->object.refs = 1;
    return l;
}
static ThreeMMI32 count(void *p)
{
    return ((List *)p)->count;
}
static void scene_row(void *p, ThreeMMI32 i, ThreeMMComposeScene *out)
{
    *out = ((List *)p)->scenes[i];
}
static void movie_row(void *p, ThreeMMI32 i, ThreeMMComposeMovie *out)
{
    *out = ((List *)p)->descriptors[i];
}
static int insert(void *p, ThreeMMI32 i, const ThreeMMComposeScene *row)
{
    List *l = p;
    if (fails(l->object.test, INSERT))
        return 0;
    memmove(l->scenes + i + 1, l->scenes + i, (size_t)(l->count - i) * sizeof(*row));
    l->scenes[i] = *row;
    ++l->count;
    return 1;
}
static int append_movie(void *p, const ThreeMMComposeMovie *row, ThreeMMI32 *index)
{
    List *l = p;
    if (fails(l->object.test, APPEND_MOVIE))
        return 0;
    *index = l->count;
    l->descriptors[l->count++] = *row;
    return 1;
}
static void put_movie(void *p, ThreeMMI32 i, const ThreeMMComposeMovie *row)
{
    ((List *)p)->descriptors[i] = *row;
}
static void erase(void *p, ThreeMMI32 i)
{
    List *l = p;
    --l->count;
    if (l->movies)
        memmove(l->descriptors + i, l->descriptors + i + 1, (size_t)(l->count - i) * sizeof(l->descriptors[0]));
    else
        memmove(l->scenes + i, l->scenes + i + 1, (size_t)(l->count - i) * sizeof(l->scenes[0]));
}
static void *duplicate_movies(void *p)
{
    List *l = p;
    Test *t = l->object.test;
    if (fails(t, DUP_LIST))
        return 0;
    t->copy = *l;
    t->copy.object.id = 12;
    t->copy.object.refs = 1;
    return &t->copy;
}
static void *file(void *p)
{
    return p;
}
static int child(void *p, ThreeMMU32 root, ThreeMMU32 slot, ThreeMMU32 *out)
{
    Object *o = p;
    if (fails(o->test, CHILD))
        return 0;
    *out = (root == 10 ? 100 : 200) + slot;
    return 1;
}
static int read_transition(void *p, ThreeMMU32 number, ThreeMMI32 *out)
{
    Object *o = p;
    if (fails(o->test, READ_TRANS))
        return 0;
    *out = (ThreeMMI32)number;
    return 1;
}
static ThreeMMI32 transition(void *p)
{
    return ((Object *)p)->test->openTransition;
}
static void *read_thumbnail(void *p, ThreeMMU32 number)
{
    Object *o = p;
    Test *t = o->test;
    (void)number;
    if (fails(t, READ_THUMB))
        return 0;
    t->thumbs[0].refs = 1;
    return &t->thumbs[0];
}
static void *thumbnail(void *p)
{
    return &((Object *)p)->test->thumbs[1];
}
static int clone(void *p, ThreeMMU32 number, void *destination, ThreeMMU32 *out)
{
    Object *o = p;
    (void)destination;
    if (fails(o->test, CLONE))
        return 0;
    *out = number + 100;
    return 1;
}
static void delete_root(void *p, ThreeMMU32 number)
{
    Object *o = p;
    note(o->test, DELETE_ROOT);
    o->test->deleted[o->test->deletedCount++] = (int)number;
}
static void *read_table(void *p, ThreeMMU32 number)
{
    Object *o = p;
    Test *t = o->test;
    int i;
    (void)number;
    if (fails(t, READ_TABLE))
        return 0;
    t->source.object.refs = 1;
    t->source.count = t->withActors ? 2 : 0;
    for (i = 0; i < t->source.count; ++i)
    {
        memset(&t->source.rows[i], 0, sizeof(t->source.rows[i]));
        t->source.rows[i].data.actor_id = i * 2 + 5;
        t->source.rows[i].data.reference_count = 3;
        t->source.rows[i].cache = &t->tags[i];
        ++t->tags[i].refs;
    }
    return &t->source;
}
static ThreeMMI32 actor_count(void *p)
{
    return ((Table *)p)->count;
}
static void actor_row(void *p, ThreeMMI32 i, ThreeMMRollCallActor *out)
{
    *out = ((Table *)p)->rows[i];
}
static int append_actor(void *p, void *source, ThreeMMI32 i, const ThreeMMRollCallActor *row)
{
    Table *a = p;
    Test *t = a->object.test;
    (void)source;
    (void)i;
    if (fails(t, APPEND_ACTOR))
        return 0;
    a->rows[a->count++] = *row;
    return 1;
}
static void erase_actor(void *p, ThreeMMI32 i)
{
    Table *a = p;
    note(a->object.test, ERASE_ROW);
    --a->count;
    memmove(a->rows + i, a->rows + i + 1, (size_t)(a->count - i) * sizeof(a->rows[0]));
}
static void close_tag(ThreeMMTag *tag)
{
    Object *o = tag->cache;
    note(o->test, CLOSE_TAG);
    release(o);
    tag->cache = 0;
}
static ThreeMMU32 graph_count(void *p, ThreeMMU32 type, ThreeMMU32 number)
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
static int graph_child(void *p, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 i, ThreeMMChunkChild *out)
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
static int adjust(void *p, ThreeMMU32 number, ThreeMMI32 offset)
{
    Object *o = p;
    Test *t = o->test;
    if (fails(t, ADJUST))
        return 0;
    t->actorIds[number - 100] += offset;
    t->adjusted[t->adjustCount++] = (int)number;
    return 1;
}
static int adopt(void *p, ThreeMMU32 root, ThreeMMU32 number, ThreeMMU32 slot)
{
    Object *o = p;
    (void)root;
    (void)number;
    (void)slot;
    if (fails(o->test, ADOPT))
        return 0;
    ++o->test->adopted;
    return 1;
}
static int sounds(void *p, void *f, ThreeMMU32 number)
{
    (void)f;
    (void)number;
    return !fails(p, SOUNDS);
}
static void *read_scene(void *p, void *resources, ThreeMMU32 number)
{
    Test *t = p;
    (void)resources;
    (void)number;
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
static void delete_child(void *p, ThreeMMU32 root, ThreeMMU32 number, ThreeMMU32 slot)
{
    Object *o = p;
    (void)root;
    (void)number;
    (void)slot;
    fails(o->test, DELETE_CHILD);
}
static void change_child(void *p, ThreeMMU32 root, ThreeMMU32 number, ThreeMMU32 before, ThreeMMU32 after)
{
    Object *o = p;
    (void)root;
    (void)number;
    (void)before;
    (void)after;
    fails(o->test, CHANGE_CHILD);
}
static int write_transition(void *p, ThreeMMU32 number, ThreeMMI32 value)
{
    Object *o = p;
    (void)number;
    if (fails(o->test, WRITE_TRANS))
        return 0;
    o->test->fileTransition = value;
    return 1;
}
static void set_transition(void *p, ThreeMMI32 value)
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
static void *resize(void *p, void *old, ThreeMMU32 before, ThreeMMU32 bytes, int priority)
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
static void free_buffer(void *p, void *memory, ThreeMMU32 bytes)
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
    ThreeMMMovieComposeServices *s = &t->s;
    int i;
    memset(t, 0, sizeof(*t));
    active = t;
    t->failAt = 1;
    for (i = 0; i < 3; ++i)
    {
        object(&t->resources[i], t, 1 + i, 1);
        object(&t->thumbs[i], t, 4 + i, i == 1);
        object(&t->tags[i], t, 20 + i, i == 2);
    }
    object(&t->sceneList.object, t, 10, 0);
    object(&t->movieList.object, t, 11, 0);
    t->movieList.movies = 1;
    object(&t->open, t, 13, 1);
    object(&t->read, t, 14, 0);
    object(&t->source.object, t, 15, 0);
    object(&t->destination.object, t, 16, 1);
    t->destination.count = 1;
    t->destination.rows[0].data.actor_id = 1;
    t->destination.rows[0].data.reference_count = 2;
    t->destination.rows[0].cache = &t->tags[2];
    t->actorIds[0] = 5;
    t->actorIds[1] = 7;
    t->data.resources = &t->resources[0];
    t->data.actors = &t->destination;
    t->data.movieNumber = 10;
    t->data.actorIdLimit = 10;
    t->data.sceneCount = 2;
    t->scenes.index = 1;
    t->scenes.open = &t->open;
    t->openTransition = 4;
    s->context = t;
    s->data = &t->data;
    s->scenes = &t->scenes;
    s->temporary_before_save = 1;
    s->use_temporary = temporary;
    s->autosave = save;
    s->new_list = new_list;
    s->duplicate_movies = duplicate_movies;
    s->count = count;
    s->scene = scene_row;
    s->movie = movie_row;
    s->insert_scene = insert;
    s->append_movie = append_movie;
    s->put_movie = put_movie;
    s->erase = erase;
    s->retain = retain;
    s->release = release;
    s->file = file;
    s->scene_child = child;
    s->read_transition = read_transition;
    s->transition = transition;
    s->read_thumbnail = read_thumbnail;
    s->thumbnail = thumbnail;
    s->clone_movie = clone;
    s->delete_movie = delete_root;
    s->read_rollcall = read_table;
    s->actor_count = actor_count;
    s->actor = actor_row;
    s->append_actor = append_actor;
    s->erase_actor = erase_actor;
    s->close_tag = close_tag;
    s->adjust_actor = adjust;
    s->graph.context = t;
    s->graph.children = graph_count;
    s->graph.child = graph_child;
    s->adopt_scene = adopt;
    s->adopt_sounds = sounds;
    s->read_scene = read_scene;
    s->start_scene = start;
    s->add_actors = add_actors;
    s->remove_actors = remove_actors;
    s->actor_tools = actor_tools;
    s->text_tools = text_tools;
    s->delete_scene_child = delete_child;
    s->change_child = change_child;
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
    s->allocator.context = t;
    s->allocator.resize = resize;
    s->allocator.release = free_buffer;
}
static void apply_setup(Test *t, int external)
{
    int i;
    ThreeMMComposeScene row;
    ThreeMMComposeMovie movie;
    t->composite.scenes = &t->sceneList;
    t->composite.movies = &t->movieList;
    t->sceneList.object.refs = t->movieList.object.refs = 1;
    t->sceneList.count = external ? 3 : 2;
    t->movieList.count = external ? 2 : 1;
    memset(&row, 0, sizeof(row));
    for (i = 0; i < t->sceneList.count; ++i)
    {
        row.movie = i == 2 ? 1 : 0;
        row.number = 100 + (ThreeMMU32)i;
        row.child = i == 2 ? 0 : (ThreeMMU32)i;
        row.transition = 6 + i;
        t->sceneList.scenes[i] = row;
    }
    for (i = 0; i < t->movieList.count; ++i)
    {
        movie.resources = &t->resources[i];
        ++t->resources[i].refs;
        movie.number = 10 + (ThreeMMU32)i * 10;
        movie.actor_limit = i ? 5 : 10;
        t->movieList.descriptors[i] = movie;
    }
}
static int cleanup_tags(Test *t)
{
    ThreeMMTag tag;
    int i;
    for (i = 1; i < t->destination.count; ++i)
    {
        tag = three_mm_movie_rollcall_tag(&t->destination.rows[i]);
        close_tag(&tag);
    }
    CHECK(!t->tags[0].refs && !t->tags[1].refs && t->tags[2].refs == 1 && !t->badRef && !t->blocks);
    return 0;
}
static int adding(void)
{
    Test t;
    ThreeMMI32 index;
    int i;
    const int faults[] = {0, TEMP, SAVE, NEW_SCENES, NEW_MOVIES, APPEND_MOVIE, CHILD, READ_TRANS, READ_THUMB, INSERT};
    for (i = 0; i < 10; ++i)
    {
        init(&t);
        t.fail = faults[i];
        index = 0;
        CHECK(three_mm_movie_compose_add(&t.composite, &index, &t.s) == !i);
        CHECK(t.events[0] == TEMP && (i == 1 || t.events[1] == SAVE));
        if (!i)
        {
            CHECK(index == 2 && t.sceneList.count == 2 && t.movieList.count == 1 && t.resources[0].refs == 2);
            CHECK(t.thumbs[0].refs == 1 && t.thumbs[1].refs == 2 && t.sceneList.scenes[1].transition == 4);
            three_mm_composite_empty(&t.composite, &t.s);
        }
        else
            CHECK(index == 0 && !t.composite.scenes && !t.composite.movies);
        CHECK(t.resources[0].refs == 1 && !t.thumbs[0].refs && t.thumbs[1].refs == 1 && !t.badRef);
    }
    init(&t);
    t.fail = INSERT;
    t.failAt = 2;
    index = 0;
    CHECK(!three_mm_movie_compose_add(&t.composite, &index, &t.s) && index == 0 && !t.thumbs[0].refs &&
          t.thumbs[1].refs == 1);
    CHECK(!t.composite.scenes && !t.composite.movies && t.resources[0].refs == 1);
    init(&t);
    t.s.temporary_before_save = 0;
    t.data.sceneCount = 0;
    index = 0;
    CHECK(three_mm_movie_compose_add(&t.composite, &index, &t.s) && !index && !t.calls[TEMP] && t.calls[SAVE] == 1);
    three_mm_composite_empty(&t.composite, &t.s);
    three_mm_composite_empty(&t.composite, &t.s);
    CHECK(!t.badRef && t.resources[0].refs == 1);
    return 0;
}
static int merging(void)
{
    Test t;
    int i, offset;
    for (offset = -3; offset <= 10; offset += 13)
    {
        init(&t);
        t.withActors = 1;
        CHECK(three_mm_movie_compose_merge(10, offset, &t.s));
        CHECK(t.destination.count == 3 && !t.source.object.refs && !t.blocks);
        CHECK(t.destination.rows[1].data.actor_id == 5 + offset && !t.destination.rows[1].data.reference_count);
        CHECK(t.adjustCount == (offset > 0 ? 2 : 0) && t.actorIds[0] == 5 + (offset > 0 ? offset : 0));
        if (offset > 0)
            CHECK(t.adjusted[0] == 100 && t.adjusted[1] == 101 && t.actorIds[1] == 17);
        CHECK(!cleanup_tags(&t));
    }
    for (i = 1; i <= 2; ++i)
    {
        init(&t);
        t.withActors = 1;
        t.fail = APPEND_ACTOR;
        t.failAt = i;
        CHECK(!three_mm_movie_compose_merge(10, 10, &t.s) && t.destination.count == 1 && !t.source.object.refs);
        CHECK(!t.tags[0].refs && !t.tags[1].refs && t.tags[2].refs == 1 && !t.adjustCount && !t.blocks && !t.badRef);
    }
    init(&t);
    t.withActors = 1;
    t.fail = ADJUST;
    t.failAt = 2;
    CHECK(!three_mm_movie_compose_merge(10, 10, &t.s) && t.destination.count == 1 && t.actorIds[0] == 15 &&
          t.actorIds[1] == 7);
    CHECK(!t.tags[0].refs && !t.tags[1].refs && !t.blocks);
    init(&t);
    t.withActors = 1;
    t.allocFailAt = 2;
    CHECK(!three_mm_movie_compose_merge(10, 10, &t.s) && !t.adjustCount && t.destination.count == 1 && !t.blocks);
    CHECK(!t.tags[0].refs && !t.tags[1].refs);
    init(&t);
    t.withActors = 1;
    t.allocFailAt = 1;
    CHECK(three_mm_movie_compose_merge(10, 10, &t.s) && !t.adjustCount && t.destination.count == 3 && !t.blocks);
    CHECK(!cleanup_tags(&t));
    init(&t);
    t.fail = READ_TABLE;
    CHECK(!three_mm_movie_compose_merge(10, 10, &t.s) && t.destination.count == 1 && !t.source.object.refs);
    return 0;
}
static int applying(void)
{
    Test t;
    int i;
    const int failures[] = {0, DUP_LIST, CLONE, READ_TABLE, CHILD, ADOPT, SOUNDS, WRITE_TRANS};
    for (i = 0; i < 8; ++i)
    {
        init(&t);
        apply_setup(&t, 1);
        t.fail = failures[i];
        CHECK(three_mm_movie_compose_apply(&t.composite, &t.s) == !i);
        CHECK(!t.copy.object.refs && t.resources[0].refs == 2 && t.resources[1].refs == 2 && !t.read.refs &&
              !t.blocks && !t.badRef);
        CHECK(t.deletedCount == (i == 1 || i == 2 ? 0 : 1));
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
        if (i >= 4 && i <= 6)
            CHECK(t.openTransition == 7);
        if (i == 6)
            CHECK(t.adopted == 1 && !t.calls[READ_SCENE]);
    }
    init(&t);
    apply_setup(&t, 1);
    t.movieList.descriptors[0].actor_limit = 2147483647;
    CHECK(three_mm_movie_compose_apply(&t.composite, &t.s) && t.data.actorIdLimit == -2147483644);
    init(&t);
    apply_setup(&t, 1);
    t.movieList.count = 3;
    t.movieList.descriptors[2] = t.movieList.descriptors[1];
    t.movieList.descriptors[2].number = 30;
    t.fail = READ_TABLE;
    t.failAt = 2;
    CHECK(!three_mm_movie_compose_apply(&t.composite, &t.s) && t.deletedCount == 2 && t.deleted[0] == 130 &&
          t.deleted[1] == 120);
    CHECK(t.resources[0].refs == 2 && !t.copy.object.refs);
    for (i = READ_SCENE; i <= ADD_ACTORS; ++i)
    {
        init(&t);
        apply_setup(&t, 1);
        t.fail = i;
        CHECK(three_mm_movie_compose_apply(&t.composite, &t.s) && t.diagnostic[THREE_MM_COMPOSE_IMPORT_ACTORS] == 1);
        CHECK(t.calls[ACTOR_TOOLS] == 1 && t.calls[TEXT_TOOLS] == 1 && !t.read.refs && !t.copy.object.refs);
        CHECK(t.calls[ADD_ACTORS] == (i == ADD_ACTORS));
    }
    init(&t);
    apply_setup(&t, 1);
    t.sceneList.scenes[0].deleted = 1;
    CHECK(three_mm_movie_compose_apply(&t.composite, &t.s) && t.scenes.index == 0 && t.data.sceneCount == 2 &&
          t.removedActors == 1);
    CHECK(t.calls[DELETE_CHILD] == 1 && t.open.refs == 1);
    init(&t);
    apply_setup(&t, 0);
    t.sceneList.scenes[0].deleted = t.sceneList.scenes[1].deleted = 1;
    t.fail = START;
    CHECK(three_mm_movie_compose_apply(&t.composite, &t.s) && !t.data.sceneCount && !t.scenes.open &&
          t.scenes.index == -1);
    CHECK(t.removedActors == 2 && t.diagnostic[THREE_MM_COMPOSE_REMOVE_ACTORS] == 1 && t.closed == 1 &&
          t.calls[NUKED] == 1);
    CHECK(!t.open.refs && !t.read.refs && !t.badRef);
    return 0;
}
static int emptying(void)
{
    Test t;
    ThreeMMComposeScene scene;
    ThreeMMComposeMovie movie;
    int start;
    init(&t);
    memset(&scene, 0, sizeof(scene));
    t.sceneList.object.refs = t.movieList.object.refs = 1;
    t.composite.scenes = &t.sceneList;
    t.composite.movies = &t.movieList;
    scene.thumbnail = &t.thumbs[1];
    CHECK(three_mm_composite_insert_scene(&t.sceneList, 0, &scene, &t.s) && t.thumbs[1].refs == 2);
    three_mm_composite_delete_scene(&t.sceneList, 0, &t.s);
    CHECK(t.thumbs[1].refs == 1 && !t.sceneList.count);
    t.sceneList.scenes[0] = scene;
    t.sceneList.count = 1;
    movie.resources = &t.resources[0];
    movie.number = 10;
    movie.actor_limit = 10;
    t.movieList.descriptors[0] = movie;
    t.movieList.count = 1;
    start = t.n;
    three_mm_composite_empty(&t.composite, &t.s);
    CHECK(t.events[start] == 205 && t.events[start + 1] == 210 && t.events[start + 2] == 201 &&
          t.events[start + 3] == 211);
    CHECK(!t.composite.scenes && !t.composite.movies && !t.thumbs[1].refs && !t.resources[0].refs && !t.badRef);
    three_mm_composite_empty(&t.composite, &t.s);
    return 0;
}
int main(void)
{
    if (adding() || merging() || applying() || emptying())
        return 1;
    puts("Movie composition, ownership, rollback and shared actor remapping passed");
    return 0;
}

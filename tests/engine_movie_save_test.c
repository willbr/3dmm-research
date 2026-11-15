/* Movie save replacement/publication, rollback, retry and ownership checks. */
#include "three_mm/movie_save.h"
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
#define NIL ((ThreeMMU32)0xffffffffUL)
enum
{
    PATH = 1,
    CREATE,
    RESOURCE,
    ROOT,
    SCENE,
    ADOPT_SCENE,
    ROLLCALL,
    ADD_SOURCE,
    WRITE_SOURCE,
    ADOPT_SOURCE,
    TITLE,
    SAVE,
    FIND,
    UNIQUE,
    TAGS,
    SCENE_TAGS,
    PACKAGE
};
enum
{
    RELEASE_SAVED = 100,
    RETAIN_SAVED,
    PUBLICATION,
    DELETE_ACTOR,
    CLOSE_TAG,
    PHYSICAL_SAVE,
    TITLE_UPDATE,
    END_LONG,
    UNDO_CLEAR,
    PACK,
    CACHE_TOSS,
    DELETE_CHUNK,
    RELEASE_SOURCE
};
typedef struct Test Test;
typedef struct Saved
{
    Test *test;
    int refs;
} Saved;
typedef struct Path
{
    int vmm, readOnly;
    Saved *registered;
} Path;
struct Test
{
    ThreeMMMovieSaveServices s;
    ThreeMMMovieState data;
    ThreeMMMovieScenes scenes;
    ThreeMMMovieSaveState state;
    Path named, temporary, unique, publicName;
    Saved saved[2];
    ThreeMMChunkChild children[64];
    int childCount, fail, left, skip, markError, error, resets, errors, clears, missing;
    int temp, marks, tempSets, persistent, first, archiveRefs, cacheBytes, savedCalls, tempCalls;
    int begins, ends, dirty, updates, titleCalls, undos, packages, packageNull, container, bundled;
    int sourceArchive, sourceCalls, tagCount, tagReads, tagReleases, sceneTagCalls;
    ThreeMMTag tags[12];
    int actorCount, actorRefs[8], actorIds[8], actorsErased, tagsClosed, rollcalls;
    int unused, valid, purgeAnswer, queries, badChildren, badIndex, refs[8], soundExists[8];
    ThreeMMU32 materials[16], templates[16];
    int materialCount, templateCount, tossed, deletedChunks, allocations, failAlloc, liveAlloc;
    int sourceActive, sourceReleases, badSourceLifetime;
    int events[512], eventCount, deletedCount;
    ThreeMMChunkChild deleted[64];
    ThreeMMU32 next;
    void *lastTitle;
};
static void event(Test *t, int e)
{
    t->events[t->eventCount++] = e;
}
static int succeeds(Test *t, int kind)
{
    if (t->fail != kind || !t->left)
        return 1;
    if (t->skip)
    {
        --t->skip;
        return 1;
    }
    --t->left;
    if (t->markError)
        t->error = 1;
    return 0;
}
static void append(Test *t, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 slot)
{
    ThreeMMChunkChild *c = &t->children[t->childCount++];
    c->type = type;
    c->number = number;
    c->slot = slot;
}
static int has(Test *t, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 slot)
{
    int i;
    for (i = 0; i < t->childCount; ++i)
        if (t->children[i].type == type && t->children[i].number == number && t->children[i].slot == slot)
            return 1;
    return 0;
}
static void *temporary(void *p)
{
    Test *t = p;
    ++t->tempCalls;
    return succeeds(t, PATH) ? &t->temporary : 0;
}
static void *unique(void *p, void *path)
{
    Test *t = p;
    (void)path;
    return succeeds(t, UNIQUE) ? &t->unique : 0;
}
static void *file(void *p)
{
    return p;
}
static void *create(void *p, void *path)
{
    Test *t = p;
    (void)path;
    if (!succeeds(t, CREATE))
        return 0;
    ++t->archiveRefs;
    return t;
}
static void release_archive(void *p)
{
    --((Test *)p)->archiveRefs;
}
static void *resources(void *p, void *f, ThreeMMU32 cache)
{
    Test *t = p;
    (void)f;
    t->cacheBytes = (int)cache;
    if (!succeeds(t, RESOURCE))
        return 0;
    ++t->archiveRefs;
    return t;
}
static int is_temp(void *p)
{
    return ((Test *)p)->temp;
}
static int save(void *p, void *path)
{
    Test *t = p;
    ++t->savedCalls;
    event(t, PHYSICAL_SAVE);
    if (!succeeds(t, SAVE))
        return 0;
    if (path)
        ((Path *)path)->registered = &t->saved[1];
    return 1;
}
static void set_temp(void *p, int value)
{
    Test *t = p;
    t->temp = value;
    ++t->tempSets;
}
static void mark_temp(void *p)
{
    Test *t = p;
    t->temp = 1;
    ++t->marks;
}
static void persistent(void *p)
{
    Test *t = p;
    t->temp = 0;
    ++t->persistent;
}
static ThreeMMU32 first(void *p)
{
    ++((Test *)p)->first;
    return 99;
}
static int root(void *p, void *f, ThreeMMU32 *n)
{
    Test *t = p;
    (void)f;
    if (!succeeds(t, ROOT))
        return 0;
    *n = 9;
    return 1;
}
static void *find(void *p, void *path)
{
    return succeeds(p, FIND) ? ((Path *)path)->registered : 0;
}
static void retain_saved(void *p)
{
    Saved *s = p;
    ++s->refs;
    event(s->test, RETAIN_SAVED);
}
static void release_saved(void *p)
{
    Saved *s = p;
    --s->refs;
    event(s->test, RELEASE_SAVED);
}
static void flush(void *p)
{
    Saved *s = p;
    event(s->test, 200);
}
static int read_only(void *p, void *path)
{
    (void)p;
    return ((Path *)path)->readOnly;
}
static void name(void *p, void *out)
{
    Saved *s = p;
    *(int *)out = s == &s->test->saved[0] ? 1 : 2;
}
static void copy_path(void *p, void *out)
{
    (void)p;
    *(int *)out = 3;
}
static void *container_name(void *p)
{
    return &((Test *)p)->publicName;
}
static void set_name(void *p, void *path)
{
    Test *t = p;
    t->publicName = *(Path *)path;
    event(t, PUBLICATION);
}
static void long_op(void *p, int begin)
{
    Test *t = p;
    if (begin)
        ++t->begins;
    else
    {
        ++t->ends;
        event(t, END_LONG);
    }
}
static void dirty(void *p, int value)
{
    ((Test *)p)->dirty = value;
}
static int scene(void *p, void *open, ThreeMMU32 *n)
{
    Test *t = p;
    (void)open;
    event(t, SCENE);
    if (!succeeds(t, SCENE))
        return 0;
    *n = t->next++;
    return 1;
}
static int child(void *p, ThreeMMU32 root_number, ThreeMMU32 type, ThreeMMU32 slot, ThreeMMU32 *n)
{
    Test *t = p;
    int i;
    (void)root_number;
    for (i = 0; i < t->childCount; ++i)
        if (t->children[i].type == type && t->children[i].slot == slot)
        {
            *n = t->children[i].number;
            return 1;
        }
    return 0;
}
static int adopt(void *p, ThreeMMU32 root_number, ThreeMMU32 type, ThreeMMU32 n, ThreeMMU32 slot)
{
    Test *t = p;
    (void)root_number;
    if (!succeeds(t, type == THREE_MM_SAVE_SCENE ? ADOPT_SCENE : ADOPT_SOURCE))
        return 0;
    append(t, type, n, slot);
    return 1;
}
static void delete_child(void *p, ThreeMMU32 root_number, ThreeMMU32 type, ThreeMMU32 n, ThreeMMU32 slot)
{
    Test *t = p;
    int i;
    ThreeMMChunkChild *c = &t->deleted[t->deletedCount++];
    (void)root_number;
    c->type = type;
    c->number = n;
    c->slot = slot;
    for (i = 0; i < t->childCount; ++i)
        if (t->children[i].type == type && t->children[i].number == n && t->children[i].slot == slot)
        {
            memmove(t->children + i, t->children + i + 1, (t->childCount - i - 1) * sizeof(*c));
            --t->childCount;
            break;
        }
}
static void delete_chunk(void *p, ThreeMMU32 type, ThreeMMU32 n)
{
    Test *t = p;
    int i, *count;
    ThreeMMU32 *values;
    ++t->deletedChunks;
    event(t, DELETE_CHUNK);
    values = type == THREE_MM_SAVE_MATERIAL ? t->materials : t->templates;
    count = type == THREE_MM_SAVE_MATERIAL ? &t->materialCount : &t->templateCount;
    if (type != THREE_MM_SAVE_MATERIAL && type != THREE_MM_SAVE_TEMPLATE)
        return;
    for (i = 0; i < *count; ++i)
        if (values[i] == n)
        {
            memmove(values + i, values + i + 1, (*count - i - 1) * sizeof(*values));
            --*count;
            break;
        }
}
static ThreeMMI32 actor_count(void *p)
{
    return ((Test *)p)->actorCount;
}
static ThreeMMI32 actor_refs(void *p, ThreeMMI32 i, ThreeMMTag *tag)
{
    Test *t = p;
    memset(tag, 0, sizeof(*tag));
    tag->number = t->actorIds[i];
    return t->actorRefs[i];
}
static void erase_actor(void *p, ThreeMMI32 i)
{
    Test *t = p;
    int j;
    event(t, DELETE_ACTOR);
    ++t->actorsErased;
    for (j = i; j + 1 < t->actorCount; ++j)
    {
        t->actorRefs[j] = t->actorRefs[j + 1];
        t->actorIds[j] = t->actorIds[j + 1];
    }
    --t->actorCount;
}
static void close_tag(void *p, ThreeMMTag *tag)
{
    Test *t = p;
    (void)tag;
    ++t->tagsClosed;
    event(t, CLOSE_TAG);
}
static void rollcall(void *p)
{
    ++((Test *)p)->rollcalls;
}
static int write_actors(void *p, void *f, ThreeMMU32 root_number, void *actors, ThreeMMU32 *n)
{
    Test *t = p;
    (void)f;
    (void)root_number;
    (void)actors;
    event(t, ROLLCALL);
    if (!succeeds(t, ROLLCALL))
        return 0;
    *n = t->next++;
    append(t, THREE_MM_SAVE_TABLE, *n, 0);
    return 1;
}
static ThreeMMU32 source_bytes(void *p)
{
    (void)p;
    return 42;
}
static int add_sources(void *p, void *f, ThreeMMU32 bytes, ThreeMMU32 *n)
{
    Test *t = p;
    (void)f;
    (void)bytes;
    if (!succeeds(t, ADD_SOURCE))
        return 0;
    t->sourceActive = 1;
    *n = t->next++;
    return 1;
}
static int write_sources(void *p)
{
    return succeeds(p, WRITE_SOURCE);
}
static void release_sources(void *p)
{
    Test *t = p;
    if (!t->eventCount || t->events[t->eventCount - 1] != END_LONG)
        ++t->badSourceLifetime;
    t->sourceActive = 0;
    ++t->sourceReleases;
    event(t, RELEASE_SOURCE);
}
static int write_title(void *p, void *f)
{
    Test *t = p;
    (void)f;
    ++t->titleCalls;
    return succeeds(t, TITLE);
}
static int error(void *p)
{
    return ((Test *)p)->error;
}
static void reset(void *p)
{
    Test *t = p;
    t->error = 0;
    ++t->resets;
}
static void clear_errors(void *p)
{
    ++((Test *)p)->clears;
}
static void save_error(void *p)
{
    ++((Test *)p)->errors;
}
static void missing(void *p)
{
    ++((Test *)p)->missing;
}
static void update_title(void *p, void *path)
{
    Test *t = p;
    ++t->updates;
    t->lastTitle = path;
    event(t, TITLE_UPDATE);
}
static void clear_undo(void *p)
{
    Test *t = p;
    ++t->undos;
    event(t, UNDO_CLEAR);
}
static int container(void *p)
{
    return ((Test *)p)->container;
}
static int bundled(void *p)
{
    return ((Test *)p)->bundled;
}
static int vmm(void *p)
{
    return p && ((Path *)p)->vmm;
}
static int package(void *p, void *working, void *destination)
{
    Test *t = p;
    (void)destination;
    event(t, PACK);
    if (t->sourceActive)
        ++t->badSourceLifetime;
    ++t->packages;
    t->packageNull += !working;
    return working && succeeds(t, PACKAGE);
}
static int source_archive(void *p, ThreeMMI32 source)
{
    Test *t = p;
    ++t->sourceCalls;
    return source == 6 && t->sourceArchive;
}
static void *tags(void *p)
{
    return succeeds(p, TAGS) ? p : 0;
}
static int scene_tags(void *p, ThreeMMU32 n, void *list)
{
    Test *t = list;
    (void)p;
    (void)n;
    ++t->sceneTagCalls;
    return succeeds(t, SCENE_TAGS);
}
static ThreeMMI32 tag_count(void *p)
{
    return ((Test *)p)->tagCount;
}
static void tag(void *p, ThreeMMI32 i, ThreeMMTag *out)
{
    Test *t = p;
    ++t->tagReads;
    *out = t->tags[i];
}
static void release_tags(void *p)
{
    ++((Test *)p)->tagReleases;
}
static int chunk(void *p, ThreeMMU32 type, ThreeMMI32 i, ThreeMMU32 *n)
{
    Test *t = p;
    int count = type == THREE_MM_SAVE_MATERIAL ? t->materialCount : t->templateCount;
    if (i >= count)
        return 0;
    *n = (type == THREE_MM_SAVE_MATERIAL ? t->materials : t->templates)[i];
    return 1;
}
static void toss(void *p, ThreeMMU32 type, ThreeMMU32 n)
{
    Test *t = p;
    (void)type;
    (void)n;
    ++t->tossed;
    event(t, CACHE_TOSS);
}
static ThreeMMI32 child_count(void *p, ThreeMMU32 root_number)
{
    (void)root_number;
    return ((Test *)p)->childCount;
}
static ThreeMMI32 references(void *p, ThreeMMU32 type, ThreeMMU32 n)
{
    (void)type;
    return ((Test *)p)->refs[n % 8];
}
static int child_at(void *p, ThreeMMU32 root_number, ThreeMMI32 i, ThreeMMChunkChild *out)
{
    Test *t = p;
    (void)root_number;
    if (i == t->badIndex)
        return 0;
    *out = t->children[i];
    return 1;
}
static int sound_child(void *p, ThreeMMU32 n)
{
    return ((Test *)p)->soundExists[n % 8];
}
static int unused(void *p, int *valid)
{
    Test *t = p;
    *valid = t->valid;
    return t->unused;
}
static int purge(void *p)
{
    Test *t = p;
    ++t->queries;
    return t->purgeAnswer;
}
static void bad_child(void *p)
{
    ++((Test *)p)->badChildren;
}
static void *resize(void *p, void *old, ThreeMMU32 old_bytes, ThreeMMU32 bytes, int priority)
{
    Test *t = p;
    void *fresh;
    int had = old != 0;
    (void)old_bytes;
    (void)priority;
    ++t->allocations;
    if (t->failAlloc)
        return 0;
    fresh = realloc(old, bytes);
    if (fresh && !had)
        ++t->liveAlloc;
    return fresh;
}
static void free_storage(void *p, void *data, ThreeMMU32 bytes)
{
    Test *t = p;
    (void)bytes;
    --t->liveAlloc;
    free(data);
}
static void init(Test *t)
{
    ThreeMMMovieSaveServices *s;
    memset(t, 0, sizeof(*t));
    three_mm_movie_save_init(&t->state);
    t->data.resources = t;
    t->data.actors = t;
    t->data.movieNumber = 9;
    t->scenes.open = t;
    t->scenes.index = 0;
    t->state.autosaveDirty = 1;
    t->temp = 1;
    t->next = 100;
    t->badIndex = -1;
    append(t, THREE_MM_SAVE_SCENE, 10, 0);
    append(t, THREE_MM_SAVE_TABLE, 20, 0);
    append(t, THREE_MM_SAVE_TABLE, 30, 1);
    t->saved[0].test = t;
    t->saved[1].test = t;
    t->named.registered = &t->saved[0];
    s = &t->s;
    s->context = t;
    s->data = &t->data;
    s->state = &t->state;
    s->scenes = &t->scenes;
    s->temporary_path = temporary;
    s->unique_path = unique;
    s->file = file;
    s->create_file = create;
    s->release_archive = release_archive;
    s->new_resources = resources;
    s->is_temp = is_temp;
    s->save = save;
    s->set_temp = set_temp;
    s->mark_temporary = mark_temp;
    s->persistent = persistent;
    s->first_root = first;
    s->create_root = root;
    s->find_file = find;
    s->retain_saved = retain_saved;
    s->release_saved = release_saved;
    s->flush_saved = flush;
    s->read_only = read_only;
    s->get_name = name;
    s->copy_path = copy_path;
    s->container_name = container_name;
    s->set_container_name = set_name;
    s->long_op = long_op;
    s->document_dirty = dirty;
    s->write_scene = scene;
    s->child = child;
    s->adopt = adopt;
    s->delete_child = delete_child;
    s->delete_chunk = delete_chunk;
    s->actor_count = actor_count;
    s->actor_refs = actor_refs;
    s->erase_actor = erase_actor;
    s->close_tag = close_tag;
    s->rollcall_changed = rollcall;
    s->write_rollcall = write_actors;
    s->source_bytes = source_bytes;
    s->add_sources = add_sources;
    s->write_sources = write_sources;
    s->release_sources = release_sources;
    s->write_title = write_title;
    s->has_error = error;
    s->reset_error = reset;
    s->clear_errors = clear_errors;
    s->save_error = save_error;
    s->bad_file = save_error;
    s->update_title = update_title;
    s->clear_undo = clear_undo;
    s->has_container = container;
    s->bundled = bundled;
    s->is_vmm = vmm;
    s->package = package;
    s->source_archive = source_archive;
    s->new_tags = tags;
    s->scene_tags = scene_tags;
    s->tag_count = tag_count;
    s->tag = tag;
    s->release_tags = release_tags;
    s->chunk = chunk;
    s->toss_cache = toss;
    s->child_count = child_count;
    s->references = references;
    s->child_at = child_at;
    s->sound_child = sound_child;
    s->unused_sounds = unused;
    s->purge_sounds = purge;
    s->bad_child = bad_child;
    s->missing_scene = missing;
    s->allocator.context = t;
    s->allocator.resize = resize;
    s->allocator.release = free_storage;
}
static int autosaving(void)
{
    Test t;
    int fault, with_scene, physical_error;
    for (with_scene = 0; with_scene <= 1; ++with_scene)
        for (physical_error = 0; physical_error <= 1; ++physical_error)
            for (fault = ROOT; fault <= SAVE; ++fault)
            {
                if (!with_scene && (fault == SCENE || fault == ADOPT_SCENE))
                    continue;
                init(&t);
                t.data.movieNumber = NIL;
                if (!with_scene)
                    t.scenes.open = 0;
                t.fail = fault;
                t.left = 1;
                t.markError = physical_error;
                CHECK(!three_mm_movie_autosave(0, 0, &t.s));
                CHECK(t.begins == 1 && t.ends == 1 && t.updates == 0 && t.state.autosaveDirty && t.dirty == 0);
                CHECK(t.sourceReleases == 1 && !t.sourceActive && !t.badSourceLifetime);
                CHECK(t.resets == physical_error && t.errors == physical_error && !t.error);
                if (fault < SAVE)
                {
                    CHECK(has(&t, THREE_MM_SAVE_SCENE, 10, 0) && has(&t, THREE_MM_SAVE_TABLE, 20, 0) &&
                          has(&t, THREE_MM_SAVE_TABLE, 30, 1));
                    CHECK(t.childCount == 3);
                }
                else
                    CHECK(!has(&t, THREE_MM_SAVE_TABLE, 20, 0) && !has(&t, THREE_MM_SAVE_TABLE, 30, 1));
                if (fault == TITLE)
                {
                    CHECK(t.deleted[0].slot == 1 && t.deleted[1].slot == 0);
                    CHECK(t.deletedCount == 2 + with_scene);
                }
                CHECK(!t.liveAlloc);
            }
    init(&t);
    t.actorCount = 3;
    t.actorRefs[1] = 1;
    t.actorIds[0] = 1;
    t.actorIds[1] = 2;
    t.actorIds[2] = 3;
    CHECK(three_mm_movie_autosave(0, 1, &t.s));
    CHECK(!t.state.autosaveDirty && t.dirty == 1 && t.actorCount == 1 && t.actorIds[0] == 2);
    CHECK(t.actorsErased == 2 && t.tagsClosed == 2 && t.rollcalls == 1 && t.childCount == 3 && t.savedCalls == 1);
    CHECK(t.events[1] == DELETE_ACTOR && t.events[2] == CLOSE_TAG && t.events[3] == DELETE_ACTOR &&
          t.events[4] == CLOSE_TAG);
    CHECK(t.updates == 1 && t.lastTitle == 0 && t.begins == t.ends && t.errors == 0);
    CHECK(three_mm_movie_autosave(0, 1, &t.s) && t.savedCalls == 1 && t.rollcalls == 1);
    t.state.closing = 1;
    CHECK(three_mm_movie_autosave(0, 0, &t.s) && t.savedCalls == 2);
    init(&t);
    t.temp = 0;
    t.fail = PATH;
    t.left = 1;
    CHECK(!three_mm_movie_autosave(0, 0, &t.s) && t.ends == 1 && !t.titleCalls && !t.errors);
    init(&t);
    t.temp = 0;
    t.fail = SAVE;
    t.left = 1;
    t.markError = 1;
    CHECK(!three_mm_movie_autosave(0, 0, &t.s) && t.ends == 1 && t.error && !t.resets && !t.titleCalls);
    init(&t);
    t.actorCount = 2;
    t.actorRefs[1] = 1;
    t.fail = TITLE;
    t.left = 1;
    CHECK(!three_mm_movie_autosave(0, 1, &t.s) && t.actorCount == 1 && t.tagsClosed == 1 && t.childCount == 3);
    init(&t);
    t.data.resources = 0;
    CHECK(!three_mm_movie_autosave(0, 0, &t.s) && !t.begins);
    return 0;
}
static void publish(void *p, void *resources_value)
{
    Test *t = p;
    if (resources_value == t)
        event(t, PUBLICATION);
}
static int ownership(void)
{
    Test t;
    int fault, output = 77;
    for (fault = PATH; fault <= RESOURCE; ++fault)
    {
        init(&t);
        t.data.resources = 0;
        t.fail = fault;
        t.left = 1;
        CHECK(!three_mm_movie_save_resources(&t.s) && !t.archiveRefs && !t.data.resources);
    }
    init(&t);
    t.data.resources = 0;
    t.scenes.open = 0;
    t.childCount = 0;
    t.data.movieNumber = NIL;
    CHECK(three_mm_movie_save_resources(&t.s) && t.data.resources == &t && t.archiveRefs == 1 && t.cacheBytes == 0);
    init(&t);
    t.data.resources = 0;
    t.scenes.open = 0;
    t.fail = TITLE;
    t.left = 1;
    CHECK(!three_mm_movie_save_resources(&t.s) && t.data.resources == &t && t.archiveRefs == 1);
    init(&t);
    t.temp = 0;
    t.fail = PATH;
    t.left = 1;
    CHECK(!three_mm_movie_save_ensure(&t, publish, &t.s) && t.events[0] == PUBLICATION && !t.begins);
    init(&t);
    t.temp = 0;
    CHECK(three_mm_movie_save_temporary(&t.s) && t.temp && t.marks == 1 && t.first == 1 && t.data.movieNumber == 99);
    init(&t);
    t.named.readOnly = 1;
    CHECK(three_mm_movie_save_bind(&t.named, &t.s) && t.saved[0].refs == 1 && t.state.nameValid && t.state.readOnly);
    CHECK(three_mm_movie_save_name(&output, &t.s) && output == 1);
    three_mm_movie_save_flush(&t.s);
    CHECK(t.events[t.eventCount - 1] == 200);
    t.fail = FIND;
    t.left = 1;
    CHECK(!three_mm_movie_save_bind(&t.named, &t.s) && !t.state.file && t.saved[0].refs == 0 && t.state.nameValid &&
          t.state.readOnly);
    three_mm_movie_save_release(1, &t.s);
    CHECK(!t.state.nameValid);
    output = 77;
    CHECK(!three_mm_movie_save_name(&output, &t.s) && output == 77);
    t.state.containerNamed = 1;
    CHECK(!three_mm_movie_save_name(&output, &t.s) && output == 3);
    CHECK(three_mm_movie_save_command(10, 10, 11, &t.state) == 11);
    CHECK(three_mm_movie_save_command(12, 10, 11, &t.state) == 12);
    init(&t);
    CHECK(three_mm_movie_save_bind(&t.named, &t.s));
    t.eventCount = 0;
    CHECK(three_mm_movie_autosave(&t.named, 0, &t.s));
    CHECK(t.saved[0].refs == 0 && t.saved[1].refs == 1 && t.state.file == &t.saved[1] && t.state.nameValid);
    CHECK(t.events[2] == RELEASE_SAVED && t.events[3] == PHYSICAL_SAVE && t.events[4] == RETAIN_SAVED);
    three_mm_movie_save_release(1, &t.s);
    CHECK(t.saved[1].refs == 0);
    init(&t);
    CHECK(three_mm_movie_save_bind(&t.named, &t.s));
    t.fail = SAVE;
    t.left = 1;
    CHECK(!three_mm_movie_autosave(&t.named, 0, &t.s) && t.state.file && t.state.nameValid && t.saved[0].refs == 1);
    CHECK(!has(&t, THREE_MM_SAVE_TABLE, 20, 0));
    three_mm_movie_save_release(1, &t.s);
    init(&t);
    CHECK(three_mm_movie_save_bind(&t.named, &t.s));
    t.fail = FIND;
    t.left = 1;
    t.skip = 1;
    CHECK(three_mm_movie_autosave(&t.named, 0, &t.s) && !t.state.file && !t.state.nameValid && !t.saved[0].refs);
    return 0;
}
static int retrying(void)
{
    Test t;
    int count;
    for (count = 1; count <= 2; ++count)
    {
        init(&t);
        t.s.retry_enabled = 1;
        t.fail = TITLE;
        t.left = count;
        t.markError = 1;
        CHECK(three_mm_movie_autosave(&t.named, 0, &t.s) == (count == 1));
        CHECK(t.begins == 1 && t.ends == 1 && t.resets == count && t.clears == 1 && t.tempSets == 1);
        CHECK(t.errors == (count == 2) && t.savedCalls == (count == 1 ? 2 : 1));
        CHECK(t.childCount == 3 && !t.error && !t.liveAlloc);
        CHECK(t.sourceReleases == 1 && !t.sourceActive && !t.badSourceLifetime);
    }
    init(&t);
    t.s.retry_enabled = 1;
    t.fail = SAVE;
    t.left = 2;
    t.markError = 1;
    CHECK(!three_mm_movie_autosave(&t.named, 0, &t.s) && t.savedCalls == 2 && t.errors == 1 && t.resets == 1 &&
          t.error);
    return 0;
}
static int packaging(void)
{
    Test t;
    int remember;
    init(&t);
    t.named.vmm = 1;
    CHECK(!three_mm_movie_save_to(&t.named, 1, &t.s) && t.errors == 1 && !t.begins);
    init(&t);
    t.container = 1;
    t.bundled = 1;
    CHECK(!three_mm_movie_save_to(&t.named, 1, &t.s) && t.packageNull == 1 && !t.begins && !t.undos);
    for (remember = 0; remember <= 1; ++remember)
    {
        init(&t);
        t.container = 1;
        t.named.vmm = 1;
        t.named.readOnly = 1;
        CHECK(three_mm_movie_save_to(&t.named, remember, &t.s));
        CHECK(t.packages == 1 && t.undos == 1 && t.tempSets == 1 && t.temp && t.first == 1 &&
              t.data.movieNumber == 99 && !t.dirty);
        CHECK(t.state.containerNamed == remember && t.state.readOnly == remember && t.state.nameValid == remember);
        CHECK(t.lastTitle == &t.named && t.begins == t.ends && t.rollcalls == 1);
        CHECK(t.sourceReleases == 1 && !t.sourceActive && !t.badSourceLifetime);
        three_mm_movie_save_release(1, &t.s);
        CHECK(!t.saved[1].refs);
        init(&t);
        CHECK(three_mm_movie_save_to(&t.named, remember, &t.s));
        CHECK(t.persistent == 1 && !t.temp && t.undos == 1 && !t.dirty && t.state.nameValid == remember &&
              t.first == 1);
        three_mm_movie_save_release(1, &t.s);
    }
    init(&t);
    t.container = 1;
    t.named.vmm = 1;
    t.fail = PACKAGE;
    t.left = 1;
    CHECK(!three_mm_movie_save_to(&t.named, 1, &t.s) && t.dirty && !t.state.autosaveDirty && t.temp && !t.undos &&
          !t.state.containerNamed);
    CHECK(t.updates == 2 && !t.lastTitle);
    init(&t);
    t.container = 1;
    t.named.vmm = 1;
    t.state.containerNamed = 1;
    t.fail = PACKAGE;
    t.left = 1;
    CHECK(!three_mm_movie_save_to(&t.named, 1, &t.s) && t.lastTitle == &t.publicName);
    init(&t);
    t.container = 1;
    t.named.vmm = 1;
    CHECK(three_mm_movie_autosave(&t.named, 1, &t.s) && !t.state.containerNamed && t.packages == 1);
    init(&t);
    t.container = 1;
    t.state.autosaveDirty = 0;
    t.sourceArchive = 1;
    t.tagCount = 3;
    t.tags[0].sid = 0;
    t.tags[1].sid = 6;
    t.tags[2].sid = 7;
    CHECK(three_mm_movie_needs_container(&t.s) && t.tagReads == 3 && t.tagReleases == 1 && !t.savedCalls);
    t.fail = TAGS;
    t.left = 1;
    CHECK(three_mm_movie_needs_container(&t.s));
    t.fail = SCENE_TAGS;
    t.left = 1;
    CHECK(three_mm_movie_needs_container(&t.s) && t.tagReleases == 2);
    init(&t);
    t.container = t.sourceArchive = 1;
    t.fail = TITLE;
    t.left = 1;
    CHECK(three_mm_movie_needs_container(&t.s) && t.begins == 1 && t.ends == 1 && !t.tagReleases);
    return 0;
}
static int collecting(void)
{
    Test t, foreign;
    int purge_all;
    init(&t);
    t.materials[0] = 1;
    t.materials[1] = 2;
    t.materialCount = 2;
    t.templates[0] = 3;
    t.templates[1] = 4;
    t.templateCount = 2;
    t.tagCount = 3;
    t.tags[0].type = THREE_MM_SAVE_MATERIAL;
    t.tags[0].number = 1;
    t.tags[1].type = THREE_MM_SAVE_TEMPLATE;
    t.tags[1].number = 3;
    t.tags[2].sid = 1;
    CHECK(three_mm_movie_save_assets(&t, &t.s) && t.materialCount == 1 && t.templateCount == 1 && t.materials[0] == 1 &&
          t.templates[0] == 3);
    CHECK(t.tossed == 2 && t.deletedChunks == 2 && t.tagReleases == 1 && !t.liveAlloc);
    CHECK(t.events[0] == DELETE_CHUNK && t.events[1] == CACHE_TOSS && t.events[2] == DELETE_CHUNK &&
          t.events[3] == CACHE_TOSS);
    init(&t);
    t.materials[0] = 1;
    t.materialCount = 1;
    t.failAlloc = 1;
    CHECK(!three_mm_movie_save_assets(&t, &t.s) && t.materialCount == 1 && !t.deletedChunks && t.tagReleases == 1 &&
          !t.liveAlloc);
    init(&t);
    init(&foreign);
    foreign.materials[0] = 1;
    foreign.materialCount = 1;
    CHECK(three_mm_movie_save_assets(&foreign, &t.s) && !foreign.materialCount && !t.tossed);
    for (purge_all = 0; purge_all <= 1; ++purge_all)
    {
        init(&t);
        append(&t, THREE_MM_SAVE_SOUND, 0, 20);
        append(&t, THREE_MM_SAVE_SOUND, 1, 21);
        append(&t, THREE_MM_SAVE_SOUND, 2, 22);
        t.refs[0] = t.refs[1] = 1;
        t.refs[2] = 2;
        t.soundExists[1] = 1;
        three_mm_movie_save_sounds(purge_all, &t.s);
        CHECK(!has(&t, THREE_MM_SAVE_SOUND, 0, 20) && has(&t, THREE_MM_SAVE_SOUND, 1, 21) == !purge_all &&
              has(&t, THREE_MM_SAVE_SOUND, 2, 22));
        CHECK(t.deletedCount == 1 + purge_all);
        if (purge_all)
            CHECK(t.deleted[0].number == 1 && t.deleted[1].number == 0);
    }
    init(&t);
    t.badIndex = 2;
    three_mm_movie_save_sounds(1, &t.s);
    CHECK(t.badChildren == 1 && !t.deletedCount);
    init(&t);
    t.state.closing = 1;
    t.unused = 1;
    append(&t, THREE_MM_SAVE_SOUND, 0, 20);
    t.refs[0] = 1;
    CHECK(three_mm_movie_save_gc(&t, &t.s) && !has(&t, THREE_MM_SAVE_SOUND, 0, 20) && !t.queries);
    init(&t);
    t.state.closing = 1;
    t.unused = 1;
    t.valid = 1;
    t.purgeAnswer = 1;
    t.materials[0] = 1;
    t.materialCount = 1;
    t.failAlloc = 1;
    CHECK(!three_mm_movie_save_gc(&t, &t.s) && t.queries == 1 && !t.liveAlloc);
    return 0;
}
int main(void)
{
    if (autosaving() || ownership() || retrying() || packaging() || collecting())
        return 1;
    puts("Movie autosave rollback, publication, ownership, packaging and collection passed");
    return 0;
}

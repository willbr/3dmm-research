/* Movie save replacement/publication, rollback, retry and ownership checks. */
#include "mm/movie_save.h"
#include "mm/movie_sound.h"
#include "mm/movie_publish.h"
#include "mm/scene_data.h"
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
#define NIL ((MMU32)0xffffffffUL)
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
    SCENE_TAGS,
    PACKAGE,
    PRESERVE_MATERIALS
};
enum
{
    RELEASE_SAVED = 100,
    RETAIN_SAVED,
    PUBLICATION,
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
    MMMovieSaveServices s;
    MMMovieState data;
    MMMovieScenes scenes;
    MMMovieSaveState state;
    Path named, temporary, unique, publicName;
    Saved saved[2];
    MMChunkChild children[64];
    MMChunkEntry extraChunks[64];
    MMU32 extraCount, indexChildCount;
    int childCount, fail, left, skip, markError, error, resets, errors, clears, missing;
    int temp, marks, tempSets, persistent, missingRoot, archiveRefs, cacheBytes, savedCalls, tempCalls;
    MMU32 rootNumber, savedRootNumber;
    int changeSavedRoot;
    int begins, ends, dirty, updates, titleCalls, undos, packages, packageNull, container, bundled;
    int sourceArchive, sourceCalls, tagCount, sceneTagCalls, failTags, failFrames;
    MMU32 lastSceneTagged;
    MMTag tags[12];
    MMU32 closedTags[8], remainingActors[8];
    int tagsClosed, rollcalls;
    int purgeAnswer, queries, badChildren, badSoundIndex, badIndex;
    int refs[8], soundExists[8], soundPresent[8];
    MMU32 soundSlots[8];
    MMU32 materials[16], templates[16], materialChild, materialParent, materialFlags, materialReferences;
    int materialCount, templateCount, tossed, deletedChunks, allocations, failAlloc, liveAlloc;
    int sourceActive, sourceReleases, badSourceLifetime;
    int events[512], eventCount, deletedCount;
    MMChunkChild deleted[64];
    MMU32 next;
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
static MMChunkEntry *extra_chunk(Test *t, MMU32 type, MMU32 number, int create)
{
    MMU32 i;
    MMChunkEntry *entry;
    for (i = 0; i < t->extraCount; ++i)
    {
        entry = &t->extraChunks[i];
        if (entry->type > type || (entry->type == type && entry->number > number))
            break;
        if (entry->type == type && entry->number == number)
            return entry;
    }
    if (!create)
        return NULL;
    if (t->extraCount == sizeof(t->extraChunks) / sizeof(t->extraChunks[0]))
        abort();
    entry = &t->extraChunks[i];
    memmove(entry + 1, entry, (t->extraCount++ - i) * sizeof(*entry));
    memset(entry, 0, sizeof(*entry));
    entry->type = type;
    entry->number = number;
    entry->flags = MM_CHUNK_LONER;
    return entry;
}
static void append(Test *t, MMU32 type, MMU32 number, MMU32 slot)
{
    int i = t->childCount++;
    MMChunkChild *c;
    while (i > 0)
    {
        c = &t->children[i - 1];
        if (c->slot < slot || (c->slot == slot &&
            (c->type < type || (c->type == type && c->number <= number))))
            break;
        t->children[i--] = *c;
    }
    c = &t->children[i];
    c->type = type;
    c->number = number;
    c->slot = slot;
    t->indexChildCount = (MMU32)t->childCount;
    if (type == MM_SAVE_TABLE || type == MM_SAVE_SCENE)
    {
        MMChunkEntry *entry = extra_chunk(t, type, number, 1);
        entry->flags &= ~MM_CHUNK_LONER;
        ++entry->parent_count;
    }
}
static int has(Test *t, MMU32 type, MMU32 number, MMU32 slot)
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
static void *resources(void *p, void *f, MMU32 cache)
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
    {
        ((Path *)path)->registered = &t->saved[1];
        if (t->changeSavedRoot)
            t->rootNumber = t->savedRootNumber;
    }
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
static void missing_root(void *p)
{
    ++((Test *)p)->missingRoot;
}
static int root(void *p, void *f, MMU32 *n)
{
    Test *t = p;
    (void)f;
    if (!succeeds(t, ROOT))
        return 0;
    *n = t->rootNumber = 9;
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
static int scene(void *p, void *open, MMU32 *n)
{
    Test *t = p;
    (void)open;
    event(t, SCENE);
    if (!succeeds(t, SCENE))
        return 0;
    *n = t->next++;
    extra_chunk(t, MM_SAVE_SCENE, *n, 1);
    return 1;
}
static int insert_child(void *p, MMU32 parent, MMU32 position, const MMChunkChild *child)
{
    Test *t = p;
    (void)parent;
    if (!succeeds(t, child->type == MM_SAVE_SCENE ? ADOPT_SCENE : ADOPT_SOURCE))
        return 0;
    if (position > (MMU32)t->childCount)
        abort();
    memmove(t->children + position + 1, t->children + position,
            (t->childCount - position) * sizeof(*child));
    t->children[position] = *child;
    ++t->childCount;
    return 1;
}
static void erase_child(void *p, MMU32 parent, MMU32 position)
{
    Test *t = p;
    (void)parent;
    if (position >= (MMU32)t->childCount)
        abort();
    t->deleted[t->deletedCount++] = t->children[position];
    --t->childCount;
    memmove(t->children + position, t->children + position + 1,
            (t->childCount - position) * sizeof(t->children[0]));
}
static void delete_chunk(void *p, MMU32 type, MMU32 n)
{
    Test *t = p;
    int i, *count;
    MMU32 *values;
    ++t->deletedChunks;
    event(t, DELETE_CHUNK);
    values = type == MM_SAVE_MATERIAL ? t->materials : t->templates;
    count = type == MM_SAVE_MATERIAL ? &t->materialCount : &t->templateCount;
    if (type != MM_SAVE_MATERIAL && type != MM_SAVE_TEMPLATE)
        return;
    for (i = 0; i < *count; ++i)
        if (values[i] == n)
        {
            memmove(values + i, values + i + 1, (*count - i - 1) * sizeof(*values));
            --*count;
            break;
        }
}
static int seed_actor(Test *t, MMU32 id, MMI32 refs)
{
    MMRollCallActor actor;
    memset(&actor, 0, sizeof(actor));
    actor.data.actor_id = (MMI32)id;
    actor.data.template_number = id;
    actor.data.reference_count = refs;
    return mm_rollcall_table_add(&t->data.actors, "Actor", 5, &actor);
}
static void close_tag(void *p, MMTag *tag)
{
    Test *t = p;
    t->closedTags[t->tagsClosed] = tag->number;
    t->remainingActors[t->tagsClosed] = t->data.actors.count;
    ++t->tagsClosed;
    event(t, CLOSE_TAG);
}
static void rollcall(void *p)
{
    ++((Test *)p)->rollcalls;
}
static int write_actors(void *p, void *f, MMU32 root_number, MMRollCallTable *actors, MMU32 *n)
{
    Test *t = p;
    (void)f;
    (void)root_number;
    (void)actors;
    event(t, ROLLCALL);
    if (!succeeds(t, ROLLCALL))
        return 0;
    *n = t->next++;
    append(t, MM_SAVE_TABLE, *n, 0);
    return 1;
}
static MMU32 source_bytes(void *p)
{
    (void)p;
    return 42;
}
static int add_sources(void *p, void *f, MMU32 bytes, MMU32 *n)
{
    Test *t = p;
    (void)f;
    (void)bytes;
    if (!succeeds(t, ADD_SOURCE))
        return 0;
    t->sourceActive = 1;
    *n = t->next++;
    extra_chunk(t, MM_SAVE_TABLE, *n, 1);
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
static int write_title(void *f, MMU32 type, MMU32 number, const void *wire, MMU32 bytes)
{
    static const unsigned char expected[] = {3, 3, 5, 'M', 'o', 'v', 'i', 'e', 0};
    Test *t = f;
    if (type != MM_MOVIE_CHUNK || number != t->data.movieNumber || bytes != sizeof(expected) ||
        memcmp(wire, expected, bytes))
        abort();
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
static int source_archive(void *p, MMI32 source)
{
    Test *t = p;
    ++t->sourceCalls;
    return source == 6 && t->sourceArchive;
}
static const MMU32 tag_scenes[] = {10, 12, 13};

static int tag_entry(void *p, MMU32 row, MMChunkEntry *out)
{
    Test *t = p;
    memset(out, 0, sizeof(*out));
    if (row == 0)
    {
        out->type = MM_SCENE_FRAME_GROUP;
        out->number = 1;
    }
    else if (row < 4)
    {
        out->type = MM_SCENE_START_GROUP;
        out->number = tag_scenes[row - 1];
    }
    else if (row == 4)
    {
        out->type = MM_SOUND_MOVIE;
        out->number = t->rootNumber;
        out->child_count = t->childCount;
    }
    else
    {
        out->type = MM_SCENE_CHUNK;
        out->number = tag_scenes[row - 5];
        out->child_count = 2;
    }
    return 1;
}

static int tag_child(void *p, MMU32 row, MMU32 i, MMChunkChild *out)
{
    Test *t = p;
    if (row == 4)
        *out = t->children[i];
    else if (row >= 5)
    {
        out->type = i ? MM_SCENE_START_GROUP : MM_SCENE_FRAME_GROUP;
        out->number = i ? tag_scenes[row - 5] : 1;
        out->slot = i;
    }
    else
        return 0;
    return 1;
}

static void tag_graph_access(void *p, MMChunkAccess *out)
{
    MMChunkAccess access = {NULL, 8, tag_entry, tag_child};
    access.context = p;
    *out = access;
}

static void put_word(unsigned char *bytes, MMU32 word)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        bytes[i] = (unsigned char)(word >> (8 * i));
}

static int tag_graph_payload(void *p, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    Test *t = p;
    unsigned char bytes[20 + 12 * 32];
    unsigned char *row;
    MMMemoryStream memory;
    int count = 0, i;
    if (entry->type == MM_SCENE_START_GROUP)
    {
        t->lastSceneTagged = entry->number;
        ++t->sceneTagCalls;
        if (!succeeds(t, SCENE_TAGS))
            return MM_MOVIE_IO;
        count = t->tagCount;
    }
    else if (t->failFrames)
        return MM_MOVIE_IO;
    memset(bytes, 0, sizeof(bytes));
    bytes[0] = 1;
    put_word(bytes + 4, count);
    put_word(bytes + 8, count * 24);
    put_word(bytes + 12, 0xffffffffUL);
    put_word(bytes + 16, MM_SCENE_EVENT_BYTES);
    for (i = 0; i < count; ++i)
    {
        row = bytes + 20 + i * 24;
        put_word(row + 4, MM_SCENE_SET_BACKGROUND);
        put_word(row + 8, t->tags[i].sid);
        put_word(row + 16, t->tags[i].type);
        put_word(row + 20, t->tags[i].number);
        put_word(bytes + 20 + count * 24 + i * 8, i * 24);
        put_word(bytes + 24 + count * 24 + i * 8, 24);
    }
    mm_memory_stream_init(&memory, bytes, 20 + count * 32);
    return receive(out, &memory.stream);
}

static MMMovieSessionGraph tag_graph(void *p)
{
    MMMovieSessionGraph graph = {NULL, tag_graph_access, NULL, tag_graph_payload, NULL};
    graph.context = p;
    return graph;
}
static void toss(void *p, MMU32 type, MMU32 n)
{
    Test *t = p;
    (void)type;
    (void)n;
    ++t->tossed;
    event(t, CACHE_TOSS);
}
/* Sound rows precede the movie row, which shifts whenever a sound is dropped. */
static int graph_entry(void *p, MMU32 row, MMChunkEntry *entry)
{
    Test *t = p;
    MMU32 n, extra = 0;
    memset(entry, 0, sizeof(*entry));
    while (extra < t->extraCount && t->extraChunks[extra].type == MM_SAVE_TABLE)
    {
        if (row-- == 0)
        {
            *entry = t->extraChunks[extra];
            return 1;
        }
        ++extra;
    }
    for (n = 0; n < sizeof(t->refs) / sizeof(t->refs[0]); ++n)
        if (t->soundPresent[n] && row-- == 0)
        {
            entry->type = MM_SOUND_CHUNK;
            entry->number = n;
            entry->parent_count = t->refs[n];
            entry->child_count = t->soundExists[n];
            return 1;
        }
    if (row < (MMU32)t->materialCount)
    {
        entry->type = MM_SAVE_MATERIAL;
        entry->number = t->materials[row];
        if (entry->number == t->materialChild)
        {
            entry->parent_count = t->materialReferences;
            entry->flags = t->materialFlags;
        }
        return 1;
    }
    row -= t->materialCount;
    if (t->rootNumber != NIL && row-- == 0)
    {
        entry->type = MM_SOUND_MOVIE;
        entry->number = t->rootNumber;
        entry->child_count = t->indexChildCount;
        return 1;
    }
    if (row < t->extraCount - extra)
    {
        *entry = t->extraChunks[extra + row];
        return 1;
    }
    row -= t->extraCount - extra;
    if (row >= (MMU32)t->templateCount)
        return 0;
    entry->type = MM_SAVE_TEMPLATE;
    entry->number = t->templates[row];
    return 1;
}
static int graph_child(void *p, MMU32 row, MMU32 i, MMChunkChild *out)
{
    Test *t = p;
    MMChunkEntry entry;
    if (!graph_entry(p, row, &entry))
        return 0;
    if (entry.type == MM_SOUND_CHUNK)
    {
        out->type = MM_SAVE_TABLE;
        out->number = 256 + entry.number;
        out->slot = t->soundSlots[entry.number];
        return 1;
    }
    if (entry.type == MM_SAVE_SCENE && entry.number == t->materialParent && i == 0)
    {
        if (t->fail == PRESERVE_MATERIALS)
        {
            if (!has(t, MM_SAVE_SCENE, entry.number, 0) || !has(t, MM_SAVE_SCENE, 100, 0))
                abort();
            if (!succeeds(t, PRESERVE_MATERIALS))
                return 0;
        }
        out->type = MM_SAVE_MATERIAL;
        out->number = t->materialChild;
        out->slot = 0;
        return 1;
    }
    if (entry.type != MM_MOVIE_CHUNK || (MMI32)i == t->badIndex)
        return 0;
    *out = t->children[i];
    return 1;
}
static MMChunkAccess chunks(void *p)
{
    Test *t = p;
    MMChunkAccess access;
    MMU32 n;
    access.context = p;
    access.count = t->extraCount + t->materialCount + t->templateCount + (t->rootNumber != NIL);
    for (n = 0; n < sizeof(t->refs) / sizeof(t->refs[0]); ++n)
        if (t->soundPresent[n])
            ++access.count;
    access.entry = graph_entry;
    access.child = graph_child;
    return access;
}
static MMU32 edit_count(void *p) { return chunks(p).count; }
static void edit_store(void *p, MMU32 row, const MMChunkEntry *entry, int initialize)
{
    Test *t = p;
    (void)row;
    if (initialize)
        abort();
    if (entry->type == MM_MOVIE_CHUNK)
        t->indexChildCount = entry->child_count;
    else if (entry->type == MM_SOUND_CHUNK)
        t->refs[entry->number] = entry->parent_count;
    else if (entry->type == MM_SAVE_MATERIAL && entry->number == t->materialChild)
    {
        t->materialReferences = entry->parent_count;
        t->materialFlags = entry->flags;
    }
    else if (entry->type == MM_SAVE_TABLE || entry->type == MM_SAVE_SCENE)
        *extra_chunk(t, entry->type, entry->number, 0) = *entry;
}
static void edit_erase(void *p, MMU32 row)
{
    Test *t = p;
    MMChunkEntry entry, *extra;
    if (!graph_entry(p, row, &entry))
        abort();
    if (entry.type == MM_SOUND_CHUNK)
    {
        t->soundPresent[entry.number] = t->soundExists[entry.number] = 0;
        t->refs[entry.number] = 0;
    }
    else if ((extra = extra_chunk(t, entry.type, entry.number, 0)) != NULL)
    {
        --t->extraCount;
        memmove(extra, extra + 1, (t->extraChunks + t->extraCount - extra) * sizeof(*extra));
    }
    else
        delete_chunk(p, entry.type, entry.number);
}
static void edit_free(void *p, int extra, MMU32 offset, MMU32 bytes)
{
    (void)p;
    (void)extra;
    (void)offset;
    (void)bytes;
}
static void edit_reset(void *p, MMU32 type, MMU32 number)
{
    (void)p;
    (void)type;
    (void)number;
}
static MMChunkEdit edit_index(void *p)
{
    MMChunkEdit edit = {0};
    edit.context = p;
    edit.max_references = 65535;
    edit.count = edit_count;
    edit.entry = graph_entry;
    edit.child = graph_child;
    edit.store = edit_store;
    edit.erase = edit_erase;
    edit.free_space = edit_free;
    edit.reset_runtime = edit_reset;
    edit.insert_child = insert_child;
    edit.erase_child = erase_child;
    return edit;
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
static void bad_sound_index(void *p)
{
    ++((Test *)p)->badSoundIndex;
}
static void *resize(void *p, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Test *t = p;
    void *fresh;
    int had = old != 0;
    (void)old_bytes;
    (void)priority;
    ++t->allocations;
    if (t->failAlloc || t->failTags)
        return 0;
    fresh = realloc(old, bytes);
    if (fresh && !had)
        ++t->liveAlloc;
    return fresh;
}
static void free_storage(void *p, void *data, MMU32 bytes)
{
    Test *t = p;
    (void)bytes;
    --t->liveAlloc;
    free(data);
}
static void init(Test *t)
{
    MMMovieSaveServices *s;
    memset(t, 0, sizeof(*t));
    mm_movie_save_init(&t->state);
    if (!mm_movie_title_init(&t->data.title, 1) || !mm_movie_title_set(&t->data.title, "Movie", 5))
        abort();
    t->data.resources = t;
    if (mm_rollcall_table_init(&t->data.actors, sizeof(MMRollCallActor), 1, 0x0303, 0, NULL) != MM_STORAGE_OK)
        abort();
    t->data.movieNumber = t->rootNumber = 9;
    t->scenes.open = t;
    t->scenes.index = 0;
    t->state.autosaveDirty = 1;
    t->temp = 1;
    t->next = 100;
    t->badIndex = -1;
    t->materialReferences = 1;
    append(t, MM_SAVE_SCENE, 10, 0);
    append(t, MM_SAVE_TABLE, 20, 0);
    append(t, MM_SAVE_TABLE, 30, 1);
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
    s->edit = edit_index;
    s->close_tag = close_tag;
    s->rollcall_changed = rollcall;
    s->write_rollcall = write_actors;
    s->source_bytes = source_bytes;
    s->add_sources = add_sources;
    s->write_sources = write_sources;
    s->release_sources = release_sources;
    s->titles.encoding = MM_TEXT_WINDOWS;
    s->titles.write = write_title;
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
    s->tag_graph = tag_graph;
    s->toss_cache = toss;
    s->chunks = chunks;
    s->purge_sounds = purge;
    s->bad_child = bad_child;
    s->bad_sound_index = bad_sound_index;
    s->missing_scene = missing;
    s->missing_root = missing_root;
    s->allocator.context = t;
    s->allocator.resize = resize;
    s->allocator.release = free_storage;
}
static int autosaving(void)
{
    Test t;
    MMRollCallActor actor;
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
                CHECK(!mm_movie_autosave(0, 0, &t.s));
                CHECK(t.begins == 1 && t.ends == 1 && t.updates == 0 && t.state.autosaveDirty && t.dirty == 0);
                CHECK(t.sourceReleases == 1 && !t.sourceActive && !t.badSourceLifetime);
                CHECK(t.resets == physical_error && t.errors == physical_error && !t.error);
                if (fault < SAVE)
                {
                    CHECK(has(&t, MM_SAVE_SCENE, 10, 0) && has(&t, MM_SAVE_TABLE, 20, 0) &&
                          has(&t, MM_SAVE_TABLE, 30, 1));
                    CHECK(t.childCount == 3 && t.indexChildCount == 3 && t.extraCount == 3);
                }
                else
                    CHECK(!has(&t, MM_SAVE_TABLE, 20, 0) && !has(&t, MM_SAVE_TABLE, 30, 1));
                if (fault == TITLE)
                {
                    CHECK(t.deleted[0].slot == 1 && t.deleted[1].slot == 0);
                    CHECK(t.deletedCount == 2 + with_scene);
                }
                CHECK(!t.liveAlloc);
            }
    for (fault = 0; fault <= 1; ++fault)
    {
        init(&t);
        t.materials[0] = t.materialChild = 2;
        t.materialCount = 1;
        t.materialParent = 10;
        extra_chunk(&t, MM_SAVE_SCENE, 10, 0)->child_count = 1;
        t.fail = fault ? PRESERVE_MATERIALS : 0;
        t.left = fault;
        CHECK(mm_movie_autosave(0, 0, &t.s) == !fault && !t.left && t.savedCalls == !fault && t.titleCalls == !fault);
        CHECK(has(&t, MM_SAVE_SCENE, 10, 0) == fault && has(&t, MM_SAVE_SCENE, 100, 0) == !fault &&
              t.childCount == 3 && !t.sourceActive);
        CHECK(t.materialCount == 1 && t.materialReferences == (MMU32)fault &&
              t.materialFlags == (fault ? 0 : MM_CHUNK_LONER));
    }
    init(&t);
    CHECK(seed_actor(&t, 1, 0) && seed_actor(&t, 2, 1) && seed_actor(&t, 3, 0));
    CHECK(mm_movie_autosave(0, 1, &t.s));
    CHECK(mm_rollcall_table_extra(&t.data.actors, 0, &actor) && actor.data.actor_id == 2);
    CHECK(!t.state.autosaveDirty && t.dirty == 1 && t.data.actors.count == 1);
    CHECK(t.tagsClosed == 2 && t.rollcalls == 1 && t.childCount == 3 && t.savedCalls == 1);
    CHECK(t.extraCount == 3 && !extra_chunk(&t, MM_SAVE_SCENE, 10, 0) &&
          !extra_chunk(&t, MM_SAVE_TABLE, 20, 0) && !extra_chunk(&t, MM_SAVE_TABLE, 30, 0));
    CHECK(t.events[1] == CLOSE_TAG && t.events[2] == CLOSE_TAG && t.closedTags[0] == 1 &&
          t.closedTags[1] == 3 && t.remainingActors[0] == 2 && t.remainingActors[1] == 1);
    CHECK(t.updates == 1 && t.lastTitle == 0 && t.begins == t.ends && t.errors == 0);
    CHECK(mm_movie_autosave(0, 1, &t.s) && t.savedCalls == 1 && t.rollcalls == 1);
    CHECK(mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    init(&t);
    t.scenes.index = 2;
    t.next = 0;
    CHECK(mm_movie_autosave(0, 0, &t.s) && t.missing == 1 && has(&t, MM_SAVE_SCENE, 0, 2));
    t.state.closing = 1;
    CHECK(mm_movie_autosave(0, 0, &t.s) && t.savedCalls == 2);
    init(&t);
    t.temp = 0;
    t.fail = PATH;
    t.left = 1;
    CHECK(!mm_movie_autosave(0, 0, &t.s) && t.ends == 1 && !t.titleCalls && !t.errors);
    init(&t);
    t.temp = 0;
    t.fail = SAVE;
    t.left = 1;
    t.markError = 1;
    CHECK(!mm_movie_autosave(0, 0, &t.s) && t.ends == 1 && t.error && !t.resets && !t.titleCalls);
    init(&t);
    CHECK(seed_actor(&t, 1, 0) && seed_actor(&t, 2, 1));
    t.fail = TITLE;
    t.left = 1;
    CHECK(!mm_movie_autosave(0, 1, &t.s) && t.data.actors.count == 1 && t.tagsClosed == 1 && t.childCount == 3);
    CHECK(mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    init(&t);
    t.data.resources = 0;
    CHECK(!mm_movie_autosave(0, 0, &t.s) && !t.begins);
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
        CHECK(!mm_movie_save_resources(&t.s) && !t.archiveRefs && !t.data.resources);
    }
    init(&t);
    t.data.resources = 0;
    t.scenes.open = 0;
    t.childCount = t.indexChildCount = 0;
    t.data.movieNumber = NIL;
    CHECK(mm_movie_save_resources(&t.s) && t.data.resources == &t && t.archiveRefs == 1 && t.cacheBytes == 0);
    init(&t);
    t.data.resources = 0;
    t.scenes.open = 0;
    t.fail = TITLE;
    t.left = 1;
    CHECK(!mm_movie_save_resources(&t.s) && t.data.resources == &t && t.archiveRefs == 1);
    init(&t);
    t.temp = 0;
    t.fail = PATH;
    t.left = 1;
    CHECK(!mm_movie_save_ensure(&t, publish, &t.s) && t.events[0] == PUBLICATION && !t.begins);
    init(&t);
    t.temp = 0;
    t.changeSavedRoot = 1;
    t.savedRootNumber = 99;
    CHECK(mm_movie_save_temporary(&t.s) && t.temp && t.marks == 1 && t.data.movieNumber == 99);
    init(&t);
    t.temp = 0;
    t.changeSavedRoot = 1;
    t.savedRootNumber = NIL;
    CHECK(!mm_movie_save_temporary(&t.s) && t.temp && t.missingRoot == 1 && t.data.movieNumber == 9);
    init(&t);
    t.named.readOnly = 1;
    CHECK(mm_movie_save_bind(&t.named, &t.s) && t.saved[0].refs == 1 && t.state.nameValid && t.state.readOnly);
    CHECK(mm_movie_save_name(&output, &t.s) && output == 1);
    mm_movie_save_flush(&t.s);
    CHECK(t.events[t.eventCount - 1] == 200);
    t.fail = FIND;
    t.left = 1;
    CHECK(!mm_movie_save_bind(&t.named, &t.s) && !t.state.file && t.saved[0].refs == 0 && t.state.nameValid &&
          t.state.readOnly);
    mm_movie_save_release(1, &t.s);
    CHECK(!t.state.nameValid);
    output = 77;
    CHECK(!mm_movie_save_name(&output, &t.s) && output == 77);
    t.state.containerNamed = 1;
    CHECK(!mm_movie_save_name(&output, &t.s) && output == 3);
    CHECK(mm_movie_save_command(10, 10, 11, &t.state) == 11);
    CHECK(mm_movie_save_command(12, 10, 11, &t.state) == 12);
    init(&t);
    CHECK(mm_movie_save_bind(&t.named, &t.s));
    t.eventCount = 0;
    CHECK(mm_movie_autosave(&t.named, 0, &t.s));
    CHECK(t.saved[0].refs == 0 && t.saved[1].refs == 1 && t.state.file == &t.saved[1] && t.state.nameValid);
    CHECK(t.events[2] == RELEASE_SAVED && t.events[3] == PHYSICAL_SAVE && t.events[4] == RETAIN_SAVED);
    mm_movie_save_release(1, &t.s);
    CHECK(t.saved[1].refs == 0);
    init(&t);
    CHECK(mm_movie_save_bind(&t.named, &t.s));
    t.fail = SAVE;
    t.left = 1;
    CHECK(!mm_movie_autosave(&t.named, 0, &t.s) && t.state.file && t.state.nameValid && t.saved[0].refs == 1);
    CHECK(!has(&t, MM_SAVE_TABLE, 20, 0));
    mm_movie_save_release(1, &t.s);
    init(&t);
    CHECK(mm_movie_save_bind(&t.named, &t.s));
    t.fail = FIND;
    t.left = 1;
    t.skip = 1;
    CHECK(mm_movie_autosave(&t.named, 0, &t.s) && !t.state.file && !t.state.nameValid && !t.saved[0].refs);
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
        CHECK(mm_movie_autosave(&t.named, 0, &t.s) == (count == 1));
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
    CHECK(!mm_movie_autosave(&t.named, 0, &t.s) && t.savedCalls == 2 && t.errors == 1 && t.resets == 1 &&
          t.error);
    return 0;
}
static int packaging(void)
{
    Test t;
    int remember;
    init(&t);
    t.named.vmm = 1;
    CHECK(!mm_movie_save_to(&t.named, 1, &t.s) && t.errors == 1 && !t.begins);
    init(&t);
    t.container = 1;
    t.bundled = 1;
    CHECK(!mm_movie_save_to(&t.named, 1, &t.s) && t.packageNull == 1 && !t.begins && !t.undos);
    for (remember = 0; remember <= 1; ++remember)
    {
        init(&t);
        t.container = 1;
        t.named.vmm = 1;
        t.named.readOnly = 1;
        t.changeSavedRoot = 1;
        t.savedRootNumber = 99;
        CHECK(mm_movie_save_to(&t.named, remember, &t.s));
        CHECK(t.packages == 1 && t.undos == 1 && t.tempSets == 1 && t.temp &&
              t.data.movieNumber == 99 && !t.dirty);
        CHECK(t.state.containerNamed == remember && t.state.readOnly == remember && t.state.nameValid == remember);
        CHECK(t.lastTitle == &t.named && t.begins == t.ends && t.rollcalls == 1);
        CHECK(t.sourceReleases == 1 && !t.sourceActive && !t.badSourceLifetime);
        mm_movie_save_release(1, &t.s);
        CHECK(!t.saved[1].refs);
        init(&t);
        t.changeSavedRoot = 1;
        t.savedRootNumber = 99;
        CHECK(mm_movie_save_to(&t.named, remember, &t.s));
        CHECK(t.persistent == 1 && !t.temp && t.undos == 1 && !t.dirty && t.state.nameValid == remember &&
              t.data.movieNumber == 99);
        mm_movie_save_release(1, &t.s);
    }
    init(&t);
    t.container = 1;
    t.named.vmm = 1;
    t.fail = PACKAGE;
    t.left = 1;
    CHECK(!mm_movie_save_to(&t.named, 1, &t.s) && t.dirty && !t.state.autosaveDirty && t.temp && !t.undos &&
          !t.state.containerNamed);
    CHECK(t.updates == 2 && !t.lastTitle);
    init(&t);
    t.container = 1;
    t.named.vmm = 1;
    t.state.containerNamed = 1;
    t.fail = PACKAGE;
    t.left = 1;
    CHECK(!mm_movie_save_to(&t.named, 1, &t.s) && t.lastTitle == &t.publicName);
    init(&t);
    t.container = 1;
    t.named.vmm = 1;
    CHECK(mm_movie_autosave(&t.named, 1, &t.s) && !t.state.containerNamed && t.packages == 1);
    init(&t);
    t.container = 1;
    t.state.autosaveDirty = 0;
    t.sourceArchive = 1;
    t.tagCount = 3;
    t.tags[0].sid = 0;
    t.tags[1].sid = 6;
    t.tags[2].sid = 7;
    append(&t, MM_SAVE_SCENE, 12, 0);
    append(&t, MM_SAVE_SCENE, 13, 2);
    CHECK(mm_movie_needs_container(&t.s) && !t.liveAlloc && !t.savedCalls);
    CHECK(t.sceneTagCalls == 1 && t.lastSceneTagged == 10);
    t.tags[1].sid = 5;
    CHECK(!mm_movie_needs_container(&t.s) && !t.liveAlloc);
    t.failTags = 1;
    CHECK(mm_movie_needs_container(&t.s) && !t.liveAlloc);
    t.failTags = 0;
    t.fail = SCENE_TAGS;
    t.left = 1;
    CHECK(mm_movie_needs_container(&t.s) && !t.liveAlloc);
    init(&t);
    t.container = t.sourceArchive = 1;
    t.fail = TITLE;
    t.left = 1;
    CHECK(mm_movie_needs_container(&t.s) && t.begins == 1 && t.ends == 1 && !t.sceneTagCalls);
    return 0;
}
static void add_sound(Test *t, MMU32 number, MMU32 slot, int references, int has_payload, MMU32 payload_slot)
{
    append(t, MM_SOUND_CHUNK, number, slot);
    t->soundPresent[number] = 1;
    t->refs[number] = references;
    t->soundExists[number] = has_payload;
    t->soundSlots[number] = payload_slot;
    if (has_payload)
    {
        MMChunkEntry *payload = extra_chunk(t, MM_SAVE_TABLE, 256 + number, 1);
        payload->flags = 0;
        payload->parent_count = 1;
    }
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
    t.tags[0].type = MM_SAVE_MATERIAL;
    t.tags[0].number = 1;
    t.tags[1].type = MM_SAVE_TEMPLATE;
    t.tags[1].number = 3;
    t.tags[2].sid = 1;
    CHECK(mm_movie_save_assets(&t, &t.s) && t.materialCount == 1 && t.templateCount == 1 && t.materials[0] == 1 &&
          t.templates[0] == 3);
    CHECK(t.tossed == 2 && t.deletedChunks == 2 && !t.liveAlloc);
    CHECK(t.events[0] == DELETE_CHUNK && t.events[1] == CACHE_TOSS && t.events[2] == DELETE_CHUNK &&
          t.events[3] == CACHE_TOSS);
    init(&t);
    t.materials[0] = 1;
    t.materials[1] = 2;
    t.materialCount = 2;
    t.materialChild = 2;
    CHECK(mm_movie_save_assets(&t, &t.s) && t.materialCount == 1 && t.materials[0] == 2 && t.deletedChunks == 1);
    init(&t);
    t.materials[0] = 1;
    t.materialCount = 1;
    t.tagCount = 1;
    t.tags[0].number = 2;
    t.tags[0].type = MM_SAVE_MATERIAL;
    t.failFrames = 1;
    CHECK(mm_movie_save_assets(&t, &t.s) && t.sceneTagCalls == 1 && t.allocations > 0 &&
          t.materialCount == 1 && !t.deletedChunks && !t.liveAlloc);
    init(&t);
    t.materials[0] = 1;
    t.materialCount = 1;
    t.failAlloc = 1;
    CHECK(!mm_movie_save_assets(&t, &t.s) && t.materialCount == 1 && !t.deletedChunks &&
          !t.liveAlloc);
    init(&t);
    init(&foreign);
    foreign.materials[0] = 1;
    foreign.materialCount = 1;
    CHECK(mm_movie_save_assets(&foreign, &t.s) && !foreign.materialCount && !t.tossed);
    for (purge_all = 0; purge_all <= 1; ++purge_all)
    {
        init(&t);
        add_sound(&t, 0, 20, 1, 1, 1);
        add_sound(&t, 1, 21, 1, 1, 0);
        add_sound(&t, 2, 22, 2, 0, 0);
        mm_movie_save_sounds(purge_all, &t.s);
        CHECK(!has(&t, MM_SOUND_CHUNK, 0, 20) && has(&t, MM_SOUND_CHUNK, 1, 21) == !purge_all &&
              has(&t, MM_SOUND_CHUNK, 2, 22) && !t.badChildren);
        CHECK(t.deletedCount == 1 + purge_all);
        CHECK(!t.soundPresent[0] && !extra_chunk(&t, MM_SAVE_TABLE, 256, 0));
        CHECK(t.soundPresent[1] == !purge_all && (extra_chunk(&t, MM_SAVE_TABLE, 257, 0) != NULL) == !purge_all);
        if (purge_all)
            CHECK(t.deleted[0].number == 1 && t.deleted[1].number == 0);
    }
    init(&t);
    t.badIndex = 2;
    mm_movie_save_sounds(1, &t.s);
    CHECK(t.badChildren == 1 && !t.deletedCount);
    init(&t);
    t.state.closing = 1;
    add_sound(&t, 0, 20, 1, 0, 0);
    CHECK(mm_movie_save_gc(&foreign, &t.s) && !has(&t, MM_SOUND_CHUNK, 0, 20) && !t.queries);
    init(&t);
    t.state.closing = 1;
    add_sound(&t, 1, 20, 1, 1, 0);
    CHECK(mm_movie_save_gc(&t, &t.s) && t.queries == 1 && has(&t, MM_SOUND_CHUNK, 1, 20));
    t.purgeAnswer = 1;
    t.materials[0] = 1;
    t.materialCount = 1;
    t.failAlloc = 1;
    CHECK(!mm_movie_save_gc(&t, &t.s) && t.queries == 2 && !has(&t, MM_SOUND_CHUNK, 1, 20) && !t.liveAlloc);
    return 0;
}
typedef struct PublishTest
{
    int collisions, fail_reserve, fail_write, fail_replace;
    char calls[MM_MOVIE_STAGE_ATTEMPTS + 4];
    size_t used;
} PublishTest;

static void call(PublishTest *test, char code)
{
    test->calls[test->used++] = code;
    test->calls[test->used] = '\0';
}

static MMMovieStageResult reserve_stage(void *context)
{
    PublishTest *test = (PublishTest *)context;
    call(test, 'S');
    if (test->fail_reserve) return MM_MOVIE_STAGE_ERROR;
    if (test->collisions) { --test->collisions; return MM_MOVIE_STAGE_COLLISION; }
    return MM_MOVIE_STAGE_OK;
}

static int write_candidate(void *context)
{
    PublishTest *test = (PublishTest *)context;
    call(test, 'W');
    return !test->fail_write;
}

static int replace_candidate(void *context)
{
    PublishTest *test = (PublishTest *)context;
    call(test, 'R');
    return !test->fail_replace;
}

static void discard_stage(void *context)
{
    call((PublishTest *)context, 'D');
}

static int publishing(void)
{
    PublishTest test = {0};
    MMMoviePublishServices services = {NULL, reserve_stage, write_candidate, replace_candidate, discard_stage};
    MMMovieCopyFormat format;
    const char *suffix;
    services.context = &test;
    CHECK(mm_movie_copy_format("", 0, &format, &suffix) == MM_MOVIE_COPY_OK &&
          format == MM_MOVIE_COPY_NATIVE && !strcmp(suffix, ".3mm"));
    CHECK(mm_movie_copy_format("", 1, &format, &suffix) == MM_MOVIE_COPY_OK &&
          format == MM_MOVIE_COPY_BUNDLE && !strcmp(suffix, ".vmm"));
    CHECK(mm_movie_copy_format(".3MM", 1, &format, &suffix) == MM_MOVIE_COPY_BUNDLE_REQUIRED &&
          format == MM_MOVIE_COPY_BUNDLE && !strcmp(suffix, ".vmm"));
    CHECK(mm_movie_copy_format(".VMM", 0, &format, &suffix) == MM_MOVIE_COPY_OK &&
          format == MM_MOVIE_COPY_BUNDLE && !suffix);
    CHECK(mm_movie_copy_format(".avi", 0, &format, &suffix) == MM_MOVIE_COPY_BAD_EXTENSION);

    test.collisions = 2;
    CHECK(mm_movie_publish_save(&services) == MM_MOVIE_PUBLISH_OK && !strcmp(test.calls, "SSSWRD"));
    test.used = 0;
    test.collisions = MM_MOVIE_STAGE_ATTEMPTS + 1;
    CHECK(mm_movie_publish_save(&services) == MM_MOVIE_PUBLISH_STAGE_FAILED &&
          test.used == MM_MOVIE_STAGE_ATTEMPTS && test.collisions == 1 &&
          strspn(test.calls, "S") == MM_MOVIE_STAGE_ATTEMPTS);
    test.used = 0;
    test.collisions = 0;
    test.fail_reserve = 1;
    CHECK(mm_movie_publish_save(&services) == MM_MOVIE_PUBLISH_STAGE_FAILED && !strcmp(test.calls, "S"));
    test.used = 0;
    test.fail_reserve = 0;
    test.fail_write = 1;
    CHECK(mm_movie_publish_save(&services) == MM_MOVIE_PUBLISH_WRITE_FAILED && !strcmp(test.calls, "SWD"));
    test.used = 0;
    test.fail_write = 0;
    test.fail_replace = 1;
    CHECK(mm_movie_publish_save(&services) == MM_MOVIE_PUBLISH_REPLACE_FAILED && !strcmp(test.calls, "SWRD"));
    return 0;
}

int main(void)
{
    if (autosaving() || ownership() || retrying() || packaging() || collecting() || publishing())
        return 1;
    puts("Movie autosave rollback, publication, ownership, packaging and collection passed");
    return 0;
}

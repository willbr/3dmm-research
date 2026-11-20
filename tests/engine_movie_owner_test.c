#include "mm/movie_owner.h"
#include "mm/movie_runtime.h"
#include "mm/movie_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "movie owner failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Host Host;
typedef struct Resource
{
    Host *host;
    int refs;
} Resource;
struct Host
{
    char log[512];
    int used, resources, fail, scenes, previous, persistent;
    int open_calls;
    int opening;
    MMU32 root_number;
    void *open_path;
    MMU32 rollcall_bytes;
    unsigned char written_rollcall[512];
    int buffers;
};
enum
{
    FAIL_CREATE = 1,
    FAIL_OPEN,
    FAIL_VERIFY,
    FAIL_RESOURCE,
    FAIL_ROOT,
    FAIL_READ,
    FAIL_WRITE,
    FAIL_NAME,
    FAIL_SAVE,
    FAIL_WIRE,
    FAIL_TAG,
    FAIL_APPEND,
    FAIL_ADD,
    FAIL_TABLE_WRITE,
    FAIL_ADOPT
};
static void event(Host *h, char c)
{
    CHECK(h->used < 511);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static void reset(Host *h)
{
    h->used = 0;
    h->log[0] = 0;
    h->open_calls = 0;
}
static void *create(void *c)
{
    Host *h = (Host *)c;
    event(h, 'c');
    return h->fail == FAIL_CREATE ? NULL : h;
}
static void *open_file(void *c, void *path)
{
    Host *h = (Host *)c;
    CHECK(path && (!h->open_path || path == h->open_path));
    event(h, 'o');
    return h->fail == FAIL_OPEN ? NULL : h;
}
static int graph_entry(void *context, MMU32 position, MMChunkEntry *entry)
{
    Host *h = (Host *)context;
    memset(entry, 0, sizeof(*entry));
    if (position > (MMU32)h->scenes)
        return 0;
    entry->type = position ? 0x5343454eUL : 0x4d564945UL;
    entry->number = position ? position : h->root_number;
    if (!position)
        entry->child_count = (MMU32)h->scenes;
    return 1;
}
static int graph_child(void *context, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    Host *h = (Host *)context;
    if (parent || position >= (MMU32)h->scenes)
        return 0;
    child->type = 0x5343454eUL;
    child->number = position + 1;
    child->slot = position;
    return 1;
}
static void graph_access(void *context, MMChunkAccess *access)
{
    Host *h = (Host *)context;
    if (!h->opening)
        event(h, 's');
    access->context = h;
    access->count = (MMU32)h->scenes + 1;
    access->entry = graph_entry;
    access->child = graph_child;
}
static int graph_read(void *context, void *request, MMU32 position, const MMChunkEntry *entry, void *bytes, MMU32 size)
{
    const unsigned char header[] = {1, 0, 3, 3, 2, 0, 2, 0};
    (void)context;
    (void)request;
    CHECK(!position && entry->type == 0x4d564945UL && size == sizeof(header));
    memcpy(bytes, header, size);
    return MM_MOVIE_OK;
}
static int open_session(void *c, void *f, MMU32 *n, MMMovieSession **out)
{
    Host *h = (Host *)c;
    MMMovieSessionGraph graph;
    int status;
    CHECK(f == h);
    event(h, 'v');
    *n = h->root_number;
    if (h->fail == FAIL_VERIFY)
        return 0;
    graph.context = h;
    graph.access = graph_access;
    graph.read = graph_read;
    graph.payload = NULL;
    graph.name = NULL;
    h->opening = 1;
    status = mm_movie_session_open_graph(&graph, NULL, n, out);
    h->opening = 0;
    return status == MM_MOVIE_OK;
}
static void *new_resource(void *c, void *f)
{
    Host *h = (Host *)c;
    Resource *r;
    CHECK(f == h);
    event(h, 'r');
    if (h->fail == FAIL_RESOURCE)
        return NULL;
    r = (Resource *)malloc(sizeof(*r));
    CHECK(r != NULL);
    r->host = h;
    r->refs = 1;
    ++h->resources;
    return r;
}
static void release_file(void *f)
{
    event((Host *)f, 'f');
}
static void release_resource(void *p)
{
    Resource *r = (Resource *)p;
    event(r->host, 'R');
    CHECK(r->refs > 0);
    if (!--r->refs)
    {
        --r->host->resources;
        free(r);
    }
}
static void close_actor(void *c, MMRollCallActor *a)
{
    Host *h = (Host *)c;
    event(h, (char)('A' + a->data.actor_id));
    if (a->cache)
        release_resource(a->cache);
    a->cache = NULL;
}
static void *file(void *p)
{
    return ((Resource *)p)->host;
}
static int root(void *c, void *f, MMU32 *n)
{
    Host *h = (Host *)c;
    CHECK(f == h);
    event(h, 'b');
    *n = 7;
    return h->fail != FAIL_ROOT;
}
static int read_call(void *c, void *r, MMU32 n, MMRollCallTable *out, MMI32 *limit)
{
    Host *h = (Host *)c;
    MMRollCallActor actor;
    CHECK(n == 7);
    event(h, 'i');
    if (h->fail == FAIL_READ)
        return 0;
    memset(&actor, 0, sizeof(actor));
    actor.cache = r;
    if (!mm_rollcall_table_add(out, "Actor", 5, &actor))
        return 0;
    ++((Resource *)r)->refs;
    *limit = 1;
    return 1;
}
static const unsigned char stored_name[] = {3, 3, 6, 'L', 'o', 'a', 'd', 'e', 'd', 0};
static int load_name(void *f, MMU32 type, MMU32 n, void *wire, MMU32 capacity, MMU32 *written)
{
    Host *h = (Host *)f;
    CHECK(type == MM_MOVIE_CHUNK && n == 7 && capacity >= sizeof(stored_name));
    event(h, 'n');
    memcpy(wire, stored_name, sizeof(stored_name));
    *written = sizeof(stored_name);
    return 1;
}
static int previous(void *c, void *f, MMU32 n, MMU32 *out)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 7);
    event(h, 'p');
    *out = 9;
    return h->previous;
}
static int write_call(void *c, void *f, MMU32 n, MMRollCallTable *table, MMU32 *out)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 7 && table);
    event(h, 'j');
    *out = 10;
    return h->fail != FAIL_WRITE;
}
static void delete_previous(void *c, void *f, MMU32 n, MMU32 old)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 7 && old == 9);
    event(h, 'd');
}
static int set_name(void *f, MMU32 type, MMU32 n, const void *wire, MMU32 bytes)
{
    Host *h = (Host *)f;
    CHECK(type == MM_MOVIE_CHUNK && n == 7 && bytes == sizeof(stored_name));
    CHECK(!memcmp(wire, stored_name, bytes));
    event(h, 'N');
    return h->fail != FAIL_NAME;
}
static int save(void *c, void *f)
{
    Host *h = (Host *)c;
    CHECK(f == h);
    event(h, 'S');
    return h->fail != FAIL_SAVE;
}
static void persist(void *f)
{
    Host *h = (Host *)f;
    event(h, 'P');
    ++h->persistent;
}
static void *resize_rollcall(void *, void *, MMU32, MMU32, int);
static void free_rollcall(void *, void *, MMU32);
static MMMovieOwnerServices owners(Host *h)
{
    MMMovieOwnerServices s = {
        NULL, {NULL, resize_rollcall, free_rollcall}, 0x0303, 0,
        create, open_file, open_session, new_resource, release_file, release_resource,
        close_actor, file, root, read_call,
        {NULL, MM_TEXT_WINDOWS, MM_TEXT_UTF16_LE, NULL, load_name, set_name, NULL},
        previous, write_call, delete_previous, save, persist};
    s.context = s.actor_allocator.context = h;
    return s;
}
static void ownership(void)
{
    Host h;
    MMMovieState m;
    MMMovieOwnerServices s;
    void *old_r, *old_entries;
    MMRollCallActor actor;
    MMMovieSession *original;
    MMU32 number = 99;
    int i;
    const int load_failures[] = {FAIL_OPEN, FAIL_VERIFY, FAIL_RESOURCE, FAIL_READ, FAIL_WIRE};
    memset(&h, 0, sizeof(h));
    h.root_number = 7;
    s = owners(&h);
    mm_movie_state_init(&m);
    CHECK(m.movieNumber == 0xffffffffUL && !m.resources && !m.actors.stride);
    CHECK(!mm_movie_save(&m, &s) && !h.used);
    CHECK(mm_movie_init_empty(&m, &s) && !strcmp(h.log, "crfb") && m.actors.stride && !h.buffers);
    CHECK(mm_movie_title_set(&m.title, "Old", 3));
    old_r = m.resources;
    memset(&actor, 0, sizeof(actor));
    actor.data.actor_id = 2;
    actor.cache = old_r;
    CHECK(mm_rollcall_table_add(&m.actors, "Old actor", 9, &actor));
    ++((Resource *)old_r)->refs;
    old_entries = m.actors.storage.buffers[1].data;
    m.sceneCount = 5;
    m.actorIdLimit = 42;
    for (i = FAIL_CREATE; i <= FAIL_ROOT; ++i)
    {
        if (i == FAIL_OPEN || i == FAIL_VERIFY)
            continue;
        reset(&h);
        h.fail = i;
        CHECK(!mm_movie_init_empty(&m, &s));
        CHECK(m.resources == old_r && m.actors.storage.buffers[1].data == old_entries && m.actors.count == 1 &&
              m.sceneCount == 5 && m.actorIdLimit == 42 && h.buffers == 2 && h.resources == 1);
        CHECK(m.title.count == 3 && !memcmp(mm_movie_title_text(&m.title), "Old", 3));
        if (i == FAIL_RESOURCE)
            CHECK(!strcmp(h.log, "crf"));
        if (i == FAIL_ROOT)
            CHECK(!strcmp(h.log, "crfbR"));
    }
    for (i = 0; i < (int)(sizeof(load_failures) / sizeof(*load_failures)); ++i)
    {
        reset(&h);
        h.fail = load_failures[i];
        CHECK(!mm_movie_load(&m, &h, 99, &s));
        CHECK(m.resources == old_r && m.actors.storage.buffers[1].data == old_entries && m.actors.count == 1 &&
              h.resources == 1 && h.buffers == 2 && ((Resource *)old_r)->refs == 2);
        CHECK(m.title.count == 3 && !memcmp(mm_movie_title_text(&m.title), "Old", 3));
    }
    reset(&h);
    h.fail = 0;
    h.scenes = 2;
    CHECK(mm_movie_load(&m, &h, 99, &s) && m.movieNumber == 7 && m.actorIdLimit == 1 && m.sceneCount == 2);
    CHECK(!strcmp(h.log, "ovrfinsssCRR") && h.resources == 1 && h.buffers == 2);
    h.previous = 1;
    reset(&h);
    CHECK(mm_movie_save(&m, &s) && !strcmp(h.log, "pjdNSP") && h.persistent == 1);
    reset(&h);
    h.fail = FAIL_WRITE;
    CHECK(!mm_movie_save(&m, &s) && !strcmp(h.log, "pj"));
    reset(&h);
    h.fail = FAIL_NAME;
    CHECK(!mm_movie_save(&m, &s) && !strcmp(h.log, "pjdN"));
    reset(&h);
    h.fail = FAIL_SAVE;
    CHECK(!mm_movie_save(&m, &s) && !strcmp(h.log, "pjdNS") && h.persistent == 1);
    h.fail = 0;
    original = m.session;
    h.scenes = 3;
    CHECK(mm_movie_state_scene_number(&m, 2, &number, &h, s.context, s.open_session) &&
          number == 3 && m.session == original);
    h.root_number = m.movieNumber = 11;
    h.fail = FAIL_VERIFY;
    CHECK(!mm_movie_state_scene_number(&m, 0, &number, &h, s.context, s.open_session) &&
          number == 3 && m.session == original);
    h.fail = 0;
    CHECK(mm_movie_state_scene_number(&m, 0, &number, &h, s.context, s.open_session) && number == 1 && m.session &&
          mm_movie_session_root(m.session)->number == 11);
    reset(&h);
    mm_movie_state_dispose(&m, &s);
    CHECK(!strcmp(h.log, "ARR") && !h.buffers && !h.resources && !m.session && !m.actors.stride);
    reset(&h);
    mm_movie_state_dispose(&m, &s);
    CHECK(!h.used);
}
static int open_actor(void *c, MMRollCallActor *a, void *r)
{
    Host *h = (Host *)c;
    event(h, 'h');
    ++h->open_calls;
    if (h->fail == FAIL_TAG && h->open_calls == 2)
        return 0;
    a->cache = r;
    ++((Resource *)r)->refs;
    return 1;
}
static void *resize_rollcall(void *p, void *old, MMU32 old_size, MMU32 new_size, int priority)
{
    Host *h = (Host *)p;
    int fresh = old == NULL;
    void *data;
    (void)old_size;
    (void)priority;
    if (h->fail == FAIL_WIRE || (h->fail == FAIL_APPEND && h->open_calls == 2))
        return NULL;
    data = realloc(old, new_size);
    if (data && fresh)
        ++h->buffers;
    return data;
}
static void free_rollcall(void *p, void *data, MMU32 size)
{
    --((Host *)p)->buffers;
    (void)size;
    free(data);
}
static int write_rollcall(void *p, MMU32 offset, const void *data, size_t bytes)
{
    Host *h = (Host *)p;
    CHECK(offset <= h->rollcall_bytes && bytes <= h->rollcall_bytes - offset);
    if (!offset)
        event(h, '=');
    if (h->fail == FAIL_TABLE_WRITE)
        return 0;
    memcpy(h->written_rollcall + offset, data, bytes);
    return 1;
}
static int add_chunk(void *c, MMU32 bytes, MMU32 *n, MMStorageWriter *writer)
{
    Host *h = (Host *)c;
    CHECK(bytes > 20 && bytes <= sizeof(h->written_rollcall));
    event(h, '+');
    *n = 10;
    if (h->fail == FAIL_ADD)
        return 0;
    h->rollcall_bytes = bytes;
    writer->context = h;
    writer->length = bytes;
    writer->write_at = write_rollcall;
    return 1;
}
static void check_wire(Host *h)
{
    static const char *names[] = {"Long first", "B", "Third"};
    MMMemoryStream m;
    MMRollCallTable table;
    MMMovieActor a;
    unsigned char bytes[MM_MOVIE_ACTOR_BYTES];
    const void *name;
    MMU32 i, count;
    int big;
    CHECK(mm_memory_stream_init(&m, h->written_rollcall, h->rollcall_bytes));
    CHECK(mm_rollcall_table_init(&table, 0, 1, 0x0303, 0, NULL) == MM_STORAGE_OK);
    CHECK(mm_rollcall_table_read(&table, &m.stream, NULL, NULL, &big) == MM_STORAGE_OK && table.count == 3);
    CHECK(table.stride == 4 + MM_MOVIE_ACTOR_BYTES);
    for (i = 0; i < table.count; ++i)
    {
        CHECK(mm_rollcall_table_name(&table, i, &name, &count) && count == strlen(names[i]) &&
              !memcmp(name, names[i], count));
        CHECK(mm_rollcall_table_extra(&table, i, bytes));
        mm_memory_stream_init(&m, bytes, sizeof(bytes));
        CHECK(mm_movie_read_actor(&m.stream, big, &a, NULL) == MM_MOVIE_OK && a.actor_id == (MMI32)i &&
              bytes[16] == 0 && bytes[17] == 0 && bytes[18] == 0 && bytes[19] == 0);
    }
    CHECK(mm_rollcall_table_dispose(&table) == MM_STORAGE_OK);
}
static int adopt(void *c, MMU32 m, MMU32 n)
{
    Host *h = (Host *)c;
    CHECK(m == 7 && n == 10);
    event(h, '>');
    return h->fail != FAIL_ADOPT;
}
static void delete_chunk(void *c, MMU32 n)
{
    Host *h = (Host *)c;
    CHECK(n == 10);
    event(h, '-');
}
static void make_wire_rollcall(Host *h, MMMemoryStream *memory, int big, int bad_row, MMU32 extra)
{
    MMMovieActor actor;
    MMRollCallTable wire;
    MMStorageWriter writer = {NULL, 0, write_rollcall};
    unsigned char row[MM_MOVIE_ACTOR_BYTES];
    char long_name[160];
    int i;
    memset(long_name, 'B', sizeof(long_name));
    memset(&actor, 0, sizeof(actor));
    actor.reference_count = 3;
    actor.template_number = 123;
    CHECK(mm_rollcall_table_init(&wire, extra, 1, 0x0303, 0, NULL) == MM_STORAGE_OK);
    for (i = 0; i < 3; ++i)
    {
        actor.actor_id = i == bad_row ? -1 : i;
        mm_movie_write_actor(&actor, row, big);
        CHECK(mm_rollcall_table_add(&wire, i == 1 ? long_name : "Actor", i == 1 ? sizeof(long_name) : 5, row));
    }
    h->rollcall_bytes = mm_rollcall_table_wire_size(&wire);
    CHECK(h->rollcall_bytes <= sizeof(h->written_rollcall));
    writer.context = h;
    writer.length = h->rollcall_bytes;
    CHECK(mm_rollcall_table_write(&wire, &writer, big) == MM_STORAGE_OK);
    CHECK(mm_rollcall_table_dispose(&wire) == MM_STORAGE_OK);
    CHECK(mm_memory_stream_init(memory, h->written_rollcall, h->rollcall_bytes));
}
static void write_rollcalls(Host *h, MMRollCallTable *source)
{
    static const char *names[] = {"A", "B", "Third"};
    static const int failures[] = {FAIL_WIRE, FAIL_ADD, FAIL_TABLE_WRITE, FAIL_ADOPT};
    static const char *logs[] = {"", "+", "+=-", "+=>-"};
    MMRollCallTable input;
    MMRollCallActor actor;
    MMStorageAllocator allocator = {NULL, resize_rollcall, free_rollcall};
    MMRollCallWriter writer = {NULL, add_chunk, adopt, delete_chunk};
    MMU32 i, written = 99;
    allocator.context = writer.context = h;
    CHECK(mm_rollcall_table_init(&input, sizeof(MMRollCallActor), 1, 0x0303, 0, &allocator) == MM_STORAGE_OK);
    for (i = 0; i < source->count; ++i)
    {
        CHECK(mm_rollcall_table_extra(source, i, &actor));
        CHECK(mm_rollcall_table_add(&input, names[i], (MMU32)strlen(names[i]), &actor));
    }
    CHECK(mm_rollcall_table_rename(&input, 0, "Long first", 10));
    reset(h);
    CHECK(mm_movie_write_rollcall(7, &input, &written, &writer) && written == 10 && !strcmp(h->log, "+=>"));
    check_wire(h);
    for (i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i)
    {
        h->fail = failures[i];
        reset(h);
        CHECK(!mm_movie_write_rollcall(7, &input, &written, &writer) && !strcmp(h->log, logs[i]) &&
              h->buffers == 4);
    }
    h->fail = 0;
    CHECK(mm_rollcall_table_dispose(&input) == MM_STORAGE_OK && h->buffers == 2);
}
static void rollcalls(void)
{
    Host h;
    Resource *r;
    MMRollCallReader reader = {NULL, NULL, open_actor, close_actor};
    MMStorageAllocator allocator = {NULL, resize_rollcall, free_rollcall};
    MMRollCallTable actors;
    MMRollCallActor actor;
    MMMemoryStream memory;
    const void *name;
    MMU32 length, row;
    MMI32 limit;
    int big, failures[] = {FAIL_WIRE, FAIL_TAG, FAIL_APPEND}, i;
    memset(&h, 0, sizeof(h));
    reader.context = allocator.context = &h;
    r = (Resource *)new_resource(&h, &h);
    CHECK(mm_rollcall_table_init(&actors, sizeof(MMRollCallActor), 1, 0x0303, 0, &allocator) == MM_STORAGE_OK);
    for (big = 0; big < 2; ++big)
    {
        make_wire_rollcall(&h, &memory, big, -2, MM_MOVIE_ACTOR_BYTES);
        reset(&h);
        limit = 0;
        CHECK(mm_movie_read_rollcall(&memory.stream, r, &actors, &limit, &reader) && limit == 3 && actors.count == 3 &&
              r->refs == 4 && h.buffers == 2);
        CHECK(mm_rollcall_table_name(&actors, 1, &name, &length) && length == 160 && ((const char *)name)[159] == 'B');
        write_rollcalls(&h, &actors);
        CHECK(r->refs == 4);
        for (row = 0; row < actors.count; ++row)
        {
            CHECK(mm_rollcall_table_extra(&actors, row, &actor) && actor.cache == r &&
                  actor.data.actor_id == (MMI32)row && actor.data.template_number == 123);
            close_actor(&h, &actor);
        }
        CHECK(mm_rollcall_table_dispose(&actors) == MM_STORAGE_OK && r->refs == 1 && !h.buffers);

        make_wire_rollcall(&h, &memory, big, -2, MM_MOVIE_ACTOR_BYTES);
        for (i = 0; i < (int)(sizeof(failures) / sizeof(*failures)); ++i)
        {
            h.fail = failures[i];
            reset(&h);
            limit = 42;
            CHECK(!mm_movie_read_rollcall(&memory.stream, r, &actors, &limit, &reader) && !actors.count &&
                  limit == 42 && r->refs == 1 && !h.buffers);
            if (h.fail == FAIL_APPEND)
                CHECK(!strcmp(h.log, "hhBRAR"));
        }
        h.fail = 0;
        memory.stream.length = 19;
        reset(&h);
        CHECK(!mm_movie_read_rollcall(&memory.stream, r, &actors, &limit, &reader) && !h.used && !h.buffers);
        make_wire_rollcall(&h, &memory, big, 1, MM_MOVIE_ACTOR_BYTES);
        reset(&h);
        CHECK(!mm_movie_read_rollcall(&memory.stream, r, &actors, &limit, &reader) && !actors.count &&
              limit == 42 && r->refs == 1 && !h.buffers && !strcmp(h.log, "hAR"));
        make_wire_rollcall(&h, &memory, big, -2, 4);
        reset(&h);
        CHECK(!mm_movie_read_rollcall(&memory.stream, r, &actors, &limit, &reader) && !h.used && !h.buffers);
    }
    release_resource(r);
    CHECK(mm_rollcall_table_dispose(&actors) == MM_STORAGE_OK && !h.resources && !h.buffers);
}
typedef struct RuntimeCase RuntimeCase;
typedef struct RuntimeRef
{
    RuntimeCase *test;
    int refs;
    char released;
} RuntimeRef;
struct RuntimeCase
{
    Host archive;
    MMMovieState movie;
    MMMovieRuntime runtime;
    MMPlaybackState playback;
    MMMovieSaveState saved;
    MMMovieScenes scenes;
    MMMovieOwnerServices owners;
    MMMovieSaveServices saves;
    MMMovieSceneServices scene_services;
    MMMovieRuntimeServices services;
    RuntimeRef client, world, sounds, palette;
    unsigned fail;
    int working, bundle, busy, discarded, disable_fades;
    void *container_name, *title_path;
};
enum { PREPARE_FAILED = 1, UNDO_FAILED, WORLD_FAILED, SOUNDS_FAILED, BIND_FAILED, CACHE_FAILED };

static int prepare_runtime(void *context, void *path, void **working)
{
    RuntimeCase *t = context;
    event(&t->archive, 'k');
    if (path == &t->bundle)
        *working = &t->working;
    return t->fail != PREPARE_FAILED;
}
static int init_undo(void *context)
{
    RuntimeCase *t = context;
    event(&t->archive, 'u');
    return t->fail != UNDO_FAILED;
}
static void *create_world(void *context, void *client, int half)
{
    RuntimeCase *t = context;
    CHECK(client == &t->client && t->client.refs == 1 && half && !t->busy);
    event(&t->archive, 'w');
    if (t->fail == WORLD_FAILED)
        return NULL;
    ++t->world.refs;
    return &t->world;
}
static void *create_sounds(void *context)
{
    RuntimeCase *t = context;
    CHECK(t->runtime.world == &t->world && t->client.refs == 1 && !t->busy);
    event(&t->archive, 'q');
    if (t->fail == SOUNDS_FAILED)
        return NULL;
    ++t->sounds.refs;
    return &t->sounds;
}
static void retain_runtime(void *p)
{
    RuntimeRef *ref = p;
    CHECK(ref->refs > 0);
    ++ref->refs;
    event(&ref->test->archive, '+');
}
static void release_runtime(void *p)
{
    RuntimeRef *ref = p;
    CHECK(ref->refs > 0);
    --ref->refs;
    event(&ref->test->archive, ref->released);
}
static void discard_document(void *context)
{
    RuntimeCase *t = context;
    CHECK(!t->discarded);
    ++t->discarded;
    event(&t->archive, 'D');
    mm_movie_runtime_dispose(&t->runtime, &t->playback, &t->services);
}
static void hold_archive(void *p) { event(p, 'h'); }
static void missing_saved(void *p) { event(&((RuntimeCase *)p)->archive, '!'); }
static void merge_sources(void *p, MMU32 root)
{
    CHECK(root == 7);
    event(p, 'm');
}
static MMMovieSessionGraph runtime_graph(void *p)
{
    MMMovieSessionGraph graph = {NULL, graph_access, graph_read, NULL, NULL};
    graph.context = p;
    return graph;
}
static int cache_tags(void *context, MMTagList *tags)
{
    RuntimeCase *t = context;
    CHECK(t->busy && t->saved.nameValid && tags->count == 0);
    event(&t->archive, 'K');
    return t->fail != CACHE_FAILED;
}
static void audio_resume(void *p)
{
    RuntimeCase *t = p;
    CHECK(t->runtime.user_paused && t->movie.resources && t->saved.file);
    event(&t->archive, 'a');
}
static void playback_stopped(void *p)
{
    RuntimeCase *t = ((RuntimeRef *)p)->test;
    CHECK(!t->runtime.user_paused && !t->movie.resources && !t->movie.session && !t->saved.file);
    event(&t->archive, 'p');
}
static int fades_disabled(void *p) { return ((RuntimeCase *)p)->disable_fades; }
static void restore_volume(void *p, MMI32 volume)
{
    CHECK(volume == 1000);
    event(&((RuntimeCase *)p)->archive, 'V');
}
static void roll_call_changed(void *p) { event(&((RuntimeRef *)p)->test->archive, 'j'); }
static void close_scene(void *p)
{
    RuntimeCase *t = p;
    CHECK(t->scenes.open == t && t->movie.actors.stride && t->runtime.world == &t->world);
    event(&t->archive, 'z');
}
static void *find_saved(void *context, void *path)
{
    RuntimeCase *t = context;
    CHECK(path == &t->working);
    event(&t->archive, 'g');
    return t->fail == BIND_FAILED ? NULL : &t->archive;
}
static void retain_saved(void *p) { event(p, 'G'); }
static void release_saved(void *p) { event(p, 'F'); }
static int is_bundle(void *p) { return p && *(int *)p == 1; }
static int read_only(void *context, void *path)
{
    RuntimeCase *t = context;
    event(&t->archive, 'y');
    return path == &t->working;
}
static void name_container(void *p, void *path) { ((RuntimeCase *)p)->container_name = path; }
static void *container_path(void *p) { return ((RuntimeCase *)p)->container_name; }
static void update_title(void *p, void *path)
{
    RuntimeCase *t = p;
    t->title_path = path;
    event(&t->archive, 'T');
}
static void long_op(void *p, int begin)
{
    RuntimeCase *t = p;
    CHECK(t->busy != begin);
    t->busy = begin;
    event(&t->archive, begin ? '[' : ']');
}
static void runtime_init(RuntimeCase *t)
{
    const MMMovieRuntimeServices runtime = {
        NULL, NULL, NULL, NULL, prepare_runtime, init_undo, create_world, create_sounds,
        retain_runtime, release_runtime, discard_document, hold_archive, missing_saved, merge_sources,
        cache_tags, audio_resume, playback_stopped, fades_disabled, restore_volume, roll_call_changed};
    memset(t, 0, sizeof(*t));
    t->bundle = 1;
    t->archive.root_number = 7;
    t->archive.open_path = &t->working;
    t->client.test = t->world.test = t->sounds.test = t->palette.test = t;
    t->client.refs = t->palette.refs = 1;
    t->client.released = '-';
    t->world.released = 'W';
    t->sounds.released = 'Q';
    t->palette.released = 'L';
    mm_movie_state_init(&t->movie);
    mm_movie_save_init(&t->saved);
    mm_movie_scenes_init(&t->scenes);
    mm_playback_state_init(&t->playback);
    t->owners = owners(&t->archive);
    t->saves.context = t;
    t->saves.state = &t->saved;
    t->saves.data = &t->movie;
    t->saves.file = file;
    t->saves.find_file = find_saved;
    t->saves.retain_saved = retain_saved;
    t->saves.release_saved = release_saved;
    t->saves.read_only = read_only;
    t->saves.is_vmm = is_bundle;
    t->saves.set_container_name = name_container;
    t->saves.container_name = container_path;
    t->saves.update_title = update_title;
    t->saves.long_op = long_op;
    t->saves.tag_graph = runtime_graph;
    t->scene_services.state = &t->scenes;
    t->scene_services.close = close_scene;
    t->services = runtime;
    t->services.context = t;
    t->services.owner = &t->owners;
    t->services.save = &t->saves;
    t->services.scenes = &t->scene_services;
}
static void runtime_lifecycle(void)
{
    RuntimeCase t;
    unsigned i;
    const char *failure_order[] = {
        "kD", "kuD", "kuwD", "kuwqDW", "kuwq+[ovrfinshg!fDRjAR-WQ]", "kuwq+[ovrfinshgGymsKfDRFjAR-WQ]"};
    for (i = PREPARE_FAILED; i <= CACHE_FAILED; ++i)
    {
        runtime_init(&t);
        t.fail = i;
        CHECK(!mm_movie_runtime_open(&t.runtime, &t.client, &t.bundle, 99, 1, &t.services));
        if (strcmp(t.archive.log, failure_order[i - 1]))
        {
            fprintf(stderr, "runtime failure %u: expected %s, saw %s\n", i, failure_order[i - 1], t.archive.log);
            exit(1);
        }
        CHECK(t.discarded == 1 && !t.busy && t.client.refs == 1 && !t.world.refs && !t.sounds.refs &&
              !t.archive.resources && !t.archive.buffers && !t.title_path);
    }
    runtime_init(&t);
    t.archive.fail = FAIL_OPEN;
    CHECK(!mm_movie_runtime_open(&t.runtime, &t.client, &t.working, 99, 1, &t.services));
    CHECK(!strcmp(t.archive.log, "kuwq+[oDj-WQ]") && !t.busy && t.discarded == 1);

    runtime_init(&t);
    CHECK(mm_movie_runtime_open(&t.runtime, &t.client, NULL, 99, 1, &t.services));
    CHECK(!strcmp(t.archive.log, "kuwq+") && !t.busy && !t.title_path && !t.movie.resources && t.movie.actors.stride);
    mm_movie_runtime_dispose(&t.runtime, &t.playback, &t.services);
    CHECK(!strcmp(t.archive.log, "kuwq+j-WQ"));

    for (i = 0; i < 3; ++i)
    {
        runtime_init(&t);
        CHECK(mm_movie_runtime_open(&t.runtime, &t.client, &t.bundle, 99, 1, &t.services));
        CHECK(t.movie.movieNumber == 7 && t.saved.containerNamed && t.saved.nameValid && !t.saved.readOnly &&
              t.container_name == &t.bundle && t.title_path == &t.bundle && !t.busy && !t.discarded);
        CHECK(!strcmp(t.archive.log, "kuwq+[ovrfinshgGymsKTyf]"));
        reset(&t.archive);
        mm_movie_runtime_set_ref(&t.runtime.thumbnail_palette, &t.palette, retain_runtime, release_runtime);
        mm_movie_runtime_set_ref(&t.runtime.thumbnail_palette, &t.palette, retain_runtime, release_runtime);
        t.scenes.open = &t;
        t.scenes.index = 0;
        t.runtime.user_paused = t.playback.playing = 1;
        t.playback.fadeOriginalVolume = 1000;
        t.disable_fades = i != 0;
        t.playback.fadeChangedVolume = i == 2;
        mm_movie_runtime_dispose(&t.runtime, &t.playback, &t.services);
        CHECK(!strcmp(t.archive.log, i == 1 ? "+aRFpjzAR-WQL" : "+aRFpVjzAR-WQL"));
        CHECK(!t.playback.fadeOriginalVolume && !t.scenes.open && t.scenes.index == -1 &&
              t.client.refs == 1 && !t.world.refs && !t.sounds.refs && t.palette.refs == 1 &&
              !t.archive.resources && !t.archive.buffers);
    }
}
int main(void)
{
    ownership();
    rollcalls();
    runtime_lifecycle();
    puts("movie ownership/roll-call passed");
    return 0;
}

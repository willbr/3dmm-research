#include "mm/movie_owner.h"
#include "mm/movie_rollcall.h"
#include "mm/movie_runtime.h"
#include "mm/movie_session.h"
#include "chunk_writer_fixture.h"
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
    int used, resources, fail, scenes, persistent;
    int open_calls;
    int open_error;
    MMU32 root_number;
    void *open_path;
    MMU32 rollcall_bytes;
    unsigned char written_rollcall[512];
    ChunkWriterFixture archive;
    MMArchiveWriter owner_writer;
    MMResourceCache owner_cache;
    MMTagServices tags;
    unsigned char owner_bytes[512];
    MMU32 owner_size;
    int buffers, packed, missing_extent;
};
enum
{
    FAIL_CREATE = 1,
    FAIL_OPEN,
    FAIL_VERIFY,
    FAIL_RESOURCE,
    FAIL_ROOT,
    FAIL_READ,
    FAIL_MISSING,
    FAIL_WRITE,
    FAIL_NAME,
    FAIL_SAVE,
    FAIL_WIRE,
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
    h->open_error = MM_MOVIE_OK;
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
    event(h, 's');
    access->context = h;
    access->count = (MMU32)h->scenes + 1;
    access->entry = graph_entry;
    access->child = graph_child;
}
static int graph_read(void *context, void *request, MMU32 position, const MMChunkEntry *entry, void *bytes, MMU32 size)
{
    Host *h = context;
    unsigned char header[] = {1, 0, 3, 3, 2, 0, 2, 0};
    (void)request;
    CHECK(!position && entry->type == 0x4d564945UL && size == sizeof(header));
    event(h, 'v');
    if (h->fail == FAIL_VERIFY)
        header[6] = MM_MOVIE_CURRENT_VERSION + 1;
    memcpy(bytes, header, size);
    return MM_MOVIE_OK;
}
static void session_graph(void *resources, MMMovieSessionGraph *out)
{
    MMMovieSessionGraph graph = {NULL, graph_access, graph_read, NULL, NULL};
    graph.context = ((Resource *)resources)->host;
    *out = graph;
}
static void session_failed(void *resources, int status)
{
    ((Resource *)resources)->host->open_error = status;
}
static void *new_resource(Host *h)
{
    Resource *r;
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
static void *create_resources(void *c)
{
    Host *h = c;
    event(h, 'c');
    return h->fail == FAIL_CREATE ? NULL : new_resource(h);
}
static void *open_resources(void *c, void *path)
{
    Host *h = c;
    CHECK(path && (!h->open_path || path == h->open_path));
    event(h, 'o');
    return h->fail == FAIL_OPEN ? NULL : new_resource(h);
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
static void retain_resource(void *p)
{
    Resource *r = p;
    event(r->host, 'h');
    ++r->host->open_calls;
    ++r->refs;
}
static void publish_tag(void *p, const MMTag *tag)
{
    if (!tag->cache)
        event(p, (char)('A' + tag->number));
}
static void init_tags(Host *h)
{
    h->tags.context = h;
    h->tags.retain = retain_resource;
    h->tags.release = release_resource;
    h->tags.publish = publish_tag;
}
static int owner_extent(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    Host *h = ((ChunkIndexFixture *)p)->observer;
    CHECK(bytes <= sizeof(h->owner_bytes) - h->owner_size);
    event(h, bytes == MM_MOVIE_PREFIX_BYTES ? 'b' : 'j');
    extent->file = &h->archive;
    extent->offset = h->owner_size;
    extent->bytes = bytes;
    extent->flags = 0;
    h->owner_size += bytes;
    return 1;
}
static int owner_write(void *p, void *f, MMU32 offset, const void *data, size_t bytes)
{
    ChunkWriterFixture *fixture = p;
    Host *h = fixture->context;
    CHECK(f == p && fixture->file_refs == 1 && offset <= h->owner_size && bytes <= h->owner_size - offset);
    if (h->fail == FAIL_ROOT || h->fail == FAIL_WRITE)
        return 0;
    memcpy(h->owner_bytes + offset, data, bytes);
    return 1;
}
static int owner_read(void *p, void *f, MMU32 offset, void *data, size_t bytes)
{
    ChunkWriterFixture *fixture = p;
    Host *h = fixture->context;
    CHECK(f == p && fixture->file_refs == 1 && offset <= h->owner_size && bytes <= h->owner_size - offset);
    if (h->fail == FAIL_READ)
        return 0;
    memcpy(data, h->owner_bytes + offset, bytes);
    return 1;
}
static MMChunkAccess rollcall_graph(void *p)
{
    return fixture_chunk_access(&((Host *)p)->archive.index);
}
static int owner_locate(void *p, MMU32 type, MMU32 number, MMBlockFile *extent, int *packed)
{
    Host *h = p;
    const MMChunkAccess access = rollcall_graph(p);
    MMChunkEntry entry;
    MMU32 row;
    CHECK(type == MM_ROLLCALL_CHUNK && mm_chunk_find(&access, type, number, &row) == MM_CHUNK_OK &&
          mm_chunk_at(&access, row, &entry) == MM_CHUNK_OK);
    event(h, 'i');
    if (h->fail == FAIL_MISSING)
        return 0;
    extent->file = &h->archive;
    extent->offset = entry.offset;
    extent->length = entry.bytes;
    *packed = 0;
    return 1;
}
static MMArchiveWriter owner_archive(void *p)
{
    return ((Resource *)p)->host->owner_writer;
}
static MMResourceCache *owner_cache(void *r)
{
    return &((Resource *)r)->host->owner_cache;
}
static void missing_rollcall(void *c)
{
    event(c, '?');
}
static void unlinked_rollcall(void *c, MMU32 parent, const MMChunkChild *child)
{
    Host *h = c;
    CHECK(parent == h->root_number && child->type == MM_ROLLCALL_CHUNK && child->slot == MM_ROLLCALL_SLOT);
    event(h, 'd');
}
static const unsigned char stored_name[] = {3, 3, 6, 'L', 'o', 'a', 'd', 'e', 'd', 0};
static int load_name(void *p, MMU32 type, MMU32 n, void *wire, MMU32 capacity, MMU32 *written)
{
    Host *h = ((Resource *)p)->host;
    CHECK(type == MM_MOVIE_CHUNK && n == 7 && capacity >= sizeof(stored_name));
    event(h, 'n');
    memcpy(wire, stored_name, sizeof(stored_name));
    *written = sizeof(stored_name);
    return 1;
}
static int set_name(void *p, MMU32 type, MMU32 n, const void *wire, MMU32 bytes)
{
    Host *h = ((Resource *)p)->host;
    CHECK(type == MM_MOVIE_CHUNK && n == 7 && bytes == sizeof(stored_name));
    CHECK(!memcmp(wire, stored_name, bytes));
    event(h, 'N');
    return h->fail != FAIL_NAME;
}
static int save(void *c, void *p)
{
    Host *h = c;
    CHECK(((Resource *)p)->host == h);
    event(h, 'S');
    return h->fail != FAIL_SAVE;
}
static void persist(void *p)
{
    Host *h = ((Resource *)p)->host;
    event(h, 'P');
    ++h->persistent;
}
static void *resize_rollcall(void *, void *, MMU32, MMU32, int);
static void free_rollcall(void *, void *, MMU32);
static int decode_rollcall(void *, void **);
static MMMovieOwnerServices owners(Host *h)
{
    MMMovieOwnerServices s = {
        NULL, {NULL, resize_rollcall, free_rollcall}, 0x0303, 0,
        create_resources, open_resources, {session_graph, session_failed}, release_resource,
        owner_archive, {owner_cache, decode_rollcall, {NULL, NULL, NULL}, missing_rollcall},
        NULL,
        {NULL, MM_TEXT_WINDOWS, MM_TEXT_UTF16_LE, NULL, load_name, set_name, NULL},
        save, persist};
    s.context = s.actor_allocator.context = s.rollcall.reader.context = h;
    init_tags(h);
    h->tags.cache = owner_cache;
    s.rollcall.reader.tags = &h->tags;
    h->owner_writer = chunk_writer_fixture(&h->archive, h, owner_extent, owner_write);
    h->owner_writer.system = MM_TEXT_WINDOWS;
    h->archive.block.files.read = owner_read;
    h->archive.index.before_unlink = unlinked_rollcall;
    h->owner_cache.file = h;
    h->owner_cache.services.handles = h->owner_writer.payloads->handles;
    h->owner_cache.services.block_files = h->owner_writer.payloads->files;
    h->owner_cache.services.live_chunks = rollcall_graph;
    h->owner_cache.services.locate = owner_locate;
    return s;
}
static void seed_owner_movie(Host *h)
{
    MMRollCallTable table;
    MMRollCallActor actor;
    const MMChunkChild source = {MM_ROLLCALL_CHUNK, 9, 1};
    MMU32 written;
    memset(&actor, 0, sizeof(actor));
    fixture_chunk_add(&h->archive.index, MM_MOVIE_CHUNK, h->root_number);
    fixture_chunk_add(&h->archive.index, source.type, source.number);
    CHECK(mm_chunk_adopt(&h->owner_writer.edit, MM_MOVIE_CHUNK, h->root_number, &source, 1) == MM_CHUNK_OK);
    CHECK(mm_rollcall_table_init(&table, sizeof(actor), 1, MM_TEXT_WINDOWS, 0, NULL) == MM_STORAGE_OK);
    CHECK(mm_rollcall_table_add(&table, "Actor", 5, &actor));
    CHECK(mm_movie_write_rollcall(&h->owner_writer, h->root_number, &table, &written));
    CHECK(mm_rollcall_table_dispose(&table) == MM_STORAGE_OK);
    reset(h);
}
static MMU32 owner_rollcall(Host *h)
{
    const MMChunkAccess chunks = rollcall_graph(h);
    MMU32 number, source;
    CHECK(mm_chunk_link_number(&chunks, MM_MOVIE_CHUNK, h->root_number, MM_ROLLCALL_SLOT, MM_ROLLCALL_CHUNK,
                               &number) == MM_CHUNK_OK);
    CHECK(mm_chunk_link_number(&chunks, MM_MOVIE_CHUNK, h->root_number, 1, MM_ROLLCALL_CHUNK,
                               &source) == MM_CHUNK_OK && source == 9);
    CHECK(!h->archive.file_refs);
    return number;
}
static MMU32 replaced_rollcall(Host *h, MMU32 previous)
{
    const MMChunkAccess chunks = rollcall_graph(h);
    const MMU32 next = owner_rollcall(h);
    MMU32 row;
    CHECK(next != previous && mm_chunk_find(&chunks, MM_ROLLCALL_CHUNK, previous, &row) == MM_CHUNK_NOT_FOUND);
    return next;
}
static void ownership(void)
{
    Host h;
    MMMovieState m;
    MMMovieOwnerServices s;
    void *old_r, *old_entries;
    MMRollCallActor actor;
    MMMovieSession *original;
    MMMemoryStream root;
    MMMovieHeader header;
    MMU32 saved, number = 99;
    int i;
    const int load_failures[] = {FAIL_OPEN, FAIL_VERIFY, FAIL_RESOURCE, FAIL_READ, FAIL_MISSING, FAIL_WIRE};
    memset(&h, 0, sizeof(h));
    h.root_number = 7;
    s = owners(&h);
    mm_movie_state_init(&m);
    CHECK(m.movieNumber == 0xffffffffUL && !m.resources && !m.actors.stride);
    CHECK(!mm_movie_save(&m, &s) && !h.used);
    CHECK(mm_movie_init_empty(&m, &s) && !strcmp(h.log, "crb") && m.actors.stride && !h.buffers);
    CHECK(m.movieNumber == 0 && h.archive.index.count == 1 && !h.archive.file_refs &&
          h.owner_size == MM_MOVIE_PREFIX_BYTES && mm_memory_stream_init(&root, h.owner_bytes, h.owner_size));
    CHECK(mm_movie_read_header(&root.stream, &header) == MM_MOVIE_OK &&
          header.current_version == MM_MOVIE_CURRENT_VERSION && header.system == MM_TEXT_WINDOWS);
    CHECK(mm_movie_title_set(&m.title, "Old", 3));
    old_r = m.resources;
    memset(&actor, 0, sizeof(actor));
    actor.data.actor_id = 2;
    actor.data.template_number = 2;
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
            CHECK(!strcmp(h.log, "cr"));
        if (i == FAIL_ROOT)
            CHECK(!strcmp(h.log, "crbR") && m.movieNumber == 0 && h.archive.index.count == 1 &&
                  !h.archive.file_refs);
    }
    h.fail = 0;
    seed_owner_movie(&h);
    CHECK(!mm_movie_load(&m, &h, 99, &s) && !strcmp(h.log, "orsR") &&
          h.open_error == MM_MOVIE_ROOT_NOT_FOUND && m.resources == old_r && m.movieNumber == 0);
    for (i = 0; i < (int)(sizeof(load_failures) / sizeof(*load_failures)); ++i)
    {
        reset(&h);
        h.fail = load_failures[i];
        CHECK(!mm_movie_load(&m, &h, h.root_number, &s));
        CHECK(m.resources == old_r && m.actors.storage.buffers[1].data == old_entries && m.actors.count == 1 &&
              h.resources == 1 && h.buffers == 2 && ((Resource *)old_r)->refs == 2);
        CHECK(m.title.count == 3 && !memcmp(mm_movie_title_text(&m.title), "Old", 3));
        if (h.fail == FAIL_VERIFY)
            CHECK(!strcmp(h.log, "orsvR") && h.open_error == MM_MOVIE_BAD_VERSION);
        if (h.fail == FAIL_MISSING)
            CHECK(!strcmp(h.log, "orsvi?R"));
        if (h.fail == FAIL_READ)
            CHECK(!strcmp(h.log, "orsviR"));
        CHECK(!h.archive.file_refs);
    }
    reset(&h);
    h.fail = 0;
    h.scenes = 2;
    CHECK(mm_movie_load(&m, &h, 0xffffffffUL, &s) && m.movieNumber == 7 && m.actorIdLimit == 1 && m.sceneCount == 2);
    CHECK(!strcmp(h.log, "orsvihnsssRCR") && h.resources == 1 && h.buffers == 2);
    saved = owner_rollcall(&h);
    reset(&h);
    CHECK(mm_movie_save(&m, &s) && !strcmp(h.log, "jdNSP") && h.persistent == 1);
    saved = replaced_rollcall(&h, saved);
    reset(&h);
    h.fail = FAIL_WRITE;
    CHECK(!mm_movie_save(&m, &s) && !strcmp(h.log, "j") && owner_rollcall(&h) == saved &&
          h.archive.index.count == 4);
    reset(&h);
    h.fail = FAIL_NAME;
    CHECK(!mm_movie_save(&m, &s) && !strcmp(h.log, "jdN"));
    saved = replaced_rollcall(&h, saved);
    reset(&h);
    h.fail = FAIL_SAVE;
    CHECK(!mm_movie_save(&m, &s) && !strcmp(h.log, "jdNS") && h.persistent == 1);
    replaced_rollcall(&h, saved);
    h.fail = 0;
    original = m.session;
    h.scenes = 3;
    CHECK(mm_movie_state_scene_number(&m, 2, &number, &s.session) &&
          number == 3 && m.session == original);
    m.movieNumber = 12;
    CHECK(!mm_movie_state_scene_number(&m, 0, &number, &s.session) && h.open_error == MM_MOVIE_ROOT_NOT_FOUND &&
          number == 3 && m.movieNumber == 12 && m.session == original);
    h.root_number = m.movieNumber = 11;
    h.fail = FAIL_VERIFY;
    CHECK(!mm_movie_state_scene_number(&m, 0, &number, &s.session) && h.open_error == MM_MOVIE_BAD_VERSION &&
          number == 3 && m.movieNumber == 11 && m.session == original);
    h.fail = 0;
    CHECK(mm_movie_state_scene_number(&m, 0, &number, &s.session) && number == 1 && m.session &&
          mm_movie_session_root(m.session)->number == 11);
    reset(&h);
    mm_movie_state_dispose(&m, &s);
    CHECK(!strcmp(h.log, "RAR") && !h.buffers && !h.resources && !m.session && !m.actors.stride);
    reset(&h);
    mm_movie_state_dispose(&m, &s);
    CHECK(!h.used);
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
static int rollcall_extent(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    Host *h = ((ChunkIndexFixture *)p)->observer;
    CHECK(bytes > 20 && bytes <= sizeof(h->written_rollcall));
    event(h, '+');
    if (h->fail == FAIL_ADD)
        return 0;
    h->rollcall_bytes = bytes;
    extent->file = &h->archive;
    extent->offset = 0;
    extent->bytes = bytes;
    extent->flags = 0;
    return 1;
}
static int rollcall_write_file(void *p, void *file, MMU32 offset, const void *data, size_t bytes)
{
    ChunkWriterFixture *fixture = p;
    CHECK(file == p && fixture->file_refs == 1);
    return write_rollcall(fixture->context, offset, data, bytes);
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
static int adopt_rollcall(void *c, MMU32 movie, const MMChunkChild *child)
{
    Host *h = (Host *)c;
    CHECK(movie == 7 && child->type == MM_ROLLCALL_CHUNK && child->slot == MM_ROLLCALL_SLOT && !child->number);
    event(h, '>');
    return h->fail != FAIL_ADOPT;
}
static void delete_rollcall(void *c, const MMChunkEntry *entry)
{
    Host *h = (Host *)c;
    if (!entry->type)
        return;
    CHECK(entry->type == MM_ROLLCALL_CHUNK && !entry->number);
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
    CHECK(mm_rollcall_table_init(&wire, extra, 1, 0x0303, 0, NULL) == MM_STORAGE_OK);
    for (i = 0; i < 3; ++i)
    {
        actor.actor_id = i == bad_row ? -1 : i;
        actor.source_id = i == 2 ? 3 : 0;
        actor.template_number = (MMU32)i;
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

static int locate_rollcall(void *p, MMU32 type, MMU32 number, MMBlockFile *extent, int *packed)
{
    Host *h = p;
    CHECK(type == MM_ROLLCALL_CHUNK && number == 4);
    if (h->missing_extent)
        return 0;
    extent->file = &h->archive;
    extent->offset = 0;
    extent->length = h->rollcall_bytes;
    *packed = h->packed;
    return 1;
}

static int rollcall_read_file(void *p, void *file, MMU32 offset, void *data, size_t bytes)
{
    ChunkWriterFixture *fixture = p;
    Host *h = fixture->context;
    CHECK(file == p && fixture->file_refs == 1 && offset <= h->rollcall_bytes && bytes <= h->rollcall_bytes - offset);
    memcpy(data, h->written_rollcall + offset, bytes);
    return 1;
}

static int decode_rollcall(void *p, void **handle)
{
    CHECK(!p && *handle && resource_handle_size(NULL, *handle) > 20);
    return 1;
}

static int reject_rollcall_decode(void *p, void **handle)
{
    (void)p; (void)handle;
    return 0;
}

static void load_rollcall(Host *h, Resource *r, MMRollCallTable *actors, MMI32 *limit, const MMRollCallReader *reader)
{
    MMResourceCache cache = {0};
    const MMArchiveWriter archive = chunk_writer_fixture(&h->archive, h, rollcall_extent, rollcall_write_file);
    const MMChunkChild selected = {MM_ROLLCALL_CHUNK, 4, MM_ROLLCALL_SLOT};
    const MMChunkChild source = {MM_ROLLCALL_CHUNK, 3, 1};
    h->archive.block.files.read = rollcall_read_file;
    cache.file = h;
    cache.services.handles = archive.payloads->handles;
    cache.services.block_files = archive.payloads->files;
    cache.services.live_chunks = rollcall_graph;
    cache.services.locate = locate_rollcall;
    fixture_chunk_add(&h->archive.index, MM_MOVIE_CHUNK, 7);
    fixture_chunk_add(&h->archive.index, source.type, source.number);
    fixture_chunk_add(&h->archive.index, selected.type, selected.number);
    CHECK(mm_chunk_adopt(&archive.edit, MM_MOVIE_CHUNK, 7, &source, 1) == MM_CHUNK_OK);
    CHECK(mm_movie_load_rollcall(&cache, 7, r, actors, limit, decode_rollcall, reader) == MM_CHUNK_NOT_FOUND);
    CHECK(mm_chunk_adopt(&archive.edit, MM_MOVIE_CHUNK, 7, &selected, 1) == MM_CHUNK_OK);
    h->missing_extent = 1;
    CHECK(mm_movie_load_rollcall(&cache, 7, r, actors, limit, decode_rollcall, reader) == MM_CHUNK_NOT_FOUND);
    h->missing_extent = 0;
    h->packed = 1;
    CHECK(mm_movie_load_rollcall(&cache, 7, r, actors, limit, reject_rollcall_decode, reader) == MM_CHUNK_BAD_FILE);
    CHECK(!actors->count && !*limit && r->refs == 1 && !h->archive.file_refs);
    CHECK(mm_movie_load_rollcall(&cache, 7, r, actors, limit, decode_rollcall, reader) == MM_CHUNK_OK &&
          !h->archive.file_refs);
}
static void write_rollcalls(Host *h, MMRollCallTable *source, int big)
{
    static const char *names[] = {"A", "B", "Third"};
    static const int failures[] = {FAIL_WIRE, FAIL_ADD, FAIL_TABLE_WRITE, FAIL_ADOPT};
    static const char *logs[] = {"", "+", "+=-", "+=>-"};
    MMRollCallTable input;
    MMRollCallActor actor;
    MMStorageAllocator allocator = {NULL, resize_rollcall, free_rollcall};
    MMArchiveWriter writer;
    MMChunkAccess access;
    MMChunkChild previous = {MM_ROLLCALL_CHUNK, 4, MM_ROLLCALL_SLOT};
    MMU32 i, written = 99;
    allocator.context = h;
    CHECK(mm_rollcall_table_init(&input, sizeof(MMRollCallActor), 1, 0x0303, 0, &allocator) == MM_STORAGE_OK);
    for (i = 0; i < source->count; ++i)
    {
        CHECK(mm_rollcall_table_extra(source, i, &actor));
        CHECK(mm_rollcall_table_add(&input, names[i], (MMU32)strlen(names[i]), &actor));
    }
    CHECK(mm_rollcall_table_rename(&input, 0, "Long first", 10));
    for (i = 0; i <= sizeof(failures) / sizeof(failures[0]); ++i)
    {
        writer = chunk_writer_fixture(&h->archive, h, rollcall_extent, rollcall_write_file);
        writer.big_endian = big;
        fixture_chunk_add(&h->archive.index, MM_MOVIE_CHUNK, 7);
        fixture_chunk_add(&h->archive.index, previous.type, previous.number);
        CHECK(mm_chunk_adopt(&writer.edit, MM_MOVIE_CHUNK, 7, &previous, 1) == MM_CHUNK_OK);
        h->archive.index.before_insert = adopt_rollcall;
        h->archive.index.before_delete = delete_rollcall;
        h->fail = i ? failures[i - 1] : 0;
        reset(h);
        written = 99;
        CHECK(mm_movie_write_rollcall(&writer, 7, &input, &written) == (i == 0));
        CHECK(!strcmp(h->log, i ? logs[i - 1] : "+=>") && h->buffers == 4 && !h->archive.file_refs);
        CHECK(written == (i == 1 ? 99U : 0U));
        access = fixture_chunk_access(&h->archive.index);
        CHECK(access.count == (i ? 2U : 3U));
        CHECK(mm_chunk_link_number(&access, MM_MOVIE_CHUNK, 7, MM_ROLLCALL_SLOT, MM_ROLLCALL_CHUNK,
                                   &written) == MM_CHUNK_OK && written == (i ? previous.number : 0));
        if (!i)
            check_wire(h);
    }
    h->fail = 0;
    CHECK(mm_rollcall_table_dispose(&input) == MM_STORAGE_OK && h->buffers == 2);
}
static void rollcalls(void)
{
    Host h;
    Resource *r;
    MMRollCallReader reader = {NULL, NULL, NULL};
    MMStorageAllocator allocator = {NULL, resize_rollcall, free_rollcall};
    MMRollCallTable actors;
    MMRollCallActor actor;
    MMTag tag;
    MMMemoryStream memory;
    const void *name;
    MMU32 length, row;
    MMI32 limit;
    int big, failures[] = {FAIL_WIRE, FAIL_APPEND}, i;
    memset(&h, 0, sizeof(h));
    reader.context = allocator.context = &h;
    init_tags(&h);
    reader.tags = &h.tags;
    r = (Resource *)new_resource(&h);
    CHECK(mm_rollcall_table_init(&actors, sizeof(MMRollCallActor), 1, 0x0303, 0, &allocator) == MM_STORAGE_OK);
    for (big = 0; big < 2; ++big)
    {
        make_wire_rollcall(&h, &memory, big, -2, MM_MOVIE_ACTOR_BYTES);
        reset(&h);
        limit = 0;
        load_rollcall(&h, r, &actors, &limit, &reader);
        CHECK(limit == 3 && actors.count == 3 && r->refs == 3 && h.buffers == 2);
        CHECK(mm_rollcall_table_name(&actors, 1, &name, &length) && length == 160 && ((const char *)name)[159] == 'B');
        write_rollcalls(&h, &actors, big);
        CHECK(r->refs == 3);
        for (row = 0; row < actors.count; ++row)
        {
            CHECK(mm_rollcall_table_extra(&actors, row, &actor) && actor.cache == (row == 2 ? NULL : r) &&
                  actor.data.actor_id == (MMI32)row && actor.data.source_id == (row == 2 ? 3 : 0) &&
                  actor.data.template_number == row);
            tag = mm_movie_rollcall_tag(&actor);
            mm_tag_close(&tag, &h.tags);
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
                CHECK(!strcmp(h.log, "hhRBRA"));
        }
        h.fail = 0;
        memory.stream.length = 19;
        reset(&h);
        CHECK(!mm_movie_read_rollcall(&memory.stream, r, &actors, &limit, &reader) && !h.used && !h.buffers);
        make_wire_rollcall(&h, &memory, big, 1, MM_MOVIE_ACTOR_BYTES);
        reset(&h);
        CHECK(!mm_movie_read_rollcall(&memory.stream, r, &actors, &limit, &reader) && !actors.count &&
              limit == 42 && r->refs == 1 && !h.buffers && !strcmp(h.log, "hRA"));
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
static void missing_saved(void *p) { event(&((RuntimeCase *)p)->archive, '!'); }
static void merge_sources(void *p, MMU32 root)
{
    CHECK(root == 7);
    event(((Resource *)p)->host, 'm');
}
static MMMovieSessionGraph runtime_graph(void *p)
{
    MMMovieSessionGraph graph = {NULL, graph_access, graph_read, NULL, NULL};
    graph.context = ((Resource *)p)->host;
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
    CHECK(t->runtime.transport.paused && t->movie.resources && t->saved.file);
    event(&t->archive, 'a');
}
static void playback_stopped(void *p)
{
    RuntimeCase *t = ((RuntimeRef *)p)->test;
    CHECK(!t->runtime.transport.paused && !t->movie.resources && !t->movie.session && !t->saved.file);
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
        retain_runtime, release_runtime, discard_document, missing_saved, merge_sources,
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
    seed_owner_movie(&t->archive);
    t->saves.context = t;
    t->saves.state = &t->saved;
    t->saves.data = &t->movie;
    t->saves.tags = &t->archive.tags;
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
        "kD", "kuD", "kuwD", "kuwqDW", "kuwq+[orsvihnsg!DRjRA-WQ]", "kuwq+[orsvihnsgGymsKDRFjRA-WQ]"};
    for (i = PREPARE_FAILED; i <= CACHE_FAILED; ++i)
    {
        runtime_init(&t);
        t.fail = i;
        CHECK(!mm_movie_runtime_open(&t.runtime, &t.client, &t.bundle, 7, 1, &t.services));
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
        CHECK(mm_movie_runtime_open(&t.runtime, &t.client, &t.bundle, 7, 1, &t.services));
        CHECK(t.movie.movieNumber == 7 && t.saved.containerNamed && t.saved.nameValid && !t.saved.readOnly &&
              t.container_name == &t.bundle && t.title_path == &t.bundle && !t.busy && !t.discarded);
        CHECK(!strcmp(t.archive.log, "kuwq+[orsvihnsgGymsKTy]"));
        reset(&t.archive);
        mm_movie_runtime_set_ref(&t.runtime.thumbnail_palette, &t.palette, retain_runtime, release_runtime);
        mm_movie_runtime_set_ref(&t.runtime.thumbnail_palette, &t.palette, retain_runtime, release_runtime);
        t.scenes.open = &t;
        t.scenes.index = 0;
        t.runtime.transport.paused = t.playback.playing = 1;
        t.playback.fadeOriginalVolume = 1000;
        t.disable_fades = i != 0;
        t.playback.fadeChangedVolume = i == 2;
        mm_movie_runtime_dispose(&t.runtime, &t.playback, &t.services);
        CHECK(!strcmp(t.archive.log, i == 1 ? "+aRFpjzRA-WQL" : "+aRFpVjzRA-WQL"));
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

#include "mm/chunk_owner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "chunk owner failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Token
{
    int refs, error;
    char name;
} Token;
typedef struct Host
{
    MMChunkOwner owner;
    Token main, extra, index, replacement;
    MMU32 flags;
    int fail, reads, validates, publishes, sets;
    char log[128];
    int used;
} Host;
static char log_buffer[64];
static int log_used;
static void log_error(char c)
{
    CHECK(log_used < 63);
    log_buffer[log_used++] = c;
    log_buffer[log_used] = 0;
}
static MMI32 get_error(void *p)
{
    Token *t = (Token *)p;
    log_error(t->name);
    return t->error;
}
static void set_error(void *p, MMI32 level)
{
    Token *t = (Token *)p;
    log_error((char)(t->name + ('a' - 'A')));
    t->error = level;
}
static void event(Host *h, char c)
{
    CHECK(h->used < 127);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static void retain_file(void *c, void *p)
{
    Host *h = (Host *)c;
    CHECK(p == &h->main && h->main.refs == 1);
    ++h->main.refs;
    event(h, '+');
}
static void release_file(void *c, void *p)
{
    Host *h = (Host *)c;
    Token *t = (Token *)p;
    CHECK(t->refs > 0);
    --t->refs;
    event(h, t->name);
}
static void release_index(void *c, void *p)
{
    Host *h = (Host *)c;
    Token *t = (Token *)p;
    CHECK(t->refs == 1);
    --t->refs;
    event(h, t->name);
}
static void publish(void *c, const MMChunkReopenState *s)
{
    Host *h = (Host *)c;
    CHECK(s == &h->owner.files);
    ++h->publishes;
    event(h, 'P');
}
static int writable(void *c, void *p)
{
    Host *h = (Host *)c;
    CHECK(p == &h->main);
    event(h, 'W');
    return h->fail == 3;
}
static int read_index(void *c, MMChunkReopenState *s)
{
    Host *h = (Host *)c;
    CHECK(s == &h->owner.files && s->main_file == &h->main && h->main.refs == 2);
    CHECK(!s->index && !s->main_map && !s->extra_file && !s->main_end && !h->owner.runtime.count);
    ++h->reads;
    event(h, 'R');
    s->index = &h->replacement;
    h->replacement.refs = 1;
    s->extra_file = &h->extra;
    h->extra.refs = 1;
    s->main_end = 333;
    s->extra_end = 44;
    CHECK(mm_free_map_create(NULL, 1, &s->main_map) == MM_CHUNK_OK);
    CHECK(mm_free_map_create(NULL, 1, &s->extra_map) == MM_CHUNK_OK);
    if (!h->owner.expanded_runtime)
        CHECK(mm_chunk_runtime_set(&h->owner.runtime, 1, 1, 22) == MM_CHUNK_OK);
    h->owner.map_offset = 888;
    h->owner.map_bytes = 16;
    return h->fail != 1;
}
static int validate(void *c)
{
    Host *h = (Host *)c;
    CHECK(h->owner.files.index == &h->replacement && h->owner.files.main_end == 333);
    ++h->validates;
    event(h, 'V');
    return h->fail != 2;
}
static MMU32 file_flags(void *c)
{
    return ((Host *)c)->flags;
}
static int set_flags(void *c, MMU32 flags, MMU32 mask)
{
    Host *h = (Host *)c;
    CHECK(!h->owner.marked && !h->owner.add_to_extra && !h->owner.read_from_extra);
    ++h->sets;
    event(h, 'S');
    if (h->fail)
        return 0;
    h->flags = (h->flags & ~mask) | (flags & mask);
    return 1;
}
static void read_map(void *c)
{
    Host *h = (Host *)c;
    CHECK(h->owner.files.free_map_unread && !h->owner.marked && !h->owner.add_to_extra && !h->owner.read_from_extra);
    h->owner.files.free_map_unread = 0;
    event(h, 'L');
}
static void init(Host *h, int expanded)
{
    memset(h, 0, sizeof(*h));
    h->main.name = 'M';
    h->extra.name = 'E';
    h->index.name = 'I';
    h->replacement.name = 'J';
    CHECK(mm_chunk_owner_init(&h->owner, NULL, expanded) == MM_CHUNK_OK);
}
static void dispose(Host *h)
{
    MMChunkDisposal s;
    s.context = h;
    s.release_file = release_file;
    s.release_index = release_index;
    s.publish = publish;
    CHECK(mm_chunk_owner_dispose(&h->owner, &s) == MM_CHUNK_OK);
    CHECK(!h->main.refs && !h->extra.refs && !h->index.refs && !h->replacement.refs);
    CHECK(!h->owner.files.main_file && !h->owner.files.extra_file && !h->owner.files.index && !h->owner.runtime.count);
    CHECK(mm_chunk_owner_dispose(&h->owner, &s) == MM_CHUNK_OK);
}
static void reopen_cases(void)
{
    Host h;
    MMChunkReopenServices s;
    MMChunkFreeMap *old_map;
    int expanded, fail, result;
    for (expanded = 0; expanded < 2; ++expanded)
    {
        for (fail = 0; fail < 5; ++fail)
        {
            init(&h, expanded);
            h.fail = fail;
            h.owner.files.main_file = &h.main;
            h.main.refs = 1;
            h.owner.files.index = &h.index;
            h.index.refs = 1;
            h.owner.files.main_end = 200;
            h.owner.files.free_map_unread = 1;
            h.owner.files.invalid_main = fail == 4;
            h.owner.marked = h.owner.add_to_extra = h.owner.read_from_extra = 1;
            h.owner.map_offset = 111;
            h.owner.map_bytes = 8;
            CHECK(mm_free_map_create(NULL, 1, &h.owner.files.main_map) == MM_CHUNK_OK);
            old_map = h.owner.files.main_map;
            if (!expanded)
                CHECK(mm_chunk_runtime_set(&h.owner.runtime, 1, 1, 11) == MM_CHUNK_OK);
            s.context = &h;
            s.writable = writable;
            s.retain_file = retain_file;
            s.release_file = release_file;
            s.release_index = release_index;
            s.read_index = read_index;
            s.validate = validate;
            s.publish = publish;
            result = mm_chunk_owner_reopen(&h.owner, &s);
            CHECK(result == (!fail ? MM_CHUNK_OK : fail >= 3 ? MM_CHUNK_BAD_FILE : MM_CHUNK_IO));
            CHECK(h.owner.marked && h.owner.add_to_extra && h.owner.read_from_extra && h.main.refs == 1);
            if (fail)
            {
                CHECK(h.owner.files.index == &h.index && h.owner.files.main_map == old_map &&
                      h.owner.files.main_end == 200);
                CHECK(h.owner.files.free_map_unread && !h.replacement.refs && !h.extra.refs);
                if (!expanded)
                    CHECK(mm_chunk_runtime_get(&h.owner.runtime, 1, 1) == 11);
            }
            else
            {
                CHECK(h.owner.files.index == &h.replacement && h.owner.files.main_map != old_map && !h.index.refs);
                CHECK(h.owner.files.main_end == 333 && h.owner.files.extra_end == 44);
                if (!expanded)
                    CHECK(mm_chunk_runtime_get(&h.owner.runtime, 1, 1) == 22);
            }
            CHECK(h.owner.map_offset == (fail >= 3 ? 111UL : 888UL));
            CHECK(h.owner.map_bytes == (fail >= 3 ? 8UL : 16UL));
            CHECK(h.reads == (fail < 3) && h.validates == (fail < 3 && fail != 1));
            if (fail >= 3)
                CHECK(!h.publishes);
            else
                CHECK(h.publishes == 2 && !strncmp(h.log, "W+PR", 4));
            dispose(&h);
        }
    }
}
static void policy_cases(void)
{
    Host h;
    MMChunkOptionServices s;
    MMChunkSaveState saved;
    MMChunkStoragePolicy storage;
    MMChunkFreeMap *map;
    int fail;
    for (fail = 0; fail < 2; ++fail)
    {
        init(&h, 0);
        h.fail = fail;
        h.owner.files.main_file = &h.main;
        h.main.refs = 1;
        h.owner.files.index = &h.index;
        h.index.refs = 1;
        h.owner.files.free_map_unread = 1;
        s.context = &h;
        s.file_flags = file_flags;
        s.set_file_flags = set_flags;
        s.read_map = read_map;
        CHECK(mm_chunk_owner_options(&h.owner, &s, 1 | 4 | 8 | 16, 31) ==
              (fail ? MM_CHUNK_IO : MM_CHUNK_OK));
        CHECK(h.owner.marked == !fail && h.owner.add_to_extra == !fail && h.owner.read_from_extra == !fail);
        CHECK(h.owner.files.free_map_unread == fail && !strcmp(h.log, fail ? "S" : "SL"));
        dispose(&h);
    }
    init(&h, 0);
    h.owner.marked = h.owner.read_from_extra = 1;
    h.owner.map_offset = 100;
    h.owner.map_bytes = 32;
    h.owner.files.main_file = &h.main;
    h.main.refs = 1;
    h.owner.files.index = &h.index;
    h.index.refs = 1;
    CHECK(mm_free_map_create(NULL, 1, &map) == MM_CHUNK_OK);
    CHECK(mm_chunk_runtime_set(&h.owner.runtime, 2, 3, 99) == MM_CHUNK_OK);
    saved = mm_chunk_owner_save_state(&h.owner);
    CHECK(saved.main_file == &h.main && !saved.extra_file && !saved.main_map && !saved.add_to_extra);
    saved.main_map = map;
    saved.main_end = 400;
    saved.invalid_main = saved.add_to_extra = saved.free_map_unread = 7;
    mm_chunk_owner_publish_save(&h.owner, &saved);
    CHECK(h.owner.files.main_map == map && h.owner.files.main_end == 400 && h.owner.files.index == &h.index);
    CHECK(h.owner.files.invalid_main == 1 && h.owner.files.free_map_unread == 1 && h.owner.add_to_extra == 1);
    CHECK(h.owner.marked && h.owner.read_from_extra && h.owner.map_offset == 100 && h.owner.map_bytes == 32);
    CHECK(mm_chunk_runtime_get(&h.owner.runtime, 2, 3) == 99);
    storage = mm_chunk_owner_storage_policy(&h.owner, 1234);
    CHECK(storage.max_bytes == 1234 && storage.add_to_extra && storage.read_from_extra && storage.free_map_unread);
    h.owner.runtime.storage.buffers[0].locks = 1;
    {
        MMChunkDisposal d;
        d.context = &h;
        d.release_file = release_file;
        d.release_index = release_index;
        d.publish = publish;
        CHECK(mm_chunk_owner_dispose(&h.owner, &d) == MM_CHUNK_BAD_FILE && !h.publishes &&
              h.main.refs == 1);
    }
    h.owner.runtime.storage.buffers[0].locks = 0;
    dispose(&h);
}
static void error_cases(void)
{
    Host h;
    MMChunkErrorServices s;
    int a, b, level;
    s.get = get_error;
    s.set = set_error;
    init(&h, 0);
    h.owner.files.main_file = &h.main;
    h.main.refs = 1;
    h.owner.files.extra_file = &h.extra;
    h.extra.refs = 1;
    for (a = 0; a < 10; ++a)
        for (b = 0; b < 10; ++b)
            for (level = 0; level < 10; ++level)
            {
                h.main.error = a;
                h.extra.error = b;
                log_used = 0;
                CHECK(mm_chunk_owner_error(&h.owner, &s) == (a > b ? a : b) && !strcmp(log_buffer, "ME"));
                log_used = 0;
                mm_chunk_owner_reset_error(&h.owner, &s, level);
                CHECK(h.main.error == (a > level ? level : a) && h.extra.error == (b > level ? level : b));
                CHECK(!strcmp(log_buffer, a > level ? (b > level ? "MmEe" : "MmE") : b > level ? "MEe" : "ME"));
            }
    h.owner.files.extra_file = NULL;
    h.extra.refs = 0;
    h.main.error = 4;
    log_used = 0;
    CHECK(mm_chunk_owner_error(&h.owner, &s) == 4 && !strcmp(log_buffer, "M"));
    log_used = 0;
    mm_chunk_owner_reset_error(&h.owner, &s, 3);
    CHECK(h.main.error == 3 && !strcmp(log_buffer, "Mm"));
    dispose(&h);
}

typedef struct TestFile
{
    unsigned char bytes[256];
    MMU32 length;
    int resize_fails, stream_calls, resize_calls, releases;
} TestFile;

typedef struct StorageHost
{
    TestFile main, extra;
    int create_fails, copy_fails, last_issue, creates, copies;
    int trace[3];
} StorageHost;

static int read_file(void *p, MMU32 offset, void *buffer, size_t count)
{
    TestFile *file = p;
    CHECK(offset <= file->length && count <= file->length - offset);
    memcpy(buffer, file->bytes + offset, count);
    return 1;
}

static MMReadStream file_stream(void *p)
{
    TestFile *file = p;
    MMReadStream stream = {NULL, 0, read_file};
    stream.context = file;
    stream.length = file->length;
    ++file->stream_calls;
    return stream;
}

static MMU32 file_size(void *p)
{
    return ((TestFile *)p)->length;
}

static int resize_file(void *p, MMU32 bytes)
{
    TestFile *file = p;
    ++file->resize_calls;
    CHECK(bytes <= sizeof(file->bytes));
    if (file->resize_fails)
        return 0;
    file->length = bytes;
    return 1;
}

static void *create_extra(void *p)
{
    StorageHost *host = p;
    ++host->creates;
    return host->create_fails ? NULL : &host->extra;
}

static int copy_extent(void *p, const MMChunkLocation *from, const MMChunkLocation *to)
{
    StorageHost *host = p;
    CHECK(from->file == &host->main && to->file == &host->extra && from->bytes == to->bytes);
    CHECK(to->offset + to->bytes <= host->extra.length);
    ++host->copies;
    return !host->copy_fails && read_file(from->file, from->offset, host->extra.bytes + to->offset, from->bytes);
}

static void storage_trace(void *p, int kind, const MMChunkEntry *entry)
{
    StorageHost *host = p;
    CHECK(kind >= MM_STORAGE_CACHE && kind <= MM_STORAGE_FETCH_DIRECT && entry->type == 1);
    ++host->trace[kind];
}

static void storage_issue(void *p, int kind)
{
    ((StorageHost *)p)->last_issue = kind;
}

static void close_file(void *p, void *file)
{
    (void)p;
    ++((TestFile *)file)->releases;
}

static void close_table(void *p, void *table)
{
    (void)p;
    CHECK(mm_chunk_table_close(table) == MM_STORAGE_OK);
}

static void publish_storage_disposal(void *p, const MMChunkReopenState *state)
{
    const StorageHost *host = p;
    CHECK(!state->index && !state->main_file && !state->extra_file && !state->main_map && !state->extra_map);
    CHECK(!host->main.releases && !host->extra.releases);
}

static void storage_cases(int expanded)
{
    static const unsigned char wire_map[] = {1, 0, 3, 3, 8, 0, 0, 0, 1, 0, 0, 0, 140, 0, 0, 0, 8, 0, 0, 0};
    MMChunkOwnerFileIO io = {NULL, create_extra, file_stream, file_size, resize_file, copy_extent,
                             storage_trace, storage_issue};
    MMChunkDisposal disposal = {NULL, close_file, close_table, publish_storage_disposal};
    StorageHost host;
    MMChunkOwner owner;
    MMChunkOwnerStorage storage;
    MMChunkTable *table;
    MMChunkEdit edit;
    MMChunkEntry entry;
    MMChunkLocation location;
    MMChunkFreeRange range;

    memset(&host, 0, sizeof(host));
    host.main.length = 220;
    memcpy(host.main.bytes + 200, wire_map, sizeof(wire_map));
    CHECK(mm_chunk_owner_init(&owner, NULL, expanded) == MM_CHUNK_OK);
    CHECK(mm_chunk_table_create(NULL, expanded ? sizeof(MMChunkRecordBig) : sizeof(MMChunkRecordSmall),
                                0, 0, &table) == MM_STORAGE_OK);
    owner.files.index = table;
    owner.files.main_file = &host.main;
    owner.files.main_end = 200;
    owner.files.free_map_unread = 1;
    owner.map_offset = 200;
    owner.map_bytes = sizeof(wire_map);
    io.context = &host;
    mm_chunk_owner_bind_storage(&storage, &owner, NULL, 16, &io);
    edit = mm_chunk_owner_editor(&storage, 65535, 1);

    CHECK(mm_chunk_add(&edit, 1, 0, 0, 0, &location) == MM_CHUNK_OK);
    CHECK(location.file == &host.main && !location.bytes && !location.flags && !location.offset);
    CHECK(mm_chunk_owner_cache(&storage, 0, &location) == MM_CHUNK_OK);
    CHECK(!location.file && !location.bytes && location.flags == 1 && !host.creates);
    CHECK(mm_chunk_add(&edit, 1, 1, 1, 4, NULL) == MM_CHUNK_IO && table->count == 1);
    CHECK(host.last_issue == MM_CHUNK_OWNER_MAP_UNREAD && !host.main.stream_calls);
    CHECK(mm_chunk_owner_read_free_map(&storage) == MM_CHUNK_OK);
    CHECK(mm_chunk_owner_read_free_map(&storage) == MM_CHUNK_OK && host.main.stream_calls == 1);
    CHECK(mm_chunk_add(&edit, 1, 1, 1, 17, NULL) == MM_CHUNK_IO && table->count == 1);
    CHECK(host.last_issue == MM_CHUNK_OWNER_EXTENT_LIMIT);

    CHECK(mm_chunk_add(&edit, 1, 1, 1, 4, &location) == MM_CHUNK_OK);
    CHECK(location.file == &host.main && location.offset == 140 && location.bytes == 4 && !location.flags);
    CHECK(mm_free_map_get(owner.files.main_map, 0, &range) == MM_CHUNK_OK && range.offset == 144 && range.bytes == 4);
    CHECK(owner.files.main_end == 200 && !host.main.resize_calls);
    memcpy(host.main.bytes + location.offset, "data", location.bytes);
    CHECK(edit.set_runtime(edit.context, 1, 1, 7));
    CHECK(mm_chunk_set_flag(&edit, 1, 1, MM_CHUNK_PACKED, 1) == MM_CHUNK_OK);
    CHECK(!edit.runtime(edit.context, 1, 1));
    CHECK(edit.set_runtime(edit.context, 1, 1, 19));

    owner.read_from_extra = 1;
    host.create_fails = 1;
    CHECK(mm_chunk_owner_fetch(&storage, 1, &location) == MM_CHUNK_OK && location.file == &host.main);
    CHECK(host.creates == 1 && !owner.files.extra_file && !host.copies);
    host.create_fails = 0;
    host.extra.resize_fails = 1;
    CHECK(mm_chunk_owner_cache(&storage, 1, &location) == MM_CHUNK_IO && location.file == &host.main);
    CHECK(host.creates == 2 && owner.files.extra_file == &host.extra && !owner.files.extra_end);
    host.extra.resize_fails = 0;
    host.copy_fails = 1;
    CHECK(mm_chunk_owner_cache(&storage, 1, &location) == MM_CHUNK_IO && location.file == &host.main);
    CHECK(!owner.files.extra_end && host.extra.length == 4 && host.extra.resize_calls == 2);
    CHECK(edit.entry(edit.context, 1, &entry) && entry.offset == 140 && entry.flags == (MM_CHUNK_LONER | MM_CHUNK_PACKED));
    host.copy_fails = 0;
    CHECK(mm_chunk_owner_fetch(&storage, 1, &location) == MM_CHUNK_OK);
    CHECK(location.file == &host.extra && !location.offset && location.flags == 1 && owner.files.extra_end == 4);
    CHECK(host.copies == 2 && host.extra.resize_calls == 2 && !memcmp(host.extra.bytes, "data", 4));
    CHECK(edit.entry(edit.context, 1, &entry) && !entry.offset && entry.flags == (1 | MM_CHUNK_LONER | MM_CHUNK_PACKED));
    CHECK(edit.runtime(edit.context, 1, 1) == 19);
    CHECK(host.trace[MM_STORAGE_CACHE] == 4 && host.trace[MM_STORAGE_FETCH_EXTRA] == 2 &&
          host.trace[MM_STORAGE_FETCH_DIRECT] == 1);

    CHECK(mm_chunk_remove(&edit, 1) == MM_CHUNK_OK && !owner.files.extra_end && table->count == 1);
    CHECK(!edit.runtime(edit.context, 1, 1));
    CHECK(mm_chunk_owner_fetch(&storage, 1, &location) == MM_CHUNK_NOT_FOUND && location.file == &host.extra);

    CHECK(mm_chunk_add(&edit, 1, 1, 1, 12, &location) == MM_CHUNK_OK);
    CHECK(location.file == &host.main && location.offset == 200 && owner.files.main_end == 212);
    mm_free_map_close(owner.files.main_map);
    owner.files.main_map = NULL;
    owner.files.free_map_unread = 1;
    CHECK(mm_chunk_remove(&edit, 1) == MM_CHUNK_OK && owner.files.main_end == 200);
    CHECK(!owner.files.free_map_unread && host.main.stream_calls == 2 && owner.files.main_map);
    mm_free_map_close(owner.files.main_map);
    owner.files.main_map = NULL;
    owner.files.free_map_unread = 1;
    owner.map_offset = host.main.length;
    CHECK(mm_chunk_owner_read_free_map(&storage) == MM_CHUNK_BAD_FILE && !owner.files.free_map_unread);
    CHECK(mm_chunk_owner_read_free_map(&storage) == MM_CHUNK_OK && host.main.stream_calls == 3);
    disposal.context = &host;
    CHECK(mm_chunk_owner_dispose(&owner, &disposal) == MM_CHUNK_OK);
    CHECK(host.main.releases == 1 && host.extra.releases == 1);
}

int main(void)
{
    reopen_cases();
    policy_cases();
    error_cases();
    storage_cases(0);
    storage_cases(1);
    puts("chunk owner C89 checks passed");
    return 0;
}

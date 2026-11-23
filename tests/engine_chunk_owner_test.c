#include "mm/chunk_owner.h"
#include "mm/file_ops.h"
#include "mm/movie_session.h"
#include "mm/private_archive_cache.h"
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
    unsigned char bytes[512];
    const MMChunkTable *source;
    MMU32 length, flags;
    int resize_fails, stream_calls, resize_calls, retains, releases;
    int prefix_fails, temporary;
} TestFile;

typedef struct StorageHost
{
    TestFile main, extra, staging[3];
    const MMChunkOwner *saving;
    int create_fails, copy_fails, last_issue, creates, copies;
    int saved, swapped, rename_fails;
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

static int write_file(void *p, MMU32 offset, const void *bytes, size_t count)
{
    TestFile *file = p;
    CHECK(offset <= sizeof(file->bytes) && count <= sizeof(file->bytes) - offset);
    if (file->source)
        CHECK(!file->source->storage.buffers[0].locks);
    if (!offset && file->prefix_fails)
        return 0;
    memcpy(file->bytes + offset, bytes, count);
    if (offset + count > file->length)
        file->length = offset + (MMU32)count;
    return 1;
}

static int copy_extent(void *p, void *source, MMU32 from, void *destination, MMU32 to, MMU32 bytes)
{
    StorageHost *host = p;
    TestFile *file = destination;
    CHECK(to <= sizeof(file->bytes) && bytes <= sizeof(file->bytes) - to);
    if (file == &host->extra)
        CHECK(source == &host->main && to + bytes <= file->length);
    if (file->source)
        CHECK(file->source->storage.buffers[0].locks > 0);
    ++host->copies;
    if (host->copy_fails)
        return 0;
    CHECK(read_file(source, from, file->bytes + to, bytes));
    if (bytes && to + bytes > file->length)
        file->length = to + bytes;
    return 1;
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

static void release_storage_file(void *file)
{
    ++((TestFile *)file)->releases;
}

static int file_is_temp(void *file)
{
    return ((TestFile *)file)->temporary;
}

static void file_set_temp(void *file, int temporary)
{
    ((TestFile *)file)->temporary = temporary;
}

static int file_is_writable(void *file)
{
    return (((TestFile *)file)->flags & MM_FILE_WRITE) != 0;
}

static MMChunkOwnerFileIO storage_io(StorageHost *host)
{
    MMChunkOwnerFileIO io = {NULL, create_extra, file_stream, file_size, resize_file, copy_extent, write_file,
                             release_storage_file, file_is_temp, file_set_temp, storage_trace, storage_issue,
                             file_is_writable};
    io.context = host;
    return io;
}

static void close_file(void *p, void *file)
{
    (void)p;
    release_storage_file(file);
}

static void close_table(void *p, void *table)
{
    (void)p;
    CHECK(mm_chunk_table_close(table) == MM_STORAGE_OK);
}

static void publish_storage_disposal(void *p, const MMChunkReopenState *state)
{
    (void)p;
    CHECK(!state->index && !state->main_file && !state->extra_file && !state->main_map && !state->extra_map);
}

static const MMChunkDisposal storage_disposal = {NULL, close_file, close_table, publish_storage_disposal};

static void storage_cases(int expanded)
{
    static const unsigned char wire_map[] = {1, 0, 3, 3, 8, 0, 0, 0, 1, 0, 0, 0, 140, 0, 0, 0, 8, 0, 0, 0};
    MMChunkOwnerFileIO io;
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
    io = storage_io(&host);
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
    CHECK(mm_chunk_owner_dispose(&owner, &storage_disposal) == MM_CHUNK_OK);
    CHECK(host.main.releases == 1 && host.extra.releases == 1);
}

static void bind_archive(MMChunkOwnerStorage *storage, MMChunkOwner *owner, const MMChunkOwnerFileIO *io,
                         TestFile *file, int expanded, int create)
{
    MMChunkTable *table;
    CHECK(mm_chunk_owner_init(owner, NULL, expanded) == MM_CHUNK_OK);
    owner->files.main_file = file;
    if (create)
    {
        CHECK(mm_chunk_table_create(NULL, expanded ? sizeof(MMChunkRecordBig) : sizeof(MMChunkRecordSmall),
                                    0, 1, &table) == MM_STORAGE_OK);
        owner->files.index = table;
        owner->files.main_end = file->length = MM_CHUNK_PREFIX_BYTES;
    }
    mm_chunk_owner_bind_storage(storage, owner, NULL, 32, io);
}

static int save_path_matches(void *p, void *file, void *path)
{
    (void)p;
    return file == path;
}

static void *create_save_temp(void *p, void *current, void *path)
{
    StorageHost *host = p;
    TestFile *file;
    CHECK(host->saving->files.main_file == current);
    CHECK(path == (host->saved < 2 ? &host->main : NULL));
    CHECK(host->saved < 3);
    file = host->staging + host->saved++;
    file->temporary = 1;
    return file;
}

static int rename_save(void *p, void *file, void *path)
{
    const StorageHost *host = p;
    CHECK(path == &host->main);
    CHECK(host->saving->files.main_file == file && !host->saving->files.extra_file);
    CHECK(!memcmp(((TestFile *)file)->bytes, "CHN2", 4));
    return !host->rename_fails;
}

static int swap_save(void *p, void *file, void *old)
{
    StorageHost *host = p;
    CHECK(host->saving->files.main_file == file && !host->saving->files.extra_file);
    CHECK(old == &host->staging[1] && file == &host->staging[2]);
    CHECK(!memcmp(((TestFile *)file)->bytes, "CHN2", 4));
    ++host->swapped;
    return 1;
}

static void persistence_cases(int expanded)
{
    static const unsigned char counted_name[] = {3, 3, 4, 'n', 'a', 'm', 'e', 0};
    const MMChunkNameValue value = {counted_name, sizeof(counted_name), 4};
    StorageHost host;
    TestFile reopened, output;
    MMChunkOwner owner, loaded, destination;
    MMChunkOwnerStorage storage, source, target;
    MMChunkOwnerFileIO io;
    MMChunkOwnerSavePaths save_paths = {NULL, save_path_matches, create_save_temp, rename_save, swap_save};
    MMChunkLocation location;
    MMChunkEdit edit;
    MMChunkNames names;
    MMChunkEntry row, before[4];
    MMChunkChild link = {1, 2, 77}, found;
    MMChunkFreeRange hole;
    MMChunkHeader header;
    MMReadStream stream;
    MMU32 saved_length, i;
    int failure, directory_failed, status;
    const MMU32 save_creator = 0x53415645UL, copy_creator = 0x434f5059UL;
    const int system = 0x0303;

    memset(&host, 0, sizeof(host));
    io = storage_io(&host);
    bind_archive(&storage, &owner, &io, &host.main, expanded, 1);
    edit = mm_chunk_owner_editor(&storage, 65535, 1);
    CHECK(mm_chunk_add(&edit, 1, 0, 0, 3, &location) == MM_CHUNK_OK && location.offset == 128);
    CHECK(write_file(location.file, location.offset, "one", 3));
    CHECK(mm_chunk_add(&edit, 1, 1, 1, 7, NULL) == MM_CHUNK_OK);
    CHECK(mm_chunk_add(&edit, 1, 2, 2, 4, &location) == MM_CHUNK_OK && location.offset == 138);
    CHECK(write_file(location.file, location.offset, "last", 4));
    CHECK(mm_chunk_adopt(&edit, 1, 0, &link, 1) == MM_CHUNK_OK);
    CHECK(mm_chunk_delete(&edit, 1, 1) == MM_CHUNK_OK);
    CHECK(mm_chunk_add(&edit, 1, 3, 2, 0, NULL) == MM_CHUNK_OK);
    names = mm_chunk_table_names(owner.files.index, 32);
    CHECK(mm_chunk_name_set(&names, 0, &value) == MM_CHUNK_OK);
    owner.files.invalid_main = 1;
    CHECK(mm_chunk_owner_save(&storage, NULL, NULL, save_creator, system) == MM_CHUNK_BAD_FILE);
    owner.files.invalid_main = 0;
    CHECK(mm_chunk_owner_save(&storage, NULL, NULL, save_creator, system) == MM_CHUNK_OK);
    CHECK(mm_chunk_owner_save(&storage, &save_paths, &host.main, save_creator, system) == MM_CHUNK_OK);
    CHECK(owner.files.main_file == &host.main && !host.saved);
    stream = file_stream(&host.main);
    CHECK(mm_chunk_read_header(&stream, &header) == MM_CHUNK_OK &&
          header.creator == save_creator && header.index_offset == 142 && header.map_bytes == 20);

    reopened = host.main;
    bind_archive(&source, &loaded, &io, &reopened, expanded, 0);
    loaded.map_offset = 9;
    loaded.map_bytes = 11;
    saved_length = reopened.length;
    reopened.length = 40;
    CHECK(mm_chunk_owner_read_index(&source, NULL, 1, 32, NULL, NULL) != MM_CHUNK_OK);
    CHECK(!loaded.files.index && !loaded.files.main_end && !loaded.files.free_map_unread &&
          loaded.map_offset == 9 && loaded.map_bytes == 11);
    reopened.length = saved_length;
    CHECK(mm_chunk_owner_read_index(&source, NULL, 1, 32, NULL, NULL) == MM_CHUNK_OK);
    CHECK(loaded.files.main_end == 142 && loaded.files.free_map_unread && !loaded.files.main_map);
    CHECK(mm_chunk_owner_read_free_map(&source) == MM_CHUNK_OK);
    CHECK(mm_free_map_get(loaded.files.main_map, 0, &hole) == MM_CHUNK_OK && hole.offset == 131 && hole.bytes == 7);
    loaded.add_to_extra = 1;
    edit = mm_chunk_owner_editor(&source, 65535, 1);
    CHECK(mm_chunk_add(&edit, 1, 4, 3, 3, &location) == MM_CHUNK_OK && location.file == &host.extra);
    CHECK(write_file(location.file, location.offset, "out", 3));
    CHECK(edit.set_runtime(edit.context, 1, 0, 29));
    for (i = 0; i < 4; ++i)
        CHECK(edit.entry(edit.context, i, before + i));

    for (failure = 0; failure < 3; ++failure)
    {
        memset(&output, 0, sizeof(output));
        output.source = loaded.files.index;
        output.prefix_fails = failure == 1;
        host.copy_fails = failure == 0;
        bind_archive(&target, &destination, &io, &output, expanded, 1);
        status = mm_chunk_owner_save_copy(&source, &target, copy_creator, system, &directory_failed);
        CHECK(status == (failure < 2 ? MM_CHUNK_IO : MM_CHUNK_OK) && directory_failed == (failure == 1));
        CHECK(!output.source->storage.buffers[0].locks && !memcmp(host.main.bytes, reopened.bytes, saved_length));
        for (i = 0; i < 4; ++i)
        {
            CHECK(edit.entry(edit.context, i, &row));
            CHECK(!memcmp(&row, before + i, sizeof(row)));
        }
        if (failure != 0)
        {
            MMChunkTable *table = destination.files.index;
            MMU32 bytes;
            const unsigned char *variable = mm_chunk_table_variable(table, 0, &bytes);
            CHECK(mm_chunk_table_child(table, 0, 0, &found));
            CHECK(found.type == link.type && found.number == link.number && found.slot == link.slot);
            CHECK(bytes == 12 + sizeof(counted_name) && !memcmp(variable + 12, counted_name, sizeof(counted_name)));
            CHECK(mm_chunk_table_entry(table, 2, &row) && row.number == 3 && row.offset == 135 && !row.bytes);
            CHECK(mm_chunk_table_entry(table, 3, &row) && row.number == 4 && row.offset == 135 && !(row.flags & 1));
            CHECK(!memcmp(output.bytes + 128, "onelastout", 10));
            if (expanded)
                CHECK(((const MMChunkRecordBig *)mm_chunk_table_fixed(table, 0, NULL))->rti == 29);
            stream = file_stream(&output);
            if (failure == 1)
                CHECK(mm_chunk_read_header(&stream, &header) != MM_CHUNK_OK);
            else
                CHECK(mm_chunk_read_header(&stream, &header) == MM_CHUNK_OK && header.creator == copy_creator &&
                      header.index_offset == 138 && !header.map_bytes);
        }
        CHECK(mm_chunk_owner_dispose(&destination, &storage_disposal) == MM_CHUNK_OK && output.releases == 1);
    }

    host.saving = &loaded;
    save_paths.context = &host;
    host.rename_fails = 1;
    CHECK(mm_chunk_owner_save(&source, &save_paths, &host.main, save_creator, system) == MM_CHUNK_IO);
    CHECK(loaded.files.main_file == &reopened && loaded.files.main_end == 128 && !loaded.files.main_map);
    CHECK(loaded.files.extra_file == host.staging && loaded.files.extra_end == 138 && host.staging[0].temporary);
    CHECK(!reopened.releases && host.extra.releases == 1 && edit.runtime(edit.context, 1, 0) == 29);
    CHECK(edit.entry(edit.context, 2, &row) && row.number == 3 && !row.offset && !(row.flags & 1));
    host.rename_fails = 0;
    CHECK(mm_chunk_owner_save(&source, &save_paths, &host.main, save_creator, system) == MM_CHUNK_OK);
    CHECK(loaded.files.main_file == host.staging + 1 && loaded.files.main_end == 138 && !loaded.files.extra_file);
    CHECK(reopened.releases == 1 && host.staging[0].releases == 1 && edit.runtime(edit.context, 1, 0) == 29);
    CHECK(!memcmp(host.staging[1].bytes + 128, "onelastout", 10));
    CHECK(mm_chunk_owner_save(&source, &save_paths, loaded.files.main_file, save_creator, system) == MM_CHUNK_OK);
    CHECK(host.swapped == 1 && loaded.files.main_file == host.staging + 2 && loaded.files.main_end == 138);
    CHECK(host.staging[1].releases == 1 && !memcmp(host.staging[2].bytes + 128, "onelastout", 10));
    CHECK(mm_chunk_owner_dispose(&loaded, &storage_disposal) == MM_CHUNK_OK && host.staging[2].releases == 1);
    CHECK(mm_chunk_owner_dispose(&owner, &storage_disposal) == MM_CHUNK_OK && host.main.releases == 1);
}

static int receive_name(void *out, const MMReadStream *stream)
{
    return mm_movie_read_fixed(stream, out, 5);
}

static void reader_cases(void)
{
    static const MMChunkPayloadServices payloads = {0};
    static const unsigned char wire[] = {3, 3, 1, 'A', 0};
    const MMChunkNameValue name = {wire, sizeof(wire), 1};
    const MMChunkChild child = {1, 1, 0};
    StorageHost host = {0};
    MMChunkOwnerFileIO io = storage_io(&host);
    MMChunkOwner owner;
    MMChunkOwnerStorage storage;
    MMChunkOwnerRead reader;
    MMMovieSessionGraph graph;
    MMChunkTable *old_table, *table;
    MMChunkAccess access;
    MMChunkEdit edit;
    MMChunkNames names;
    MMChunkEntry entry;
    MMBlockFile extent;
    int packed;
    unsigned char title[sizeof(wire)];

    bind_archive(&storage, &owner, &io, &host.main, 0, 1);
    reader = mm_chunk_owner_reader(&storage, &payloads, sizeof(wire));
    graph = mm_movie_owner_graph(&reader);
    edit = mm_chunk_owner_editor(&storage, 65535, 1);
    old_table = owner.files.index;
    graph.access(graph.context, &access);
    CHECK(!access.count);
    CHECK(mm_chunk_add(&edit, 1, 0, 0, 0, NULL) == MM_CHUNK_OK);
    graph.access(graph.context, &access);
    CHECK(access.count == 1 && mm_chunk_at(&access, 0, &entry) == MM_CHUNK_OK);
    CHECK(graph.name(graph.context, &entry, receive_name, title) == MM_MOVIE_BAD_FILE);

    CHECK(mm_chunk_table_create(NULL, sizeof(MMChunkRecordSmall), 0, 0, &table) == MM_STORAGE_OK);
    owner.files.index = table;
    close_table(NULL, old_table);
    CHECK(mm_chunk_add(&edit, 1, 0, 0, 4, NULL) == MM_CHUNK_OK);
    CHECK(mm_chunk_add(&edit, 1, 1, 1, 0, NULL) == MM_CHUNK_OK);
    CHECK(mm_chunk_adopt(&edit, 1, 0, &child, 1) == MM_CHUNK_OK);
    names = mm_chunk_table_names(table, sizeof(wire));
    CHECK(mm_chunk_name_set(&names, 0, &name) == MM_CHUNK_OK);
    graph.access(graph.context, &access);
    CHECK(access.context == table && access.count == 2);
    CHECK(graph.name(graph.context, &entry, receive_name, title) == MM_MOVIE_OK && !memcmp(wire, title, sizeof(wire)));
    CHECK(reader.payloads.locate(graph.context, entry.type, entry.number, &extent, &packed));
    CHECK(extent.file == &host.main && extent.length == 4 && !packed);
    CHECK(mm_chunk_owner_dispose(&owner, &storage_disposal) == MM_CHUNK_OK);
}

static void retain_block(void *context, void *file)
{
    (void)context;
    ++((TestFile *)file)->retains;
}
static void release_block(void *context, void *file)
{
    (void)context;
    release_storage_file(file);
}
static int read_block(void *context, void *file, MMU32 offset, void *out, size_t count)
{
    (void)context;
    return read_file(file, offset, out, count);
}
static int write_block(void *context, void *file, MMU32 offset, const void *bytes, size_t count)
{
    (void)context;
    return write_file(file, offset, bytes, count);
}
static size_t convert_text(void *context, const void *bytes, size_t count, int encoding, void *out, size_t capacity)
{
    (void)context;
    if (encoding != MM_TEXT_WINDOWS || count > capacity)
        return 0;
    memcpy(out, bytes, count);
    return count;
}
typedef struct NamedFiles
{
    TestFile *file;
    MMFileOwner owner;
    MMReference references;
    MMRegisteredFile registration;
    int opens, name_matches, legacy_prepares, fail_open;
} NamedFiles;

static int equal_named_files(void *context, void *a, void *b)
{
    ++((NamedFiles *)context)->name_matches;
    return a == b;
}

static void *open_named_file(void *context, int create, MMU32 flags)
{
    NamedFiles *files = context;
    CHECK(!create && flags == MM_FILE_DENY_WRITE);
    ++files->opens;
    return files->fail_open ? NULL : files->file;
}

static MMFileOwnerHost named_file_host(void *context)
{
    MMFileOwnerHost host = {0};
    host.context = context;
    host.open = open_named_file;
    return host;
}

static int refuse_legacy_archive(void *archive)
{
    ++((NamedFiles *)archive)->legacy_prepares;
    return 0;
}

static void private_archive_cases(void)
{
    const MMPrivateArchiveProfile profile = {1, MM_TEXT_WINDOWS, MM_TEXT_UTF16_LE, 0, 1, NULL, convert_text};
    StorageHost host = {0};
    MMBlockFileServices blocks = {NULL, retain_block, release_block, create_extra, read_block, write_block};
    MMChunkContext registry = {0};
    MMChunkContext legacy_registry = {0};
    MMChunkLink legacy_link;
    MMChunkOwner legacy_owner = {0};
    MMReference legacy_ref = {0};
    MMResourceCache cache = {0};
    MMChunkTitleIO titles = {0};
    MMMovieTitle title;
    MMChunkOwnerFileIO io = storage_io(&host);
    MMFileRegistry file_registry;
    MMFileOpsHost file_ops = {0};
    MMNamedArchiveServices files = {0};
    MMSharedArchiveCacheServices shared = {NULL, NULL, NULL, NULL, NULL, refuse_legacy_archive, NULL, NULL};
    NamedFiles name = {0};
    MMPrivateArchive *archive, *alias;
    MMResourceCache *shared_cache;
    MMChunkOwnerRead *borrowed = NULL;
    MMMovieArchiveEditor editor;
    MMRichTextArchive rich;
    MMTextLocation text;
    MMRichTextStream stream;
    MMMovieSessionGraph graph;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMU32 number;
    char value[4];
    unsigned char magic;
    int live;

    io.trace = NULL;
    blocks.context = &host;
    files.registry = &registry;
    files.io = &io;
    files.blocks = &blocks;
    files.profile = &profile;
    files.file_ops = &file_ops;
    shared.files = &files;
    shared.legacy_registry = &legacy_registry;
    name.file = &host.main;
    mm_file_registry_init(&file_registry, NULL, NULL, NULL);
    mm_registered_file_init(&name.registration, name.file, &name, &name.owner, &name.references, &name,
                            named_file_host);
    mm_file_registry_attach(&file_registry, &name.registration);
    file_ops.registry = &file_registry;
    file_ops.context = &name;
    file_ops.equal_names = equal_named_files;
    file_ops.retain = retain_block;
    mm_chunk_link_init(&legacy_link, &name, &legacy_owner, &legacy_ref);
    legacy_owner.files.main_file = name.file;
    archive = mm_private_archive_open_registered(&registry, &host.main, MM_PRIVATE_ARCHIVE_CREATE, &io, &blocks,
                                                  &profile);
    CHECK(archive != NULL && mm_private_archive_find(&registry, &host.main) == archive);
    editor = mm_private_archive_editor(archive);
    graph = mm_movie_owner_graph(mm_private_archive_reader(archive));
    mm_private_archive_retain(archive);
    mm_private_archive_close(archive);
    CHECK(editor.add(editor.context, 1, 0, &number, NULL) && number == 0);
    CHECK(editor.put(editor.context, 1, number, "test", sizeof(value)));
    cache.file = archive;
    mm_private_archive_cache_services(archive, NULL, &cache.services);
    titles.encoding = MM_TEXT_WINDOWS;
    titles.swapped_unicode_encoding = MM_TEXT_UTF16_LE;
    titles = mm_resource_cache_title_io(titles);
    CHECK(mm_movie_title_init(&title, 1) && mm_movie_title_set(&title, "Title", 5));
    CHECK(mm_chunk_title_write(&title, &cache, 1, number, &titles));
    graph.access(graph.context, &access);
    CHECK(access.count == 1 && mm_chunk_at(&access, 0, &entry) == MM_CHUNK_OK);
    CHECK(graph.read(graph.context, NULL, 0, &entry, value, sizeof(value)) == MM_MOVIE_OK);
    CHECK(!memcmp(value, "test", sizeof(value)) && host.main.releases == host.main.retains);
    retain_block(NULL, &host.main);
    alias = mm_private_archive_open_registered(&registry, &host.main, MM_PRIVATE_ARCHIVE_WORKING, &io, &blocks,
                                                &profile);
    CHECK(alias == archive && !mm_private_archive_reader(alias)->storage->owner->add_to_extra);
    mm_private_archive_close(alias);
    alias = mm_private_archive_open_named(&files, &name, 0);
    CHECK(alias == archive && !name.opens);
    mm_private_archive_close(alias);
    CHECK(mm_private_archive_save(archive, 0));
    mm_private_archive_close(archive);
    CHECK(host.main.releases == host.main.retains + 1 && !mm_private_archive_find(&registry, &host.main));

    name.name_matches = 0;
    CHECK(!mm_shared_archive_cache_open(&shared, &name, 0, 0, &live, &borrowed) && !live && !borrowed && !name.opens &&
          name.name_matches == 1);
    CHECK(mm_chunk_context_attach(&legacy_registry, &legacy_link) == MM_CHUNK_OK);
    CHECK(!mm_shared_archive_cache_open(&shared, &name, 1, 0, &live, &borrowed) && live && !borrowed && !name.opens &&
          name.legacy_prepares == 1 && name.name_matches == 2);
    CHECK(mm_chunk_context_detach(&legacy_link) == MM_CHUNK_OK);
    name.fail_open = 1;
    name.owner.flags = MM_FILE_DENY_READ;
    CHECK(!mm_shared_archive_cache_open(&shared, &name, 0, 0, &live, &borrowed) && live && !borrowed &&
          name.opens == 1);
    mm_file_owner_init(&name.owner, 0);
    name.fail_open = 0;
    shared_cache = mm_shared_archive_cache_open(&shared, &name, 1, 0, &live, &borrowed);
    CHECK(shared_cache && live && borrowed && name.opens == 2);
    archive = mm_private_archive_open_named(&files, &name, 0);
    CHECK(archive && borrowed == mm_private_archive_reader(archive) && name.opens == 2);
    mm_resource_cache_release(shared_cache);
    CHECK(mm_chunk_context_attach(&legacy_registry, &legacy_link) == MM_CHUNK_OK);
    shared_cache = mm_shared_archive_cache_open(&shared, &name, 0, 0, &live, &borrowed);
    CHECK(shared_cache && live && borrowed == mm_private_archive_reader(archive) && name.opens == 2 &&
          name.legacy_prepares == 1);
    CHECK(mm_chunk_context_detach(&legacy_link) == MM_CHUNK_OK);
    mm_resource_cache_release(shared_cache);
    CHECK(archive && mm_private_archive_reader(archive)->storage->owner->add_to_extra);
    cache.file = archive;
    mm_private_archive_cache_services(archive, NULL, &cache.services);
    mm_chunk_title_read(&title, &cache, 1, number, &titles);
    CHECK(title.count == 5 && !memcmp(mm_movie_title_text(&title), "Title", 5));
    editor = mm_private_archive_editor(archive);
    CHECK(editor.put(editor.context, 1, number, "edit", sizeof(value)));
    CHECK(mm_private_archive_reader(archive)->storage->owner->files.extra_file == &host.extra);
    memcpy(value, host.main.bytes + MM_CHUNK_PREFIX_BYTES, sizeof(value));
    CHECK(!memcmp(value, "test", sizeof(value)));
    graph = mm_movie_owner_graph(mm_private_archive_reader(archive));
    CHECK(mm_private_archive_reopen(archive, 1) == MM_CHUNK_BAD_FILE);
    host.main.flags = MM_FILE_WRITE;
    mm_resource_cache_reopen(&cache);
    CHECK(mm_private_archive_reader(archive)->storage->owner->files.extra_file == &host.extra);
    host.main.flags = 0;
    magic = host.main.bytes[0];
    host.main.bytes[0] ^= 0xff;
    CHECK(mm_private_archive_reopen(archive, 0) == MM_CHUNK_IO);
    CHECK(mm_private_archive_reader(archive)->storage->owner->files.extra_file == &host.extra);
    graph.access(graph.context, &access);
    CHECK(access.count == 1 && mm_chunk_at(&access, 0, &entry) == MM_CHUNK_OK);
    CHECK(graph.read(graph.context, NULL, 0, &entry, value, sizeof(value)) == MM_MOVIE_OK);
    CHECK(!memcmp(value, "edit", sizeof(value)));
    host.main.bytes[0] = magic;
    mm_resource_cache_reopen(&cache);
    CHECK(!mm_private_archive_reader(archive)->storage->owner->files.extra_file &&
          mm_private_archive_find(&registry, &host.main) == archive);
    graph.access(graph.context, &access);
    CHECK(access.count == 1 && mm_chunk_at(&access, 0, &entry) == MM_CHUNK_OK);
    CHECK(graph.read(graph.context, NULL, 0, &entry, value, sizeof(value)) == MM_MOVIE_OK);
    CHECK(!memcmp(value, "test", sizeof(value)));
    CHECK(mm_movie_title_set(&title, NULL, 0) && mm_chunk_title_write(&title, &cache, 1, number, &titles));
    CHECK(mm_movie_title_set(&title, "Title", 5));
    mm_chunk_title_read(&title, &cache, 1, number, &titles);
    CHECK(!title.count);
    rich = mm_private_archive_rich_text(archive, &editor);
    CHECK(!rich.location(&rich, 0, &text));
    CHECK(editor.add(editor.context, MM_TEXT_BYTES, 0, &number, NULL));
    CHECK(editor.put(editor.context, MM_TEXT_BYTES, number, "text", sizeof(value)));
    CHECK(mm_chunk_set_flag(&editor.edit, MM_TEXT_BYTES, number, MM_CHUNK_PACKED, 1) == MM_CHUNK_OK);
    CHECK(rich.location(&rich, number, &text) && text.file == &host.extra && text.bytes == sizeof(value));
    mm_rich_text_archive_stream(&stream, &rich, &text);
    CHECK(mm_read_exact(&stream.stream, 1, value, 2) && !memcmp(value, "ex", 2));
    CHECK(!rich.read_at(&rich, &text, 3, value, 2) && !memcmp(value, "ex", 2));
    CHECK(rich.read_at(&rich, &text, sizeof(value), NULL, 0));
    mm_private_archive_close(archive);
    CHECK(host.main.releases == host.main.retains + 1 && !mm_private_archive_find(&registry, &host.main));
    mm_file_registry_detach(&file_registry, &name.registration);
}

static void private_forest_cases(void)
{
    static const unsigned char forest[] = {
        1,0,3,0, 'T','O','O','R', 0,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0,
        1,0,3,0, 'F','A','E','L', 7,0,0,0, 4,0,0,0, 0,0,0,0, 0,0,0,0, 'd','a','t','a'
    };
    const MMPrivateArchiveProfile profile = {1, MM_TEXT_WINDOWS, MM_TEXT_UTF16_LE, 0, 1, NULL, convert_text};
    StorageHost host = {0};
    MMChunkOwnerFileIO io = storage_io(&host);
    MMBlockFileServices blocks = {NULL, retain_block, release_block, create_extra, read_block, write_block};
    MMBlockFile extent;
    MMPrivateArchive *archive;
    MMChunkOwnerRead *reader;
    MMChunkAccess chunks;
    MMBlockFile payload;
    MMU32 number = 0;
    int packed;
    const MMU32 root = 0x524f4f54UL, leaf = 0x4c454146UL;
    io.trace = NULL;
    blocks.context = &host;
    extent.file = &host.main;
    extent.offset = 7;
    extent.length = sizeof(forest);
    host.main.length = extent.offset + extent.length;
    memcpy(host.main.bytes + extent.offset, forest, sizeof(forest));
    archive = mm_private_archive_open_forest(&extent, &io, &blocks, &profile);
    CHECK(archive && !host.main.releases && !mm_private_archive_save(archive, 0));
    reader = mm_private_archive_reader(archive);
    chunks = mm_chunk_owner_access(reader->storage);
    CHECK(chunks.count == 2 && mm_chunk_link_number(&chunks, root, 0, 7, leaf, &number) == MM_CHUNK_OK);
    CHECK(mm_chunk_owner_locate(reader->storage, leaf, number, &payload, &packed));
    CHECK(payload.file == &host.main && payload.length == 4 && !packed &&
          !memcmp(host.main.bytes + payload.offset, "data", 4));
    mm_private_archive_close(archive);
    CHECK(host.main.releases == 1);
    retain_block(NULL, &host.main);
    --extent.length;
    CHECK(!mm_private_archive_open_forest(&extent, &io, &blocks, &profile) && host.main.releases == 2);
}

int main(void)
{
    reopen_cases();
    policy_cases();
    error_cases();
    storage_cases(0);
    storage_cases(1);
    persistence_cases(0);
    persistence_cases(1);
    reader_cases();
    private_archive_cases();
    private_forest_cases();
    puts("chunk owner C89 checks passed");
    return 0;
}

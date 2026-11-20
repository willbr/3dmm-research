#include "mm/resource_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "resource cache failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Host Host;
typedef struct Object
{
    MMResourceObject state;
    MMReference refs;
    Host *host;
    struct Object *cascade;
    int id;
} Object;
struct Host
{
    MMResourceCache cache;
    MMHandleServices handles;
    MMReference refs;
    int file_refs, objects, allocations, fail, fail_speed, issues;
    int destroyed[128], deaths, reads, estimates, scope_calls;
    int block_scans, active_blocks, active_handles, change_estimate_block;
    const MMReadStream *wire;
    MMI32 bytes, estimate;
    int fail_read, fail_estimate, recursive, missing;
    MMResourceManager *manager;
    Host *append_file;
    int file_id, *close_order, *close_count, free_owner;
};
static MMResourceObjectServices objects;
static void *resize(void *context, void *old, MMU32 before, MMU32 bytes, int priority)
{
    Host *h = (Host *)context;
    void *p;
    if (h->fail || (h->fail_speed && priority == MM_STORAGE_SPEED))
        return NULL;
    p = malloc(bytes);
    CHECK(p != NULL);
    if (old)
    {
        memcpy(p, old, before < bytes ? before : bytes);
        free(old);
    }
    else
        ++h->allocations;
    return p;
}
static void free_memory(void *context, void *p, MMU32 bytes)
{
    Host *h = (Host *)context;
    (void)bytes;
    --h->allocations;
    free(p);
}
static MMResourceObject *state(void *p)
{
    return &((Object *)p)->state;
}
static MMReference *references(void *p)
{
    return &((Object *)p)->refs;
}
static void release(void *p)
{
    mm_resource_object_release(p, &objects);
}
static void detach(void *p)
{
    mm_resource_object_detach(p, &objects);
}
static void priority(void *p, MMI32 n)
{
    mm_resource_object_priority(p, n, &objects);
}
static void destroy_owner(void *p)
{
    Host *h = (Host *)p;
    CHECK(h->refs.count == 0 && mm_resource_cache_dispose(&h->cache) == 0);
    if (h->free_owner)
    {
        CHECK(!h->objects && !h->allocations && h->file_refs == 1 &&
              !h->active_blocks && !h->active_handles);
        free(h);
    }
}
static void destroyed(void *p)
{
    Object *o = (Object *)p;
    Host *h = o->host;
    CHECK(!o->state.attached && h->deaths < 128);
    h->destroyed[h->deaths++] = o->id;
    if (o->cascade)
        release(o->cascade);
    --h->objects;
    mm_resource_object_dispose(o, &objects);
    free(o);
}
static void object_issue(void *p, int kind)
{
    (void)kind;
    ++((Object *)p)->host->issues;
}
/* Reader identity is part of the key. Distinct effects prevent optimized
 * identical-code folding from merging these two test readers. */
static volatile int reader_marker;
static void reader_a(void)
{
    reader_marker=1;
}
static void reader_b(void)
{
    reader_marker=2;
}
static int compare_reader(MMResourceReader a, MMResourceReader b)
{
    int x = a == reader_a ? 0 : 1, y = b == reader_a ? 0 : 1;
    return (x > y) - (x < y);
}
static int contains(void *p, MMU32 type, MMU32 number)
{
    Host *h = (Host *)p;
    if (type == 0x52534320UL)
    {
        Host *file = h->append_file;
        if (file)
        {
            h->append_file = NULL;
            CHECK(mm_resource_manager_add(h->manager, file, 100, NULL) == MM_STORAGE_OK);
        }
        ++h->scope_calls;
        return number == 7;
    }
    return !h->missing && number > 0 && number < 100;
}
static int locate_block(void *p, MMU32 type, MMU32 number, MMBlockFile *file, int *packed)
{
    Host *h = (Host *)p;
    ++h->block_scans;
    if (!contains(h, type, number))
        return 0;
    file->file = h;
    file->offset = number;
    file->length = 1;
    *packed = h->change_estimate_block;
    return 1;
}
static void retain_block(void *context, void *file)
{
    (void)context;
    ++((Host *)file)->active_blocks;
}
static void release_block(void *context, void *file)
{
    Host *h = (Host *)file;
    (void)context;
    CHECK(h->active_blocks > 0);
    --h->active_blocks;
}
static void *no_temp(void *context)
{
    (void)context;
    return NULL;
}
static int read_block(void *context, void *file, MMU32 offset, void *out, size_t bytes)
{
    const Host *h = (const Host *)file;
    (void)context;
    CHECK(h->active_blocks > 0 && bytes == 1);
    if (h->wire)
        return mm_read_exact(h->wire, offset, out, bytes);
    CHECK(offset < 100);
    *(unsigned char *)out = (unsigned char)offset;
    return 1;
}
static int no_write(void *context, void *file, MMU32 offset, const void *in, size_t bytes)
{
    (void)context;
    (void)file;
    (void)offset;
    (void)in;
    (void)bytes;
    CHECK(0);
    return 0;
}
static const MMBlockFileServices block_files = {
    NULL, retain_block, release_block, no_temp, read_block, no_write
};
static int resize_handle(void *context, void **handle, MMU32 bytes)
{
    MMU32 *p = (MMU32 *)realloc(*handle, sizeof(*p) + bytes);
    (void)context;
    if (!p)
        return 0;
    *p = bytes;
    *handle = p;
    return 1;
}
static int alloc_handle(void *context, MMU32 bytes, void **handle)
{
    *handle = NULL;
    if (!resize_handle(context, handle, bytes))
        return 0;
    ++((Host *)context)->active_handles;
    return 1;
}
static void free_handle(void *context, void *handle)
{
    CHECK(((Host *)context)->active_handles > 0);
    --((Host *)context)->active_handles;
    free(handle);
}
static MMU32 size_handle(void *context, void *handle)
{
    (void)context;
    return *(MMU32 *)handle;
}
static void *lock_handle(void *context, void *handle)
{
    (void)context;
    return (unsigned char *)handle + sizeof(MMU32);
}
static void unlock_handle(void *context, void *handle)
{
    (void)context;
    (void)handle;
}
static Object *fetch(Host *h, MMU32 number, MMResourceReader reader, int *error)
{
    MMResourceRequest q;
    q.type = 1;
    q.number = number;
    q.reader = reader;
    q.scope = 0;
    q.priority = 2;
    return (Object *)mm_resource_cache_fetch(&h->cache, &q, error);
}
static int read_object(void *p, MMResourceReader reader, MMU32 type, MMU32 number, MMOwnedBlock *block,
                       void **out, MMI32 *bytes)
{
    Host *h = (Host *)p;
    Object *o, *child = NULL;
    MMBlockIO io;
    unsigned char key;
    int error;
    (void)type;
    CHECK(mm_owned_block_io(block, &io) == MM_DATA_OK);
    if (block->file && block->file != h)
        CHECK(io.view.packed && io.view.start == 128 && io.view.length == 1);
    CHECK(mm_block_read(&io, 0, &key, 1, 1) == MM_DATA_OK && key == number);
    if (!out)
    {
        if (h->change_estimate_block)
        {
            void *handle = NULL;
            CHECK(h->active_blocks == 1 && io.view.packed);
            CHECK(mm_block_read_handle(&io, 0, 1, 1, &handle) == MM_DATA_OK);
            CHECK(mm_owned_block_set_handle(block, &handle, 0) == MM_DATA_OK && !handle);
        }
        ++h->estimates;
        *bytes = h->estimate;
        return !h->fail_estimate;
    }
    ++h->reads;
    if (h->change_estimate_block)
        CHECK(io.view.kind == MM_BLOCK_MEMORY && !io.view.packed && !h->active_blocks && h->active_handles == 1);
    if (h->fail_read)
        return 0;
    if (h->recursive && number == 2)
        child = fetch(h, 1, reader_a, &error);
    o = (Object *)calloc(1, sizeof(*o));
    CHECK(o != NULL);
    mm_resource_object_init(&o->state);
    o->host = h;
    o->refs.count = 1;
    o->cascade = child;
    o->id = (int)number * 10 + (reader == reader_b);
    ++h->objects;
    *out = o;
    *bytes = h->bytes;
    return 1;
}
static void retain_file(void *p)
{
    ++((Host *)p)->file_refs;
}
static void release_file(void *p)
{
    Host *h = (Host *)p;
    --h->file_refs;
    if (h->close_order)
        h->close_order[(*h->close_count)++] = h->file_id;
}
static void issue(void *p, int kind, MMU32 type, MMU32 number)
{
    (void)kind;
    (void)type;
    (void)number;
    ++((Host *)p)->issues;
}
static MMStorageAllocator allocator(Host *h)
{
    MMStorageAllocator a;
    a.context = h;
    a.resize = resize;
    a.release = free_memory;
    return a;
}
static void init(Host *h, MMI32 budget)
{
    MMResourceServices s;
    MMStorageAllocator a = allocator(h);
    memset(h, 0, sizeof(*h));
    memset(&s, 0, sizeof(s));
    h->refs.count = h->file_refs = 1;
    h->bytes = h->estimate = 100;
    s.context = h;
    s.objects = objects;
    s.compare_reader = compare_reader;
    s.contains = contains;
    h->handles.context = h;
    h->handles.allocate = alloc_handle;
    h->handles.release = free_handle;
    h->handles.size = size_handle;
    h->handles.lock = lock_handle;
    h->handles.unlock = unlock_handle;
    h->handles.resize = resize_handle;
    s.handles = &h->handles;
    s.block_files = &block_files;
    s.locate = locate_block;
    s.read = read_object;
    s.retain_file = retain_file;
    s.release_file = release_file;
    s.destroy_owner = destroy_owner;
    s.issue = issue;
    CHECK(mm_resource_cache_init(&h->cache, h, &h->refs, h, budget, &s, &a, 1) == 0);
    CHECK(h->file_refs == 2);
}
static void finish(Host *h)
{
    mm_resource_cache_release(&h->cache);
    CHECK(!h->objects && !h->allocations && h->file_refs == 1 && !h->issues &&
          !h->active_blocks && !h->active_handles);
}
static int load(Host *h, MMU32 number, MMI32 p)
{
    MMResourceRequest q;
    q.type = 1;
    q.number = number;
    q.reader = reader_a;
    q.scope = 0;
    q.priority = p;
    return mm_resource_cache_load(&h->cache, &q);
}
static void cache_cases(void)
{
    Host h;
    Object *a, *b, *held[24];
    MMResourceEntry e;
    MMResourceRequest scoped = {1, 99, 8, reader_a, 2};
    int error, i, full;
    init(&h, 300);
    CHECK(load(&h, 1, 1) == 1 && load(&h, 2, 2) == 1 && load(&h, 3, 3) == 1);
    CHECK(load(&h, 4, 2) == 1 && h.deaths == 1 && h.destroyed[0] == 10);
    CHECK(load(&h, 5, 1) == 2 && h.cache.count == 3);
    h.estimate = 50;
    h.bytes = 200;
    CHECK(load(&h, 6, 4) == 1 && h.cache.count == 2);
    CHECK(h.destroyed[1] == 20 && h.destroyed[2] == 40);
    mm_resource_cache_budget(&h.cache, 0);
    CHECK(!h.cache.count && !h.objects && !h.cache.resident.low && !h.cache.resident.high);
    finish(&h);

    init(&h, 100);
    h.change_estimate_block = 1;
    h.fail_estimate = 1;
    CHECK(load(&h, 1, 2) == 2 && !h.active_handles && !h.active_blocks);
    h.fail_estimate = 0;
    h.fail_read = 1;
    CHECK(load(&h, 1, 2) == 2 && !h.active_handles && !h.active_blocks);
    h.fail_read = 0;
    CHECK(load(&h, 1, 2) == 1 && !h.active_handles && !h.active_blocks);
    CHECK(h.block_scans == 3 && h.estimates == 3 && h.reads == 2);
    finish(&h);

    init(&h, 1000);
    h.recursive = 1;
    h.fail_speed = 1;
    a = fetch(&h, 2, reader_a, &error);
    CHECK(a && !error && h.cache.count == 2 && h.reads == 2 && h.block_scans == 2 && !h.active_blocks);
    CHECK(a->state.cache == &h.cache && a->state.attached && h.refs.count == 1);
    CHECK(load(&h, 2, 2) == 1 && h.block_scans == 2);
    b = fetch(&h, 2, reader_a, &error);
    CHECK(b == a && !error && h.block_scans == 2);
    release(b);
    CHECK(mm_resource_cache_at(&h.cache, 0, &e) == 0 && state(e.object)->number == 1);
    b = fetch(&h, 2, reader_b, &error);
    CHECK(b && b != a && h.cache.count == 3);
    release(b);
    CHECK(!mm_resource_cache_find(&h.cache, 1, 99, reader_a, 8) && !h.scope_calls);
    CHECK(!mm_resource_cache_fetch(&h.cache, &scoped, &error) && !error &&
          h.scope_calls == 1 && h.block_scans == 3);
    CHECK(mm_resource_cache_priority(&h.cache, 32768L, 1, 2, reader_a, 7));
    CHECK(a->state.priority == -32768L);
    priority(a, 32767);
    CHECK(a->state.priority == 32767);
    priority(a, -1);
    release(a);
    CHECK(h.cache.count == 2);
    mm_resource_cache_budget(&h.cache, 0);
    CHECK(!h.objects && !h.cache.count);
    finish(&h);

    init(&h, 1000);
    a = fetch(&h, 1, reader_a, &error);
    b = fetch(&h, 2, reader_a, &error);
    a->cascade = b;
    release(a);
    /* The held higher key is visited first, then released by the lower key. */
    mm_resource_cache_budget(&h.cache, 0);
    CHECK(!h.cache.count && !h.objects && h.deaths == 2);
    finish(&h);

    init(&h, 1000);
    a = fetch(&h, 1, reader_a, &error);
    detach(a);
    CHECK(!h.cache.count && a->state.cache == &h.cache && !a->state.attached && h.refs.count == 2);
    mm_resource_cache_release(&h.cache);
    CHECK(h.file_refs == 2);
    release(a);
    CHECK(!h.objects && !h.allocations && h.file_refs == 1 && !h.issues);

    init(&h, 1000);
    a = fetch(&h, 1, reader_a, &error);
    mm_resource_cache_release(&h.cache);
    CHECK(!a->state.cache && !a->state.attached && h.file_refs == 1 && !h.allocations);
    release(a);
    CHECK(!h.objects && !h.issues);

    init(&h, 1000);
    h.fail = 1;
    CHECK(load(&h, 1, 2) == 2 && !h.objects && h.refs.count == 1 && !h.cache.count);
    a = fetch(&h, 1, reader_a, &error);
    CHECK(a && error && !a->state.attached && h.refs.count == 2 && !h.cache.count);
    release(a);
    CHECK(h.refs.count == 1);
    h.fail = 0;
    CHECK(load(&h, 2, 2) == 1);
    finish(&h);

    init(&h, 0x7fffffffL);
    h.bytes = 192L * 1024 * 1024;
    for (i = 0; i < 24; ++i)
    {
        held[i] = fetch(&h, (MMU32)i + 1, reader_a, &error);
        CHECK(held[i] && !error);
    }
    CHECK(h.cache.resident.high == 1 && h.cache.resident.low == 0x20000000UL);
    for (i = 0; i < 24; ++i)
        release(held[i]);
    CHECK(h.cache.resident.high == 0 && h.cache.resident.low <= 0x7fffffffUL);
    finish(&h);

    init(&h, 10000);
    h.bytes = h.estimate = 400;
    CHECK(load(&h, 1, 2) == 1);
    h.bytes = h.estimate = 1000;
    CHECK(load(&h, 2, 2) == 1);
    /* Undersized candidates pay triple shortfall, versus ordinary excess. */
    CHECK(mm_resource_cache_purge(&h.cache, mm_wide_i32(700), 2));
    CHECK(h.deaths == 1 && h.destroyed[0] == 20 && h.cache.count == 1);
    CHECK(!mm_resource_cache_purge(&h.cache, mm_wide_i32(1), 1));
    finish(&h);

    init(&h, 10000);
    CHECK(load(&h, 1, 2) == 1);
    full = (int)(h.cache.storage.buffers[0].capacity / sizeof(MMResourceEntry));
    CHECK(full >= 3 && full < 90);
    for (i = 2; i <= full; ++i)
        CHECK(load(&h, (MMU32)i, 2) == 1);
    h.fail = 1;
    CHECK(load(&h, (MMU32)full + 1, 2) == 2 && h.cache.count == (MMU32)full && h.objects == full);
    a = fetch(&h, 3, reader_a, &error);
    CHECK(a && !error && a->id == 30);
    release(a);
    h.fail = 0;
    CHECK(load(&h, (MMU32)full + 1, 2) == 1 && h.cache.count == (MMU32)full + 1);
    CHECK(mm_resource_cache_contains(&h.cache, 1, 1, 7));
    CHECK(!mm_resource_cache_contains(&h.cache, 1, 99, 8));
    finish(&h);

    init(&h, 1000);
    h.fail_read = 1;
    CHECK(!fetch(&h, 1, reader_a, &error) && error && h.issues == 1);
    h.fail_read = 0;
    h.bytes = -1;
    CHECK(!fetch(&h, 2, reader_a, &error) && error && h.issues == 2 && !h.objects);
    h.issues = 0;
    finish(&h);
}

static void last_detached_reference(void)
{
    Host *h = (Host *)malloc(sizeof(*h));
    Object *object;
    int error, closes = 0, closed_file = -1;
    CHECK(h != NULL);
    init(h, 100);
    h->free_owner = 1;
    h->close_order = &closed_file;
    h->close_count = &closes;
    object = fetch(h, 1, reader_a, &error);
    CHECK(object && !error);
    detach(object);
    mm_resource_cache_release(&h->cache);
    CHECK(!closes && object->state.cache == &h->cache && h->refs.count == 1);
    release(object);
    CHECK(closes == 1 && closed_file == 0);
}
static void wire_index_lifetime(void)
{
    static const unsigned char prefix[128] = {
        'C','H','N','2', 0,0,0,0, 5,0,4,0, 1,0,3,3, 205,0,0,0, 129,0,0,0, 76,0,0,0, 205,0,0,0
    };
    static const unsigned char group[76] = {
        1,0,3,3, 2,0,0,0, 40,0,0,0, 255,255,255,255, 20,0,0,0,
        1,0,0,0, 7,0,0,0, 128,0,0,0, 6,1,0,0, 0,0,0,0,
        1,0,0,0, 8,0,0,0, 128,0,0,0, 2,2,0,0, 0,0,0,0,
        0,0,0,0, 20,0,0,0, 20,0,0,0, 20,0,0,0
    };
    unsigned char archive[205];
    MMMemoryStream memory;
    MMChunkPolicy policy = {0, 0, 516};
    MMChunkIndex *index;
    Host h, backing = {0};
    Object *object;
    MMOwnedBlock block;
    int error = 1;
    memcpy(archive, prefix, sizeof(prefix));
    archive[128] = 7;
    memcpy(archive + 129, group, sizeof(group));
    CHECK(mm_memory_stream_init(&memory, archive, sizeof(archive)));
    CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_OK);
    backing.wire = &memory.stream;
    init(&h, 100);
    h.missing = 1;
    CHECK(mm_resource_cache_bind_index(&h.cache, index, &backing) == MM_STORAGE_OK);
    CHECK(mm_resource_cache_bind_index(&h.cache, index, &backing) == MM_STORAGE_ARGUMENT);
    mm_chunk_index_close(index);
    CHECK(backing.active_blocks == 1 && mm_resource_cache_contains(&h.cache, 1, 7, 0));
    CHECK(!mm_resource_cache_contains(&h.cache, 1, 7, 7));
    CHECK(!mm_resource_cache_contains(&h.cache, 1, 9, 0));
    CHECK(mm_owned_block_init(&block, &h.handles, &block_files) == MM_DATA_OK);
    CHECK(mm_resource_cache_read_block(&h.cache, 1, 7, &block) == MM_CHUNK_OK &&
          block.file == &backing && block.view.packed && backing.active_blocks == 2);
    CHECK(mm_resource_cache_read_block(&h.cache, 1, 8, &block) == MM_CHUNK_NOT_FOUND &&
          !block.file && backing.active_blocks == 1);
    CHECK(!fetch(&h, 8, reader_a, &error) && !error);
    object = fetch(&h, 7, reader_a, &error);
    CHECK(object && !error && !h.block_scans && backing.active_blocks == 1);
    detach(object);
    mm_resource_cache_release(&h.cache);
    CHECK(backing.active_blocks == 1 && object->state.cache == &h.cache);
    release(object);
    CHECK(!backing.active_blocks && h.file_refs == 1 && !h.objects && !h.issues && !h.allocations);
}
typedef struct ManagerHost
{
    MMResourceManager manager;
    Host memory;
    Host files[3];
    int fail_create, order[3], closes;
} ManagerHost;
static MMResourceCache *create_cache(void *p, void *file, MMI32 budget)
{
    ManagerHost *h = (ManagerHost *)p;
    Host *owner = (Host *)file;
    if (h->fail_create)
        return NULL;
    init(owner, budget);
    owner->manager = &h->manager;
    owner->file_id = (int)(owner - h->files);
    owner->close_order = h->order;
    owner->close_count = &h->closes;
    return &owner->cache;
}
static void manager_cases(void)
{
    ManagerHost h;
    MMResourceManagerServices s;
    MMStorageAllocator a;
    MMResourceRequest q = {1, 2, 7, reader_a, 2};
    Host *first = &h.files[0], *second = &h.files[1], *third = &h.files[2];
    Object *object;
    MMU32 slot = 99;
    int error, scopes;
    memset(&h, 0, sizeof(h));
    s.context = &h;
    s.create = create_cache;
    a = allocator(&h.memory);
    CHECK(mm_resource_manager_init(&h.manager, &s, &a) == 0);
    h.fail_create = 1;
    CHECK(mm_resource_manager_add(&h.manager, first, 100, &slot) == MM_STORAGE_IO && slot == 99);
    h.fail_create = 0;
    h.memory.fail = 1;
    CHECK(mm_resource_manager_add(&h.manager, first, 100, &slot) == MM_STORAGE_MEMORY && slot == 99);
    CHECK(h.closes == 1 && h.order[0] == 0 && first->file_refs == 1 && !h.manager.count);
    h.closes = 0;
    h.memory.fail = 0;
    h.memory.fail_speed = 1;
    CHECK(mm_resource_manager_add(&h.manager, first, 100, &slot) == 0 && slot == 0);
    CHECK(mm_resource_manager_add(&h.manager, second, 100, &slot) == 0 && slot == 1);
    CHECK(mm_resource_manager_at(&h.manager, 0) == &first->cache);
    CHECK(!mm_resource_manager_at(&h.manager, 2));
    first->fail_estimate = 1;
    CHECK(mm_resource_manager_load(&h.manager, &q) == 2 && !second->scope_calls);
    first->fail_read = 1;
    CHECK(!mm_resource_manager_fetch(&h.manager, &q, &error) && error && !second->scope_calls);
    first->fail_read = 0;
    first->fail = 1;
    object = (Object *)mm_resource_manager_fetch(&h.manager, &q, &error);
    CHECK(object && object->host == first && !object->state.attached && error && !second->scope_calls);
    release(object);
    first->fail = 0;
    object = (Object *)mm_resource_manager_fetch(&h.manager, &q, &error);
    CHECK(object && object->host == first && !error && !second->scope_calls);
    release(object);
    mm_resource_cache_budget(&first->cache, 0);
    CHECK(load(second, 2, 2) == 1 && second->block_scans == 1);
    CHECK(mm_resource_manager_find_file(&h.manager, q.type, q.number, q.scope) == &first->cache);
    CHECK(!mm_resource_manager_find(&h.manager, &q) && !second->scope_calls);
    q.priority = 3;
    CHECK(mm_resource_manager_priority(&h.manager, &q) && second->scope_calls == 1);
    first->missing = 1;
    scopes = second->scope_calls;
    object = (Object *)mm_resource_manager_find(&h.manager, &q);
    CHECK(object && object->host == second && object->state.priority == 3 &&
          second->scope_calls == scopes + 1);
    release(object);
    mm_resource_cache_budget(&second->cache, 0);
    second->missing = 1;
    first->append_file = third;
    CHECK(!mm_resource_manager_load(&h.manager, &q) && h.manager.count == 3 && !third->scope_calls);
    CHECK(mm_resource_manager_dispose(&h.manager) == 0);
    CHECK(h.closes == 3 && h.order[0] == 2 && h.order[1] == 1 && h.order[2] == 0 && !h.memory.allocations);
    CHECK(first->file_refs == 1 && second->file_refs == 1 && third->file_refs == 1);
    CHECK(!first->active_blocks && !second->active_blocks && !third->active_blocks);
}
static MMU32 word_hash(MMU32 hash, MMU32 word)
{
    int i;
    for (i = 0; i < 4; ++i)
    {
        hash = (hash ^ (word & 255U)) * 16777619U;
        word >>= 8;
    }
    return hash;
}
static void wide_cases(void)
{
    MMU32 seed = 0x13579bdfUL, hash = 2166136261UL, v[4];
    MMWide a, b, x, y;
    int i, j;
    /* Frozen oracle from Python arbitrary-precision signed arithmetic. */
    for (i = 0; i < 65536; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            seed = seed * 1664525U + 1013904223U;
            v[j] = seed;
        }
        a.low = v[0];
        a.high = mm_signed_word(v[1]);
        b.low = v[2];
        b.high = mm_signed_word(v[3]);
        x = mm_wide_add(a, b);
        y = mm_wide_subtract(a, b);
        hash = word_hash(hash, x.low);
        hash = word_hash(hash, (MMU32)x.high);
        hash = word_hash(hash, y.low);
        hash = word_hash(hash, (MMU32)y.high);
        hash = word_hash(hash, (MMU32)(mm_wide_compare(a, b) + 1));
    }
    CHECK(hash == 0xe143d5d7UL);
    a.low = 0xffffffffUL;
    a.high = 0x7fffffffL;
    b = mm_wide_i32(1);
    x = mm_wide_add(a, b);
    CHECK(x.low == 0 && x.high == (-0x7fffffffL - 1));
    CHECK(mm_wide_compare(x, a) < 0 && mm_wide_compare(x, x) == 0);
    y = mm_wide_subtract(x, b);
    CHECK(y.low == a.low && y.high == a.high);
    x = mm_wide_i32(-1);
    CHECK(x.low == 0xffffffffUL && x.high == -1);
}
int main(void)
{
    objects.state = state;
    objects.references = references;
    objects.destroy = destroyed;
    objects.issue = object_issue;
    wide_cases();
    cache_cases();
    last_detached_reference();
    wire_index_lifetime();
    manager_cases();
    puts("resource cache C89 checks passed");
    return 0;
}

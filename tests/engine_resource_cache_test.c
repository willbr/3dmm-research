#include "three_mm/resource_cache.h"
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
    ThreeMMResourceObject state;
    ThreeMMI32 refs;
    Host *host;
    struct Object *cascade;
    int id;
} Object;
struct Host
{
    ThreeMMResourceCache cache;
    int refs, file_refs, objects, allocations, fail, fail_speed, issues;
    int destroyed[128], deaths, reads, estimates, scope_calls;
    ThreeMMI32 bytes, estimate;
    int fail_read, fail_estimate, recursive;
};
static ThreeMMResourceObjectServices objects;
static void *resize(void *context, void *old, ThreeMMU32 before, ThreeMMU32 bytes, int priority)
{
    Host *h = (Host *)context;
    void *p;
    if (h->fail || (h->fail_speed && priority == THREE_MM_STORAGE_SPEED))
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
static void free_memory(void *context, void *p, ThreeMMU32 bytes)
{
    Host *h = (Host *)context;
    (void)bytes;
    --h->allocations;
    free(p);
}
static ThreeMMResourceObject *state(void *p)
{
    return &((Object *)p)->state;
}
static ThreeMMI32 refs(void *p)
{
    return ((Object *)p)->refs;
}
static void set_refs(void *p, ThreeMMI32 n)
{
    ((Object *)p)->refs = n;
}
static void retain(void *p)
{
    ++((Object *)p)->refs;
}
static void release(void *p)
{
    three_mm_resource_object_release(p, &objects);
}
static void detach(void *p)
{
    three_mm_resource_object_detach(p, &objects);
}
static void priority(void *p, ThreeMMI32 n)
{
    three_mm_resource_object_priority(p, n, &objects);
}
static void retain_owner(void *p)
{
    ++((Host *)p)->refs;
}
static void release_owner(void *p)
{
    Host *h = (Host *)p;
    CHECK(h->refs > 0);
    if (--h->refs == 0)
        CHECK(three_mm_resource_cache_dispose(&h->cache) == 0);
}
static void destroyed(void *p)
{
    Object *o = (Object *)p;
    Host *h = o->host;
    CHECK(!o->state.attached && h->deaths < 128);
    h->destroyed[h->deaths++] = o->id;
    if (o->cascade)
        release(o->cascade);
    three_mm_resource_object_dispose(o, &objects);
    --h->objects;
    free(o);
}
static void released(void *h, void *p)
{
    three_mm_resource_cache_released(&((Host *)h)->cache, p);
}
static void detached(void *h, void *p)
{
    three_mm_resource_cache_detached(&((Host *)h)->cache, p);
}
static void object_issue(void *p, int kind)
{
    (void)kind;
    ++((Object *)p)->host->issues;
}
static void reader_a(void)
{
}
static void reader_b(void)
{
}
static int compare_reader(ThreeMMResourceReader a, ThreeMMResourceReader b)
{
    int x = a == reader_a ? 0 : 1, y = b == reader_a ? 0 : 1;
    return (x > y) - (x < y);
}
static int contains(void *p, ThreeMMU32 type, ThreeMMU32 number)
{
    Host *h = (Host *)p;
    if (type == 0x52534320UL)
    {
        ++h->scope_calls;
        return number == 7;
    }
    return number > 0 && number < 100;
}
static int find_block(void *p, ThreeMMU32 type, ThreeMMU32 number, void *block)
{
    *(ThreeMMU32 *)block = number;
    return contains(p, type, number);
}
static Object *fetch(Host *h, ThreeMMU32 number, ThreeMMResourceReader reader, int *error)
{
    ThreeMMU32 block;
    return (Object *)three_mm_resource_cache_fetch(&h->cache, 1, number, reader, 0, &block, error);
}
static int read_object(void *p, ThreeMMResourceReader reader, ThreeMMU32 type, ThreeMMU32 number, void *block,
                       void **out, ThreeMMI32 *bytes)
{
    Host *h = (Host *)p;
    Object *o, *child = NULL;
    int error;
    (void)type;
    CHECK(*(ThreeMMU32 *)block == number);
    if (!out)
    {
        ++h->estimates;
        *bytes = h->estimate;
        return !h->fail_estimate;
    }
    ++h->reads;
    if (h->fail_read)
        return 0;
    if (h->recursive && number == 2)
        child = fetch(h, 1, reader_a, &error);
    o = (Object *)calloc(1, sizeof(*o));
    CHECK(o != NULL);
    three_mm_resource_object_init(&o->state);
    o->host = h;
    o->refs = 1;
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
    --((Host *)p)->file_refs;
}
static ThreeMMI32 owner_refs(void *p)
{
    return ((Host *)p)->refs;
}
static void set_owner_refs(void *p, ThreeMMI32 n)
{
    ((Host *)p)->refs = n;
}
static void issue(void *p, int kind, ThreeMMU32 type, ThreeMMU32 number)
{
    (void)kind;
    (void)type;
    (void)number;
    ++((Host *)p)->issues;
}
static ThreeMMStorageAllocator allocator(Host *h)
{
    ThreeMMStorageAllocator a;
    a.context = h;
    a.resize = resize;
    a.release = free_memory;
    return a;
}
static void init(Host *h, ThreeMMI32 budget)
{
    ThreeMMResourceServices s;
    ThreeMMStorageAllocator a = allocator(h);
    memset(h, 0, sizeof(*h));
    memset(&s, 0, sizeof(s));
    h->refs = h->file_refs = 1;
    h->bytes = h->estimate = 100;
    s.context = h;
    s.objects = objects;
    s.compare_reader = compare_reader;
    s.contains = contains;
    s.find_block = find_block;
    s.read = read_object;
    s.retain_file = retain_file;
    s.release_file = release_file;
    s.owner_refs = owner_refs;
    s.set_owner_refs = set_owner_refs;
    s.issue = issue;
    CHECK(three_mm_resource_cache_init(&h->cache, h, h, budget, &s, &a, 1) == 0);
    CHECK(h->file_refs == 2);
}
static void finish(Host *h)
{
    release_owner(h);
    CHECK(!h->objects && !h->allocations && h->file_refs == 1 && !h->issues);
}
static int load(Host *h, ThreeMMU32 number, ThreeMMI32 p)
{
    ThreeMMU32 block;
    return three_mm_resource_cache_load(&h->cache, 1, number, reader_a, 0, p, &block);
}
static void cache_cases(void)
{
    Host h;
    Object *a, *b, *held[24];
    ThreeMMResourceEntry e;
    ThreeMMU32 block;
    int error, i, full;
    init(&h, 300);
    CHECK(load(&h, 1, 1) == 1 && load(&h, 2, 2) == 1 && load(&h, 3, 3) == 1);
    CHECK(load(&h, 4, 2) == 1 && h.deaths == 1 && h.destroyed[0] == 10);
    CHECK(load(&h, 5, 1) == 2 && h.cache.count == 3);
    h.estimate = 50;
    h.bytes = 200;
    CHECK(load(&h, 6, 4) == 1 && h.cache.count == 2);
    CHECK(h.destroyed[1] == 20 && h.destroyed[2] == 40);
    three_mm_resource_cache_budget(&h.cache, 0);
    CHECK(!h.cache.count && !h.objects && !h.cache.resident.low && !h.cache.resident.high);
    finish(&h);

    init(&h, 1000);
    h.recursive = 1;
    h.fail_speed = 1;
    a = fetch(&h, 2, reader_a, &error);
    CHECK(a && !error && h.cache.count == 2 && h.reads == 2);
    CHECK(three_mm_resource_cache_at(&h.cache, 0, &e) == 0 && state(e.object)->number == 1);
    b = fetch(&h, 2, reader_b, &error);
    CHECK(b && b != a && h.cache.count == 3);
    release(b);
    CHECK(!three_mm_resource_cache_find(&h.cache, 1, 99, reader_a, 8) && !h.scope_calls);
    CHECK(!three_mm_resource_cache_fetch(&h.cache, 1, 99, reader_a, 8, &block, &error) && h.scope_calls == 1);
    CHECK(three_mm_resource_cache_priority(&h.cache, 32768L, 1, 2, reader_a, 7));
    CHECK(a->state.priority == -32768L);
    priority(a, 32767);
    CHECK(a->state.priority == 32767);
    priority(a, -1);
    release(a);
    CHECK(h.cache.count == 2);
    three_mm_resource_cache_budget(&h.cache, 0);
    CHECK(!h.objects && !h.cache.count);
    finish(&h);

    init(&h, 1000);
    a = fetch(&h, 1, reader_a, &error);
    b = fetch(&h, 2, reader_a, &error);
    a->cascade = b;
    release(a);
    /* The held higher key is visited first, then released by the lower key. */
    three_mm_resource_cache_budget(&h.cache, 0);
    CHECK(!h.cache.count && !h.objects && h.deaths == 2);
    finish(&h);

    init(&h, 1000);
    a = fetch(&h, 1, reader_a, &error);
    detach(a);
    CHECK(!h.cache.count && !a->state.attached && h.refs == 2);
    release_owner(&h);
    CHECK(h.file_refs == 2);
    release(a);
    CHECK(!h.objects && !h.allocations && h.file_refs == 1 && !h.issues);

    init(&h, 1000);
    a = fetch(&h, 1, reader_a, &error);
    release_owner(&h);
    CHECK(!a->state.owner && !a->state.attached && h.file_refs == 1 && !h.allocations);
    release(a);
    CHECK(!h.objects && !h.issues);

    init(&h, 1000);
    h.fail = 1;
    CHECK(load(&h, 1, 2) == 2 && !h.objects && h.refs == 1 && !h.cache.count);
    a = fetch(&h, 1, reader_a, &error);
    CHECK(a && error && !a->state.attached && h.refs == 2 && !h.cache.count);
    release(a);
    CHECK(h.refs == 1);
    h.fail = 0;
    CHECK(load(&h, 2, 2) == 1);
    finish(&h);

    init(&h, 0x7fffffffL);
    h.bytes = 192L * 1024 * 1024;
    for (i = 0; i < 24; ++i)
    {
        held[i] = fetch(&h, (ThreeMMU32)i + 1, reader_a, &error);
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
    CHECK(three_mm_resource_cache_purge(&h.cache, three_mm_wide_i32(700), 2));
    CHECK(h.deaths == 1 && h.destroyed[0] == 20 && h.cache.count == 1);
    CHECK(!three_mm_resource_cache_purge(&h.cache, three_mm_wide_i32(1), 1));
    finish(&h);

    init(&h, 10000);
    CHECK(load(&h, 1, 2) == 1);
    full = (int)(h.cache.storage.buffers[0].capacity / sizeof(ThreeMMResourceEntry));
    CHECK(full >= 3 && full < 90);
    for (i = 2; i <= full; ++i)
        CHECK(load(&h, (ThreeMMU32)i, 2) == 1);
    h.fail = 1;
    CHECK(load(&h, (ThreeMMU32)full + 1, 2) == 2 && h.cache.count == (ThreeMMU32)full && h.objects == full);
    a = fetch(&h, 3, reader_a, &error);
    CHECK(a && !error && a->id == 30);
    release(a);
    h.fail = 0;
    CHECK(load(&h, (ThreeMMU32)full + 1, 2) == 1 && h.cache.count == (ThreeMMU32)full + 1);
    CHECK(three_mm_resource_cache_contains(&h.cache, 1, 1, 7));
    CHECK(!three_mm_resource_cache_contains(&h.cache, 1, 99, 8));
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
typedef struct ManagerHost ManagerHost;
typedef struct Mock
{
    ManagerHost *host;
    int id, result, error, contains, visits;
    void *object;
} Mock;
struct ManagerHost
{
    ThreeMMResourceManager manager;
    Host memory;
    Mock mock[4];
    int fail_create, order[16], deaths, append;
};
static void *create_cache(void *p, void *file, ThreeMMI32 budget)
{
    ManagerHost *h = (ManagerHost *)p;
    (void)budget;
    return h->fail_create ? NULL : file;
}
static void release_cache(void *p)
{
    Mock *m = (Mock *)p;
    m->host->order[m->host->deaths++] = m->id;
}
static int mock_load(void *p, const ThreeMMResourceRequest *q)
{
    Mock *m = (Mock *)p;
    ManagerHost *h = m->host;
    (void)q;
    ++m->visits;
    if (h->append)
    {
        h->append = 0;
        CHECK(three_mm_resource_manager_add(&h->manager, &h->mock[2], 1, NULL) == 0);
    }
    return m->result;
}
static void *mock_fetch(void *p, const ThreeMMResourceRequest *q, int *error)
{
    Mock *m = (Mock *)p;
    (void)q;
    ++m->visits;
    *error = m->error;
    return m->object;
}
static void *mock_find(void *p, const ThreeMMResourceRequest *q)
{
    Mock *m = (Mock *)p;
    CHECK(q->scope == 0);
    ++m->visits;
    return m->object;
}
static int mock_contains(void *p, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 scope)
{
    Mock *m = (Mock *)p;
    CHECK(type == 1 && number == 2 && scope == 7);
    return m->contains;
}
static void manager_cases(void)
{
    ManagerHost h;
    ThreeMMResourceManagerServices s;
    ThreeMMStorageAllocator a;
    ThreeMMResourceRequest q;
    ThreeMMU32 slot = 99;
    int error, i;
    memset(&h, 0, sizeof(h));
    memset(&s, 0, sizeof(s));
    memset(&q, 0, sizeof(q));
    for (i = 0; i < 4; ++i)
    {
        h.mock[i].host = &h;
        h.mock[i].id = i;
    }
    s.context = &h;
    s.create = create_cache;
    s.release = release_cache;
    s.load = mock_load;
    s.fetch = mock_fetch;
    s.find = mock_find;
    s.priority = mock_load;
    s.contains = mock_contains;
    a = allocator(&h.memory);
    CHECK(three_mm_resource_manager_init(&h.manager, &s, &a) == 0);
    h.fail_create = 1;
    CHECK(three_mm_resource_manager_add(&h.manager, &h.mock[0], 1, &slot) == THREE_MM_STORAGE_IO && slot == 99);
    h.fail_create = 0;
    h.memory.fail = 1;
    CHECK(three_mm_resource_manager_add(&h.manager, &h.mock[0], 1, &slot) == THREE_MM_STORAGE_MEMORY && slot == 99);
    CHECK(h.deaths == 1 && h.order[0] == 0 && !h.manager.count);
    h.deaths = 0;
    h.memory.fail = 0;
    h.memory.fail_speed = 1;
    CHECK(three_mm_resource_manager_add(&h.manager, &h.mock[0], 1, &slot) == 0 && slot == 0);
    CHECK(three_mm_resource_manager_add(&h.manager, &h.mock[1], 1, &slot) == 0 && slot == 1);
    CHECK(!three_mm_resource_manager_at(&h.manager, 2));
    q.type = 1;
    q.number = 2;
    q.scope = 7;
    h.mock[0].result = 2;
    h.mock[1].result = 1;
    CHECK(three_mm_resource_manager_load(&h.manager, &q) == 2 && !h.mock[1].visits);
    h.mock[0].error = 1;
    CHECK(!three_mm_resource_manager_fetch(&h.manager, &q, &error) && error && !h.mock[1].visits);
    h.mock[0].object = &h;
    h.mock[0].error = 0;
    CHECK(three_mm_resource_manager_fetch(&h.manager, &q, &error) == &h && !error && !h.mock[1].visits);
    h.mock[0].object = NULL;
    h.mock[0].contains = h.mock[1].contains = 1;
    h.mock[1].object = &h;
    CHECK(!three_mm_resource_manager_find(&h.manager, &q) && !h.mock[1].visits);
    h.mock[0].result = 0;
    CHECK(three_mm_resource_manager_priority(&h.manager, &q) && h.mock[1].visits == 1);
    h.mock[1].result = 0;
    h.append = 1;
    CHECK(!three_mm_resource_manager_load(&h.manager, &q) && h.manager.count == 3 && !h.mock[2].visits);
    CHECK(three_mm_resource_manager_dispose(&h.manager) == 0);
    CHECK(h.deaths == 3 && h.order[0] == 2 && h.order[1] == 1 && h.order[2] == 0 && !h.memory.allocations);
}
static ThreeMMU32 word_hash(ThreeMMU32 hash, ThreeMMU32 word)
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
    ThreeMMU32 seed = 0x13579bdfUL, hash = 2166136261UL, v[4];
    ThreeMMWide a, b, x, y;
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
        a.high = three_mm_signed_word(v[1]);
        b.low = v[2];
        b.high = three_mm_signed_word(v[3]);
        x = three_mm_wide_add(a, b);
        y = three_mm_wide_subtract(a, b);
        hash = word_hash(hash, x.low);
        hash = word_hash(hash, (ThreeMMU32)x.high);
        hash = word_hash(hash, y.low);
        hash = word_hash(hash, (ThreeMMU32)y.high);
        hash = word_hash(hash, (ThreeMMU32)(three_mm_wide_compare(a, b) + 1));
    }
    CHECK(hash == 0xe143d5d7UL);
    a.low = 0xffffffffUL;
    a.high = 0x7fffffffL;
    b = three_mm_wide_i32(1);
    x = three_mm_wide_add(a, b);
    CHECK(x.low == 0 && x.high == (-0x7fffffffL - 1));
    CHECK(three_mm_wide_compare(x, a) < 0 && three_mm_wide_compare(x, x) == 0);
    y = three_mm_wide_subtract(x, b);
    CHECK(y.low == a.low && y.high == a.high);
    x = three_mm_wide_i32(-1);
    CHECK(x.low == 0xffffffffUL && x.high == -1);
}
int main(void)
{
    objects.state = state;
    objects.refs = refs;
    objects.set_refs = set_refs;
    objects.retain = retain;
    objects.release = release;
    objects.detach = detach;
    objects.priority = priority;
    objects.destroy = destroyed;
    objects.retain_owner = retain_owner;
    objects.release_owner = release_owner;
    objects.released = released;
    objects.detached = detached;
    objects.issue = object_issue;
    wide_cases();
    cache_cases();
    manager_cases();
    puts("resource cache C89 checks passed");
    return 0;
}

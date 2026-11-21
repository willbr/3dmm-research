#include "mm/tag.h"
#include "tag_copy_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "tag failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Host Host;
typedef struct Cache
{
    Host *host;
    int id, refs;
    MMResourceCache core;
    MMResourceEntry entry;
    MMResourceObject object;
    MMReference object_refs;
    TagCopyFile archive;
} Cache;
struct Host
{
    Cache caches[3];
    MMResourceCache *files[3];
    MMResourceManager manager;
    MMTag visible;
    char log[256];
    int used, fail, found, no_first_child, local, missing, calls, stop, allocations, alloc_fail;
    MMU32 child_types[8], child_slots[8];
    int children;
};
static void event(Host *h, char c)
{
    CHECK(h->used < 255);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static void reset(Host *h)
{
    h->used = 0;
    h->log[0] = 0;
}
static void *file(void *p)
{
    Cache *c = (Cache *)p;
    event(c->host, (char)('1' + c->id));
    return c->core.file;
}
static void retain(void *p)
{
    Cache *c = (Cache *)p;
    event(c->host, (char)('A' + c->id));
    ++c->refs;
}
static void release(void *p)
{
    Cache *c = (Cache *)p;
    CHECK(c->refs > 0);
    event(c->host, (char)('a' + c->id));
    --c->refs;
}
static int compare(void *a, void *b)
{
    int x = ((Cache *)a)->id, y = ((Cache *)b)->id;
    return (x > y) - (x < y);
}
static MMChunkEndpoint bind_file(void *p, MMChunkFileCopy *binding)
{
    Cache *c = p;
    event(c->host, 'F');
    return tag_copy_file_endpoint(&c->archive, binding);
}
static MMChunkEndpoint bind_cache(void *p, MMResourceChunkCopy *binding)
{
    const MMChunkEndpoint unavailable = {0};
    Cache *c = p;
    event(c->host, 'C');
    return c->host->fail ? unavailable : tag_copy_file_endpoint(&c->archive, &binding->live);
}
static MMChunkEndpoint bind_target(void *p, MMChunkFileCopy *binding)
{
    const MMChunkEndpoint denied = {0};
    Cache *c = p;
    event(c->host, 'T');
    return c->core.file ? tag_copy_file_endpoint(&c->archive, binding) : denied;
}
static void missing_chunk(void *p)
{
    event(p, '!');
}
static void publish(void *p, const MMTag *tag)
{
    Host *h = (Host *)p;
    h->visible = *tag;
    event(h, 'P');
}
static MMResourceCache *cache(void *p)
{
    return p ? &((Cache *)p)->core : NULL;
}
static MMResourceManager *source(void *p, MMI32 sid, int local, const MMTag *tag)
{
    Host *h = (Host *)p;
    CHECK(sid == 42 && tag->sid == 42);
    h->local = local;
    event(h, 'S');
    return h->fail ? NULL : &h->manager;
}
static int contains(void *p, MMU32 type, MMU32 number)
{
    Cache *c = (Cache *)p;
    CHECK(type == 7 && number == 1);
    event(c->host, 'K');
    return c->host->found & (1 << c->id);
}
static int entry_at(void *p, MMU32 i, MMChunkEntry *entry)
{
    Cache *c = (Cache *)p;
    CHECK(i == 0);
    memset(entry, 0, sizeof(*entry));
    entry->type = 7;
    entry->number = 1;
    entry->child_count = c->id || !c->host->no_first_child ? 1 : 0;
    return 1;
}
static int child_at(void *p, MMU32 parent, MMU32 i, MMChunkChild *child)
{
    CHECK(parent == 0 && i == 0);
    child->slot = 9;
    child->type = 8;
    child->number = 10 + (MMU32)((Cache *)p)->id;
    return 1;
}
static MMChunkAccess chunks(void *p)
{
    Cache *c = (Cache *)p;
    MMChunkAccess access;
    access.context = p;
    access.count = 1;
    access.entry = entry_at;
    access.child = child_at;
    event(c->host, 'H');
    return access;
}
static MMResourceObject *object_state(void *p)
{
    return &((Cache *)p)->object;
}
static MMReference *object_refs(void *p)
{
    Cache *c = (Cache *)p;
    event(c->host, 'T');
    return &c->object_refs;
}
static void missing(void *p, const MMTag *tag)
{
    Host *h = (Host *)p;
    (void)tag;
    ++h->missing;
    event(h, 'M');
}
static int reader(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    (void)cache; (void)type; (void)number; (void)block; (void)out; (void)bytes;
    CHECK(0);
    return 0;
}
static int compare_reader(MMResourceBlockReader a, MMResourceBlockReader b)
{
    CHECK(a == reader && b == reader);
    return 0;
}
static MMTagServices init(Host *h)
{
    MMTagServices s;
    Cache *c;
    int i;
    memset(h, 0, sizeof(*h));
    memset(&s, 0, sizeof(s));
    for (i = 0; i < 3; ++i)
    {
        c = h->caches + i;
        c->host = h;
        c->id = i;
        c->refs = 1;
        tag_copy_file_init(&c->archive);
        c->core.file = i < 2 ? c : NULL;
        c->core.budget = 128;
        c->core.services.contains = contains;
        c->core.services.live_chunks = chunks;
        h->files[i] = &c->core;
    }
    c = h->caches;
    c->core.services.compare_reader = compare_reader;
    c->core.services.objects.state = object_state;
    c->core.services.objects.references = object_refs;
    c->object.type = 7;
    c->object.number = 1;
    c->object_refs.count = 1;
    c->entry.reader = reader;
    c->entry.object = c;
    c->core.storage.buffers[0].data = (unsigned char *)&c->entry;
    c->core.storage.buffers[0].capacity = sizeof(c->entry);
    c->core.count = 1;
    h->manager.storage.buffers[0].data = (unsigned char *)h->files;
    h->manager.storage.buffers[0].capacity = sizeof(h->files);
    h->manager.count = 2;
    h->found = 3;
    fixture_chunk_add(&h->caches[0].archive.index, 7, 1);
    fixture_chunk_add(&h->caches[1].archive.index, 7, 0);
    fixture_chunk_add(&h->caches[1].archive.index, 7, 1);
    s.context = h;
    s.file = file;
    s.retain = retain;
    s.release = release;
    s.compare_cache = compare;
    s.bind_file = bind_file;
    s.bind_cache = bind_cache;
    s.bind_target = bind_target;
    s.publish = publish;
    s.cache = cache;
    s.source = source;
    s.missing_chunk = missing_chunk;
    s.missing = missing;
    return s;
}
static void lifecycle(void)
{
    Host h;
    MMTagServices s = init(&h);
    MMTag tag, out, old;
    Cache *a = h.caches, *b = h.caches + 1;
    memset(&tag, 0, sizeof(tag));
    tag.type = 7;
    tag.number = 1;
    CHECK(mm_tag_open(&tag, a, NULL, &s) && tag.cache == a && a->refs == 2 && !strcmp(h.log, "PA"));
    CHECK(h.visible.cache == a);
    reset(&h);
    CHECK(mm_tag_save(&tag, b, 0, &s) && tag.cache == a && tag.number == 1 && !strcmp(h.log, "TC") &&
          b->archive.index.count == 3);
    reset(&h);
    CHECK(mm_tag_save(&tag, b, 1, &s) && tag.cache == b && tag.number == 2 && a->refs == 1 && b->refs == 2 &&
          !strcmp(h.log, "TCBaP") && b->archive.index.count == 3);
    reset(&h);
    old = tag;
    h.fail = 1;
    CHECK(!mm_tag_save(&tag, a, 1, &s) && tag.cache == old.cache && tag.number == old.number &&
          !strcmp(h.log, "TC"));
    h.fail = 0;
    reset(&h);
    CHECK(!mm_tag_save(&tag, h.caches + 2, 1, &s) && tag.cache == b && tag.number == 2 &&
          !strcmp(h.log, "T"));
    reset(&h);
    mm_tag_dup(&tag, &s);
    CHECK(b->refs == 3 && !strcmp(h.log, "B"));
    mm_tag_close(&tag, &s);
    CHECK(!tag.cache && b->refs == 2);
    release(b);
    CHECK(b->refs == 1);
    tag.cache = a;
    tag.number = 99;
    reset(&h);
    CHECK(!mm_tag_open(&tag, b, a, &s) && !tag.cache && tag.number == 99 && a->refs == 1 && b->refs == 1 &&
          !strcmp(h.log, "2FF!P"));
    tag.number = 1;
    reset(&h);
    CHECK(mm_tag_open(&tag, b, a, &s) && tag.cache == b && tag.number == 2 && !strcmp(h.log, "2FFPB"));
    mm_tag_close(&tag, &s);
    reset(&h);
    CHECK(mm_tag_open(&tag, b, b, &s) && tag.cache == b && tag.number == 2 && !strcmp(h.log, "2PB"));
    mm_tag_close(&tag, &s);
    tag.sid = 42;
    tag.cache = a;
    reset(&h);
    old = tag;
    CHECK(mm_tag_open(&tag, b, a, &s) && mm_tag_save(&tag, b, 1, &s));
    mm_tag_dup(&tag, &s);
    mm_tag_close(&tag, &s);
    CHECK(!h.used && tag.cache == old.cache && tag.number == old.number);
    tag.sid = -1;
    mm_tag_close(&tag, &s);
    CHECK(!h.used);
    tag.sid = 0;
    tag.cache = a;
    tag.type = 7;
    tag.number = 1;
    memset(&out, 0, sizeof(out));
    CHECK(mm_tag_child(&tag, 9, 8, &out, &s) && out.cache == a && out.type == 8 && out.number == 10 &&
          a->refs == 2 && !strcmp(h.log, "HPA"));
    mm_tag_close(&out, &s);
    tag.sid = 42;
    out.cache = b;
    reset(&h);
    CHECK(mm_tag_child(&tag, 9, 8, &out, &s) && out.sid == 42 && out.cache == b && !h.local &&
          !strcmp(h.log, "SKHP"));
    reset(&h);
    old = out;
    h.no_first_child = 1;
    CHECK(!mm_tag_child(&tag, 9, 8, &out, &s) && out.number == old.number && !strcmp(h.log, "SKH"));
    reset(&h);
    h.found = 2;
    CHECK(mm_tag_child(&tag, 9, 8, &out, &s) && out.number == 11 && !strcmp(h.log, "SKKHP"));
    reset(&h);
    h.found = 0;
    CHECK(!mm_tag_child(&tag, 9, 8, &out, &s) && out.number == 11 && !strcmp(h.log, "SKK"));
    reset(&h);
    CHECK(mm_tag_fetch(&tag, reader, 0, &s) == a && h.local && !strcmp(h.log, "ST"));
    CHECK(a->object_refs.count == 2);
    reset(&h);
    h.fail = 1;
    CHECK(!mm_tag_fetch(&tag, reader, 1, &s) && !h.local && h.missing == 1 && !strcmp(h.log, "SM"));
    reset(&h);
    h.fail = 0;
    tag.sid = 0;
    CHECK(mm_tag_fetch(&tag, reader, 0, &s) == a && a->object_refs.count == 3 && !strcmp(h.log, "T"));
    out = tag;
    out.cache = b;
    CHECK(mm_tag_compare(&tag, &out, &s) < 0);
    tag.sid = out.sid = 42;
    CHECK(!mm_tag_compare(&tag, &out, &s));
    tag.number = 0xffffffffUL;
    CHECK(mm_tag_compare(&tag, &out, &s) > 0);
    reset(&h);
    CHECK(mm_tag_cache_file(&tag, 1) && !h.used && a->refs == 1 && b->refs == 1);
}
static void *resize(void *p, void *old, MMU32 before, MMU32 bytes, int priority)
{
    Host *h = (Host *)p;
    void *value;
    (void)priority;
    if (h->alloc_fail)
        return NULL;
    value = malloc(bytes);
    CHECK(value != NULL);
    if (old)
    {
        memcpy(value, old, before < bytes ? before : bytes);
        free(old);
    }
    else
        ++h->allocations;
    return value;
}
static void free_memory(void *p, void *value, MMU32 bytes)
{
    Host *h = (Host *)p;
    (void)bytes;
    --h->allocations;
    free(value);
}
static int cache_tag(void *p, const MMTag *tag, int children)
{
    Host *h = (Host *)p;
    ++h->calls;
    event(h, 'C');
    if (tag->sid == 2 && tag->number == 3)
        CHECK(children);
    return h->calls != h->stop;
}
static int build_child(void *p, const MMTag *tag, MMU32 slot, MMU32 type, MMTag *out)
{
    Host *h = (Host *)p;
    CHECK(h->children < 8);
    h->child_types[h->children] = type;
    h->child_slots[h->children++] = slot;
    *out = *tag;
    out->number = 99;
    event(h, 'B');
    return !h->fail;
}
static void lists(void)
{
    Host h;
    MMTagServices unused = init(&h);
    MMTagList list;
    MMStorageAllocator a;
    MMTagCached value;
    MMTag tags[4];
    MMTagListServices s;
    MMU32 before;
    (void)unused;
    memset(tags, 0, sizeof(tags));
    tags[0].sid = 2;
    tags[0].cache = h.caches;
    tags[0].type = 9;
    tags[0].number = 3;
    tags[1] = tags[0];
    tags[1].sid = 1;
    tags[2] = tags[0];
    tags[2].cache = h.caches + 1;
    tags[3] = tags[0];
    tags[3].sid = 0;
    a.context = &h;
    a.resize = resize;
    a.release = free_memory;
    CHECK(mm_tag_list_init(&list, &a) == MM_STORAGE_OK);
    CHECK(mm_tag_list_child(&list, tags, 1, 2) == MM_TAG_PARENT_NOT_FOUND);
    h.alloc_fail = 1;
    CHECK(mm_tag_list_insert(&list, tags, 0) == MM_STORAGE_MEMORY && !list.count && !list.bytes);
    h.alloc_fail = 0;
    CHECK(mm_tag_list_insert(&list, tags, 0) == MM_STORAGE_OK &&
          mm_tag_list_insert(&list, tags + 1, 0) == MM_STORAGE_OK);
    CHECK(mm_tag_list_insert(&list, tags + 2, 1) == MM_STORAGE_OK &&
          mm_tag_list_insert(&list, tags + 3, 0) == MM_STORAGE_OK);
    CHECK(list.count == 3 && mm_tag_list_get(&list, 2, &value) == MM_STORAGE_OK &&
          value.tag.cache == h.caches && value.children);
    CHECK(mm_tag_list_child(&list, tags, 2, 9) == MM_STORAGE_OK &&
          mm_tag_list_child(&list, tags, 1, 8) == MM_STORAGE_OK);
    CHECK(mm_tag_list_child(&list, tags, 2, 9) == MM_STORAGE_OK);
    s.context = &h;
    s.cache = cache_tag;
    s.child = build_child;
    CHECK(mm_tag_list_cache(&list, &s));
    CHECK(h.calls == 6 && h.children == 3 && h.child_types[0] == 8 && h.child_slots[0] == 1 && h.child_types[1] == 9 &&
          h.child_slots[1] == 2 && h.child_types[2] == 9);
    h.stop = 2;
    h.calls = 0;
    h.children = 0;
    CHECK(!mm_tag_list_cache(&list, &s) && h.calls == 2 && !h.children);
    h.stop = 0;
    h.calls = 0;
    h.fail = 1;
    CHECK(!mm_tag_list_cache(&list, &s) && h.calls == 3 && h.children == 1);
    before = list.count;
    h.alloc_fail = 1;
    tags[1].sid = 4;
    while (mm_tag_list_insert(&list, tags + 1, 0) == MM_STORAGE_OK)
    {
        ++tags[1].sid;
        CHECK(list.count < 30);
    }
    CHECK(list.count >= before);
    h.alloc_fail = 0;
    CHECK(mm_tag_list_dispose(&list) == MM_STORAGE_OK && !h.allocations);
}
static MMU32 sources(void *p)
{
    (void)p;
    return 3;
}
static void source_at(void *p, MMU32 i, MMI32 *sid, MMResourceManager **manager)
{
    Host *h = (Host *)p;
    *sid = (MMI32)i + 1;
    *manager = i == 1 ? &h->manager : NULL;
}
static MMResourceManager *resolve(void *p, MMI32 sid)
{
    Host *h = (Host *)p;
    CHECK(sid == 2);
    event(h, 'S');
    return h->fail ? NULL : &h->manager;
}
static void reopen(void *p)
{
    Cache *c = (Cache *)p;
    event(c->host, (char)('0' + c->id));
}
static void clear_cases(void)
{
    Host h;
    MMTagServices unused = init(&h);
    MMTagCacheServices s;
    (void)unused;
    h.manager.count = 3;
    s.context = &h;
    s.sources = sources;
    s.source_at = source_at;
    s.resolve = resolve;
    s.reopen = reopen;
    mm_tag_clear_cache(2, 3, &s);
    CHECK(!strcmp(h.log, "S0T1") && h.caches[0].core.budget == 128);
    reset(&h);
    mm_tag_clear_cache(0, 2, &s);
    CHECK(!strcmp(h.log, "T") && h.caches[0].core.budget == 128);
    reset(&h);
    h.fail = 1;
    mm_tag_clear_cache(2, 1, &s);
    CHECK(!strcmp(h.log, "S"));
    reset(&h);
    mm_tag_clear_cache(3, 3, &s);
    CHECK(!h.used);
}
int main(void)
{
    lifecycle();
    lists();
    clear_cases();
    puts("tag C89 checks passed");
    return 0;
}

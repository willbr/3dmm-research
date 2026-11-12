#include "three_mm/tag.h"
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
    int id, refs, file, budget;
} Cache;
struct Host
{
    Cache caches[3];
    ThreeMMTag visible;
    char log[256];
    int used, fail, found, local, missing, calls, stop, allocations, alloc_fail;
    ThreeMMU32 child_types[8], child_slots[8];
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
    return c->file ? c : NULL;
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
static int copy(void *a, ThreeMMU32 type, ThreeMMU32 number, void *b, ThreeMMU32 *out)
{
    Host *h = ((Cache *)a)->host;
    (void)b;
    CHECK(type == 7);
    event(h, 'C');
    if (h->fail)
        return 0;
    *out = number + 1;
    return 1;
}
static void publish(void *p, const ThreeMMTag *tag)
{
    Host *h = (Host *)p;
    h->visible = *tag;
    event(h, 'P');
}
static void *source(void *p, ThreeMMI32 sid, int local, const ThreeMMTag *tag)
{
    Host *h = (Host *)p;
    CHECK(sid == 42 && tag->sid == 42);
    h->local = local;
    event(h, 'S');
    return h->fail ? NULL : h->caches;
}
static void *find_cache(void *p, ThreeMMU32 type, ThreeMMU32 number)
{
    Host *h = ((Cache *)p)->host;
    CHECK(type == 7 && number == 1);
    event(h, 'K');
    return h->found ? h->caches : NULL;
}
static int child(void *p, const ThreeMMTag *tag, ThreeMMU32 slot, ThreeMMU32 type, ThreeMMU32 *number)
{
    Host *h = ((Cache *)p)->host;
    CHECK(tag->type == 7 && slot == 9 && type == 8);
    event(h, 'H');
    if (h->fail)
        return 0;
    *number = 10;
    return 1;
}
static void *fetch(void *p, int manager, const ThreeMMTag *tag, ThreeMMResourceReader reader)
{
    Host *h = ((Cache *)p)->host;
    (void)reader;
    CHECK(manager == (tag->sid != 0));
    event(h, 'T');
    return h->found ? h : NULL;
}
static void missing(void *p, const ThreeMMTag *tag)
{
    Host *h = (Host *)p;
    (void)tag;
    ++h->missing;
    event(h, 'M');
}
static void reader(void)
{
}
static ThreeMMTagServices init(Host *h)
{
    ThreeMMTagServices s;
    int i;
    memset(h, 0, sizeof(*h));
    memset(&s, 0, sizeof(s));
    for (i = 0; i < 3; ++i)
    {
        h->caches[i].host = h;
        h->caches[i].id = i;
        h->caches[i].refs = 1;
        h->caches[i].file = i < 2;
        h->caches[i].budget = 128;
    }
    h->found = 1;
    s.context = h;
    s.file = file;
    s.retain = retain;
    s.release = release;
    s.compare_cache = compare;
    s.copy = copy;
    s.publish = publish;
    s.source = source;
    s.find_cache = find_cache;
    s.child = child;
    s.fetch = fetch;
    s.missing = missing;
    return s;
}
static void lifecycle(void)
{
    Host h;
    ThreeMMTagServices s = init(&h);
    ThreeMMTag tag, out, old;
    Cache *a = h.caches, *b = h.caches + 1;
    memset(&tag, 0, sizeof(tag));
    tag.type = 7;
    tag.number = 1;
    CHECK(three_mm_tag_open(&tag, a, NULL, &s) && tag.cache == a && a->refs == 2 && !strcmp(h.log, "PA"));
    CHECK(h.visible.cache == a);
    reset(&h);
    CHECK(three_mm_tag_save(&tag, b, 0, &s) && tag.cache == a && tag.number == 1 && !strcmp(h.log, "12C"));
    reset(&h);
    CHECK(three_mm_tag_save(&tag, b, 1, &s) && tag.cache == b && tag.number == 2 && a->refs == 1 && b->refs == 2 &&
          !strcmp(h.log, "12CBaP"));
    reset(&h);
    old = tag;
    h.fail = 1;
    CHECK(!three_mm_tag_save(&tag, a, 1, &s) && tag.cache == old.cache && tag.number == old.number &&
          !strcmp(h.log, "21C"));
    h.fail = 0;
    reset(&h);
    three_mm_tag_dup(&tag, &s);
    CHECK(b->refs == 3 && !strcmp(h.log, "B"));
    three_mm_tag_close(&tag, &s);
    CHECK(!tag.cache && b->refs == 2);
    release(b);
    CHECK(b->refs == 1);
    tag.cache = a;
    tag.number = 1;
    h.fail = 1;
    reset(&h);
    CHECK(!three_mm_tag_open(&tag, b, a, &s) && !tag.cache && tag.number == 1 && a->refs == 1 && b->refs == 1 &&
          !strcmp(h.log, "22CP"));
    h.fail = 0;
    reset(&h);
    CHECK(three_mm_tag_open(&tag, b, a, &s) && tag.cache == b && tag.number == 2 && !strcmp(h.log, "22CPB"));
    three_mm_tag_close(&tag, &s);
    tag.sid = 42;
    tag.cache = a;
    reset(&h);
    old = tag;
    CHECK(three_mm_tag_open(&tag, b, a, &s) && three_mm_tag_save(&tag, b, 1, &s));
    three_mm_tag_dup(&tag, &s);
    three_mm_tag_close(&tag, &s);
    CHECK(!h.used && tag.cache == old.cache && tag.number == old.number);
    tag.sid = -1;
    three_mm_tag_close(&tag, &s);
    CHECK(!h.used);
    tag.sid = 0;
    tag.cache = a;
    tag.type = 7;
    tag.number = 1;
    memset(&out, 0, sizeof(out));
    CHECK(three_mm_tag_child(&tag, 9, 8, &out, &s) && out.cache == a && out.type == 8 && out.number == 10 &&
          a->refs == 2 && !strcmp(h.log, "HPA"));
    three_mm_tag_close(&out, &s);
    tag.sid = 42;
    out.cache = b;
    reset(&h);
    CHECK(three_mm_tag_child(&tag, 9, 8, &out, &s) && out.sid == 42 && out.cache == b && !h.local &&
          !strcmp(h.log, "SKHP"));
    reset(&h);
    h.found = 0;
    old = out;
    CHECK(!three_mm_tag_child(&tag, 9, 8, &out, &s) && out.number == old.number && !strcmp(h.log, "SK"));
    reset(&h);
    CHECK(!three_mm_tag_fetch(&tag, reader, 0, &s) && h.local && h.missing == 1 && !strcmp(h.log, "STM"));
    reset(&h);
    h.fail = 1;
    CHECK(!three_mm_tag_fetch(&tag, reader, 1, &s) && !h.local && !strcmp(h.log, "SM"));
    reset(&h);
    h.fail = 0;
    h.found = 1;
    tag.sid = 0;
    CHECK(three_mm_tag_fetch(&tag, reader, 0, &s) == &h && !strcmp(h.log, "T"));
    out = tag;
    out.cache = b;
    CHECK(three_mm_tag_compare(&tag, &out, &s) < 0);
    tag.sid = out.sid = 42;
    CHECK(!three_mm_tag_compare(&tag, &out, &s));
    tag.number = 0xffffffffUL;
    CHECK(three_mm_tag_compare(&tag, &out, &s) > 0);
    reset(&h);
    CHECK(three_mm_tag_cache_file(&tag, 1) && !h.used && a->refs == 1 && b->refs == 1);
}
static void *resize(void *p, void *old, ThreeMMU32 before, ThreeMMU32 bytes, int priority)
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
static void free_memory(void *p, void *value, ThreeMMU32 bytes)
{
    Host *h = (Host *)p;
    (void)bytes;
    --h->allocations;
    free(value);
}
static int cache_tag(void *p, const ThreeMMTag *tag, int children)
{
    Host *h = (Host *)p;
    ++h->calls;
    event(h, 'C');
    if (tag->sid == 2 && tag->number == 3)
        CHECK(children);
    return h->calls != h->stop;
}
static int build_child(void *p, const ThreeMMTag *tag, ThreeMMU32 slot, ThreeMMU32 type, ThreeMMTag *out)
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
    ThreeMMTagServices unused = init(&h);
    ThreeMMTagList list;
    ThreeMMStorageAllocator a;
    ThreeMMTagCached value;
    ThreeMMTag tags[4];
    ThreeMMTagListServices s;
    ThreeMMU32 before;
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
    CHECK(three_mm_tag_list_init(&list, &a) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_tag_list_child(&list, tags, 1, 2) == THREE_MM_TAG_PARENT_NOT_FOUND);
    h.alloc_fail = 1;
    CHECK(three_mm_tag_list_insert(&list, tags, 0) == THREE_MM_STORAGE_MEMORY && !list.count && !list.bytes);
    h.alloc_fail = 0;
    CHECK(three_mm_tag_list_insert(&list, tags, 0) == THREE_MM_STORAGE_OK &&
          three_mm_tag_list_insert(&list, tags + 1, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_tag_list_insert(&list, tags + 2, 1) == THREE_MM_STORAGE_OK &&
          three_mm_tag_list_insert(&list, tags + 3, 0) == THREE_MM_STORAGE_OK);
    CHECK(list.count == 3 && three_mm_tag_list_get(&list, 2, &value) == THREE_MM_STORAGE_OK &&
          value.tag.cache == h.caches && value.children);
    CHECK(three_mm_tag_list_child(&list, tags, 2, 9) == THREE_MM_STORAGE_OK &&
          three_mm_tag_list_child(&list, tags, 1, 8) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_tag_list_child(&list, tags, 2, 9) == THREE_MM_STORAGE_OK);
    s.context = &h;
    s.cache = cache_tag;
    s.child = build_child;
    CHECK(three_mm_tag_list_cache(&list, &s));
    CHECK(h.calls == 6 && h.children == 3 && h.child_types[0] == 8 && h.child_slots[0] == 1 && h.child_types[1] == 9 &&
          h.child_slots[1] == 2 && h.child_types[2] == 9);
    h.stop = 2;
    h.calls = 0;
    h.children = 0;
    CHECK(!three_mm_tag_list_cache(&list, &s) && h.calls == 2 && !h.children);
    h.stop = 0;
    h.calls = 0;
    h.fail = 1;
    CHECK(!three_mm_tag_list_cache(&list, &s) && h.calls == 3 && h.children == 1);
    before = list.count;
    h.alloc_fail = 1;
    tags[1].sid = 4;
    while (three_mm_tag_list_insert(&list, tags + 1, 0) == THREE_MM_STORAGE_OK)
    {
        ++tags[1].sid;
        CHECK(list.count < 30);
    }
    CHECK(list.count >= before);
    h.alloc_fail = 0;
    CHECK(three_mm_tag_list_dispose(&list) == THREE_MM_STORAGE_OK && !h.allocations);
}
static ThreeMMU32 sources(void *p)
{
    (void)p;
    return 3;
}
static void source_at(void *p, ThreeMMU32 i, ThreeMMI32 *sid, void **manager)
{
    Host *h = (Host *)p;
    *sid = (ThreeMMI32)i + 1;
    *manager = i == 1 ? h->caches : NULL;
}
static void *resolve(void *p, ThreeMMI32 sid)
{
    Host *h = (Host *)p;
    CHECK(sid == 2);
    event(h, 'S');
    return h->fail ? NULL : h->caches;
}
static ThreeMMU32 caches(void *p)
{
    (void)p;
    return 3;
}
static void *cache_at(void *p, ThreeMMU32 i)
{
    return (Cache *)p + i;
}
static void reopen(void *p)
{
    event(((Cache *)p)->host, 'O');
}
static ThreeMMI32 budget(void *p)
{
    Cache *c = (Cache *)p;
    event(c->host, 'G');
    return c->budget;
}
static void set_budget(void *p, ThreeMMI32 n)
{
    Cache *c = (Cache *)p;
    event(c->host, n ? 'R' : 'Z');
    c->budget = n;
}
static void clear_cases(void)
{
    Host h;
    ThreeMMTagServices unused = init(&h);
    ThreeMMTagCacheServices s;
    (void)unused;
    s.context = &h;
    s.sources = sources;
    s.source_at = source_at;
    s.resolve = resolve;
    s.caches = caches;
    s.cache_at = cache_at;
    s.file = file;
    s.reopen = reopen;
    s.budget = budget;
    s.set_budget = set_budget;
    three_mm_tag_clear_cache(2, 3, &s);
    CHECK(!strcmp(h.log, "S1OGZR2OGZR3") && h.caches[0].budget == 128);
    reset(&h);
    three_mm_tag_clear_cache(0, 2, &s);
    CHECK(!strcmp(h.log, "1GZR2GZR3"));
    reset(&h);
    h.fail = 1;
    three_mm_tag_clear_cache(2, 1, &s);
    CHECK(!strcmp(h.log, "S"));
    reset(&h);
    three_mm_tag_clear_cache(3, 3, &s);
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

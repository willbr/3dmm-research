#include "three_mm/source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "source failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Paths
{
    int local, media;
} Paths;
typedef struct Host
{
    ThreeMMSourceCatalogue catalogue;
    int access, exists, content, archive, media, retry, install, fail_path, fail_alloc, fail_build, fail_probe,
        fail_manager;
    int paths, managers, allocations, probes, builds, retries, news, outputs, output, leaf_calls, leaf_at, prepare,
        cleared, issues;
    int create_fail, begin_fail, open_fail, add_fail, files, added, next_calls, begins;
    char log[256];
    int used;
} Host;
static Host *active;
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
static void *new_paths(void *p)
{
    Host *h = (Host *)p;
    Paths *v;
    if (h->fail_path)
        return NULL;
    v = (Paths *)calloc(1, sizeof(*v));
    CHECK(v != NULL);
    ++h->paths;
    return v;
}
static void copy_paths(void *d, const void *s)
{
    *(Paths *)d = *(const Paths *)s;
}
static void release_paths(void *p)
{
    --active->paths;
    event(active, 'p');
    free(p);
}
static void release_manager(void *p)
{
    Host *h = (Host *)p;
    CHECK(h->managers > 0);
    --h->managers;
    event(h, 'm');
}
static void *resize(void *p, void *old, ThreeMMU32 before, ThreeMMU32 bytes, int priority)
{
    Host *h = (Host *)p;
    void *v;
    (void)priority;
    if (h->fail_alloc)
        return NULL;
    v = malloc(bytes);
    CHECK(v != NULL);
    if (old)
    {
        memcpy(v, old, before < bytes ? before : bytes);
        free(old);
    }
    else
        ++h->allocations;
    return v;
}
static void release_memory(void *p, void *v, ThreeMMU32 bytes)
{
    Host *h = (Host *)p;
    (void)bytes;
    --h->allocations;
    free(v);
}
static int access_source(void *p, ThreeMMI32 sid, void *paths, int kind)
{
    Host *h = (Host *)p;
    (void)paths;
    CHECK(sid >= 0);
    event(h, 'A');
    if (kind && h->access == 1)
    {
        h->output = 77;
        ++h->outputs;
    }
    return h->access;
}
static int empty(void *p, int media)
{
    return !(media ? ((Paths *)p)->media : ((Paths *)p)->local);
}
static void clear_local(void *p)
{
    ((Paths *)p)->local = 0;
    event(active, 'X');
}
static int build_local(void *p, ThreeMMI32 sid, void *paths, int *exists)
{
    Host *h = (Host *)p;
    (void)sid;
    ++h->builds;
    event(h, 'B');
    if (h->fail_build)
        return 0;
    *exists = h->exists;
    if (*exists)
        ((Paths *)paths)->local = 10;
    return 1;
}
static int archives(void *p, ThreeMMI32 sid)
{
    Host *h = (Host *)p;
    (void)sid;
    event(h, 'H');
    return h->archive;
}
static int content(void *p, void *paths, int *exists)
{
    Host *h = (Host *)p;
    CHECK(((Paths *)paths)->local == 10);
    ++h->probes;
    event(h, 'C');
    *exists = h->content;
    return !h->fail_probe;
}
static int find_media(void *p, ThreeMMI32 sid, void *paths, int *changed)
{
    Host *h = (Host *)p;
    (void)sid;
    event(h, 'D');
    if (!h->media)
        return 0;
    *changed = !((Paths *)paths)->media;
    ((Paths *)paths)->media = 20;
    return 1;
}
static int retry(void *p, ThreeMMI32 sid)
{
    Host *h = (Host *)p;
    (void)sid;
    ++h->retries;
    event(h, 'R');
    if (h->install)
    {
        h->exists = h->content = 1;
        return 1;
    }
    return h->retry;
}
static void *new_manager(void *p, ThreeMMI32 sid, void *paths, int media)
{
    Host *h = (Host *)p;
    (void)sid;
    CHECK((media ? ((Paths *)paths)->media : ((Paths *)paths)->local) != 0);
    event(h, media ? 'M' : 'L');
    ++h->news;
    if (h->fail_manager)
        return NULL;
    ++h->managers;
    return h;
}
static void output(void *p, void *paths, int media)
{
    Host *h = (Host *)p;
    h->output = media ? ((Paths *)paths)->media : ((Paths *)paths)->local;
    ++h->outputs;
    event(h, 'O');
}
static void moved(void *p)
{
    ++((Host *)p)->issues;
}
static int prepare(void *p)
{
    Host *h = (Host *)p;
    event(h, 'F');
    return h->prepare;
}
static int leaf(void *p)
{
    Host *h = (Host *)p;
    event(h, 'E');
    return ++h->leaf_calls == h->leaf_at;
}
static void clear_output(void *p)
{
    Host *h = (Host *)p;
    h->output = 0;
    ++h->cleared;
    event(h, 'Z');
}
static ThreeMMSourceServices init(Host *h)
{
    ThreeMMSourceOwners owners;
    ThreeMMStorageAllocator a;
    ThreeMMSourceServices s;
    memset(h, 0, sizeof(*h));
    active = h;
    h->prepare = 1;
    owners.context = h;
    owners.new_paths = new_paths;
    owners.copy_paths = copy_paths;
    owners.release_paths = release_paths;
    owners.release_manager = release_manager;
    a.context = h;
    a.resize = resize;
    a.release = release_memory;
    CHECK(three_mm_source_init(&h->catalogue, &owners, &a) == THREE_MM_STORAGE_OK);
    memset(&s, 0, sizeof(s));
    s.context = h;
    s.access = access_source;
    s.path_empty = empty;
    s.clear_local = clear_local;
    s.build_local = build_local;
    s.archive_source = archives;
    s.content = content;
    s.find_media = find_media;
    s.retry = retry;
    s.new_manager = new_manager;
    s.output = output;
    s.moved_open_source = moved;
    s.prepare_file = prepare;
    s.find_leaf = leaf;
    s.clear_output = clear_output;
    return s;
}
static void finish(Host *h)
{
    CHECK(three_mm_source_dispose(&h->catalogue) == THREE_MM_STORAGE_OK);
    CHECK(!h->paths && !h->managers && !h->allocations && !h->catalogue.count);
    CHECK(three_mm_source_dispose(&h->catalogue) == THREE_MM_STORAGE_OK);
}
static void lookup_cases(void)
{
    Host h;
    ThreeMMSourceServices s;
    Paths work;
    ThreeMMSourceRecord record;
    int exists;
    s = init(&h);
    h.access = THREE_MM_SOURCE_DISABLED;
    h.retry = 1;
    CHECK(!three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) && !h.catalogue.count && !h.retries);
    CHECK(!three_mm_source_manager(&h.catalogue, 42, 0, &work, &s) && !h.catalogue.count && h.retries == 1);
    finish(&h);
    s = init(&h);
    CHECK(!three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) && h.catalogue.count == 1 && !h.retries &&
          h.builds == 1);
    CHECK(!three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) && h.builds == 1);
    h.install = 1;
    reset(&h);
    CHECK(three_mm_source_manager(&h.catalogue, 42, 0, &work, &s) == &h && h.retries == 1 && h.builds == 2 &&
          h.news == 1);
    CHECK(!strcmp(h.log, "ADRXBHCL"));
    reset(&h);
    CHECK(three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) == &h && !strcmp(h.log, "A"));
    h.access = THREE_MM_SOURCE_MISSING;
    reset(&h);
    CHECK(!three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) && !strcmp(h.log, "A"));
    CHECK(h.managers == 1);
    CHECK(three_mm_source_invalidate(&h.catalogue, 42) == THREE_MM_STORAGE_OK && !h.paths && !h.managers &&
          !h.catalogue.count);
    finish(&h);
    s = init(&h);
    h.media = 1;
    CHECK(three_mm_source_manager(&h.catalogue, 42, 0, &work, &s) == &h && !strcmp(h.log, "ABDM"));
    reset(&h);
    CHECK(three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) == &h && !strcmp(h.log, "A"));
    h.media = 0;
    reset(&h);
    CHECK(!three_mm_source_manager(&h.catalogue, 42, 0, &work, &s) && !strcmp(h.log, "ADR") && h.managers == 1);
    finish(&h);
    s = init(&h);
    h.exists = 1;
    h.archive = 1;
    h.fail_probe = 1;
    CHECK(three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) == &h && !h.probes && !strcmp(h.log, "ABHL"));
    finish(&h);
    s = init(&h);
    h.exists = 1;
    h.fail_probe = 1;
    exists = 99;
    CHECK(!three_mm_source_local_content(&h.catalogue, 42, &work, &exists, &s) && exists == 99);
    CHECK(three_mm_source_at(&h.catalogue, 0, &record) == THREE_MM_STORAGE_OK && record.local_content == 2 &&
          ((Paths *)record.paths)->local == 10);
    h.fail_probe = 0;
    h.content = 1;
    CHECK(three_mm_source_local_content(&h.catalogue, 42, &work, &exists, &s) && exists == 1 && h.builds == 1);
    finish(&h);
    s = init(&h);
    h.fail_path = 1;
    CHECK(!three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) && !h.paths && !h.catalogue.count);
    h.fail_path = 0;
    h.fail_alloc = 1;
    CHECK(!three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) && !h.paths && !h.catalogue.count);
    h.fail_alloc = 0;
    h.exists = h.content = 1;
    h.fail_manager = 1;
    CHECK(!three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) && h.catalogue.count == 1 && !h.managers);
    h.fail_manager = 0;
    CHECK(three_mm_source_manager(&h.catalogue, 42, 1, &work, &s) == &h && h.builds == 1);
    finish(&h);
}
static void paths_cases(void)
{
    Host h;
    ThreeMMSourceServices s;
    Paths work;
    int i;
    s = init(&h);
    h.access = THREE_MM_SOURCE_AVAILABLE;
    h.output = 99;
    CHECK(three_mm_source_media_path(&h.catalogue, 42, 1, &work, &s) && h.output == 99 && !h.outputs &&
          !h.catalogue.count);
    CHECK(three_mm_source_local_path(&h.catalogue, 42, &work, &s) && h.output == 77 && h.outputs == 1 &&
          !h.catalogue.count);
    finish(&h);
    s = init(&h);
    h.exists = 1;
    h.media = 1;
    h.leaf_at = 2;
    CHECK(three_mm_source_find_file(&h.catalogue, 42, 0, &work, &s) && h.output == 20 && h.leaf_calls == 2 &&
          !h.cleared);
    finish(&h);
    s = init(&h);
    h.media = 1;
    CHECK(!three_mm_source_find_file(&h.catalogue, 42, 1, &work, &s) && !h.leaf_calls && !h.retries);
    finish(&h);
    s = init(&h);
    h.exists = h.media = 1;
    CHECK(!three_mm_source_find_file(&h.catalogue, 42, 0, &work, &s) && h.cleared == 1 && h.output == 0 &&
          h.leaf_calls == 2);
    finish(&h);
    s = init(&h);
    h.exists = h.content = 1;
    for (i = 0; i < 8; ++i)
        CHECK(three_mm_source_manager(&h.catalogue, i + 1, 1, &work, &s) == &h);
    CHECK(h.managers == 8 && h.catalogue.count == 8);
    CHECK(three_mm_source_invalidate(&h.catalogue, 3) == THREE_MM_STORAGE_OK && h.catalogue.count == 7 &&
          h.managers == 7);
    CHECK(three_mm_storage_lock(&h.catalogue.storage, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_source_invalidate(&h.catalogue, 1) == THREE_MM_STORAGE_LOCKED && h.catalogue.count == 7);
    CHECK(three_mm_source_dispose(&h.catalogue) == THREE_MM_STORAGE_LOCKED && h.managers == 7);
    CHECK(three_mm_storage_unlock(&h.catalogue.storage, 0) == THREE_MM_STORAGE_OK);
    reset(&h);
    finish(&h);
    CHECK(!strcmp(h.log, "mmmmmmmppppppp"));
}
static void *create(void *p)
{
    Host *h = (Host *)p;
    event(h, 'N');
    if (h->create_fail)
        return NULL;
    ++h->managers;
    return h;
}
static int begin(void *p)
{
    Host *h = (Host *)p;
    ++h->begins;
    event(h, 'I');
    return !h->begin_fail;
}
static int next(void *p, ThreeMMI32 sid, ThreeMMU32 index, int archive)
{
    Host *h = (Host *)p;
    CHECK(sid == 42 && archive == h->archive);
    ++h->next_calls;
    event(h, 'Q');
    return index < 2;
}
static void *open_file(void *p)
{
    Host *h = (Host *)p;
    event(h, 'O');
    if (h->open_fail)
        return NULL;
    ++h->files;
    return h;
}
static int add(void *p, void *file, ThreeMMI32 budget)
{
    Host *h = (Host *)p;
    CHECK(file == h && budget == 128);
    event(h, 'A');
    if (h->add_fail)
        return 0;
    ++h->added;
    return 1;
}
static void release_file(void *p)
{
    Host *h = (Host *)p;
    CHECK(h->files == 1);
    --h->files;
    event(h, 'f');
}
static void build_cases(void)
{
    Host h;
    ThreeMMSourceBuildServices s;
    ThreeMMSourceServices unused;
    void *manager;
    int mode;
    for (mode = 0; mode < 6; ++mode)
    {
        unused = init(&h);
        (void)unused;
        h.create_fail = mode == 1;
        h.begin_fail = mode == 2;
        h.open_fail = mode == 3;
        h.add_fail = mode == 4;
        h.archive = mode == 5;
        s.context = &h;
        s.create = create;
        s.archive_source = archives;
        s.begin = begin;
        s.next = next;
        s.open = open_file;
        s.add = add;
        s.release_file = release_file;
        s.release_manager = release_manager;
        manager = three_mm_source_build(42, 128, &s);
        CHECK((manager != NULL) == (mode == 0 || mode == 5) && !h.files);
        if (manager)
        {
            CHECK(h.added == 2 && h.next_calls == 3 && h.begins == !h.archive);
            release_manager(manager);
        }
        else
            CHECK(!h.managers);
        if (mode == 4)
            CHECK(!strcmp(h.log, "NHIQOAfm"));
        finish(&h);
    }
}
int main(void)
{
    lookup_cases();
    paths_cases();
    build_cases();
    puts("source C89 checks passed");
    return 0;
}

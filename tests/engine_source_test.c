#include "mm/source.h"
#include "mm/movie_source_ids.h"
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
struct Host;
typedef struct BuildCache
{
    struct Host *host;
    MMResourceCache cache;
    MMReference references;
} BuildCache;
typedef struct Host
{
    MMSourceCatalogue catalogue;
    int access, exists, content, archive, media, retry, install, fail_path, fail_alloc, fail_build, fail_probe,
        fail_manager;
    int paths, managers, allocations, probes, builds, retries, news, outputs, output, leaf_calls, leaf_at, prepare,
        cleared, issues;
    int create_fail, begin_fail, open_failure_at, add_fail, file_count, next_calls, begins;
    BuildCache files[2];
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
static void *resize(void *p, void *old, MMU32 before, MMU32 bytes, int priority)
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
static void release_memory(void *p, void *v, MMU32 bytes)
{
    Host *h = (Host *)p;
    (void)bytes;
    --h->allocations;
    free(v);
}
static int access_source(void *p, MMI32 sid, void *paths, int kind)
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
static int build_local(void *p, MMI32 sid, void *paths, int *exists)
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
static int archives(void *p, MMI32 sid)
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
static int find_media(void *p, MMI32 sid, void *paths, int *changed)
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
static int retry(void *p, MMI32 sid)
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
static void *new_manager(void *p, MMI32 sid, void *paths, int media)
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
static MMSourceServices init(Host *h)
{
    MMSourceOwners owners;
    MMStorageAllocator a;
    MMSourceServices s;
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
    CHECK(mm_source_init(&h->catalogue, &owners, &a) == MM_STORAGE_OK);
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
    CHECK(mm_source_dispose(&h->catalogue) == MM_STORAGE_OK);
    CHECK(!h->paths && !h->managers && !h->allocations && !h->catalogue.count);
    CHECK(mm_source_dispose(&h->catalogue) == MM_STORAGE_OK);
}
static void lookup_cases(void)
{
    Host h;
    MMSourceServices s;
    Paths work;
    MMSourceRecord record;
    int exists;
    s = init(&h);
    h.access = MM_SOURCE_DISABLED;
    h.retry = 1;
    CHECK(!mm_source_manager(&h.catalogue, 42, 1, &work, &s) && !h.catalogue.count && !h.retries);
    CHECK(!mm_source_manager(&h.catalogue, 42, 0, &work, &s) && !h.catalogue.count && h.retries == 1);
    finish(&h);
    s = init(&h);
    CHECK(!mm_source_manager(&h.catalogue, 42, 1, &work, &s) && h.catalogue.count == 1 && !h.retries &&
          h.builds == 1);
    CHECK(!mm_source_manager(&h.catalogue, 42, 1, &work, &s) && h.builds == 1);
    h.install = 1;
    reset(&h);
    CHECK(mm_source_manager(&h.catalogue, 42, 0, &work, &s) == &h && h.retries == 1 && h.builds == 2 &&
          h.news == 1);
    CHECK(!strcmp(h.log, "ADRXBHCL"));
    reset(&h);
    CHECK(mm_source_manager(&h.catalogue, 42, 1, &work, &s) == &h && !strcmp(h.log, "A"));
    h.access = MM_SOURCE_MISSING;
    reset(&h);
    CHECK(!mm_source_manager(&h.catalogue, 42, 1, &work, &s) && !strcmp(h.log, "A"));
    CHECK(h.managers == 1);
    CHECK(mm_source_invalidate(&h.catalogue, 42) == MM_STORAGE_OK && !h.paths && !h.managers &&
          !h.catalogue.count);
    finish(&h);
    s = init(&h);
    h.media = 1;
    CHECK(mm_source_manager(&h.catalogue, 42, 0, &work, &s) == &h && !strcmp(h.log, "ABDM"));
    reset(&h);
    CHECK(mm_source_manager(&h.catalogue, 42, 1, &work, &s) == &h && !strcmp(h.log, "A"));
    h.media = 0;
    reset(&h);
    CHECK(!mm_source_manager(&h.catalogue, 42, 0, &work, &s) && !strcmp(h.log, "ADR") && h.managers == 1);
    finish(&h);
    s = init(&h);
    h.exists = 1;
    h.archive = 1;
    h.fail_probe = 1;
    CHECK(mm_source_manager(&h.catalogue, 42, 1, &work, &s) == &h && !h.probes && !strcmp(h.log, "ABHL"));
    finish(&h);
    s = init(&h);
    h.exists = 1;
    h.fail_probe = 1;
    exists = 99;
    CHECK(!mm_source_local_content(&h.catalogue, 42, &work, &exists, &s) && exists == 99);
    CHECK(mm_source_at(&h.catalogue, 0, &record) == MM_STORAGE_OK && record.local_content == 2 &&
          ((Paths *)record.paths)->local == 10);
    h.fail_probe = 0;
    h.content = 1;
    CHECK(mm_source_local_content(&h.catalogue, 42, &work, &exists, &s) && exists == 1 && h.builds == 1);
    finish(&h);
    s = init(&h);
    h.fail_path = 1;
    CHECK(!mm_source_manager(&h.catalogue, 42, 1, &work, &s) && !h.paths && !h.catalogue.count);
    h.fail_path = 0;
    h.fail_alloc = 1;
    CHECK(!mm_source_manager(&h.catalogue, 42, 1, &work, &s) && !h.paths && !h.catalogue.count);
    h.fail_alloc = 0;
    h.exists = h.content = 1;
    h.fail_manager = 1;
    CHECK(!mm_source_manager(&h.catalogue, 42, 1, &work, &s) && h.catalogue.count == 1 && !h.managers);
    h.fail_manager = 0;
    CHECK(mm_source_manager(&h.catalogue, 42, 1, &work, &s) == &h && h.builds == 1);
    finish(&h);
}
static void paths_cases(void)
{
    Host h;
    MMSourceServices s;
    Paths work;
    int i;
    s = init(&h);
    h.access = MM_SOURCE_AVAILABLE;
    h.output = 99;
    CHECK(mm_source_media_path(&h.catalogue, 42, 1, &work, &s) && h.output == 99 && !h.outputs &&
          !h.catalogue.count);
    CHECK(mm_source_local_path(&h.catalogue, 42, &work, &s) && h.output == 77 && h.outputs == 1 &&
          !h.catalogue.count);
    finish(&h);
    s = init(&h);
    h.exists = 1;
    h.media = 1;
    h.leaf_at = 2;
    CHECK(mm_source_find_file(&h.catalogue, 42, 0, &work, &s) && h.output == 20 && h.leaf_calls == 2 &&
          !h.cleared);
    finish(&h);
    s = init(&h);
    h.media = 1;
    CHECK(!mm_source_find_file(&h.catalogue, 42, 1, &work, &s) && !h.leaf_calls && !h.retries);
    finish(&h);
    s = init(&h);
    h.exists = h.media = 1;
    CHECK(!mm_source_find_file(&h.catalogue, 42, 0, &work, &s) && h.cleared == 1 && h.output == 0 &&
          h.leaf_calls == 2);
    finish(&h);
    s = init(&h);
    h.exists = h.content = 1;
    for (i = 0; i < 8; ++i)
        CHECK(mm_source_manager(&h.catalogue, i + 1, 1, &work, &s) == &h);
    CHECK(h.managers == 8 && h.catalogue.count == 8);
    CHECK(mm_source_invalidate(&h.catalogue, 3) == MM_STORAGE_OK && h.catalogue.count == 7 &&
          h.managers == 7);
    CHECK(mm_storage_lock(&h.catalogue.storage, 0) == MM_STORAGE_OK);
    CHECK(mm_source_invalidate(&h.catalogue, 1) == MM_STORAGE_LOCKED && h.catalogue.count == 7);
    CHECK(mm_source_dispose(&h.catalogue) == MM_STORAGE_LOCKED && h.managers == 7);
    CHECK(mm_storage_unlock(&h.catalogue.storage, 0) == MM_STORAGE_OK);
    reset(&h);
    finish(&h);
    CHECK(!strcmp(h.log, "mmmmmmmppppppp"));
}
static void *allocate_manager(void *p, void *old, MMU32 before, MMU32 bytes, int priority)
{
    Host *h = (Host *)p;
    event(h, 'N');
    if (h->create_fail)
        return NULL;
    ++h->managers;
    return resize(p, old, before, bytes, priority);
}
static void free_manager(void *p, void *data, MMU32 bytes)
{
    release_manager(p);
    release_memory(p, data, bytes);
}
static void *allocate_rows(void *p, void *old, MMU32 before, MMU32 bytes, int priority)
{
    Host *h = p;
    event(h, 'A');
    return h->add_fail ? NULL : resize(p, old, before, bytes, priority);
}
static int begin(void *p)
{
    Host *h = (Host *)p;
    ++h->begins;
    event(h, 'I');
    return !h->begin_fail;
}
static int next(void *p, MMI32 sid, MMU32 index, int archive)
{
    Host *h = (Host *)p;
    CHECK(sid == 42 && archive == h->archive);
    ++h->next_calls;
    event(h, 'Q');
    return index < 2;
}
static void release_file(void *p)
{
    BuildCache *file = p;
    Host *h = file->host;
    CHECK(h->file_count > 0);
    --h->file_count;
    event(h, 'f');
}
static MMResourceCache *open_file(void *p, MMI32 budget)
{
    Host *h = (Host *)p;
    BuildCache *file;
    CHECK(budget == 128);
    event(h, 'O');
    if (h->open_failure_at == h->next_calls)
        return NULL;
    CHECK(h->next_calls >= 1 && h->next_calls <= 2);
    file = &h->files[h->next_calls - 1];
    file->host = h;
    file->cache.owner = file;
    file->cache.references = &file->references;
    file->cache.services.destroy_owner = release_file;
    mm_reference_init(&file->references);
    ++h->file_count;
    return &file->cache;
}
static void build_cases(void)
{
    Host h;
    MMSourceBuildServices s;
    MMResourceManager *manager;
    MMStorageAllocator owner = {NULL, allocate_manager, free_manager};
    MMStorageAllocator rows = {NULL, allocate_rows, release_memory};
    int mode;
    owner.context = rows.context = &h;
    for (mode = 0; mode < 7; ++mode)
    {
        init(&h);
        h.create_fail = mode == 1;
        h.begin_fail = mode == 2;
        h.open_failure_at = mode == 3 ? 1 : mode == 6 ? 2 : 0;
        h.add_fail = mode == 4;
        h.archive = mode == 5;
        s.context = &h;
        s.archive_source = archives;
        s.begin = begin;
        s.next = next;
        s.open = open_file;
        manager = mm_source_build(42, 128, &s, &owner, &rows);
        CHECK((manager != NULL) == (mode == 0 || mode == 5));
        if (manager)
        {
            CHECK(manager->count == 2 && h.file_count == 2 && h.next_calls == 3 && h.begins == !h.archive);
            CHECK(mm_resource_manager_at(manager, 0) == &h.files[0].cache && h.files[0].references.count == 1);
            CHECK(mm_resource_manager_at(manager, 1) == &h.files[1].cache && h.files[1].references.count == 1);
            mm_resource_manager_destroy(manager);
        }
        CHECK(!h.managers && !h.file_count);
        if (mode == 4)
            CHECK(!strcmp(h.log, "NHIQOAAfm"));
        if (mode == 6)
            CHECK(!strcmp(h.log, "NHIQOAQOfm"));
        finish(&h);
    }
}
static int released_archives;
static void release_archive(void *archive)
{
    CHECK(*(int *)archive == 42);
    ++released_archives;
}

static void generated_sources(void)
{
    MMMovieSourceIds sources, reopened;
    MMStorageAllocator allocator = {NULL, resize, release_memory};
    Host h;
    MMI32 id;
    int archive = 42, duplicate = 99;
    void *found;
    memset(&h, 0, sizeof(h));
    allocator.context = &h;
    released_archives = 0;
    CHECK(mm_movie_source_ids_init(&sources, &allocator));
    CHECK(mm_movie_source_ids_init(&reopened, NULL));
    CHECK(mm_movie_source_ids_retail_alias(&sources, &id) && id == 2);
    h.fail_alloc = 1;
    CHECK(mm_movie_source_ids_add(&sources, 7, &archive) == MM_MOVIE_SOURCE_ID_MEMORY && !sources.count);
    CHECK(!mm_movie_source_ids_find(&sources, 7, NULL) && !released_archives);
    h.fail_alloc = 0;
    CHECK(mm_movie_source_ids_add(&sources, 7, NULL) == MM_MOVIE_SOURCE_ID_ADDED);
    CHECK(mm_movie_source_ids_retail_alias(&sources, &id) && id == 8);
    CHECK(mm_movie_source_ids_add(&sources, 0x7ffffffeL, NULL) == MM_MOVIE_SOURCE_ID_ADDED);
    CHECK(mm_movie_source_ids_retail_alias(&sources, &id) && id == 0x7fffffffL);
    CHECK(mm_movie_source_ids_add(&sources, 0x7fffffffL, NULL) == MM_MOVIE_SOURCE_ID_ADDED);
    CHECK(mm_movie_source_ids_add(&sources, 2, NULL) == MM_MOVIE_SOURCE_ID_ADDED);
    CHECK(mm_movie_source_ids_retail_alias(&sources, &id) && id == 3);
    CHECK(mm_movie_source_ids_take(&sources, 0x7ffffffdL, &id) && id == 0x7ffffffcL);
    CHECK(mm_movie_source_ids_add(&sources, id, &archive) == MM_MOVIE_SOURCE_ID_ADDED);
    CHECK(mm_movie_source_ids_add(&sources, id, &duplicate) == MM_MOVIE_SOURCE_ID_KNOWN);
    CHECK(mm_movie_source_ids_find(&sources, id, &found) && found == &archive);

    mm_movie_source_ids_limit(&reopened, sources.next);
    CHECK(mm_movie_source_ids_take(&reopened, 2, &id) && id == 0x7ffffffbL);
    mm_movie_source_ids_swap(&sources, &reopened);
    mm_movie_source_ids_clear(&reopened, release_archive);
    CHECK(released_archives == 1 && !h.allocations);
    CHECK(mm_movie_source_ids_take(&sources, 2, &id) && id == 0x7ffffffaL);
    mm_movie_source_ids_limit(&sources, 4);
    CHECK(mm_movie_source_ids_add(&sources, 4, NULL) == MM_MOVIE_SOURCE_ID_ADDED);
    CHECK(!mm_movie_source_ids_take(&sources, 3, &id) && id == 0x7ffffffaL);
    CHECK(mm_movie_source_ids_take(&sources, 2, &id) && id == 3);
    mm_movie_source_ids_clear(&sources, release_archive);
    CHECK(!mm_movie_source_ids_take(&sources, 2, &id) && released_archives == 1);
}

int main(void)
{
    lookup_cases();
    paths_cases();
    build_cases();
    generated_sources();
    puts("source C89 checks passed");
    return 0;
}

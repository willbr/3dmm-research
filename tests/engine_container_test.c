#include "mm/chunk_context.h"
#include "mm/chunk_owner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "container failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Host Host;
typedef struct Token
{
    int refs;
} Token;
typedef struct Owner
{
    MMChunkReopenState state;
    MMChunkRuntime runtime;
    Host *host;
} Owner;
struct Host
{
    Token main_file, extra_file, index;
    MMStorageAllocator allocator;
    int live;
    char log[128];
    int logs;
    void *runtime_buffer;
};
static void event(Host *h, char c)
{
    CHECK(h->logs < 127);
    h->log[h->logs++] = c;
    h->log[h->logs] = 0;
}
static void *resize(void *c, void *old, MMU32 before, MMU32 bytes, int priority)
{
    Host *h = (Host *)c;
    void *p;
    int had = old != NULL;
    (void)before;
    (void)priority;
    p = realloc(old, bytes);
    CHECK(p != NULL);
    if (!had)
        ++h->live;
    return p;
}
static void release_memory(void *c, void *p, MMU32 bytes)
{
    Host *h = (Host *)c;
    (void)bytes;
    CHECK(h->live > 0);
    if (p == h->runtime_buffer)
    {
        CHECK(!h->main_file.refs && !h->extra_file.refs && !h->index.refs);
        event(h, 'r');
        h->runtime_buffer = NULL;
    }
    --h->live;
    free(p);
}
static Owner *make_owner(Host *h)
{
    Owner *o = (Owner *)calloc(1, sizeof(*o));
    CHECK(o != NULL);
    o->host = h;
    CHECK(mm_chunk_runtime_init(&o->runtime, &h->allocator) == MM_CHUNK_OK);
    return o;
}
static void cleared(Owner *o)
{
    CHECK(!o->state.main_file && !o->state.extra_file && !o->state.index && !o->state.main_map && !o->state.extra_map &&
          !o->state.main_end && !o->state.extra_end);
}
static void dispose_file(void *c, void *p)
{
    Owner *o = (Owner *)c;
    cleared(o);
    CHECK(((Token *)p)->refs > 0);
    --((Token *)p)->refs;
    event(o->host, p == &o->host->main_file ? 'f' : 'e');
}
static void dispose_index(void *c, void *p)
{
    Owner *o = (Owner *)c;
    cleared(o);
    CHECK(((Token *)p)->refs == 1);
    --((Token *)p)->refs;
    event(o->host, 'j');
}
static void publish(void *c, const MMChunkReopenState *state)
{
    Owner *o = (Owner *)c;
    CHECK(state == &o->state);
    cleared(o);
    event(o->host, 'P');
}
static MMChunkDisposal disposal(Owner *o)
{
    MMChunkDisposal s;
    s.context = o;
    s.release_file = dispose_file;
    s.release_index = dispose_index;
    s.publish = publish;
    return s;
}
static void init(Host *h)
{
    memset(h, 0, sizeof(*h));
    h->allocator.context = h;
    h->allocator.resize = resize;
    h->allocator.release = release_memory;
}
static void collision(void *p)
{
    ++*(int *)p;
}
static void lookup(void)
{
    MMChunkContext registry = {0};
    MMChunkLink links[3];
    MMChunkOwner owners[3] = {0};
    MMChunkOwnerStorage storage[3] = {0};
    MMChunkOwnerRead readers[3] = {0};
    MMReference references[3] = {0};
    int collisions[3] = {0};
    int a, b, c, i;
    for (i = 2; i >= 0; --i)
    {
        storage[i].owner = owners + i;
        readers[i].storage = storage + i;
        mm_chunk_link_init(links + i, collisions + i, readers + i, references + i);
        CHECK(mm_chunk_context_attach(&registry, links + i) == MM_CHUNK_OK);
    }
    owners[0].files.main_file = &a;
    owners[0].files.invalid_main = 1;
    owners[1].files.main_file = &b;
    owners[1].files.extra_file = &a;
    owners[2].files.main_file = &a;
    CHECK(mm_chunk_context_find(&registry, &a, collision) == links + 2 && collisions[1] == 1);
    CHECK(mm_chunk_context_find(&registry, &b, collision) == links + 1 && collisions[1] == 1);
    CHECK(!mm_chunk_context_find(&registry, &c, collision) && !mm_chunk_context_find(&registry, NULL, collision));
    CHECK(mm_chunk_context_find(&registry, &a, NULL) == links + 2);
    owners[2].files.main_file = &c;
    CHECK(!mm_chunk_context_find(&registry, &a, collision) && collisions[1] == 2);
    CHECK(mm_chunk_context_find(&registry, &c, collision) == links + 2);
    for (i = 0; i < 3; ++i)
        CHECK(mm_chunk_context_detach(links + i) == MM_CHUNK_OK);
}
static void dispose_cases(void)
{
    Host h;
    Owner *o;
    MMChunkDisposal s;
    MMChunkReopenState before;
    int mode;
    for (mode = 0; mode < 5; ++mode)
    {
        init(&h);
        o = make_owner(&h);
        s = disposal(o);
        h.main_file.refs = 1;
        h.index.refs = 1;
        o->state.main_file = &h.main_file;
        o->state.index = &h.index;
        if (mode == 1)
        {
            ++h.main_file.refs;
            o->state.extra_file = &h.main_file;
        }
        else
        {
            h.extra_file.refs = 1;
            o->state.extra_file = &h.extra_file;
        }
        CHECK(mm_free_map_create(&h.allocator, 1, &o->state.main_map) == MM_CHUNK_OK &&
              mm_free_map_create(&h.allocator, 1, &o->state.extra_map) == MM_CHUNK_OK);
        if (mode != 2)
        {
            CHECK(mm_chunk_runtime_set(&o->runtime, 1, 1, 11) == MM_CHUNK_OK);
            h.runtime_buffer = o->runtime.storage.buffers[0].data;
        }
        if (mode == 3)
            o->runtime.storage.buffers[0].locks = 1;
        if (mode == 4)
            o->state.main_map->storage.buffers[0].locks = 1;
        before = o->state;
        if (mode >= 3)
        {
            CHECK(mm_chunk_dispose(&o->state, &o->runtime, &s) == MM_CHUNK_BAD_FILE && !h.logs &&
                  o->state.main_file == before.main_file && o->state.main_map == before.main_map &&
                  h.main_file.refs == 1 && h.index.refs == 1);
            o->runtime.storage.buffers[0].locks = 0;
            o->state.main_map->storage.buffers[0].locks = 0;
        }
        CHECK(mm_chunk_dispose(&o->state, mode == 2 ? NULL : &o->runtime, &s) == MM_CHUNK_OK);
        CHECK(!strcmp(h.log, mode == 1   ? "Pffjr"
                             : mode == 2 ? "Pfej"
                                         : "Pfejr") &&
              !h.live && !h.main_file.refs && !h.extra_file.refs && !h.index.refs);
        h.logs = 0;
        h.log[0] = 0;
        CHECK(mm_chunk_dispose(&o->state, &o->runtime, &s) == MM_CHUNK_OK && !strcmp(h.log, "P"));
        free(o);
    }
}
static void context_cases(void)
{
    MMChunkContext a, b;
    MMChunkLink links[4];
    MMChunkOwner owners[4] = {0};
    MMChunkOwnerStorage storage[4] = {0};
    MMChunkOwnerRead readers[4] = {0};
    MMReference references[4] = {0};
    MMChunkLink **saved;
    int values[4], i;
    mm_chunk_context_init(&a);
    mm_chunk_context_init(&b);
    CHECK(!mm_chunk_context_first(&a) && !mm_chunk_context_first(NULL));
    for (i = 0; i < 4; ++i)
    {
        storage[i].owner = owners + i;
        readers[i].storage = storage + i;
        mm_chunk_link_init(links + i, values + i, readers + i, references + i);
    }
    for (i = 0; i < 3; ++i)
        CHECK(mm_chunk_context_attach(&a, links + i) == MM_CHUNK_OK);
    CHECK(mm_chunk_context_first(&a) == values + 2 && mm_chunk_context_next(links + 2) == values + 1);
    CHECK(mm_chunk_context_attach(&b, links + 1) == MM_CHUNK_BAD_FILE && !b.first);
    CHECK(mm_chunk_context_detach(links + 1) == MM_CHUNK_OK);
    CHECK(mm_chunk_context_next(links + 2) == values && mm_chunk_context_valid(links));
    CHECK(mm_chunk_context_detach(links + 1) == MM_CHUNK_OK);
    CHECK(mm_chunk_context_attach(&b, links + 1) == MM_CHUNK_OK);
    CHECK(mm_chunk_context_attach(&b, links + 3) == MM_CHUNK_OK);
    saved = links[1].previous;
    links[1].previous = &a.first;
    CHECK(!mm_chunk_context_valid(links + 3));
    links[1].previous = saved;
    CHECK(mm_chunk_context_detach(links) == MM_CHUNK_OK);
    CHECK(!mm_chunk_context_next(links + 2));
    CHECK(mm_chunk_context_detach(links + 2) == MM_CHUNK_OK && !a.first);
    CHECK(mm_chunk_context_detach(links + 3) == MM_CHUNK_OK && b.first == links + 1);
    CHECK(mm_chunk_context_detach(links + 1) == MM_CHUNK_OK && !b.first);
    for (i = 0; i < 4; ++i)
        CHECK(mm_chunk_context_valid(links + i));
}
int main(void)
{
    lookup();
    dispose_cases();
    context_cases();
    return 0;
}

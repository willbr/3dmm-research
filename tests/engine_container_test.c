#include "three_mm/chunk_container.h"
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
    ThreeMMChunkReopenState state;
    ThreeMMChunkRuntime runtime;
    int refs;
    Host *host;
} Owner;
struct Host
{
    Owner *existing;
    Token main_file, extra_file, index;
    ThreeMMStorageAllocator allocator;
    int shells, live, fail, issues[4], created, finished;
    ThreeMMU32 file_flags, options;
    char log[128];
    int logs;
    void *runtime_buffer;
};
enum
{
    FAIL_ALLOC = 1,
    FAIL_INDEX,
    FAIL_FILE,
    FAIL_RESIZE,
    FAIL_READ,
    FAIL_VALIDATE,
    FAIL_OPTIONS
};
static void event(Host *h, char c)
{
    CHECK(h->logs < 127);
    h->log[h->logs++] = c;
    h->log[h->logs] = 0;
}
static void *resize(void *c, void *old, ThreeMMU32 before, ThreeMMU32 bytes, int priority)
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
static void release_memory(void *c, void *p, ThreeMMU32 bytes)
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
    o->refs = 1;
    ++h->shells;
    CHECK(three_mm_chunk_runtime_init(&o->runtime, &h->allocator) == THREE_MM_CHUNK_OK);
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
static void publish(void *c, const ThreeMMChunkReopenState *state)
{
    Owner *o = (Owner *)c;
    CHECK(state == &o->state);
    cleared(o);
    event(o->host, 'P');
}
static ThreeMMChunkDisposal disposal(Owner *o)
{
    ThreeMMChunkDisposal s;
    s.context = o;
    s.release_file = dispose_file;
    s.release_index = dispose_index;
    s.publish = publish;
    return s;
}
static void drop(void *c, void *owner)
{
    Host *h = (Host *)c;
    Owner *o = (Owner *)owner;
    ThreeMMChunkDisposal s;
    CHECK(o->refs > 0);
    event(h, 'D');
    if (--o->refs)
        return;
    s = disposal(o);
    CHECK(three_mm_chunk_dispose(&o->state, &o->runtime, &s) == THREE_MM_CHUNK_OK);
    if (h->existing == o)
        h->existing = NULL;
    --h->shells;
    free(o);
    event(h, 'X');
}
static void *find(void *c)
{
    Host *h = (Host *)c;
    event(h, 'F');
    return h->existing;
}
static void *allocate(void *c)
{
    Host *h = (Host *)c;
    event(h, 'A');
    return h->fail == FAIL_ALLOC ? NULL : make_owner(h);
}
static void retain(void *c, void *owner)
{
    Host *h = (Host *)c;
    Owner *o = (Owner *)owner;
    CHECK(o == h->existing && o->refs == 1);
    ++o->refs;
    event(h, '+');
}
static void *file(void *c, int create, ThreeMMU32 flags)
{
    Host *h = (Host *)c;
    event(h, 'O');
    h->created = create;
    h->file_flags = flags;
    if (h->fail == FAIL_FILE)
        return NULL;
    ++h->main_file.refs;
    return &h->main_file;
}
static void *index(void *c)
{
    Host *h = (Host *)c;
    event(h, 'I');
    if (h->fail == FAIL_INDEX)
        return NULL;
    ++h->index.refs;
    return &h->index;
}
static void attach_file(void *c, void *owner, void *p)
{
    Host *h = (Host *)c;
    Owner *o = (Owner *)owner;
    CHECK(!o->state.main_file);
    o->state.main_file = p;
    event(h, 'o');
}
static void attach_index(void *c, void *owner, void *p)
{
    Host *h = (Host *)c;
    Owner *o = (Owner *)owner;
    CHECK(!o->state.index);
    o->state.index = p;
    event(h, 'i');
}
static void set_end(void *c, void *owner, ThreeMMU32 end)
{
    Host *h = (Host *)c;
    Owner *o = (Owner *)owner;
    CHECK(end == 128 && o->state.main_file && o->state.index);
    o->state.main_end = end;
    event(h, 'E');
}
static int resize_file(void *c, void *p, ThreeMMU32 end)
{
    Host *h = (Host *)c;
    CHECK(p == &h->main_file && end == 128);
    event(h, 'Z');
    return h->fail != FAIL_RESIZE;
}
static int read_index(void *c, void *owner)
{
    Host *h = (Host *)c;
    Owner *o = (Owner *)owner;
    CHECK(o->state.main_file && !o->state.index);
    event(h, 'R');
    ++h->index.refs;
    o->state.index = &h->index;
    o->state.main_end = 220;
    CHECK(three_mm_free_map_create(&h->allocator, 1, &o->state.main_map) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_runtime_set(&o->runtime, 1, 1, 44) == THREE_MM_CHUNK_OK);
    h->runtime_buffer = o->runtime.storage.buffers[0].data;
    return h->fail != FAIL_READ;
}
static int validate(void *c, void *owner)
{
    Host *h = (Host *)c;
    CHECK(((Owner *)owner)->state.index == &h->index);
    event(h, 'V');
    return h->fail != FAIL_VALIDATE;
}
static int options(void *c, void *owner, ThreeMMU32 flags)
{
    Host *h = (Host *)c;
    CHECK(((Owner *)owner)->refs == 1);
    h->options = flags;
    event(h, 'S');
    return h->fail != FAIL_OPTIONS;
}
static void issue(void *c, int kind)
{
    Host *h = (Host *)c;
    CHECK(kind >= 0 && kind < 4);
    ++h->issues[kind];
    event(h, kind == THREE_MM_CONTAINER_ALREADY_OPEN ? '=' : kind == THREE_MM_CONTAINER_INITIAL_OPTIONS ? '@' : '!');
}
static void finish(void *c, void *owner, int create)
{
    Host *h = (Host *)c;
    Owner *o = (Owner *)owner;
    CHECK(o->refs == 1 && o->state.main_file && o->state.index && o->state.main_end == (create ? 128UL : 220UL));
    ++h->finished;
    event(h, 'N');
}
static void init(Host *h, ThreeMMChunkConstruction *s)
{
    memset(h, 0, sizeof(*h));
    memset(s, 0, sizeof(*s));
    h->allocator.context = h;
    h->allocator.resize = resize;
    h->allocator.release = release_memory;
    s->context = h;
    s->find = find;
    s->allocate = allocate;
    s->retain = retain;
    s->release = drop;
    s->file = file;
    s->index = index;
    s->attach_file = attach_file;
    s->attach_index = attach_index;
    s->set_end = set_end;
    s->resize = resize_file;
    s->read_index = read_index;
    s->validate = validate;
    s->options = options;
    s->issue = issue;
    s->finish = finish;
}
static void balanced(Host *h)
{
    CHECK(!h->shells && !h->live && !h->main_file.refs && !h->extra_file.refs && !h->index.refs && !h->runtime_buffer);
}
static void construct(void)
{
    static const struct Case
    {
        int create, fail;
        const char *log;
    } cases[] = {{1, FAIL_ALLOC, "FA!"},         {1, FAIL_INDEX, "FAIiDPX!"},
                 {1, FAIL_FILE, "FAIiOoDPjX!"},  {1, FAIL_RESIZE, "FAIiOoZDPfjX!"},
                 {0, FAIL_ALLOC, "FA!"},         {0, FAIL_FILE, "FAOoDPX!"},
                 {0, FAIL_READ, "FAOoRDPfjrX!"}, {0, FAIL_VALIDATE, "FAOoRVDPfjrX!"}};
    Host h;
    ThreeMMChunkConstruction s;
    Owner *o;
    unsigned i;
    int create, fail;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        init(&h, &s);
        h.fail = cases[i].fail;
        CHECK(!three_mm_chunk_construct(&s, 0, cases[i].create));
        CHECK(!strcmp(h.log, cases[i].log));
        CHECK(h.issues[cases[i].create ? THREE_MM_CONTAINER_CREATE_FAILED : THREE_MM_CONTAINER_OPEN_FAILED] == 1);
        balanced(&h);
    }
    for (create = 0; create < 2; ++create)
        for (fail = 0; fail < 2; ++fail)
        {
            init(&h, &s);
            h.fail = fail ? FAIL_OPTIONS : 0;
            o = (Owner *)three_mm_chunk_construct(&s, create ? THREE_MM_CHUNK_TEMP : THREE_MM_CHUNK_READ_EXTRA, create);
            CHECK(o != NULL);
            CHECK(!strcmp(h.log, create ? (fail ? "FAIiOoZES@N" : "FAIiOoZESN") : (fail ? "FAOoRVS@N" : "FAOoRVSN")));
            CHECK(h.finished == 1 && h.issues[THREE_MM_CONTAINER_INITIAL_OPTIONS] == fail && h.created == create);
            CHECK(h.file_flags == (create ? 23UL : 2UL) && h.options == (create ? 3UL : 16UL));
            drop(&h, o);
            balanced(&h);
        }
    for (create = 0; create < 2; ++create)
        for (fail = 0; fail < 2; ++fail)
        {
            init(&h, &s);
            h.existing = make_owner(&h);
            h.fail = fail ? FAIL_OPTIONS : 0;
            o = (Owner *)three_mm_chunk_construct(&s, 0, create);
            if (create)
                CHECK(!o && !strcmp(h.log, "F=!") && h.existing->refs == 1 &&
                      h.issues[THREE_MM_CONTAINER_ALREADY_OPEN] == 1 &&
                      h.issues[THREE_MM_CONTAINER_CREATE_FAILED] == 1);
            else if (fail)
                CHECK(!o && !strcmp(h.log, "FS") && h.existing->refs == 1 && !h.issues[THREE_MM_CONTAINER_OPEN_FAILED]);
            else
            {
                CHECK(o == h.existing && !strcmp(h.log, "FS+") && o->refs == 2);
                drop(&h, o);
            }
            CHECK(!h.finished);
            drop(&h, h.existing);
            balanced(&h);
        }
    init(&h, &s);
    s.finish = NULL;
    o = (Owner *)three_mm_chunk_construct(&s, 0, 1);
    CHECK(o && !h.finished);
    drop(&h, o);
    balanced(&h);
}
typedef struct Link
{
    struct Link *next;
    void *main_file, *extra_file;
    int invalid, collisions;
} Link;
static void *next_link(void *p)
{
    return ((Link *)p)->next;
}
static void *main_file(void *p)
{
    return ((Link *)p)->main_file;
}
static void *extra_file(void *p)
{
    return ((Link *)p)->extra_file;
}
static int invalid(void *p)
{
    return ((Link *)p)->invalid;
}
static void collision(void *p)
{
    ++((Link *)p)->collisions;
}
static void lookup(void)
{
    Link links[3];
    int a, b, c;
    ThreeMMChunkLookup s;
    memset(links, 0, sizeof(links));
    s.next = next_link;
    s.main_file = main_file;
    s.extra_file = extra_file;
    s.invalid_main = invalid;
    s.extra_collision = collision;
    links[0].next = links + 1;
    links[0].main_file = &a;
    links[0].invalid = 1;
    links[1].next = links + 2;
    links[1].main_file = &b;
    links[1].extra_file = &a;
    links[2].main_file = &a;
    CHECK(three_mm_chunk_find_container(links, &a, &s) == links + 2 && links[1].collisions == 1);
    CHECK(three_mm_chunk_find_container(links, &b, &s) == links + 1 && links[1].collisions == 1);
    CHECK(!three_mm_chunk_find_container(links, &c, &s) && !three_mm_chunk_find_container(links, NULL, &s));
    s.extra_collision = NULL;
    CHECK(three_mm_chunk_find_container(links, &a, &s) == links + 2);
}
static void dispose_cases(void)
{
    Host h;
    ThreeMMChunkConstruction unused;
    Owner *o;
    ThreeMMChunkDisposal s;
    ThreeMMChunkReopenState before;
    int mode;
    for (mode = 0; mode < 5; ++mode)
    {
        init(&h, &unused);
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
        CHECK(three_mm_free_map_create(&h.allocator, 1, &o->state.main_map) == THREE_MM_CHUNK_OK &&
              three_mm_free_map_create(&h.allocator, 1, &o->state.extra_map) == THREE_MM_CHUNK_OK);
        if (mode != 2)
        {
            CHECK(three_mm_chunk_runtime_set(&o->runtime, 1, 1, 11) == THREE_MM_CHUNK_OK);
            h.runtime_buffer = o->runtime.storage.buffers[0].data;
        }
        if (mode == 3)
            o->runtime.storage.buffers[0].locks = 1;
        if (mode == 4)
            o->state.main_map->storage.buffers[0].locks = 1;
        before = o->state;
        if (mode >= 3)
        {
            CHECK(three_mm_chunk_dispose(&o->state, &o->runtime, &s) == THREE_MM_CHUNK_BAD_FILE && !h.logs &&
                  o->state.main_file == before.main_file && o->state.main_map == before.main_map &&
                  h.main_file.refs == 1 && h.index.refs == 1);
            o->runtime.storage.buffers[0].locks = 0;
            o->state.main_map->storage.buffers[0].locks = 0;
        }
        CHECK(three_mm_chunk_dispose(&o->state, mode == 2 ? NULL : &o->runtime, &s) == THREE_MM_CHUNK_OK);
        CHECK(!strcmp(h.log, mode == 1   ? "Pffjr"
                             : mode == 2 ? "Pfej"
                                         : "Pfejr") &&
              !h.live && !h.main_file.refs && !h.extra_file.refs && !h.index.refs);
        h.logs = 0;
        h.log[0] = 0;
        CHECK(three_mm_chunk_dispose(&o->state, &o->runtime, &s) == THREE_MM_CHUNK_OK && !strcmp(h.log, "P"));
        --h.shells;
        free(o);
        balanced(&h);
    }
}
int main(void)
{
    construct();
    lookup();
    dispose_cases();
    return 0;
}
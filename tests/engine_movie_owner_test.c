#include "three_mm/movie_owner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "movie owner failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Host Host;
typedef struct Resource
{
    Host *host;
    int refs;
} Resource;
typedef struct Table
{
    Host *host;
    ThreeMMU32 count, width;
    int wire;
    ThreeMMRollCallActor rows[4];
    unsigned char bytes[4][28];
} Table;
struct Host
{
    char log[512];
    int used, tables, resources, fail, scenes, previous, persistent;
    int new_calls, append_calls, open_calls, big, wire_count, bad_row, bad_width;
    Table *wire, *written;
};
enum
{
    FAIL_CREATE = 1,
    FAIL_OPEN,
    FAIL_VERIFY,
    FAIL_RESOURCE,
    FAIL_TABLE,
    FAIL_ROOT,
    FAIL_READ,
    FAIL_WRITE,
    FAIL_NAME,
    FAIL_SAVE,
    FAIL_WIRE,
    FAIL_TAG,
    FAIL_APPEND,
    FAIL_ADD,
    FAIL_TABLE_WRITE,
    FAIL_ADOPT
};
static void event(Host *h, char c)
{
    CHECK(h->used < 511);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static void reset(Host *h)
{
    h->used = 0;
    h->log[0] = 0;
    h->new_calls = h->append_calls = h->open_calls = 0;
}
static void *create(void *c)
{
    Host *h = (Host *)c;
    event(h, 'c');
    return h->fail == FAIL_CREATE ? NULL : h;
}
static void *open_file(void *c)
{
    Host *h = (Host *)c;
    event(h, 'o');
    return h->fail == FAIL_OPEN ? NULL : h;
}
static int verify(void *c, void *f, ThreeMMU32 *n)
{
    Host *h = (Host *)c;
    CHECK(f == h);
    event(h, 'v');
    *n = 7;
    return h->fail != FAIL_VERIFY;
}
static void *new_resource(void *c, void *f)
{
    Host *h = (Host *)c;
    Resource *r;
    CHECK(f == h);
    event(h, 'r');
    if (h->fail == FAIL_RESOURCE)
        return NULL;
    r = (Resource *)malloc(sizeof(*r));
    CHECK(r != NULL);
    r->host = h;
    r->refs = 1;
    ++h->resources;
    return r;
}
static void release_file(void *f)
{
    event((Host *)f, 'f');
}
static void release_resource(void *p)
{
    Resource *r = (Resource *)p;
    event(r->host, 'R');
    CHECK(r->refs > 0);
    if (!--r->refs)
    {
        --r->host->resources;
        free(r);
    }
}
static void *new_table(void *c, int wire)
{
    Host *h = (Host *)c;
    Table *t;
    event(h, wire ? 'w' : 't');
    ++h->new_calls;
    if (h->fail == FAIL_TABLE || (wire && h->fail == FAIL_WIRE))
        return NULL;
    t = (Table *)calloc(1, sizeof(*t));
    CHECK(t != NULL);
    t->host = h;
    t->wire = wire;
    t->width = wire ? 28 : 64;
    ++h->tables;
    return t;
}
static void release_table(void *p)
{
    Table *t = (Table *)p;
    event(t->host, t->wire ? 'W' : 'T');
    --t->host->tables;
    free(t);
}
static ThreeMMU32 count(void *p)
{
    return ((Table *)p)->count;
}
static void actor(void *p, ThreeMMU32 i, ThreeMMRollCallActor *out)
{
    Table *t = (Table *)p;
    CHECK(i < t->count);
    *out = t->rows[i];
}
static void close_actor(void *c, ThreeMMRollCallActor *a)
{
    Host *h = (Host *)c;
    event(h, (char)('A' + a->data.actor_id));
    if (a->cache)
        release_resource(a->cache);
    a->cache = NULL;
}
static void *file(void *p)
{
    return ((Resource *)p)->host;
}
static int root(void *c, void *f, ThreeMMU32 *n)
{
    Host *h = (Host *)c;
    CHECK(f == h);
    event(h, 'b');
    *n = 7;
    return h->fail != FAIL_ROOT;
}
static int read_call(void *c, void *r, ThreeMMU32 n, void **out, ThreeMMI32 *limit)
{
    Host *h = (Host *)c;
    Table *t;
    CHECK(n == 7);
    event(h, 'i');
    if (h->fail == FAIL_READ)
    {
        *out = NULL;
        return 0;
    }
    t = (Table *)new_table(h, 0);
    CHECK(t != NULL);
    t->count = 1;
    t->rows[0].data.actor_id = 0;
    t->rows[0].cache = r;
    ++((Resource *)r)->refs;
    *out = t;
    *limit = 1;
    return 1;
}
static void load_name(void *c, void *f, ThreeMMU32 n)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 7);
    event(h, 'n');
}
static int scene(void *c, void *f, ThreeMMU32 n, ThreeMMI32 i)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 7);
    event(h, 's');
    return i < h->scenes;
}
static int previous(void *c, void *f, ThreeMMU32 n, ThreeMMU32 *out)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 7);
    event(h, 'p');
    *out = 9;
    return h->previous;
}
static int write_call(void *c, void *f, ThreeMMU32 n, void *table, ThreeMMU32 *out)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 7 && table);
    event(h, 'j');
    *out = 10;
    return h->fail != FAIL_WRITE;
}
static void delete_previous(void *c, void *f, ThreeMMU32 n, ThreeMMU32 old)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 7 && old == 9);
    event(h, 'd');
}
static int set_name(void *c, void *f, ThreeMMU32 n)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 7);
    event(h, 'N');
    return h->fail != FAIL_NAME;
}
static int save(void *c, void *f)
{
    Host *h = (Host *)c;
    CHECK(f == h);
    event(h, 'S');
    return h->fail != FAIL_SAVE;
}
static void persist(void *f)
{
    Host *h = (Host *)f;
    event(h, 'P');
    ++h->persistent;
}
static ThreeMMMovieOwnerServices owners(Host *h)
{
    ThreeMMMovieOwnerServices s = {
        NULL,          create,   open_file,  verify,          new_resource, release_file, release_resource, new_table,
        release_table, count,    actor,      close_actor,     file,         root,         read_call,        load_name,
        scene,         previous, write_call, delete_previous, set_name,     save,         persist};
    s.context = h;
    return s;
}
static void ownership(void)
{
    Host h;
    ThreeMMMovieState m;
    ThreeMMMovieOwnerServices s;
    void *old_r, *old_t;
    int i;
    memset(&h, 0, sizeof(h));
    s = owners(&h);
    three_mm_movie_state_init(&m);
    CHECK(m.movieNumber == 0xffffffffUL && !m.resources && !m.actors);
    CHECK(!three_mm_movie_save(&m, &s) && !h.used);
    CHECK(three_mm_movie_init_empty(&m, &s) && !strcmp(h.log, "crftb"));
    old_r = m.resources;
    old_t = m.actors;
    m.sceneCount = 5;
    m.actorIdLimit = 42;
    for (i = FAIL_CREATE; i <= FAIL_ROOT; ++i)
    {
        if (i == FAIL_OPEN || i == FAIL_VERIFY)
            continue;
        reset(&h);
        h.fail = i;
        CHECK(!three_mm_movie_init_empty(&m, &s));
        CHECK(m.resources == old_r && m.actors == old_t && m.sceneCount == 5 && m.actorIdLimit == 42 && h.tables == 1 &&
              h.resources == 1);
        if (i == FAIL_RESOURCE)
            CHECK(!strcmp(h.log, "crftT"));
        if (i == FAIL_TABLE)
            CHECK(!strcmp(h.log, "crftR"));
        if (i == FAIL_ROOT)
            CHECK(!strcmp(h.log, "crftbTR"));
    }
    for (i = FAIL_OPEN; i <= FAIL_READ; ++i)
    {
        if (i == FAIL_TABLE || i == FAIL_ROOT)
            continue;
        reset(&h);
        h.fail = i;
        CHECK(!three_mm_movie_load(&m, 99, &s));
        CHECK(m.resources == old_r && m.actors == old_t && h.resources == 1 && h.tables == 1);
    }
    reset(&h);
    h.fail = 0;
    h.scenes = 2;
    CHECK(three_mm_movie_load(&m, 99, &s) && m.movieNumber == 7 && m.actorIdLimit == 1 && m.sceneCount == 2);
    CHECK(!strcmp(h.log, "ovrfitnsssTR") && h.resources == 1 && h.tables == 1);
    h.previous = 1;
    reset(&h);
    CHECK(three_mm_movie_save(&m, &s) && !strcmp(h.log, "pjdNSP") && h.persistent == 1);
    reset(&h);
    h.fail = FAIL_WRITE;
    CHECK(!three_mm_movie_save(&m, &s) && !strcmp(h.log, "pj"));
    reset(&h);
    h.fail = FAIL_NAME;
    CHECK(!three_mm_movie_save(&m, &s) && !strcmp(h.log, "pjdN"));
    reset(&h);
    h.fail = FAIL_SAVE;
    CHECK(!three_mm_movie_save(&m, &s) && !strcmp(h.log, "pjdNS") && h.persistent == 1);
    h.fail = 0;
    reset(&h);
    three_mm_movie_state_dispose(&m, &s);
    CHECK(!strcmp(h.log, "ARTR") && !h.tables && !h.resources);
    reset(&h);
    three_mm_movie_state_dispose(&m, &s);
    CHECK(!h.used);
}
static void *read_wire(void *c, void *r, ThreeMMU32 movie, int *big)
{
    Host *h = (Host *)c;
    Table *t;
    ThreeMMMovieActor a;
    int i;
    (void)r;
    CHECK(movie == 7);
    event(h, 'i');
    if (h->fail == FAIL_READ)
        return NULL;
    t = (Table *)new_table(h, 1);
    if (!t)
        return NULL;
    t->width = h->bad_width ? 4 : 28;
    t->count = h->wire_count;
    for (i = 0; i < h->wire_count; ++i)
    {
        memset(&a, 0, sizeof(a));
        a.actor_id = (i == h->bad_row) ? -1 : i;
        a.reference_count = 3;
        a.template_number = 123;
        three_mm_movie_write_actor(&a, t->bytes[i], h->big);
    }
    *big = h->big;
    return t;
}
static ThreeMMU32 width(void *p)
{
    return ((Table *)p)->width;
}
static void wire_row(void *c, void *p, ThreeMMU32 i, unsigned char *bytes)
{
    Host *h = (Host *)c;
    Table *t = (Table *)p;
    event(h, 'g');
    CHECK(i < t->count);
    memcpy(bytes, t->bytes[i], 28);
}
static void runtime_row(void *c, void *p, ThreeMMU32 i, ThreeMMRollCallActor *a)
{
    event((Host *)c, 'g');
    actor(p, i, a);
}
static int append_runtime(void *c, void *p, const ThreeMMRollCallActor *a)
{
    Host *h = (Host *)c;
    Table *t = (Table *)p;
    event(h, 'a');
    ++h->append_calls;
    if (h->fail == FAIL_APPEND && h->append_calls == 2)
        return 0;
    CHECK(t->count < 4);
    t->rows[t->count++] = *a;
    return 1;
}
static int append_wire(void *c, void *p, const unsigned char *bytes)
{
    Host *h = (Host *)c;
    Table *t = (Table *)p;
    event(h, 'a');
    ++h->append_calls;
    if (h->fail == FAIL_APPEND && h->append_calls == 2)
        return 0;
    CHECK(t->count < 4);
    memcpy(t->bytes[t->count++], bytes, 28);
    return 1;
}
static int open_actor(void *c, ThreeMMRollCallActor *a, void *r)
{
    Host *h = (Host *)c;
    event(h, 'h');
    ++h->open_calls;
    if (h->fail == FAIL_TAG && h->open_calls == 2)
        return 0;
    a->cache = r;
    ++((Resource *)r)->refs;
    return 1;
}
static ThreeMMU32 wire_size(void *p)
{
    return 20 + ((Table *)p)->count * 32;
}
static int add_chunk(void *c, void *f, ThreeMMU32 bytes, ThreeMMU32 *n)
{
    Host *h = (Host *)c;
    CHECK(f == h && bytes > 20);
    event(h, '+');
    *n = 10;
    return h->fail != FAIL_ADD;
}
static int write_table(void *c, void *p)
{
    Host *h = (Host *)c;
    Table *t = (Table *)p;
    ThreeMMMemoryStream m;
    ThreeMMMovieActor a;
    ThreeMMU32 i;
    const unsigned short one = 1;
    int big = *(const unsigned char *)&one == 0;
    event(h, '=');
    for (i = 0; i < t->count; ++i)
    {
        three_mm_memory_stream_init(&m, t->bytes[i], 28);
        CHECK(three_mm_movie_read_actor(&m.stream, big, &a, NULL) == THREE_MM_MOVIE_OK && a.actor_id == (ThreeMMI32)i &&
              t->bytes[i][16] == 0 && t->bytes[i][17] == 0 && t->bytes[i][18] == 0 && t->bytes[i][19] == 0);
    }
    return h->fail != FAIL_TABLE_WRITE;
}
static int adopt(void *c, void *f, ThreeMMU32 m, ThreeMMU32 n)
{
    Host *h = (Host *)c;
    CHECK(f == h && m == 7 && n == 10);
    event(h, '>');
    return h->fail != FAIL_ADOPT;
}
static void delete_chunk(void *c, void *f, ThreeMMU32 n)
{
    Host *h = (Host *)c;
    CHECK(f == h && n == 10);
    event(h, '-');
}
static ThreeMMRollCallServices rolls(Host *h)
{
    ThreeMMRollCallServices s = {NULL,           read_wire,   width,      64,          new_table,
                                 release_table,  count,       wire_row,   actor,       runtime_row,
                                 append_runtime, append_wire, open_actor, close_actor, wire_size,
                                 add_chunk,      write_table, adopt,      delete_chunk};
    s.context = h;
    return s;
}
static void rollcalls(void)
{
    Host h;
    Resource *r;
    ThreeMMRollCallServices s;
    ThreeMMMovieOwnerServices o;
    ThreeMMMovieState m;
    Table *t;
    void *output;
    ThreeMMI32 limit;
    ThreeMMU32 written;
    int big, failures[] = {FAIL_READ, FAIL_WIRE, FAIL_TABLE, FAIL_TAG, FAIL_APPEND}, i;
    memset(&h, 0, sizeof(h));
    h.bad_row = -1;
    h.wire_count = 3;
    s = rolls(&h);
    o = owners(&h);
    r = (Resource *)new_resource(&h, &h);
    for (big = 0; big < 2; ++big)
    {
        h.big = big;
        reset(&h);
        limit = 0;
        output = NULL;
        CHECK(three_mm_movie_read_rollcall(r, 7, &output, &limit, &s) && limit == 3 && r->refs == 4 && h.tables == 1);
        t = (Table *)output;
        reset(&h);
        written = 99;
        CHECK(three_mm_movie_write_rollcall(&h, 7, t, &written, &s) && written == 10 && !strcmp(h.log, "wgagaga+=>W"));
        for (i = FAIL_WIRE; i <= FAIL_ADOPT; ++i)
        {
            if (i == FAIL_TAG)
                continue;
            h.fail = i;
            reset(&h);
            CHECK(!three_mm_movie_write_rollcall(&h, 7, t, &written, &s) && h.tables == 1 && r->refs == 4);
            if (i == FAIL_TABLE_WRITE)
                CHECK(!strcmp(h.log, "wgagaga+=W-"));
            if (i == FAIL_ADOPT)
                CHECK(!strcmp(h.log, "wgagaga+=>W-"));
        }
        h.fail = 0;
        three_mm_movie_state_init(&m);
        m.actors = t;
        three_mm_movie_clear_actors(&m, &o);
        CHECK(r->refs == 1 && !h.tables);
        for (i = 0; i < 5; ++i)
        {
            h.fail = failures[i];
            reset(&h);
            output = (void *)1;
            limit = 42;
            CHECK(!three_mm_movie_read_rollcall(r, 7, &output, &limit, &s) && !output && limit == 42 && r->refs == 1 &&
                  !h.tables);
            if (h.fail == FAIL_APPEND)
                CHECK(!strcmp(h.log, "iwtghaghaBRWART"));
        }
        h.fail = 0;
        h.bad_row = 1;
        reset(&h);
        limit = 42;
        CHECK(!three_mm_movie_read_rollcall(r, 7, &output, &limit, &s) && !output && limit == 42 && r->refs == 1 &&
              !h.tables);
        h.bad_row = -1;
        h.bad_width = 1;
        reset(&h);
        CHECK(!three_mm_movie_read_rollcall(r, 7, &output, &limit, &s) && !strcmp(h.log, "iwW"));
        h.bad_width = 0;
    }
    release_resource(r);
    CHECK(!h.resources && !h.tables);
}
int main(void)
{
    ownership();
    rollcalls();
    puts("movie ownership/roll-call passed");
    return 0;
}

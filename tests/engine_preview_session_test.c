#include "mm/preview_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Host Host;
typedef struct Cache
{
    int alive, key;
} Cache;
typedef struct Source
{
    int alive, path, serial;
    Cache cache;
} Source;
typedef struct Owner
{
    MMPreviewSession state;
    Host *host;
    int alive, assets;
    Cache cache;
    unsigned char palette[1024];
} Owner;
struct Host
{
    Owner owners[8], *current;
    Source sources[16];
    unsigned char bytes[1024], palette_wire[1036];
    MMMemoryStream palette_stream;
    int calls, fail_at, failure, input, taken, candidates, published;
    int missing_palette, malformed_palette, short_palette, missing_shade, other_order, unhealthy;
    int begins, depths, ends, depth_ends, source_count, releases, report, report_failure;
    int begin_calls, display_calls, root_queries, lookups, fail_alloc;
    char log[256];
    unsigned logged;
};
static void log_event(Host *h, char c)
{
    assert(h->logged < sizeof(h->log));
    h->log[h->logged++] = c;
}
static int step(Host *h)
{
    return ++h->calls == h->fail_at ? h->failure : 1;
}
static Host *host(void *v)
{
    return ((Owner *)v)->host;
}
static MMPreviewSessionServices services(Owner *);
static void init_owner(Host *h, Owner *o)
{
    memset(o, 0, sizeof(*o));
    o->host = h;
    o->alive = 1;
    assert(mm_preview_session_init(&o->state, 0));
}
static void begin(void *v)
{
    ++host(v)->begin_calls;
}
static int healthy(void *v)
{
    return !host(v)->unhealthy;
}
static int report(void *v, int result)
{
    Host *h = host(v);
    h->report = result;
    log_event(h, 'E');
    return h->report_failure ? h->report_failure : 1;
}
static int present(void *v)
{
    return host(v)->input;
}
static int new_candidate(void *v, void **out)
{
    Host *h = host(v);
    int i, r = step(h);
    if (r <= 0)
        return r;
    for (i = 0; i < 8; ++i)
        if (!h->owners[i].alive)
        {
            init_owner(h, h->owners + i);
            *out = h->owners + i;
            ++h->candidates;
            return 1;
        }
    assert(0);
    return 0;
}
static MMPreviewSession *state(void *v, void *candidate)
{
    (void)v;
    return &((Owner *)candidate)->state;
}
static void take(void *v, void *p)
{
    Host *h = host(v);
    ++h->taken;
    ((Owner *)p)->assets = h->input;
}
static int open_resource(void *v, void *p, const void *path)
{
    (void)p;
    assert(path);
    return step(host(v));
}
static int has_assets(void *v, void *p)
{
    (void)v;
    return ((Owner *)p)->assets;
}
static int budget(void *v, void *p, int resources)
{
    (void)p;
    assert(resources == 0 || resources == 1);
    return step(host(v));
}
static int resource(void *v, void *p, MMU32 type, MMU32 number, MMReadStream *out)
{
    Host *h = host(v);
    const unsigned short order = 1;
    MMCollectionHeader header = {0};
    int r;
    (void)p;
    assert(number == 0);
    if (type == 0x544d4150UL)
    {
        assert(!out);
        return h->missing_shade ? MM_PREVIEW_RESOURCE_MISSING : 1;
    }
    assert(type == 0x474c4352UL && out);
    if (h->missing_palette)
        return MM_PREVIEW_RESOURCE_MISSING;
    r = step(h);
    if (r <= 0)
        return r;
    header.kind = MM_COLLECTION_ARRAY;
    header.big_endian = (*(const unsigned char *)&order == 0) ^ h->other_order;
    header.width = h->malformed_palette ? 8 : 4;
    header.count = h->malformed_palette ? 128 : 256;
    assert(mm_collection_header_write(&header, h->palette_wire, sizeof(h->palette_wire)) == MM_STORAGE_OK);
    memcpy(h->palette_wire + 12, h->bytes, sizeof(h->bytes));
    assert(mm_memory_stream_init(&h->palette_stream, h->palette_wire, sizeof(h->palette_wire) - h->short_palette));
    *out = h->palette_stream.stream;
    return 1;
}
static unsigned char *palette_output(void *v, void *p)
{
    (void)v;
    return ((Owner *)p)->palette;
}
static int display(void *v, void *p)
{
    (void)p;
    ++host(v)->display_calls;
    return step(host(v));
}
static int shade_load(void *v, void *p)
{
    (void)p;
    return step(host(v));
}
static int runtime_begin(void *v)
{
    Host *h = host(v);
    ++h->begins;
    return step(h);
}
static int depth_begin(void *v)
{
    Host *h = host(v);
    ++h->depths;
    return step(h);
}
static int cache_new(void *v, void *p, void **out)
{
    Host *h = host(v);
    Owner *o = (Owner *)p;
    int r = step(h);
    if (r > 0)
    {
        o->cache.alive = 1;
        o->cache.key = 11;
        *out = &o->cache;
    }
    return r;
}
static void publish(void *v, void **candidate)
{
    Owner *old = (Owner *)v;
    Host *h = old->host;
    assert(old->alive);
    h->current = (Owner *)*candidate;
    *candidate = old;
    ++h->published;
}
static void candidate_release(void *v, void *p)
{
    Owner *o = (Owner *)p;
    MMPreviewSessionServices s = services(o);
    (void)v;
    assert(o->alive);
    mm_preview_session_dispose(&o->state, &s);
    o->alive = 0;
    ++o->host->releases;
}
static int same_path(void *v, void *p, const void *path)
{
    (void)v;
    return ((Source *)p)->path == *(const int *)path;
}
static int new_source(void *v, void **out)
{
    Host *h = host(v);
    int i, r = step(h);
    if (r <= 0)
        return r;
    for (i = 0; i < 16; ++i)
        if (!h->sources[i].alive)
        {
            memset(h->sources + i, 0, sizeof(Source));
            h->sources[i].alive = 1;
            h->sources[i].serial = ++h->source_count;
            *out = h->sources + i;
            return 1;
        }
    assert(0);
    return 0;
}
static int source_open(void *v, void *p, const void *path)
{
    ((Source *)p)->path = *(const int *)path;
    return step(host(v));
}
static int source_budget(void *v, void *p)
{
    (void)p;
    return step(host(v));
}
static int source_cache_new(void *v, void *p)
{
    Source *s = (Source *)p;
    int r = step(host(v));
    if (r > 0)
    {
        s->cache.alive = 1;
        s->cache.key = s->path;
    }
    return r;
}
static void *source_cache(void *v, void *p)
{
    (void)v;
    return &((Source *)p)->cache;
}
static int contains(void *v, void *p, MMU32 type, MMU32 number)
{
    Host *h = host(v);
    Cache *cache = (Cache *)p;
    assert(cache->alive && type == 22);
    ++h->lookups;
    if (p == &((Owner *)v)->cache)
        ++h->root_queries;
    return number == (MMU32)cache->key;
}
static void source_release(void *v, void *p)
{
    Source *s = (Source *)p;
    Host *h = host(v);
    assert(s->alive);
    s->alive = s->cache.alive = 0;
    log_event(h, (char)('0' + s->serial));
}
static void clear(void *v)
{
    Owner *o = (Owner *)v;
    log_event(o->host, 'A');
    mm_preview_orbit_reset(&o->state.orbit);
}
static void dispose_live(void *v)
{
    log_event(host(v), 'L');
}
static void release_cache(void *v, void *p)
{
    Cache *c = (Cache *)p;
    assert(c->alive);
    c->alive = 0;
    log_event(host(v), 'C');
}
static void release_material(void *v)
{
    log_event(host(v), 'M');
}
static void release_shade(void *v)
{
    log_event(host(v), 'H');
}
static void depth_end(void *v)
{
    Host *h = host(v);
    ++h->depth_ends;
    log_event(h, 'D');
}
static void runtime_end(void *v)
{
    Host *h = host(v);
    ++h->ends;
    log_event(h, 'B');
}
static MMPreviewSessionServices services(Owner *o)
{
    MMPreviewSessionServices s;
    memset(&s, 0, sizeof(s));
    s.context = o;
    s.begin = begin;
    s.healthy = healthy;
    s.report = report;
    s.input_present = present;
    s.candidate_new = new_candidate;
    s.state = state;
    s.take_assets = take;
    s.open_resources = open_resource;
    s.has_assets = has_assets;
    s.budget = budget;
    s.resource = resource;
    s.palette_output = palette_output;
    s.display_palette = display;
    s.shade_load = shade_load;
    s.runtime_begin = runtime_begin;
    s.depth_begin = depth_begin;
    s.cache_new = cache_new;
    s.publish = publish;
    s.candidate_release = candidate_release;
    s.same_path = same_path;
    s.source_new = new_source;
    s.source_open = source_open;
    s.source_budget = source_budget;
    s.source_cache_new = source_cache_new;
    s.source_cache = source_cache;
    s.contains = contains;
    s.source_release = source_release;
    s.clear_asset = clear;
    s.dispose_live = dispose_live;
    s.cache_release = release_cache;
    s.shared_material_release = release_material;
    s.shade_release = release_shade;
    s.depth_end = depth_end;
    s.runtime_end = runtime_end;
    return s;
}
static void setup(Host *h)
{
    unsigned i;
    memset(h, 0, sizeof(*h));
    h->input = 1;
    init_owner(h, h->owners);
    h->current = h->owners;
    for (i = 0; i < 1024; ++i)
        h->bytes[i] = (unsigned char)i;
}
static int open_session(Host *h, int require, int preserve)
{
    MMPreviewSessionServices s = services(h->current);
    return mm_preview_session_open(&h->current->state, h, require, preserve, &s);
}
static void dispose(Host *h)
{
    unsigned i;
    candidate_release(h->current, h->current);
    for (i = 0; i < 8; ++i)
        assert(!h->owners[i].alive && !h->owners[i].cache.alive);
    for (i = 0; i < 16; ++i)
        assert(!h->sources[i].alive);
}
static void open_tests(void)
{
    Host h;
    Owner *old;
    MMPreviewOrbit orbit;
    MMPreviewSessionServices s;
    int n, i, f, r;
    setup(&h);
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_OK);
    n = h.calls;
    assert(h.current->state.running && h.begins == 1 && h.depths == 1 && h.published == 1 && h.taken == 1);
    assert(!memcmp(h.current->palette, h.bytes, 1024));
    dispose(&h);
    assert(h.ends == 1 && h.depth_ends == 1 && !memcmp(h.log + h.logged - 7, "ALCMHDB", 7));
    for (f = -2; f <= 0; ++f)
        for (i = 1; i <= n; ++i)
        {
            setup(&h);
            old = h.current;
            h.fail_at = i;
            h.failure = f;
            r = open_session(&h, 1, 0);
            assert(r != MM_PREVIEW_SESSION_OK && h.current == old && !h.published);
            if (f < 0)
                assert(r == (f == -1 ? MM_PREVIEW_SESSION_MEMORY : MM_PREVIEW_SESSION_EXCEPTION));
            dispose(&h);
        }
    setup(&h);
    h.input = 0;
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_TRANSFER && !h.begin_calls && !h.candidates);
    assert(open_session(&h, 0, 0) == MM_PREVIEW_SESSION_OK && !h.current->state.cache);
    dispose(&h);
    setup(&h);
    h.missing_palette = 1;
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_PALETTE_MISSING && !h.begins);
    assert(h.logged == 3 && !memcmp(h.log, "EAL", 3));
    dispose(&h);
    setup(&h);
    h.malformed_palette = 1;
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_PALETTE && !h.display_calls);
    assert(h.logged == 3 && !memcmp(h.log, "EAL", 3));
    dispose(&h);
    setup(&h);
    h.short_palette = 1;
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_PALETTE && !h.display_calls);
    dispose(&h);
    setup(&h);
    h.missing_shade = 1;
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_PALETTE && h.display_calls == 1);
    dispose(&h);
    setup(&h);
    h.other_order = 1;
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_OK && h.current->palette[0] == 3 && h.current->palette[3] == 0);
    dispose(&h);
    setup(&h);
    h.unhealthy = 1;
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_CACHE && h.ends == 1);
    dispose(&h);
    setup(&h);
    h.missing_palette = 1;
    h.report_failure = -1;
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_MEMORY);
    dispose(&h);
    setup(&h);
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_OK);
    old = h.current;
    old->state.orbit.framed = 1;
    old->state.orbit.object = 3;
    old->state.orbit.center[1] = 12;
    orbit = old->state.orbit;
    h.calls = 0;
    assert(open_session(&h, 1, 1) == MM_PREVIEW_SESSION_OK && h.current != old && h.begins == 1 && !h.ends);
    assert(!memcmp(&h.current->state.orbit, &orbit, sizeof(orbit)));
    n = h.calls;
    dispose(&h);
    for (f = -2; f <= 0; ++f)
        for (i = 1; i <= n; ++i)
        {
            setup(&h);
            assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_OK);
            old = h.current;
            h.calls = 0;
            h.fail_at = i;
            h.failure = f;
            assert(open_session(&h, 1, 1) != MM_PREVIEW_SESSION_OK && h.current == old && old->state.running &&
                   !h.ends);
            dispose(&h);
        }
    setup(&h);
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_OK);
    old = h.current;
    init_owner(&h, h.owners + 7);
    s = services(h.owners + 7);
    assert(mm_preview_session_open(&h.owners[7].state, &h, 1, 0, &s) == MM_PREVIEW_SESSION_RUNTIME && !h.ends &&
           old->state.running);
    candidate_release(h.owners + 7, h.owners + 7);
    dispose(&h);
}
static void *resize(void *v, void *p, MMU32 old, MMU32 bytes, int priority)
{
    Host *h = (Host *)v;
    (void)old;
    (void)priority;
    return h->fail_alloc ? 0 : realloc(p, bytes);
}
static void release_storage(void *v, void *p, MMU32 bytes)
{
    (void)v;
    (void)bytes;
    free(p);
}
static void source_tests(void)
{
    Host h;
    MMPreviewSessionServices s;
    MMStorageAllocator a;
    int path = 101, other = 102, n, i, f, r;
    setup(&h);
    s = services(h.current);
    assert(mm_preview_session_add(&h.current->state, 1, &path, &s) == MM_PREVIEW_SESSION_CONTENT_ARGUMENT);
    dispose(&h);
    setup(&h);
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_OK);
    s = services(h.current);
    h.calls = 0;
    assert(mm_preview_session_add(&h.current->state, 0, &path, &s) == MM_PREVIEW_SESSION_CONTENT_ARGUMENT);
    assert(mm_preview_session_add(&h.current->state, 5, &path, &s) == MM_PREVIEW_SESSION_OK);
    n = h.calls;
    assert(mm_preview_session_add(&h.current->state, 5, &path, &s) == MM_PREVIEW_SESSION_OK && h.calls == n &&
           h.source_count == 1);
    assert(mm_preview_session_add(&h.current->state, 5, &other, &s) == MM_PREVIEW_SESSION_OK);
    assert(mm_preview_session_resolve(&h.current->state, 0, 22, 11, &s) == &h.current->cache && h.root_queries == 1);
    assert(mm_preview_session_resolve(&h.current->state, 5, 22, 102, &s) == &h.sources[1].cache);
    assert(!mm_preview_session_resolve(&h.current->state, 6, 22, 101, &s));
    h.sources[1].cache.key = 101;
    h.lookups = 0;
    assert(mm_preview_session_resolve(&h.current->state, 5, 22, 101, &s) == &h.sources[0].cache && h.lookups == 1);
    assert(!mm_preview_session_resolve(&h.current->state, 0, 22, 101, &s) && h.lookups == 2 && h.root_queries == 2);
    h.logged = 0;
    dispose(&h);
    assert(!memcmp(h.log, "AL12CMHDB", 9));
    for (f = -2; f <= 0; ++f)
        for (i = 1; i <= n; ++i)
        {
            setup(&h);
            assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_OK);
            s = services(h.current);
            h.calls = 0;
            h.fail_at = i;
            h.failure = f;
            r = mm_preview_session_add(&h.current->state, 5, &path, &s);
            assert(r != MM_PREVIEW_SESSION_OK && !h.current->state.source_count && h.current->state.running);
            if (f < 0)
                assert(r == (f == -1 ? MM_PREVIEW_SESSION_MEMORY : MM_PREVIEW_SESSION_EXCEPTION));
            dispose(&h);
        }
    setup(&h);
    assert(open_session(&h, 1, 0) == MM_PREVIEW_SESSION_OK);
    s = services(h.current);
    assert(mm_storage_dispose(&h.current->state.sources) == MM_STORAGE_OK);
    a.context = &h;
    a.resize = resize;
    a.release = release_storage;
    assert(mm_storage_init(&h.current->state.sources, &a) == MM_STORAGE_OK);
    h.fail_alloc = 1;
    assert(mm_preview_session_add(&h.current->state, 5, &path, &s) == MM_PREVIEW_SESSION_MEMORY &&
           !h.current->state.source_count && !h.sources[0].alive);
    h.fail_alloc = 0;
    assert(mm_preview_session_add(&h.current->state, 5, &path, &s) == MM_PREVIEW_SESSION_OK);
    h.fail_at = h.calls + 4;
    h.failure = -1;
    assert(mm_preview_session_add(&h.current->state, 5, &other, &s) == MM_PREVIEW_SESSION_MEMORY &&
           h.current->state.source_count == 1);
    assert(mm_preview_session_resolve(&h.current->state, 5, 22, 101, &s) == &h.sources[0].cache);
    dispose(&h);
}
int main(void)
{
    open_tests();
    source_tests();
    puts("Preview session: replacement ownership, runtime lease, palettes, prefixes, source lookup and fault cleanup "
         "passed.");
    return 0;
}

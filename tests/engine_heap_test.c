#include "mm/heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
typedef struct State {
    void *raw;
    size_t total;
    int allocations, resizes, frees, fail, clear;
} State;
static void *allocate(void *context, size_t total, int clear)
{
    State *s = (State *)context;
    ++s->allocations; s->total = total; s->clear = clear;
    if (s->fail) return NULL;
    s->raw = clear ? calloc(1, total) : malloc(total);
    return s->raw;
}
static void *resize(void *context, void *raw, size_t total)
{
    State *s = (State *)context;
    void *next;
    CHECK(raw == s->raw); ++s->resizes; s->total = total;
    if (s->fail) return NULL;
    next = realloc(raw, total);
    if (next) s->raw = next;
    return next;
}
static void release(void *context, void *raw)
{
    State *s = (State *)context;
    CHECK(raw == s->raw); ++s->frees; free(raw); s->raw = NULL;
}
int main(void)
{
    State s;
    MMHeapHost host;
    MMHeapLayout layout;
    unsigned char *p, *next;
    size_t i, n;
    memset(&s, 0, sizeof(s)); memset(&host, 0, sizeof(host));
    host.context = &s; host.allocate = allocate; host.reallocate = resize; host.release = release;
    layout.prefix = mm_heap_default_prefix(); CHECK(layout.prefix >= sizeof(size_t));
    for (n = 0; n < 70; ++n) {
        p = (unsigned char *)mm_heap_allocate(&layout, &host, n, 1);
        CHECK(p && mm_heap_size(&layout, p) == n && s.clear);
        CHECK(s.total == layout.prefix + (n ? n : 1));
        CHECK(p == (unsigned char *)s.raw + layout.prefix);
        for (i = 0; i < n; ++i) { CHECK(p[i] == 0); p[i] = (unsigned char)(i + 1); }
        s.fail = 1;
        CHECK(!mm_heap_reallocate(&layout, &host, p, n + 80));
        CHECK(mm_heap_size(&layout, p) == n);
        for (i = 0; i < n; ++i) CHECK(p[i] == (unsigned char)(i + 1));
        s.fail = 0;
        next = (unsigned char *)mm_heap_reallocate(&layout, &host, p, n + 80);
        CHECK(next && mm_heap_size(&layout, next) == n + 80);
        for (i = 0; i < n; ++i) CHECK(next[i] == (unsigned char)(i + 1));
        p = (unsigned char *)mm_heap_reallocate(&layout, &host, next, 0);
        CHECK(p && !mm_heap_size(&layout, p) && s.total == layout.prefix + 1);
        mm_heap_release(&layout, &host, p);
    }
    CHECK(s.allocations == 70 && s.resizes == 210 && s.frees == 70);
    s.fail = 1;
    CHECK(!mm_heap_allocate(&layout, &host, 12, 0));
    CHECK(s.allocations == 71 && !s.clear);
    CHECK(!mm_heap_allocate(&layout, &host, (size_t)-1, 0)); CHECK(s.allocations == 71);
    CHECK(!mm_heap_allocate(&layout, &host, (size_t)-1 - layout.prefix + 1, 0));
    CHECK(s.allocations == 71);
    CHECK(!mm_heap_allocate(&layout, &host, (size_t)-1 - layout.prefix, 0));
    CHECK(s.allocations == 72 && s.total == (size_t)-1);
    s.fail = 0;
    p = (unsigned char *)mm_heap_reallocate(&layout, &host, NULL, 3);
    CHECK(p && !s.clear); memcpy(p, "abc", 3);
    CHECK(!mm_heap_reallocate(&layout, &host, p, (size_t)-1));
    CHECK(mm_heap_size(&layout, p) == 3 && !memcmp(p, "abc", 3));
    CHECK(s.resizes == 210);
    mm_heap_release(&layout, &host, p);
    mm_heap_release(&layout, &host, NULL); CHECK(s.frees == 71);
    CHECK(!mm_heap_size(&layout, NULL));
    layout.prefix = sizeof(size_t) - 1;
    CHECK(!mm_heap_allocate(&layout, &host, 1, 0)); CHECK(s.allocations == 73);
    p = (unsigned char *)mm_heap_allocate(NULL, NULL, 17, 1); CHECK(p);
    for (i = 0; i < 17; ++i) CHECK(!p[i]);
    p = (unsigned char *)mm_heap_reallocate(NULL, NULL, p, 4); CHECK(p && mm_heap_size(NULL, p) == 4);
    mm_heap_release(NULL, NULL, p); mm_heap_release(NULL, NULL, NULL);
    puts("C89 raw heap checks passed"); return 0;
}

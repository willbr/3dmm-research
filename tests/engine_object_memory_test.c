#include "mm/object_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
typedef struct State {
    MMMemoryStats stats;
    MMObjectLayout layout;
    char events[100];
    int used, objects, counters, fail, allocations, failures, releases;
    size_t total;
    void *raw;
} State;
static void event(State *s, char c) { CHECK(s->used < 98); s->events[s->used++] = c; s->events[s->used] = 0; }
static void enter_objects(void *v) { State *s = (State *)v; CHECK(!s->objects && !s->counters); s->objects = 1; event(s, 'E'); }
static void leave_objects(void *v) { State *s = (State *)v; CHECK(s->objects && !s->counters); s->objects = 0; event(s, 'e'); }
static void enter_stats(void *v) { State *s = (State *)v; CHECK(s->objects && !s->counters); s->counters = 1; event(s, 'S'); }
static void leave_stats(void *v) { State *s = (State *)v; CHECK(s->objects && s->counters); s->counters = 0; event(s, 's'); }
static void *allocate(void *v, size_t total, int clear)
{
    State *s = (State *)v;
    CHECK(!s->objects && !s->counters && clear); event(s, 'A'); ++s->allocations; s->total = total;
    if (s->fail) return NULL;
    s->raw = calloc(1, total ? total : 1); return s->raw;
}
static void allocated(void *v, void *raw, size_t total)
{
    State *s = (State *)v;
    size_t i;
    CHECK(s->objects && !s->counters && raw == s->raw && total == s->total);
    CHECK(!s->stats.cv && !s->stats.cb);
    for (i = 0; i < total; ++i) CHECK(!((unsigned char *)raw)[i]);
    event(s, 'L');
}
static void before_free(void *v, void *raw)
{
    State *s = (State *)v;
    CHECK(!s->objects && !s->counters && raw == s->raw && s->stats.cv == 1);
    CHECK(s->stats.cb == (MMMemoryCounter)(s->total - s->layout.prefix)); event(s, 'U');
}
static size_t block_size(void *v, void *raw)
{
    State *s = (State *)v;
    CHECK(raw == s->raw && s->stats.cv == 1); event(s, 'B'); return s->total;
}
static void release(void *v, void *raw)
{
    State *s = (State *)v;
    size_t i;
    CHECK(raw == s->raw && !s->stats.cv && !s->stats.cb && !s->objects && !s->counters);
    for (i = 0; i < s->total; ++i) CHECK(((unsigned char *)raw)[i] == s->layout.garbage);
    event(s, 'F'); ++s->releases; free(raw); s->raw = NULL;
}
static void failed(void *v)
{
    State *s = (State *)v;
    CHECK(!s->objects && !s->counters && !s->stats.cv && !s->stats.cb);
    ++s->failures; event(s, '!');
}
static void reset_events(State *s) { s->used = 0; s->events[0] = 0; }
int main(void)
{
    State s;
    MMObjectHost h;
    MMObjectLayout plain = {0, 0, 0};
    unsigned char *p;
    size_t n, i;
    memset(&s, 0, sizeof(s)); memset(&h, 0, sizeof(h));
    s.layout.prefix = 32; s.layout.debug_fill = 1; s.layout.garbage = 0xa3;
    h.context = &s; h.allocate = allocate; h.release = release;
    h.enter_objects = enter_objects; h.leave_objects = leave_objects;
    h.enter_stats = enter_stats; h.leave_stats = leave_stats;
    h.allocated = allocated; h.before_free = before_free; h.block_size = block_size; h.failed = failed;
    for (n = 0; n < 40; ++n) {
        reset_events(&s);
        p = (unsigned char *)mm_object_allocate(&s.layout, &h, &s.stats, n);
        CHECK(p == (unsigned char *)s.raw + s.layout.prefix && s.stats.cv == 1);
        CHECK(s.stats.cb == (MMMemoryCounter)n && s.stats.cvTot == (MMMemoryCounter)(n + 1));
        CHECK(!strcmp(s.events, "ESseAELSse"));
        for (i = 0; i < n; ++i) CHECK(!p[i]);
        memset(p, 0x51, n); mm_object_free(&s.layout, &h, &s.stats, p);
        CHECK(!strcmp(s.events, "ESseAELSseUBF"));
    }
    CHECK(s.stats.cvRun == 1 && s.stats.cbRun == 39 && s.releases == 40);
    s.stats.cactFail = 1; reset_events(&s);
    CHECK(!mm_object_allocate(&s.layout, &h, &s.stats, 8));
    CHECK(!strcmp(s.events, "ESse!") && !s.stats.cactFail && s.allocations == 40);
    s.stats.cactDo = 1; s.stats.cactFail = 1; reset_events(&s);
    p = (unsigned char *)mm_object_allocate(&s.layout, &h, &s.stats, 8); CHECK(p);
    CHECK(!s.stats.cactDo && s.stats.cactFail == 1); mm_object_free(&s.layout, &h, &s.stats, p);
    reset_events(&s); CHECK(!mm_object_allocate(&s.layout, &h, &s.stats, 8));
    CHECK(!strcmp(s.events, "ESse!") && !s.stats.cactFail);
    s.fail = 1; reset_events(&s); CHECK(!mm_object_allocate(&s.layout, &h, &s.stats, 8));
    CHECK(!strcmp(s.events, "ESseA!") && s.allocations == 42 && s.failures == 3);
    s.fail = 0; reset_events(&s); CHECK(!mm_object_allocate(&s.layout, &h, &s.stats, (size_t)-1));
    CHECK(!strcmp(s.events, "ESse!") && s.allocations == 42 && s.failures == 4);
    reset_events(&s); mm_object_free(&s.layout, &h, &s.stats, NULL); CHECK(!s.used);
    p = (unsigned char *)mm_object_allocate(&plain, NULL, NULL, 23); CHECK(p);
    for (i = 0; i < 23; ++i) CHECK(!p[i]);
    mm_object_free(&plain, NULL, NULL, p);
    p = (unsigned char *)mm_object_allocate(&plain, NULL, NULL, 0); CHECK(p);
    mm_object_free(&plain, NULL, NULL, p);
    puts("C89 object allocation checks passed"); return 0;
}

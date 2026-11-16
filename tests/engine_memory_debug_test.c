#include "mm/memory_debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
typedef struct Slot { void *raw; size_t total; } Slot;
typedef struct Test {
    MMMemoryDebugGraph graph;
    MMMemoryDebugLayout layout;
    MMMemoryDebugHost debug;
    MMMemoryLayout memory;
    MMMemoryHost host;
    MMFixedMemory fixed;
    MMMemoryDebugMove move;
    MMMemoryCounter suspended, thread;
    Slot slots[4];
    int depth, maximum, expected, errors, failures, releases, mode, poison;
    int thread_depth, extra, count;
    MMMemoryCounter reported[8];
    void *mark_target;
} Test;
static Slot *slot(Test *t, const void *raw)
{
    unsigned i;
    for (i = 0; i < 4; ++i) if (t->slots[i].raw == raw) return &t->slots[i];
    CHECK(0); return NULL;
}
static void enter(void *v) { Test *t = (Test *)v; ++t->depth; if (t->maximum < t->depth) t->maximum = t->depth; }
static void leave(void *v) { Test *t = (Test *)v; CHECK(t->depth > 0); --t->depth; }
static void address(void *v, const void *p, size_t n) { (void)v; CHECK(p && n); }
static void assertion(void *v, int code, const void *p, size_t n)
{
    Test *t = (Test *)v;
    CHECK(code == t->expected); ++t->errors;
    if (code == MM_MEMORY_DEBUG_OLD_SIZE) CHECK(p && n == sizeof(MMMemoryCounter));
    else CHECK(!p && !n);
}
static size_t capacity(void *v, const void *raw) { return slot((Test *)v, raw)->total; }
static MMMemoryCounter current_thread(void *v)
{ Test *t = (Test *)v; t->thread_depth = t->depth; return t->thread; }
static void lost(void *v, MMMemoryDebugRecord *r)
{
    Test *t = (Test *)v;
    CHECK(t->depth == 1 && t->count < 8); t->reported[t->count++] = r->line;
    if (t->mark_target) {
        mm_memory_debug_mark(&t->graph, &t->layout, &t->debug, t->mark_target); t->mark_target = NULL;
    }
}
static void extra(void *v) { Test *t = (Test *)v; CHECK(t->depth == 1); ++t->extra; }
static void *allocate(void *v, size_t total)
{
    Test *t = (Test *)v;
    unsigned i;
    CHECK(!t->depth && total < 512);
    for (i = 0; i < 4; ++i) if (!t->slots[i].raw) {
        t->slots[i].raw = malloc(512); t->slots[i].total = total; CHECK(t->slots[i].raw); return t->slots[i].raw;
    }
    CHECK(0); return NULL;
}
static void *resize(void *v, void *raw, size_t total)
{
    Test *t = (Test *)v;
    Slot *s = slot(t, raw);
    MMMemoryDebugRecord *r = (MMMemoryDebugRecord *)raw;
    void *next;
    CHECK(total < 512 && !t->depth);
    if (t->mode < 0) return NULL;
    if (t->mode) {
        next = malloc(512); CHECK(next); memcpy(next, raw, s->total < total ? s->total : total);
        free(raw); s->raw = next;
    } else {
        next = raw;
        if (t->poison) {
            /* Neither these poisoned aliases nor aliases invalidated by an
             * actual moved realloc may be evaluated as pointer values. */
            if (r->previous) memset(&r->previous->next, 0xd3, sizeof(r->previous->next));
            else memset(&t->graph.first, 0xd3, sizeof(t->graph.first));
            if (r->next) memset(&r->next->previous, 0xd3, sizeof(r->next->previous));
        }
    }
    s->total = total; return next;
}
static void release(void *v, void *raw)
{
    Test *t = (Test *)v;
    Slot *s = slot(t, raw);
    MMMemoryDebugRecord *r;
    size_t i;
    for (r = t->graph.first; r; r = r->next) CHECK((void *)r != raw);
    for (i = 0; i < s->total; ++i) CHECK(((unsigned char *)raw)[i] == 0xa3);
    free(raw); s->raw = NULL; ++t->releases;
}
static void before_allocate(void *v, MMU32 bytes)
{ Test *t = (Test *)v; CHECK(t->depth == 1); mm_memory_debug_before_allocate(&t->graph, &t->layout, &t->debug, bytes); }
static void created(void *v, void *raw, MMU32 total)
{
    static char source[] = "test-source";
    Test *t = (Test *)v;
    MMMemoryDebugRecord *r = (MMMemoryDebugRecord *)raw;
    unsigned i;
    mm_memory_debug_created(&t->layout, &t->debug, r, total, source, t->thread + 10);
    CHECK(!t->thread_depth && r->source == source);
    for (i = 0; i < 5; ++i) r->stack[i] = (MMMemoryCounter)i + 100;
    mm_memory_debug_publish(&t->graph, &t->layout, &t->debug, r);
}
static void inspect(void *v, void *raw)
{ Test *t = (Test *)v; mm_memory_debug_validate_record(&t->graph, &t->layout, &t->debug, (MMMemoryDebugRecord *)raw); }
static void prepare(void *v, void *raw, MMU32 old_total, MMU32 new_total)
{ Test *t = (Test *)v; mm_memory_debug_prepare_resize(&t->layout, &t->debug, (MMMemoryDebugRecord *)raw, old_total, new_total, &t->move); }
static void relocated(void *v, void *raw, const unsigned char *old)
{ Test *t = (Test *)v; mm_memory_debug_relocated(&t->graph, &t->layout, &t->debug, raw, &t->move, old); }
static void resized(void *v, void *raw, MMU32 total)
{ Test *t = (Test *)v; mm_memory_debug_resized(&t->layout, (MMMemoryDebugRecord *)raw, total); }
static void before_free(void *v, void *raw)
{ Test *t = (Test *)v; mm_memory_debug_before_free(&t->graph, &t->layout, &t->debug, (MMMemoryDebugRecord *)raw); }
static MMU32 block_size(void *v, void *raw) { (void)v; return (MMU32)((MMMemoryDebugRecord *)raw)->total; }
static void validate(void *v, void *p, MMU32 bytes)
{ Test *t = (Test *)v; mm_memory_debug_validate_payload(&t->graph, &t->layout, &t->debug, p, (MMI32)bytes); }
static void failed(void *v, int resizing) { Test *t = (Test *)v; CHECK(resizing); ++t->failures; }
static MMMemoryDebugRecord *record(Test *t, void *p) { return (MMMemoryDebugRecord *)((unsigned char *)p - t->layout.prefix); }
static void links(Test *t)
{
    MMMemoryDebugRecord *r, *previous = NULL;
    for (r = t->graph.first; r; r = r->next) {
        CHECK(r->previous == previous && r->stack[4] == 104); previous = r;
    }
    CHECK(!t->depth);
}
static void *make(Test *t, MMU32 bytes, MMU32 clear, MMMemoryCounter owner)
{
    void *p;
    t->thread = owner; CHECK(mm_fixed_allocate(&t->fixed, &t->memory, &t->host, NULL, &p, bytes, clear, 0)); links(t); return p;
}
int main(void)
{
    Test t;
    void *a, *b, *c;
    MMMemoryDebugRecord *r;
    unsigned char snapshot[512];
    size_t n;
    int errors, extra_count;
    short magic;
    memset(&t, 0, sizeof(t)); mm_memory_debug_init(&t.graph);
    t.layout.prefix = sizeof(MMMemoryDebugRecord); t.layout.suffix = sizeof(short); t.layout.limit = 10000;
    t.layout.magic = (short)-23981; t.layout.expensive = 1; t.layout.suspended = &t.suspended;
    t.memory.prefix = t.layout.prefix; t.memory.suffix = t.layout.suffix; t.memory.limit = 9000; t.memory.debug_fill = 1; t.memory.garbage = 0xa3;
    t.debug.context = &t; t.debug.enter = enter; t.debug.leave = leave; t.debug.address = address; t.debug.assertion = assertion;
    t.debug.allocation_size = capacity; t.debug.current_thread = current_thread; t.debug.lost = lost; t.debug.after_report = extra; t.debug.after_unmark = extra;
    t.host.context = &t; t.host.allocate = allocate; t.host.reallocate = resize; t.host.release = release; t.host.enter = enter; t.host.leave = leave;
    t.host.before_allocate = before_allocate; t.host.allocated = created; t.host.inspect_resize = inspect; t.host.before_resize = prepare;
    t.host.relocated = relocated; t.host.resized = resized; t.host.before_free = before_free; t.host.block_size = block_size; t.host.validate = validate; t.host.failed = failed;
    a = make(&t, 7, 1, 1); b = make(&t, 15, 0, 2); c = make(&t, 0, 1, 1);
    CHECK(!record(&t, a)->marks && (unsigned short)record(&t, b)->marks == 0xa3a3);
    t.thread = 1; mm_memory_debug_report(&t.graph, &t.layout, &t.debug);
    CHECK(!t.thread_depth && t.count == 2 && t.reported[0] == 11 && !record(&t, c)->marks);
    t.count = 0; t.mark_target = a; mm_memory_debug_report(&t.graph, &t.layout, &t.debug); CHECK(t.count == 1 && record(&t, a)->marks == 1);
    mm_memory_debug_unmark(&t.graph, &t.layout, &t.debug); CHECK(!record(&t, a)->marks && (unsigned short)record(&t, b)->marks == 0xa3a3);
    record(&t, b)->marks = 0; mm_memory_debug_mark(&t.graph, &t.layout, &t.debug, b); CHECK(record(&t, b)->marks == 1);
    n = slot(&t, record(&t, a))->total; memcpy(snapshot, record(&t, a), n); t.mode = -1;
    CHECK(!mm_fixed_resize(&t.fixed, &t.memory, &t.host, NULL, &a, 20, 7, 1, 0));
    CHECK(t.failures == 1 && !memcmp(snapshot, record(&t, a), n)); links(&t);
    t.mode = 1; CHECK(mm_fixed_resize(&t.fixed, &t.memory, &t.host, NULL, &a, 20, 7, 1, 0)); links(&t); CHECK(t.graph.first == record(&t, a));
    CHECK(mm_fixed_resize(&t.fixed, &t.memory, &t.host, NULL, &c, 4, 0, 0, 0)); links(&t); CHECK(t.graph.first == record(&t, c));
    CHECK(mm_fixed_resize(&t.fixed, &t.memory, &t.host, NULL, &a, 2, 20, 0, 0)); links(&t); CHECK(t.graph.first == record(&t, a));
    t.mode = 0; t.poison = 1;
    CHECK(mm_fixed_resize(&t.fixed, &t.memory, &t.host, NULL, &c, 1, 4, 0, 0)); links(&t); CHECK(t.graph.first == record(&t, a));
    CHECK(mm_fixed_resize(&t.fixed, &t.memory, &t.host, NULL, &a, 0, 2, 0, 0)); links(&t); CHECK(t.graph.first == record(&t, a));
    r = record(&t, a); magic = r->magic; r->magic = 0; t.expected = MM_MEMORY_DEBUG_MAGIC;
    mm_memory_debug_validate_record(&t.graph, &t.layout, &t.debug, r); r->magic = magic;
    memcpy((unsigned char *)r + r->total - t.layout.suffix, &r->marks, sizeof(short)); t.expected = MM_MEMORY_DEBUG_TAIL;
    mm_memory_debug_validate_record(&t.graph, &t.layout, &t.debug, r); mm_memory_debug_resized(&t.layout, r, (MMU32)r->total);
    t.expected = MM_MEMORY_DEBUG_SIZE; mm_memory_debug_validate_payload(&t.graph, &t.layout, &t.debug, a, 1);
    t.expected = MM_MEMORY_DEBUG_OLD_SIZE; mm_memory_debug_prepare_resize(&t.layout, &t.debug, r, (MMU32)r->total + 1, (MMU32)r->total, &t.move);
    t.expected = 0; errors = t.errors; t.suspended = 1;
    mm_memory_debug_validate_payload(&t.graph, &t.layout, &t.debug, NULL, -1);
    mm_memory_debug_validate_payload(&t.graph, &t.layout, &t.debug, a, 1); CHECK(t.errors == errors);
    t.layout.expensive = 0; t.expected = MM_MEMORY_DEBUG_NIL; mm_memory_debug_validate_payload(&t.graph, &t.layout, &t.debug, NULL, -1);
    t.expected = 0; t.suspended = 0; t.layout.expensive = 1;
    r->marks = SHRT_MAX; mm_memory_debug_mark(&t.graph, &t.layout, &t.debug, a); CHECK(r->marks == SHRT_MIN);
    mm_memory_debug_mark(&t.graph, &t.layout, &t.debug, NULL);
    extra_count = t.extra; mm_memory_debug_unmark(&t.graph, &t.layout, &t.debug); CHECK(t.extra == extra_count + 1 && !t.thread_depth);
    mm_fixed_free(&t.memory, &t.host, NULL, &c); links(&t);
    mm_fixed_free(&t.memory, &t.host, NULL, &a); links(&t);
    mm_fixed_free(&t.memory, &t.host, NULL, &b); CHECK(!t.graph.first && !a && !b && !c && t.releases == 3 && t.maximum >= 2);
    puts("C89 fixed-memory diagnostic graph checks passed"); return 0;
}

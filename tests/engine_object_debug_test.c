#include "mm/object_debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
typedef struct Block {
    MMObjectDebugRecord header;
    MMObjectDebugState state;
    unsigned id;
    struct Block *child;
} Block;
typedef struct Test {
    MMObjectDebugGraph graph;
    MMObjectDebugLayout layout;
    MMObjectDebugHost host;
    int depth, maximum, entries, attached, validations, reports, summaries, details, thread_calls;
    int errors[20], expected, drain;
    MMMemoryCounter thread;
    unsigned order[20], marks[20];
    int used, marked;
} Test;
static Block *block(void *p) { return (Block *)((unsigned char *)p - offsetof(Block, state)); }
static void enter(void *v)
{
    Test *t = (Test *)v;
    ++t->depth; ++t->entries; if (t->maximum < t->depth) t->maximum = t->depth;
    if (t->drain) { CHECK(t->depth == 1); t->drain = 0; t->graph.raw->previous = NULL; t->graph.raw = NULL; }
}
static void leave(void *v) { Test *t = (Test *)v; CHECK(t->depth > 0); --t->depth; }
static void address(void *v, const void *p, size_t n) { (void)v; CHECK(p && n); }
static void assertion(void *v, int code, const void *p, size_t n)
{
    Test *t = (Test *)v;
    CHECK(code == t->expected); ++t->errors[code];
    if (code == MM_OBJECT_DEBUG_MAGIC || code == MM_OBJECT_DEBUG_MARKS) CHECK(p && n == sizeof(short));
    if (code == MM_OBJECT_DEBUG_KIND) CHECK(p && n == sizeof(MMMemoryCounter));
}
static MMMemoryCounter current_thread(void *v) { Test *t = (Test *)v; ++t->thread_calls; return t->thread; }
static void validate(void *v, void *p, MMU32 flags)
{
    Test *t = (Test *)v;
    ++t->validations;
    mm_object_debug_validate(&t->layout, &t->host, p, &block(p)->state, 1, flags);
}
static void attached(void *v, void *p)
{
    Test *t = (Test *)v;
    CHECK(t->depth && block(p)->state.kind == MM_OBJECT_DEBUG_ALLOCATED); ++t->attached;
    validate(v, p, 0);
}
static void mark_root(void *v, void *p)
{
    Test *t = (Test *)v;
    mm_object_debug_mark_stub(&t->layout, &t->host, p, &block(p)->state);
}
static void mark(void *v, void *p)
{
    Test *t = (Test *)v;
    Block *b = block(p);
    CHECK(t->marked < 20); t->marks[t->marked++] = b->id;
    mm_object_debug_mark(&t->layout, &t->host, p, &b->state, 1);
    if (b->child) mark_root(v, &b->child->state);
}
static int summary(void *v, MMI32 count)
{
    Test *t = (Test *)v;
    CHECK(t->depth == 1 && count == 2); ++t->summaries; return t->details;
}
static void lost(void *v, void *p, MMObjectDebugRecord *r, size_t payload)
{
    Test *t = (Test *)v;
    CHECK(t->depth == 1 && &block(p)->header == r && payload == sizeof(Block) - offsetof(Block, state));
    CHECK(t->used < 20); t->order[t->used++] = block(p)->id; ++t->reports;
}
static void links(MMObjectDebugRecord **head)
{
    MMObjectDebugRecord **slot = head;
    while (*slot) { CHECK((*slot)->previous == slot); slot = &(*slot)->next; }
}
static void create(Test *t, Block *b, unsigned id, MMMemoryCounter owner_thread)
{
    static char source[] = "source";
    memset(b, 0, sizeof(*b)); b->id = id;
    mm_object_debug_created(&t->graph, &t->layout, &t->host, &b->header, source, 27, sizeof(*b), owner_thread);
    CHECK(b->header.source == source && b->header.line == 27 && b->header.total == sizeof(*b));
    CHECK(!b->header.marks && b->header.magic == t->layout.magic && b->header.thread == owner_thread);
    links(&t->graph.raw);
}
static void attach(Test *t, Block *b)
{
    mm_object_debug_attach(&t->graph, &t->layout, &t->host, &b->state, &b->state);
    links(&t->graph.raw); links(&t->graph.objects);
    CHECK(!t->depth && b->state.kind == MM_OBJECT_DEBUG_ALLOCATED);
}
static void record_error(Test *t, MMObjectDebugRecord *r, int code)
{
    t->expected = code; mm_object_debug_validate_record(&t->layout, &t->host, r, 1);
    CHECK(t->errors[code] > 0 && !t->depth); t->expected = 0;
}
int main(void)
{
    Test t;
    Block a, b, c, local, refresh;
    MMObjectDebugRecord *saved;
    int entries, calls;
    memset(&t, 0, sizeof(t));
    mm_object_debug_init(&t.graph);
    t.layout.prefix = offsetof(Block, state); t.layout.object_bytes = sizeof(Block) - t.layout.prefix;
    t.layout.limit = 100000; t.layout.magic = (short)-23981; t.thread = 1;
    t.host.context = &t; t.host.enter = enter; t.host.leave = leave; t.host.address = address; t.host.assertion = assertion;
    t.host.current_thread = current_thread; t.host.attached = attached; t.host.validate = validate;
    t.host.mark = mark; t.host.mark_root = mark_root; t.host.lost_summary = summary; t.host.lost_object = lost;
    memset(&local, 0, sizeof(local)); local.id = 4;
    entries = t.entries;
    mm_object_debug_attach(&t.graph, &t.layout, &t.host, &local.state, &local.state);
    CHECK(local.state.kind == MM_OBJECT_DEBUG_LOCAL && t.entries == entries && !t.attached);
    create(&t, &refresh, 9, 1); t.drain = 1;
    mm_object_debug_attach(&t.graph, &t.layout, &t.host, &refresh.state, &refresh.state);
    CHECK(refresh.state.kind == MM_OBJECT_DEBUG_LOCAL && !t.graph.raw && !t.attached);
    create(&t, &a, 1, 1); create(&t, &b, 2, 2); create(&t, &c, 3, 1);
    attach(&t, &a); attach(&t, &c); attach(&t, &b); CHECK(t.attached == 3 && !t.graph.raw);
    CHECK(t.graph.objects == &b.header && b.header.next == &c.header && c.header.next == &a.header);
    b.header.marks = 7; a.child = &c; c.child = &a;
    t.details = 1; mm_object_debug_report(&t.graph, &t.layout, &t.host);
    CHECK(t.summaries == 1 && t.reports == 1 && t.order[0] == 1);
    CHECK(t.marked == 2 && t.marks[0] == 1 && t.marks[1] == 3);
    CHECK(a.header.marks == 1 && c.header.marks == 1 && b.header.marks == 7);
    calls = t.thread_calls; mark_root(&t, &a.state); CHECK(t.thread_calls == calls && t.marked == 2);
    mm_object_debug_report(&t.graph, &t.layout, &t.host); CHECK(t.summaries == 1 && t.marked == 2);
    mm_object_debug_unmark(&t.graph, &t.layout, &t.host);
    CHECK(!a.header.marks && !c.header.marks && b.header.marks == 7);
    t.details = 0; mm_object_debug_report(&t.graph, &t.layout, &t.host);
    CHECK(t.summaries == 2 && t.reports == 1 && t.marked == 4);
    b.header.marks = 0; mark_root(&t, &b.state); CHECK(!b.header.marks && t.marked == 4);
    mark_root(&t, &local.state); mark_root(&t, &local.state); CHECK(t.marked == 6);
    a.header.magic = 0; record_error(&t, &a.header, MM_OBJECT_DEBUG_MAGIC); a.header.magic = t.layout.magic;
    a.header.marks = -1; record_error(&t, &a.header, MM_OBJECT_DEBUG_MARKS); a.header.marks = 1;
    a.header.total = 1; record_error(&t, &a.header, MM_OBJECT_DEBUG_SIZE); a.header.total = sizeof(a);
    a.header.total = t.layout.limit; record_error(&t, &a.header, MM_OBJECT_DEBUG_SIZE_BIG); a.header.total = sizeof(a);
    saved = *a.header.previous; *a.header.previous = NULL;
    record_error(&t, &a.header, MM_OBJECT_DEBUG_PREVIOUS); *a.header.previous = saved;
    c.header.previous = &c.header.next;
    record_error(&t, &b.header, MM_OBJECT_DEBUG_NEXT); c.header.previous = &b.header.next;
    t.expected = MM_OBJECT_DEBUG_LINKED; mm_object_debug_validate_record(&t.layout, &t.host, &a.header, 0); t.expected = 0;
    t.expected = MM_OBJECT_DEBUG_REFERENCES; mm_object_debug_validate(&t.layout, &t.host, &a.state, &a.state, -1, 0); t.expected = 0;
    t.expected = MM_OBJECT_DEBUG_REFERENCES_BIG; mm_object_debug_validate(&t.layout, &t.host, &a.state, &a.state, t.layout.limit, 0); t.expected = 0;
    t.expected = MM_OBJECT_DEBUG_EXPECT_LOCAL; validate(&t, &a.state, MM_OBJECT_REQUIRE_LOCAL); t.expected = 0;
    t.expected = MM_OBJECT_DEBUG_EXPECT_ALLOCATED; validate(&t, &local.state, MM_OBJECT_REQUIRE_ALLOCATED); t.expected = 0;
    local.state.kind = 0; t.expected = MM_OBJECT_DEBUG_KIND; validate(&t, &local.state, 0); t.expected = 0;
    create(&t, &refresh, 9, 1); t.expected = MM_OBJECT_DEBUG_RAW_LIST;
    mm_object_debug_unmark(&t.graph, &t.layout, &t.host); t.expected = 0;
    mm_object_debug_unlink(&t.layout, &t.host, &refresh.header);
    t.expected = MM_OBJECT_DEBUG_UNLINKED; mm_object_debug_validate_record(&t.layout, &t.host, &refresh.header, 1); t.expected = 0;
    a.header.marks = SHRT_MAX; mm_object_debug_mark(&t.layout, &t.host, &a.state, &a.state, 1); CHECK(a.header.marks == SHRT_MIN); a.header.marks = 0;
    mm_object_debug_unlink(&t.layout, &t.host, &c.header); links(&t.graph.objects);
    mm_object_debug_unlink(&t.layout, &t.host, &a.header); links(&t.graph.objects);
    mm_object_debug_unlink(&t.layout, &t.host, &b.header); CHECK(!t.graph.objects && !t.graph.raw && !t.depth && t.maximum >= 3);
    CHECK(!a.header.next && !a.header.previous && !b.header.next && !c.header.previous);
    puts("C89 object diagnostic graph checks passed"); return 0;
}

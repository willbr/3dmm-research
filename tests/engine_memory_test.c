#include "mm/memory.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Heap {
    MMFixedMemory state; MMMemoryLayout layout; MMMemoryHost host; MMMemoryStats stats;
    void *raw; size_t size; void **owner;
    unsigned char old_address[sizeof(void *)];
    unsigned alloc_fail, resize_fail, allocs, resizes, frees, failures, liberations, moves;
    int depth, available, nested, nesting, phase;
    MMI32 freed, lib_bytes, lib_priority; MMU32 before_bytes;
    char events[4096]; unsigned n;
} Heap;
static void event(Heap *h, char c) { assert(h->n < sizeof(h->events) - 1); h->events[h->n++] = c; h->events[h->n] = 0; }
static MMU32 total(void *p) { MMU32 n; memcpy(&n, p, sizeof(n)); return n; }
static void footer(void *p, MMU32 size) { unsigned char *b = (unsigned char *)p; b[size - 2] = 0x53; b[size - 1] = 0xa2; }
static void enter(void *p) { Heap *h = (Heap *)p; assert(!h->depth); ++h->depth; event(h, 'L'); }
static void leave(void *p) { Heap *h = (Heap *)p; assert(h->depth == 1); --h->depth; event(h, 'U'); }
static void *allocate(void *p, size_t bytes)
{
    Heap *h = (Heap *)p;
    assert(!h->depth && !h->raw); ++h->allocs; event(h, 'A');
    if (h->alloc_fail) { --h->alloc_fail; return NULL; }
    h->raw = malloc(bytes ? bytes : 1); assert(h->raw); h->size = bytes; return h->raw;
}
static void *resize(void *p, void *old, size_t bytes)
{
    Heap *h = (Heap *)p; void *fresh;
    assert(!h->depth && old == h->raw); ++h->resizes; event(h, 'R');
    if (h->resize_fail) { --h->resize_fail; return NULL; }
    fresh = malloc(bytes ? bytes : 1); assert(fresh);
    memcpy(fresh, old, bytes < h->size ? bytes : h->size);
    assert(fresh != old); memcpy(h->old_address, &old, sizeof(old));
    free(old); h->raw = fresh; h->size = bytes;
    return fresh;
}
static void release(void *p, void *raw)
{
    Heap *h = (Heap *)p; size_t i;
    assert(!h->depth && raw == h->raw && *h->owner == raw && h->stats.cv == 0 && h->stats.cb == 0);
    for (i = 0; i < h->size; ++i) assert(((unsigned char *)raw)[i] == 0xa3);
    ++h->frees; event(h, 'F'); free(raw); h->raw = NULL; h->size = 0;
}
static int has(void *p) { Heap *h = (Heap *)p; event(h, 'H'); return h->available; }
static MMI32 liberate(void *p, MMI32 bytes, MMI32 priority)
{
    Heap *h = (Heap *)p; void *nested = NULL; void **saved;
    assert(!h->depth && h->state.in_liberator); ++h->liberations; event(h, 'G');
    h->lib_bytes = bytes; h->lib_priority = priority;
    if (h->nested) {
        saved = h->owner; h->owner = &nested; h->nesting = 1;
        assert(!mm_fixed_allocate(&h->state, &h->layout, &h->host, &h->stats, &nested, 3, 1, priority));
        assert(!nested && h->state.in_liberator); h->owner = saved; h->nesting = 0;
    }
    return h->freed;
}
static void before_allocate(void *p, MMU32 payload)
{ Heap *h = (Heap *)p; (void)payload; assert(h->depth == 1); event(h, 'P'); }
static void allocated(void *p, void *raw, MMU32 bytes)
{
    Heap *h = (Heap *)p;
    assert(!h->depth && *h->owner == (unsigned char *)raw + 16 && !h->stats.cv);
    event(h, 'M'); memcpy(raw, &bytes, sizeof(bytes)); footer(raw, bytes);
}
static void inspect(void *p, void *raw) { Heap *h = (Heap *)p; assert(raw == h->raw && !h->depth); event(h, 'I'); }
static void before_resize(void *p, void *raw, MMU32 before, MMU32 after)
{ Heap *h = (Heap *)p; assert(!h->depth && total(raw) == before && after >= 18); event(h, 'Z'); }
static void relocated(void *p, void *raw, const unsigned char *old)
{
    Heap *h = (Heap *)p;
    assert(raw == h->raw && !memcmp(old, h->old_address, sizeof(void *)) && *h->owner == raw && total(raw) == h->before_bytes + 18);
    ++h->moves; event(h, 'T');
}
static void resized(void *p, void *raw, MMU32 bytes)
{
    Heap *h = (Heap *)p;
    assert(!h->depth && *h->owner == (unsigned char *)raw + 16);
    event(h, 'S'); memcpy(raw, &bytes, sizeof(bytes)); footer(raw, bytes);
}
static void before_free(void *p, void *raw)
{ Heap *h = (Heap *)p; assert(!h->depth && *h->owner == (unsigned char *)raw + 16 && h->stats.cv == 1); event(h, 'D'); }
static MMU32 block_size(void *p, void *raw) { Heap *h = (Heap *)p; event(h, 'B'); return total(raw); }
static void validate(void *p, void *payload, MMU32 bytes)
{
    Heap *h = (Heap *)p; unsigned char *raw = (unsigned char *)payload - 16;
    assert(!h->depth && raw == h->raw && *h->owner == payload && total(raw) == bytes + 18);
    assert(raw[bytes + 16] == 0x53 && raw[bytes + 17] == 0xa2);
    assert(h->stats.cb == (h->phase == 1 ? (MMMemoryCounter)bytes : (MMMemoryCounter)h->before_bytes));
    event(h, 'V');
}
static void too_large(void *p, MMU32 bytes, int growing)
{ Heap *h = (Heap *)p; (void)growing; assert(bytes > h->layout.limit && !h->depth); event(h, 'Q'); }
static void failed(void *p, int resizing)
{
    Heap *h = (Heap *)p;
    assert(!h->depth && h->state.in_liberator == h->nesting);
    assert(resizing ? *h->owner == (unsigned char *)h->raw + 16 : !*h->owner);
    ++h->failures; event(h, 'E');
}
static void init(Heap *h, void **owner)
{
    memset(h, 0, sizeof(*h)); h->owner = owner;
    h->layout.prefix = 16; h->layout.suffix = 2; h->layout.limit = 1024; h->layout.debug_fill = 1; h->layout.garbage = 0xa3;
    h->host.context = h; h->host.allocate = allocate; h->host.reallocate = resize; h->host.release = release;
    h->host.enter = enter; h->host.leave = leave; h->host.has_liberator = has; h->host.liberate = liberate;
    h->host.before_allocate = before_allocate; h->host.allocated = allocated; h->host.inspect_resize = inspect;
    h->host.before_resize = before_resize; h->host.relocated = relocated; h->host.resized = resized;
    h->host.before_free = before_free; h->host.block_size = block_size; h->host.validate = validate;
    h->host.too_large = too_large; h->host.failed = failed; h->phase = 1;
}
static void reset(Heap *h) { h->n = 0; h->events[0] = 0; }
static void transactions(void)
{
    Heap h; void *owner = NULL, *original; unsigned char saved[64]; unsigned i;
    init(&h, &owner);
    assert(mm_fixed_allocate(&h.state, &h.layout, &h.host, &h.stats, &owner, 8, 1, 2));
    assert(!strcmp(h.events, "LULPUAMLUV") && h.stats.cv == 1 && h.stats.cb == 8);
    for (i = 0; i < 8; ++i) assert(!((unsigned char *)owner)[i]);
    memset(owner, 0x37, 8); original = owner; memcpy(saved, h.raw, h.size);
    h.phase = 2; h.before_bytes = 8; h.stats.cactFail = 1; reset(&h);
    assert(!mm_fixed_resize(&h.state, &h.layout, &h.host, &h.stats, &owner, 16, 8, 1, 2));
    assert(owner == original && !memcmp(saved, h.raw, h.size) && !strcmp(h.events, "ILUVE") && !h.stats.cactFail);
    h.stats.cactFail = 1; h.resize_fail = 1; reset(&h);
    assert(!mm_fixed_resize(&h.state, &h.layout, &h.host, &h.stats, &owner, 4, 8, 0, 1));
    assert(owner == original && h.stats.cactFail == 1 && !memcmp(saved, h.raw, h.size));
    assert(!strcmp(h.events, "IZRHVE")); /* H is the absent-liberator probe. */
    assert(h.resizes == 1 && h.liberations == 0);
    reset(&h);
    assert(mm_fixed_resize(&h.state, &h.layout, &h.host, &h.stats, &owner, 4, 8, 0, 1) && h.stats.cactFail == 1);
    for (i = 0; i < 4; ++i) assert(((unsigned char *)owner)[i] == 0x37);
    h.before_bytes = 4; h.stats.cactFail = 0;
    assert(mm_fixed_resize(&h.state, &h.layout, &h.host, &h.stats, &owner, 12, 4, 1, 3));
    for (i = 4; i < 12; ++i) assert(!((unsigned char *)owner)[i]);
    h.before_bytes = 12;
    assert(mm_fixed_resize(&h.state, &h.layout, &h.host, &h.stats, &owner, 20, 12, 0, 3));
    for (i = 12; i < 20; ++i) assert(((unsigned char *)owner)[i] == 0xa3);
    original = owner; reset(&h); h.before_bytes = 20;
    assert(!mm_fixed_resize(&h.state, &h.layout, &h.host, &h.stats, &owner, 1025, 20, 0, 0));
    assert(owner == original && !strcmp(h.events, "IQVE"));
    reset(&h); mm_fixed_free(&h.layout, &h.host, &h.stats, &owner);
    assert(!owner && h.frees == 1 && !strcmp(h.events, "DBF"));
    reset(&h); mm_fixed_free(&h.layout, &h.host, &h.stats, &owner); assert(!h.n);
    owner = (void *)1; h.phase = 1; h.stats.cactFail = 1;
    assert(!mm_fixed_allocate(&h.state, &h.layout, &h.host, &h.stats, &owner, 1025, 0, 0));
    assert(!owner && h.stats.cactFail == 1);
    reset(&h); owner = (void *)1;
    assert(!mm_fixed_allocate(&h.state, &h.layout, &h.host, &h.stats, &owner, 5, 0, 0));
    assert(!owner && !strcmp(h.events, "LUE"));
}
static void retries(void)
{
    Heap h; void *owner = NULL; unsigned i;
    init(&h, &owner); h.alloc_fail = 2; h.available = 1; h.freed = 1; h.nested = 1;
    assert(mm_fixed_allocate(&h.state, &h.layout, &h.host, &h.stats, &owner, 5, 0, 7));
    assert(h.allocs == 3 && h.liberations == 1 && h.failures == 1 && !h.state.in_liberator);
    assert(h.lib_bytes == 23 && h.lib_priority == 7 && h.stats.cv == 1);
    h.nested = 0; h.phase = 2; h.before_bytes = 5; h.resize_fail = 1;
    assert(mm_fixed_resize(&h.state, &h.layout, &h.host, &h.stats, &owner, 12, 5, 1, 9));
    assert(h.lib_bytes == 7 && h.lib_priority == 9 && h.liberations == 2 && !h.state.in_liberator);
    for (i = 5; i < 12; ++i) assert(!((unsigned char *)owner)[i]);
    h.before_bytes = 12; h.resize_fail = 1; h.freed = 0;
    assert(!mm_fixed_resize(&h.state, &h.layout, &h.host, &h.stats, &owner, 6, 12, 0, 2));
    assert(h.lib_bytes == -6 && h.liberations == 3 && !h.state.in_liberator);
    h.before_bytes = 12; h.resize_fail = 1; h.freed = -1;
    assert(!mm_fixed_resize(&h.state, &h.layout, &h.host, &h.stats, &owner, 13, 12, 0, 2));
    assert(h.liberations == 4 && !h.state.in_liberator);
    mm_fixed_free(&h.layout, &h.host, &h.stats, &owner);
}
static void defaults_and_stats(void)
{
    MMFixedMemory state = {0}; MMMemoryLayout layout = {0,0,1024,0,0};
    MMMemoryStats stats = {0}; void *owner = NULL;
    assert(mm_fixed_allocate(&state, &layout, NULL, NULL, &owner, 0, 1, 0) && owner);
    assert(mm_fixed_resize(&state, &layout, NULL, NULL, &owner, 4, 0, 1, 0));
    assert(!memcmp(owner, "\0\0\0\0", 4));
    mm_fixed_free(&layout, NULL, NULL, &owner); assert(!owner);
    stats.cactDo = 1; stats.cactFail = 2;
    assert(!mm_memory_stats_fail(&stats) && mm_memory_stats_fail(&stats) && mm_memory_stats_fail(&stats) && !mm_memory_stats_fail(&stats));
    mm_memory_stats_allocate(&stats, 8); mm_memory_stats_allocate(&stats, 12);
    mm_memory_stats_resize(&stats, -4); mm_memory_stats_free(&stats, 16); mm_memory_stats_free(&stats, 0);
    assert(stats.cv == 0 && stats.cvTot == 2 && stats.cvRun == 2 && stats.cb == 0 && stats.cbRun == 20);
    stats.cv = stats.cvTot = stats.cb = 2147483647L;
    mm_memory_stats_allocate(&stats, 1);
    assert(stats.cv == (-2147483647L - 1) && stats.cvTot == (-2147483647L - 1) && stats.cb == (-2147483647L - 1));
}
int main(void) { transactions(); retries(); defaults_and_stats(); return 0; }

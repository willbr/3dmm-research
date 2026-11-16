#include "mm/handle_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
typedef union Header { MMHandleDebugRecord debug; long double alignment; void *pointer; } Header;
typedef struct Test {
    MMHandleLayout layout;
    MMHandleHost host;
    void **watch, *saved;
    int fail_allocate, fail_resize, expected, errors, allocations, resizes, releases, marks, failures;
    char events[200]; int used;
    MMU32 flags, bytes, old_bytes;
    MMI32 priority;
} Test;
static void event(Test *t, char c) { CHECK(t->used < 198); t->events[t->used++] = c; t->events[t->used] = 0; }
static void reset(Test *t) { t->used = 0; t->events[0] = 0; }
static MMHandleDebugRecord *record(Test *t, void *p)
{ return (MMHandleDebugRecord *)((unsigned char *)p - t->layout.prefix); }
static int allocate(void *v, void **raw, MMU32 bytes, MMU32 flags, MMI32 priority)
{
    Test *t = (Test *)v;
    CHECK(raw != t->watch && *t->watch == t->saved && bytes < 512);
    ++t->allocations; t->flags = flags; t->bytes = bytes; t->priority = priority; event(t, 'A');
    if (t->fail_allocate) { *raw = NULL; event(t, 'v'); return 0; }
    *raw = flags & 1 ? calloc(1, bytes) : malloc(bytes); CHECK(*raw); return 1;
}
static int resize(void *v, void **raw, MMU32 bytes, MMU32 old_bytes, MMU32 flags, MMI32 priority)
{
    Test *t = (Test *)v;
    void *next;
    CHECK(raw != t->watch && *t->watch == t->saved && bytes < 512);
    ++t->resizes; t->flags = flags; t->bytes = bytes; t->old_bytes = old_bytes; t->priority = priority; event(t, 'R');
    if (t->fail_resize) { event(t, 'v'); return 0; }
    next = malloc(bytes); CHECK(next); memcpy(next, *raw, bytes < old_bytes ? bytes : old_bytes);
    if ((flags & 1) && bytes > old_bytes) memset((unsigned char *)next + old_bytes, 0, bytes - old_bytes);
    free(*raw); *raw = next; return 1;
}
static void release(void *v, void **raw)
{ Test *t = (Test *)v; CHECK(raw != t->watch && !*t->watch); ++t->releases; event(t, 'F'); free(*raw); *raw = NULL; }
static void address(void *v, const void *p, size_t bytes) { (void)v; (void)bytes; CHECK(p); }
static void assertion(void *v, int code, const void *p, size_t bytes)
{
    Test *t = (Test *)v;
    CHECK(code == t->expected); ++t->errors; event(t, 'D');
    if (code == MM_HANDLE_FREE_LOCKED) CHECK(!*t->watch);
    if (code == MM_HANDLE_MAGIC) CHECK(p && bytes == t->layout.prefix);
    else CHECK(!p && !bytes);
}
static void validate(void *v, void *raw, MMI32 bytes)
{
    Test *t = (Test *)v;
    MMHandleDebugRecord *r = (MMHandleDebugRecord *)raw;
    CHECK(r->magic == t->layout.magic && bytes == r->state.bytes + (MMI32)t->layout.prefix); event(t, 'V');
}
static void mark(void *v, void *raw)
{ Test *t = (Test *)v; CHECK(raw == record(t, *t->watch)); ++t->marks; event(t, 'M'); }
static void failed(void *v)
{ Test *t = (Test *)v; CHECK(*t->watch == t->saved); ++t->failures; event(t, 'H'); }
int main(void)
{
    Test t;
    void *p, *copy;
    unsigned char *data;
    int calls, errors;
    MMHandleLayout plain;
    memset(&t, 0, sizeof(t));
    t.layout.prefix = sizeof(Header); t.layout.limit = 10000; t.layout.magic = 1234567; t.layout.debug = 1; t.layout.expensive = 1;
    t.host.context = &t; t.host.allocate = allocate; t.host.resize = resize; t.host.release = release; t.host.address = address;
    t.host.assertion = assertion; t.host.validate_fixed = validate; t.host.mark_fixed = mark; t.host.failed = failed;
    p = &t; t.watch = &p; t.saved = p; t.fail_allocate = 1;
    CHECK(!mm_handle_allocate(&t.layout, &t.host, &p, 9, 1, 7) && !p);
    CHECK(!strcmp(t.events, "AvH") && t.failures == 1 && t.bytes == t.layout.prefix + 9 && t.priority == 7);
    t.fail_allocate = 0; t.saved = p; reset(&t);
    CHECK(mm_handle_allocate(&t.layout, &t.host, &p, 9, 1, 3)); CHECK(!strcmp(t.events, "AV"));
    CHECK(mm_handle_size(&t.layout, &t.host, p) == 9 && !record(&t, p)->state.locks);
    CHECK(mm_handle_pointer(&t.layout, &t.host, p) == p);
    data = (unsigned char *)mm_handle_lock(&t.layout, &t.host, p); CHECK(data == p);
    CHECK(mm_handle_lock(&t.layout, &t.host, p) == p && record(&t, p)->state.locks == 2);
    for (calls = 0; calls < 9; ++calls) { CHECK(!data[calls]); data[calls] = (unsigned char)(calls + 1); }
    reset(&t); t.saved = p; calls = t.resizes; t.expected = MM_HANDLE_RESIZE_LOCKED;
    CHECK(!mm_handle_resize(&t.layout, &t.host, &p, 20, 1, 2));
    CHECK(!strcmp(t.events, "VDH") && t.resizes == calls && p == t.saved);
    t.expected = 0; mm_handle_unlock(&t.layout, &t.host, p);
    copy = &t; t.watch = &copy; t.saved = copy; reset(&t);
    CHECK(mm_handle_copy(&t.layout, &t.host, p, &copy, 5));
    CHECK(copy != p && !memcmp(copy, p, 9) && record(&t, p)->state.locks == 1 && !record(&t, copy)->state.locks);
    CHECK(t.flags == 0 && t.priority == 5); mm_handle_free(&t.layout, &t.host, &copy); CHECK(!copy);
    t.watch = &p; t.saved = p; mm_handle_unlock(&t.layout, &t.host, p);
    t.expected = MM_HANDLE_NOT_LOCKED; mm_handle_unlock(&t.layout, &t.host, p); CHECK(!record(&t, p)->state.locks); t.expected = 0;
    reset(&t); t.fail_resize = 1;
    CHECK(!mm_handle_resize(&t.layout, &t.host, &p, 20, 1, 8));
    CHECK(!strcmp(t.events, "VRvHV") && p == t.saved && mm_handle_size(&t.layout, &t.host, p) == 9);
    for (calls = 0; calls < 9; ++calls) CHECK(((unsigned char *)p)[calls] == calls + 1);
    t.fail_resize = 0; CHECK(mm_handle_resize(&t.layout, &t.host, &p, 20, 1, 9));
    CHECK(t.old_bytes == t.layout.prefix + 9 && t.bytes == t.layout.prefix + 20 && t.priority == 9);
    for (calls = 0; calls < 20; ++calls) CHECK(((unsigned char *)p)[calls] == (calls < 9 ? calls + 1 : 0));
    t.saved = p; CHECK(mm_handle_resize(&t.layout, &t.host, &p, 0, 0, 1) && p && !mm_handle_size(&t.layout, &t.host, p));
    record(&t, p)->magic = 0; t.expected = MM_HANDLE_MAGIC; reset(&t);
    mm_handle_validate(&t.layout, &t.host, p); CHECK(!strcmp(t.events, "D")); record(&t, p)->magic = t.layout.magic; t.expected = 0;
    record(&t, p)->state.locks = -1; t.expected = MM_HANDLE_SMALL; mm_handle_validate(&t.layout, &t.host, p); t.expected = 0; record(&t, p)->state.locks = 0;
    t.expected = MM_HANDLE_NIL; mm_handle_validate(&t.layout, &t.host, NULL); t.expected = 0;
    calls = t.marks; mm_handle_mark(&t.layout, &t.host, NULL); CHECK(t.marks == calls);
    mm_handle_mark(&t.layout, &t.host, p); CHECK(t.marks == calls + 1);
    t.layout.expensive = 0; record(&t, p)->state.locks = 2147483647L; t.expected = MM_HANDLE_LOCK_OVERFLOW;
    CHECK(mm_handle_lock(&t.layout, &t.host, p) == p && record(&t, p)->state.locks == (-2147483647L - 1));
    t.expected = MM_HANDLE_NOT_LOCKED; mm_handle_unlock(&t.layout, &t.host, p); CHECK(record(&t, p)->state.locks == (-2147483647L - 1));
    t.expected = 0; record(&t, p)->state.locks = 1; t.expected = MM_HANDLE_FREE_LOCKED; reset(&t);
    mm_handle_free(&t.layout, &t.host, &p); CHECK(!p && !strcmp(t.events, "DF")); t.expected = 0;
    calls = t.releases; mm_handle_free(&t.layout, &t.host, &p); CHECK(t.releases == calls);
    t.saved = p; t.expected = MM_HANDLE_SMALL; calls = t.allocations;
    CHECK(!mm_handle_allocate(&t.layout, &t.host, &p, 0xffffffffUL, 0, 0) && !p && t.allocations == calls); t.expected = 0;
    t.layout.debug = 0; errors = t.errors; t.saved = p;
    CHECK(mm_handle_allocate(&t.layout, &t.host, &p, 0, 1, 0));
    mm_handle_unlock(&t.layout, &t.host, p); CHECK(t.errors == errors);
    mm_handle_free(&t.layout, &t.host, &p);
    plain = t.layout; plain.debug = 1; plain.expensive = 1;
    CHECK(mm_handle_allocate(&plain, NULL, &p, 7, 1, 0));
    for (calls = 0; calls < 7; ++calls) CHECK(!((unsigned char *)p)[calls]);
    memset(p, 0x37, 7); CHECK(mm_handle_resize(&plain, NULL, &p, 20, 1, 0));
    for (calls = 0; calls < 20; ++calls) CHECK(((unsigned char *)p)[calls] == (calls < 7 ? 0x37 : 0));
    CHECK(mm_handle_copy(&plain, NULL, p, &copy, 0) && !memcmp(p, copy, 20));
    mm_handle_free(&plain, NULL, &p); mm_handle_free(&plain, NULL, &copy);
    puts("C89 HQ handle policy checks passed"); return 0;
}

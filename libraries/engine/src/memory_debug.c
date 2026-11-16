/* Fixed-memory diagnostics converted from Kauai utilmem.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/memory_debug.h"
#include <string.h>
static MMI32 bits(MMU32 v) { return v <= 0x7fffffffUL ? (MMI32)v : -(MMI32)(~v) - 1; }
static void enter(const MMMemoryDebugHost *h) { if (h && h->enter) h->enter(h->context); }
static void leave(const MMMemoryDebugHost *h) { if (h && h->leave) h->leave(h->context); }
static void address(const MMMemoryDebugHost *h, const void *p, size_t n)
{ if (h && h->address) h->address(h->context, p, n); }
static void check(const MMMemoryDebugHost *h, int ok, int code, const void *p, size_t n)
{ if (!ok && h && h->assertion) h->assertion(h->context, code, p, n); }
static MMMemoryCounter thread(const MMMemoryDebugHost *h)
{ return h && h->current_thread ? h->current_thread(h->context) : 0; }
static int enabled(const MMMemoryDebugLayout *l)
{ return l->expensive && (!l->suspended || !*l->suspended); }
static void footer(const MMMemoryDebugLayout *l, MMMemoryDebugRecord *r)
{ memcpy((unsigned char *)r + r->total - l->suffix, &l->magic, sizeof(l->magic)); }
static void link(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, MMMemoryDebugRecord *r)
{
    address(h, r, l->prefix);
    enter(h); r->previous = NULL; r->next = g->first;
    if (g->first) {
        check(h, !g->first->previous, MM_MEMORY_DEBUG_FIRST_PREVIOUS, NULL, 0);
        g->first->previous = r;
    }
    g->first = r; leave(h);
}
static void unlink_token(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h,
                         MMMemoryDebugRecord *r, const unsigned char *old)
{
    MMMemoryDebugRecord *nil = NULL;
    address(h, r, l->prefix);
    check(h, memcmp(old, &nil, sizeof(nil)) != 0, MM_MEMORY_DEBUG_OLD_NIL, NULL, 0);
    enter(h);
    if (!r->previous) {
        check(h, !memcmp(&g->first, old, sizeof(g->first)), MM_MEMORY_DEBUG_HEAD, NULL, 0);
        g->first = r->next;
    } else {
        check(h, memcmp(&g->first, old, sizeof(g->first)) != 0, MM_MEMORY_DEBUG_NOT_HEAD, NULL, 0);
        check(h, !memcmp(&r->previous->next, old, sizeof(r->previous->next)), MM_MEMORY_DEBUG_PREVIOUS_NEXT, NULL, 0);
        r->previous->next = r->next;
    }
    if (r->next) {
        check(h, !memcmp(&r->next->previous, old, sizeof(r->next->previous)), MM_MEMORY_DEBUG_NEXT_PREVIOUS, NULL, 0);
        r->next->previous = r->previous;
    }
    leave(h);
}
void mm_memory_debug_init(MMMemoryDebugGraph *g) { g->first = NULL; }
void mm_memory_debug_before_allocate(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, MMU32 payload)
{
    if (g->first) mm_memory_debug_validate_record(g, l, h, g->first);
    check(h, payload + l->prefix + l->suffix > payload, MM_MEMORY_DEBUG_OVERHEAD, NULL, 0);
}
void mm_memory_debug_created(const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, MMMemoryDebugRecord *r,
                             MMU32 total, char *source, MMMemoryCounter line)
{
    r->total = bits(total); r->magic = l->magic; r->source = source; r->line = line; r->thread = thread(h);
}
void mm_memory_debug_publish(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, MMMemoryDebugRecord *r)
{ footer(l, r); link(g, l, h, r); }
void mm_memory_debug_unlink(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, MMMemoryDebugRecord *r)
{
    unsigned char old[sizeof(r)];
    memcpy(old, &r, sizeof(r)); unlink_token(g, l, h, r, old);
}
void mm_memory_debug_validate_record(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, MMMemoryDebugRecord *r)
{
    MMI32 payload;
    short tail;
    if (!enabled(l)) return;
    enter(h); address(h, r, l->prefix);
    check(h, r->magic == l->magic, MM_MEMORY_DEBUG_MAGIC, NULL, 0);
    payload = bits((MMU32)r->total - l->prefix - l->suffix);
    check(h, payload >= 0, MM_MEMORY_DEBUG_SMALL, NULL, 0);
    check(h, payload < (MMI32)l->limit, MM_MEMORY_DEBUG_BIG, NULL, 0);
    if (h && h->allocation_size)
        check(h, r->total <= bits((MMU32)h->allocation_size(h->context, r)), MM_MEMORY_DEBUG_CAPACITY, NULL, 0);
    address(h, r, (size_t)r->total);
    if (r->previous) {
        address(h, r->previous, l->prefix);
        check(h, r->previous->next == r, MM_MEMORY_DEBUG_BAD_PREVIOUS, NULL, 0);
        check(h, r != g->first, MM_MEMORY_DEBUG_FIRST_HAS_PREVIOUS, NULL, 0);
    }
    if (r->next) {
        address(h, r->next, l->prefix);
        check(h, r->next->previous == r, MM_MEMORY_DEBUG_BAD_NEXT, NULL, 0);
    }
    memcpy(&tail, (unsigned char *)r + r->total - l->suffix, sizeof(tail));
    check(h, tail == l->magic, MM_MEMORY_DEBUG_TAIL, NULL, 0);
    leave(h);
}
void mm_memory_debug_validate_payload(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, void *p, MMI32 bytes)
{
    MMMemoryDebugRecord *r;
    if (!l->expensive) { check(h, p != NULL, MM_MEMORY_DEBUG_NIL, NULL, 0); return; }
    if (!enabled(l)) return;
    check(h, p != NULL, MM_MEMORY_DEBUG_NIL, NULL, 0);
    r = (MMMemoryDebugRecord *)((unsigned char *)p - l->prefix);
    mm_memory_debug_validate_record(g, l, h, r);
    if (bytes != -1) check(h, r->total == bits((MMU32)bytes + l->prefix + l->suffix), MM_MEMORY_DEBUG_SIZE, NULL, 0);
}
void mm_memory_debug_prepare_resize(const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, MMMemoryDebugRecord *r,
                                    MMU32 old_total, MMU32 new_total, MMMemoryDebugMove *move)
{
    MMMemoryCounter expected = bits(old_total);
    check(h, old_total >= l->prefix + l->suffix && new_total >= l->prefix + l->suffix, MM_MEMORY_DEBUG_RESIZE_OVERHEAD, NULL, 0);
    check(h, r->total == expected, MM_MEMORY_DEBUG_OLD_SIZE, &expected, sizeof(expected));
    memcpy(move->record_address, &r, sizeof(r));
}
void mm_memory_debug_relocated(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h,
                               void *raw, const MMMemoryDebugMove *move, const unsigned char *old_address)
{
    MMMemoryDebugRecord *r = (MMMemoryDebugRecord *)raw;
    if (!memcmp(&raw, old_address, sizeof(raw))) {
        /* Even an in-place realloc invalidates old aliases. Publish the fresh
         * pointer without evaluating any stored alias to the previous block. */
        enter(h);
        if (r->previous) r->previous->next = r; else g->first = r;
        if (r->next) r->next->previous = r;
        leave(h); return;
    }
    unlink_token(g, l, h, r, move->record_address); link(g, l, h, r);
}
void mm_memory_debug_resized(const MMMemoryDebugLayout *l, MMMemoryDebugRecord *r, MMU32 total)
{ r->total = bits(total); footer(l, r); }
void mm_memory_debug_before_free(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, MMMemoryDebugRecord *r)
{ mm_memory_debug_validate_record(g, l, h, r); mm_memory_debug_unlink(g, l, h, r); }
void mm_memory_debug_unmark(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h)
{
    MMMemoryDebugRecord *r;
    MMMemoryCounter owner = thread(h);
    enter(h);
    for (r = g->first; r; r = r->next) {
        mm_memory_debug_validate_record(g, l, h, r);
        if (r->thread == owner) r->marks = 0;
    }
    if (h && h->after_unmark) h->after_unmark(h->context);
    leave(h);
}
void mm_memory_debug_report(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h)
{
    MMMemoryDebugRecord *r;
    MMMemoryCounter owner = thread(h);
    enter(h);
    for (r = g->first; r; r = r->next) {
        mm_memory_debug_validate_record(g, l, h, r);
        if (!r->marks && r->thread == owner && h && h->lost) h->lost(h->context, r);
    }
    if (h && h->after_report) h->after_report(h->context);
    leave(h);
}
void mm_memory_debug_mark(MMMemoryDebugGraph *g, const MMMemoryDebugLayout *l, const MMMemoryDebugHost *h, void *p)
{
    MMMemoryDebugRecord *r;
    if (!p) return;
    mm_memory_debug_validate_payload(g, l, h, p, -1);
    r = (MMMemoryDebugRecord *)((unsigned char *)p - l->prefix);
    r->marks = r->marks == SHRT_MAX ? SHRT_MIN : (short)(r->marks + 1);
}

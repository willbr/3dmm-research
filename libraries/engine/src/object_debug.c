/* Object diagnostics converted from Kauai base.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/object_debug.h"
#include <stddef.h>
static void enter(const MMObjectDebugHost *h) { if (h && h->enter) h->enter(h->context); }
static void leave(const MMObjectDebugHost *h) { if (h && h->leave) h->leave(h->context); }
static void address(const MMObjectDebugHost *h, const void *p, size_t bytes)
{ if (h && h->address) h->address(h->context, p, bytes); }
static void check(const MMObjectDebugHost *h, int condition, int code, const void *p, size_t bytes)
{ if (!condition && h && h->assertion) h->assertion(h->context, code, p, bytes); }
static MMMemoryCounter thread(const MMObjectDebugHost *h)
{ return h && h->current_thread ? h->current_thread(h->context) : 0; }
static void *object(const MMObjectDebugLayout *l, MMObjectDebugRecord *r)
{ return (unsigned char *)r + l->prefix; }
static MMObjectDebugRecord *record(const MMObjectDebugLayout *l, void *p)
{ return (MMObjectDebugRecord *)((unsigned char *)p - l->prefix); }
void mm_object_debug_init(MMObjectDebugGraph *g) { g->objects = NULL; g->raw = NULL; }
void mm_object_debug_validate_record(const MMObjectDebugLayout *l, const MMObjectDebugHost *h, MMObjectDebugRecord *r, int linked)
{
    enter(h);
    address(h, r, l->prefix + l->object_bytes);
    check(h, r->total >= (MMMemoryCounter)(l->prefix + l->object_bytes), MM_OBJECT_DEBUG_SIZE, NULL, 0);
    check(h, r->total < l->limit, MM_OBJECT_DEBUG_SIZE_BIG, NULL, 0);
    address(h, r, (size_t)r->total);
    check(h, r->magic == l->magic, MM_OBJECT_DEBUG_MAGIC, &r->magic, sizeof(r->magic));
    check(h, r->marks >= 0, MM_OBJECT_DEBUG_MARKS, &r->marks, sizeof(r->marks));
    if (!r->previous) check(h, !linked, MM_OBJECT_DEBUG_UNLINKED, NULL, 0);
    else {
        check(h, linked, MM_OBJECT_DEBUG_LINKED, NULL, 0);
        address(h, r->previous, sizeof(*r->previous));
        check(h, *r->previous == r, MM_OBJECT_DEBUG_PREVIOUS, r->previous, sizeof(*r->previous));
        if (r->next) {
            address(h, r->next, l->prefix);
            check(h, r->next->previous == &r->next, MM_OBJECT_DEBUG_NEXT, &r->next->previous, sizeof(r->next->previous));
        }
    }
    leave(h);
}
void mm_object_debug_link(const MMObjectDebugLayout *l, const MMObjectDebugHost *h, MMObjectDebugRecord *r, MMObjectDebugRecord **head)
{
    enter(h);
    mm_object_debug_validate_record(l, h, r, 0); address(h, head, sizeof(*head));
    if (*head) { mm_object_debug_validate_record(l, h, *head, 1); (*head)->previous = &r->next; }
    r->next = *head; r->previous = head; *head = r;
    mm_object_debug_validate_record(l, h, r, 1);
    leave(h);
}
void mm_object_debug_unlink(const MMObjectDebugLayout *l, const MMObjectDebugHost *h, MMObjectDebugRecord *r)
{
    enter(h);
    mm_object_debug_validate_record(l, h, r, 1);
    *r->previous = r->next;
    if (r->next) { r->next->previous = r->previous; mm_object_debug_validate_record(l, h, r->next, 1); }
    r->previous = NULL; r->next = NULL;
    mm_object_debug_validate_record(l, h, r, 0);
    leave(h);
}
void mm_object_debug_created(MMObjectDebugGraph *g, const MMObjectDebugLayout *l, const MMObjectDebugHost *h,
                             MMObjectDebugRecord *r, char *source, MMMemoryCounter line, MMMemoryCounter total, MMMemoryCounter owner_thread)
{
    r->magic = l->magic; r->marks = 0; r->source = source;
    r->line = line; r->total = total; r->thread = owner_thread; r->previous = NULL;
    mm_object_debug_link(l, h, r, &g->raw);
}
void mm_object_debug_attach(MMObjectDebugGraph *g, const MMObjectDebugLayout *l, const MMObjectDebugHost *h, void *p, MMObjectDebugState *s)
{
    MMObjectDebugRecord *r = g->raw;
    if (r) {
        enter(h);
        for (r = g->raw; r && p != object(l, r); r = r->next) mm_object_debug_validate_record(l, h, r, 1);
        if (r) {
            mm_object_debug_unlink(l, h, r); mm_object_debug_link(l, h, r, &g->objects);
            s->kind = MM_OBJECT_DEBUG_ALLOCATED;
            if (h && h->attached) h->attached(h->context, p);
        }
        leave(h);
    }
    if (!r) s->kind = MM_OBJECT_DEBUG_LOCAL;
}
void mm_object_debug_validate(const MMObjectDebugLayout *l, const MMObjectDebugHost *h, void *p, const MMObjectDebugState *s, MMI32 references, MMU32 flags)
{
    address(h, p, l->object_bytes);
    check(h, references >= 0, MM_OBJECT_DEBUG_REFERENCES, NULL, 0);
    check(h, references < l->limit, MM_OBJECT_DEBUG_REFERENCES_BIG, NULL, 0);
    if (s->kind != MM_OBJECT_DEBUG_ALLOCATED) {
        check(h, !(flags & MM_OBJECT_REQUIRE_ALLOCATED), MM_OBJECT_DEBUG_EXPECT_ALLOCATED, NULL, 0);
        check(h, s->kind == MM_OBJECT_DEBUG_LOCAL, MM_OBJECT_DEBUG_KIND, &s->kind, sizeof(s->kind));
        address(h, p, l->object_bytes); return;
    }
    check(h, !(flags & MM_OBJECT_REQUIRE_LOCAL), MM_OBJECT_DEBUG_EXPECT_LOCAL, NULL, 0);
    enter(h); mm_object_debug_validate_record(l, h, record(l, p), 1); leave(h);
}
void mm_object_debug_mark_stub(const MMObjectDebugLayout *l, const MMObjectDebugHost *h, void *p, const MMObjectDebugState *s)
{
    MMObjectDebugRecord *r;
    if (h && h->validate) h->validate(h->context, p, 0);
    if (s->kind != MM_OBJECT_DEBUG_ALLOCATED) {
        check(h, s->kind == MM_OBJECT_DEBUG_LOCAL, MM_OBJECT_DEBUG_KIND, &s->kind, sizeof(s->kind));
        if (h && h->mark) h->mark(h->context, p);
        return;
    }
    r = record(l, p);
    if (r->marks == 0 && r->thread == thread(h) && h && h->mark) h->mark(h->context, p);
}
void mm_object_debug_mark(const MMObjectDebugLayout *l, const MMObjectDebugHost *h, void *p, const MMObjectDebugState *s, MMI32 references)
{
    MMObjectDebugRecord *r;
    mm_object_debug_validate(l, h, p, s, references, 0);
    if (s->kind == MM_OBJECT_DEBUG_ALLOCATED) {
        r = record(l, p);
        r->marks = r->marks == SHRT_MAX ? SHRT_MIN : (short)(r->marks + 1);
    }
}
void mm_object_debug_unmark(MMObjectDebugGraph *g, const MMObjectDebugLayout *l, const MMObjectDebugHost *h)
{
    MMObjectDebugRecord *r;
    MMMemoryCounter owner_thread;
    enter(h); owner_thread = thread(h);
    check(h, !g->raw, MM_OBJECT_DEBUG_RAW_LIST, NULL, 0);
    for (r = g->objects; r; r = r->next) {
        if (h && h->validate) h->validate(h->context, object(l, r), MM_OBJECT_REQUIRE_ALLOCATED);
        if (r->thread == owner_thread) r->marks = 0;
    }
    leave(h);
}
void mm_object_debug_report(MMObjectDebugGraph *g, const MMObjectDebugLayout *l, const MMObjectDebugHost *h)
{
    MMObjectDebugRecord *r, *last = NULL;
    MMMemoryCounter owner_thread;
    MMI32 count = 0;
    int details;
    void *p;
    enter(h); owner_thread = thread(h);
    check(h, !g->raw, MM_OBJECT_DEBUG_RAW_LIST, NULL, 0);
    for (r = g->objects; r; r = r->next) {
        last = r;
        if (!r->marks && r->thread == owner_thread) ++count;
    }
    if (count) {
        details = h && h->lost_summary ? h->lost_summary(h->context, count) : 0;
        for (r = last;;) {
            p = object(l, r);
            if (h && h->validate) h->validate(h->context, p, MM_OBJECT_REQUIRE_ALLOCATED);
            if (!r->marks && r->thread == owner_thread) {
                if (details && h && h->lost_object) h->lost_object(h->context, p, r, (size_t)r->total - l->prefix);
                if (h && h->mark_root) h->mark_root(h->context, p);
            }
            if (r->previous == &g->objects) break;
            r = (MMObjectDebugRecord *)((unsigned char *)r->previous - offsetof(MMObjectDebugRecord, next));
        }
    }
    leave(h);
}

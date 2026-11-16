/* Error stack converted from Kauai utilerro.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/error_stack.h"
#include <string.h>
MMErrorStackView mm_error_stack_view(MMErrorStack *s)
{
    MMErrorStackView view;
    view.count = &s->count; view.entries = s->entries; view.stride = sizeof(s->entries[0]); view.debug = 0;
    return view;
}
MMErrorStackView mm_error_stack_debug_view(MMErrorStackDebug *s)
{
    MMErrorStackView view;
    view.count = &s->count; view.entries = s->entries; view.stride = sizeof(s->entries[0]); view.debug = 1;
    return view;
}
static void enter(const MMErrorStackHost *h) { if (h && h->enter) h->enter(h->context); }
static void leave(const MMErrorStackHost *h) { if (h && h->leave) h->leave(h->context); }
static MMI32 code(const MMErrorStackView *s, MMI32 i)
{
    return s->debug ? ((MMErrorRecord *)s->entries)[i].code : ((MMI32 *)s->entries)[i];
}
void mm_error_stack_init(const MMErrorStackView *s) { *s->count = 0; }
void mm_error_stack_push(const MMErrorStackView *s, const MMErrorStackHost *h, MMI32 value, const char *source, MMI32 line)
{
    MMI32 i;
    MMErrorRecord *record;
    if (h && h->report_push) h->report_push(h->context, value, source, line);
    enter(h);
    if (*s->count && code(s, *s->count - 1) == value) { leave(h); return; }
    if (*s->count == MM_ERROR_CAPACITY) {
        if (h && h->report_overflow) h->report_overflow(h->context);
        memmove(s->entries, (unsigned char *)s->entries + s->stride, (MM_ERROR_CAPACITY - 1) * s->stride);
        --*s->count;
    }
    i = *s->count;
    if (s->debug) {
        record = (MMErrorRecord *)s->entries + i;
        record->source = source; record->line = line; record->code = value;
    } else ((MMI32 *)s->entries)[i] = value;
    ++*s->count;
    leave(h);
}
int mm_error_stack_pop(const MMErrorStackView *s, const MMErrorStackHost *h, void *output)
{
    MMI32 value;
    enter(h);
    if (!*s->count) {
        if (output && h && h->underflow) h->underflow(h->context, output);
        leave(h); return 0;
    }
    value = code(s, --*s->count);
    if (output) {
        if (h && h->write) h->write(h->context, output, value);
        else *(MMI32 *)output = value;
    }
    leave(h); return 1;
}
void mm_error_stack_clear(const MMErrorStackView *s, const MMErrorStackHost *h)
{ enter(h); *s->count = 0; leave(h); }
MMI32 mm_error_stack_count(const MMErrorStackView *s) { return *s->count; }
MMI32 mm_error_stack_get(const MMErrorStackView *s, const MMErrorStackHost *h, MMI32 index)
{
    MMI32 value;
    enter(h); value = index >= 0 && index < *s->count ? code(s, index) : -1; leave(h);
    return value;
}
int mm_error_stack_contains(const MMErrorStackView *s, const MMErrorStackHost *h, MMI32 value)
{
    MMI32 i;
    enter(h);
    for (i = 0; i < *s->count; ++i) if (code(s, i) == value) { leave(h); return 1; }
    leave(h); return 0;
}
void mm_error_stack_flush(const MMErrorStackView *s, const MMErrorStackHost *h, MMI32 value)
{
    MMI32 from, to;
    enter(h);
    for (from = to = 0; from < *s->count; ++from) {
        if (code(s, from) != value) {
            if (to < from) memmove((unsigned char *)s->entries + (size_t)to * s->stride,
                                  (unsigned char *)s->entries + (size_t)from * s->stride, s->stride);
            ++to;
        }
    }
    *s->count = to; leave(h);
}

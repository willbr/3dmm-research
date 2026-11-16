/* Fixed-memory control converted from Kauai utilmem.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/memory.h"
#include <stdlib.h>
#include <string.h>
static MMI32 bits(MMU32 value) { return value <= 0x7fffffffUL ? (MMI32)value : -(MMI32)(~value) - 1; }
int mm_memory_stats_fail(MMMemoryStats *s)
{
    if (s->cactFail <= 0) return 0;
    if (s->cactDo <= 0) { --s->cactFail; return 1; }
    --s->cactDo; return 0;
}
void mm_memory_stats_allocate(MMMemoryStats *s, MMI32 bytes)
{
    s->cv = bits((MMU32)s->cv + 1);
    if (s->cvRun < s->cv) s->cvRun = s->cv;
    s->cvTot = bits((MMU32)s->cvTot + 1);
    s->cb = bits((MMU32)s->cb + (MMU32)bytes);
    if (s->cbRun < s->cb) s->cbRun = s->cb;
}
void mm_memory_stats_resize(MMMemoryStats *s, MMI32 delta)
{
    s->cb = bits((MMU32)s->cb + (MMU32)delta);
    if (s->cbRun < s->cb) s->cbRun = s->cb;
}
void mm_memory_stats_free(MMMemoryStats *s, MMI32 bytes)
{ s->cv = bits((MMU32)s->cv - 1); s->cb = bits((MMU32)s->cb - (MMU32)bytes); }
static void enter(const MMMemoryHost *h) { if (h && h->enter) h->enter(h->context); }
static void leave(const MMMemoryHost *h) { if (h && h->leave) h->leave(h->context); }
static int injected(const MMMemoryHost *h, MMMemoryStats *s)
{
    int result;
    if (!s) return 0;
    enter(h); result = mm_memory_stats_fail(s); leave(h); return result;
}
static int retry(MMFixedMemory *s, const MMMemoryHost *h, MMI32 bytes, MMI32 priority)
{
    MMI32 freed;
    if (!h || !h->has_liberator || !h->has_liberator(h->context)) return 0;
    enter(h);
    if (s->in_liberator) freed = 0;
    else {
        s->in_liberator = 1; leave(h);
        freed = h->liberate(h->context, bytes, priority);
        enter(h); s->in_liberator = 0;
    }
    leave(h); return freed > 0;
}
int mm_fixed_allocate(MMFixedMemory *state, const MMMemoryLayout *layout, const MMMemoryHost *h, MMMemoryStats *stats,
                      void **owner, MMU32 bytes, MMU32 flags, MMI32 priority)
{
    MMU32 total;
    void *raw;
    if (bytes > layout->limit) {
        if (h && h->too_large) h->too_large(h->context, bytes, 0);
        goto fail;
    }
    if (injected(h, stats)) goto fail;
    if (h && h->before_allocate) { enter(h); h->before_allocate(h->context, bytes); leave(h); }
    total = bytes + layout->prefix + layout->suffix;
    for (;;) {
        raw = h && h->allocate ? h->allocate(h->context, total) : malloc(total ? total : 1);
        *owner = raw;
        if (raw || !retry(state, h, (MMI32)total, priority)) break;
    }
    if (!raw) goto fail;
    if (flags & 1) memset(raw, 0, total);
    else if (layout->debug_fill) memset(raw, layout->garbage, total);
    *owner = (unsigned char *)raw + layout->prefix;
    if (h && h->allocated) h->allocated(h->context, raw, total);
    if (stats) { enter(h); mm_memory_stats_allocate(stats, (MMI32)bytes); leave(h); }
    if (h && h->validate) h->validate(h->context, *owner, bytes);
    return 1;
fail:
    *owner = NULL;
    if (h && h->failed) h->failed(h->context, 0);
    return 0;
}
int mm_fixed_resize(MMFixedMemory *state, const MMMemoryLayout *layout, const MMMemoryHost *h, MMMemoryStats *stats,
                    void **owner, MMU32 new_bytes, MMU32 old_bytes, MMU32 flags, MMI32 priority)
{
    void *old_raw = (unsigned char *)*owner - layout->prefix;
    void *raw;
    unsigned char old_address[sizeof(void *)];
    MMU32 old_total, new_total;
    if (h && h->inspect_resize) h->inspect_resize(h->context, old_raw);
    if (new_bytes > layout->limit) {
        if (h && h->too_large) h->too_large(h->context, new_bytes, 1);
        goto fail;
    }
    if (new_bytes > old_bytes && injected(h, stats)) goto fail;
    old_total = old_bytes + layout->prefix + layout->suffix;
    new_total = new_bytes + layout->prefix + layout->suffix;
    if (h && h->before_resize) h->before_resize(h->context, old_raw, old_total, new_total);
    if (h && h->relocated) memcpy(old_address, &old_raw, sizeof(old_raw));
    for (;;) {
        raw = h && h->reallocate ? h->reallocate(h->context, old_raw, new_total) : realloc(old_raw, new_total ? new_total : 1);
        if (raw || !retry(state, h, bits(new_total - old_total), priority)) break;
    }
    if (!raw) goto fail;
    *owner = raw;
    if ((flags & 1) && old_total < new_total) memset((unsigned char *)raw + old_total, 0, new_total - old_total);
    if (layout->debug_fill && old_total < new_total) {
        if (flags & 1) memset((unsigned char *)raw + old_total - layout->suffix, 0, layout->suffix);
        else memset((unsigned char *)raw + old_total - layout->suffix, layout->garbage, new_total - old_total + layout->suffix);
    }
    if (h && h->relocated) h->relocated(h->context, raw, old_address);
    *owner = (unsigned char *)raw + layout->prefix;
    if (h && h->resized) h->resized(h->context, raw, new_total);
    if (h && h->validate) h->validate(h->context, *owner, new_bytes);
    if (stats) { enter(h); mm_memory_stats_resize(stats, bits(new_total - old_total)); leave(h); }
    return 1;
fail:
    if (h && h->validate) h->validate(h->context, *owner, old_bytes);
    if (h && h->failed) h->failed(h->context, 1);
    return 0;
}
void mm_fixed_free(const MMMemoryLayout *layout, const MMMemoryHost *h, MMMemoryStats *stats, void **owner)
{
    void *raw;
    MMU32 total = 0;
    if (!*owner) return;
    raw = (unsigned char *)*owner - layout->prefix;
    if (h && h->before_free) h->before_free(h->context, raw);
    if (layout->debug_fill || stats) total = h->block_size(h->context, raw);
    if (stats) mm_memory_stats_free(stats, (MMI32)(total - layout->prefix - layout->suffix));
    if (layout->debug_fill) memset(raw, layout->garbage, total);
    *owner = raw;
    if (h && h->release) h->release(h->context, raw); else free(raw);
    *owner = NULL;
}

/* Object allocation control converted from Kauai base.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/object_memory.h"
#include <stdlib.h>
#include <string.h>
static void enter_objects(const MMObjectHost *h) { if (h && h->enter_objects) h->enter_objects(h->context); }
static void leave_objects(const MMObjectHost *h) { if (h && h->leave_objects) h->leave_objects(h->context); }
static void enter_stats(const MMObjectHost *h) { if (h && h->enter_stats) h->enter_stats(h->context); }
static void leave_stats(const MMObjectHost *h) { if (h && h->leave_stats) h->leave_stats(h->context); }
void *mm_object_allocate(const MMObjectLayout *layout, const MMObjectHost *h, MMMemoryStats *stats, size_t bytes)
{
    void *raw;
    size_t total;
    int fail;
    if (stats) {
        enter_objects(h); enter_stats(h);
        fail = mm_memory_stats_fail(stats);
        leave_stats(h); leave_objects(h);
        if (fail) goto failed;
    }
    if (bytes > (size_t)-1 - layout->prefix) goto failed;
    total = bytes + layout->prefix;
    raw = h && h->allocate ? h->allocate(h->context, total, 1) : calloc(1, total ? total : 1);
    if (!raw) goto failed;
    enter_objects(h);
    if (h && h->allocated) h->allocated(h->context, raw, total);
    if (stats) { enter_stats(h); mm_memory_stats_allocate(stats, (MMI32)bytes); leave_stats(h); }
    leave_objects(h);
    return (unsigned char *)raw + layout->prefix;
failed:
    if (h && h->failed) h->failed(h->context);
    return NULL;
}
void mm_object_free(const MMObjectLayout *layout, const MMObjectHost *h, MMMemoryStats *stats, void *block)
{
    void *raw;
    size_t total = 0;
    if (!block) return;
    raw = (unsigned char *)block - layout->prefix;
    if (h && h->before_free) h->before_free(h->context, raw);
    if (stats || layout->debug_fill) total = h->block_size(h->context, raw);
    if (stats) mm_memory_stats_free(stats, (MMI32)(total - layout->prefix));
    if (layout->debug_fill) memset(raw, layout->garbage, total);
    if (h && h->release) h->release(h->context, raw); else free(raw);
}

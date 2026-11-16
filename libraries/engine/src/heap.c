/* Raw heap policy converted from Kauai core_platform.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/heap.h"
#include <stdlib.h>
#include <string.h>
typedef union MMHeapHeader {
    long double extended;
    double real;
    long integer;
    size_t bytes;
    void *pointer;
    void (*function)(void);
} MMHeapHeader;
size_t mm_heap_default_prefix(void) { return sizeof(MMHeapHeader); }
static size_t prefix(const MMHeapLayout *layout)
{ return layout ? layout->prefix : sizeof(MMHeapHeader); }
static int total_bytes(size_t header, size_t bytes, size_t *total)
{
    size_t payload = bytes ? bytes : 1;
    if (header < sizeof(size_t) || payload > (size_t)-1 - header) return 0;
    *total = header + payload;
    return 1;
}
void *mm_heap_allocate(const MMHeapLayout *layout, const MMHeapHost *host, size_t bytes, int clear)
{
    size_t header = prefix(layout), total;
    void *raw;
    if (!total_bytes(header, bytes, &total)) return NULL;
    raw = host && host->allocate ? host->allocate(host->context, total, clear) :
        (clear ? calloc(1, total) : malloc(total));
    if (!raw) return NULL;
    memcpy(raw, &bytes, sizeof(bytes));
    return (unsigned char *)raw + header;
}
void *mm_heap_reallocate(const MMHeapLayout *layout, const MMHeapHost *host, void *block, size_t bytes)
{
    size_t header = prefix(layout), total;
    void *raw;
    if (!block) return mm_heap_allocate(layout, host, bytes, 0);
    if (!total_bytes(header, bytes, &total)) return NULL;
    raw = (unsigned char *)block - header;
    raw = host && host->reallocate ? host->reallocate(host->context, raw, total) : realloc(raw, total);
    if (!raw) return NULL;
    memcpy(raw, &bytes, sizeof(bytes));
    return (unsigned char *)raw + header;
}
void mm_heap_release(const MMHeapLayout *layout, const MMHeapHost *host, void *block)
{
    void *raw;
    if (!block) return;
    raw = (unsigned char *)block - prefix(layout);
    if (host && host->release) host->release(host->context, raw); else free(raw);
}
size_t mm_heap_size(const MMHeapLayout *layout, const void *block)
{
    size_t bytes = 0;
    if (block) memcpy(&bytes, (const unsigned char *)block - prefix(layout), sizeof(bytes));
    return bytes;
}

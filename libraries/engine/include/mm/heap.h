/* Raw heap policy converted from Kauai core_platform.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_HEAP_H
#define MM_ENGINE_HEAP_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMHeapLayout { size_t prefix; } MMHeapLayout;
typedef struct MMHeapHost {
    void *context;
    void *(*allocate)(void *, size_t total, int clear);
    void *(*reallocate)(void *, void *raw, size_t total);
    void (*release)(void *, void *raw);
} MMHeapHost;
/* Null layout uses a fundamental C89 alignment profile. A native profile may
 * specify its existing header width: prefix must be at least sizeof(size_t)
 * and preserve payload alignment. Use the same profile for a block's lifetime.
 * Null host callbacks use libc; callback families must use compatible storage.
 * Providers must preserve realloc failure and clear requested allocations;
 * callbacks are synchronous and must not unwind.
 * Zero logical bytes retain a physical payload byte; overflow fails before
 * calling the provider. Failed resize preserves the block and recorded size.
 * Headers are private host storage, never serialized engine data. */
size_t mm_heap_default_prefix(void);
void *mm_heap_allocate(const MMHeapLayout *, const MMHeapHost *, size_t bytes, int clear);
void *mm_heap_reallocate(const MMHeapLayout *, const MMHeapHost *, void *, size_t bytes);
void mm_heap_release(const MMHeapLayout *, const MMHeapHost *, void *);
size_t mm_heap_size(const MMHeapLayout *, const void *);
#ifdef __cplusplus
}
#endif
#endif

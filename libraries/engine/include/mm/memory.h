/* Fixed-memory control converted from Kauai utilmem.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MEMORY_H
#define MM_ENGINE_MEMORY_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Preserve the existing Windows public counter ABI, with an explicit width
 * check. Other hosts use the engine's checked signed-32-bit type. */
#ifdef _WIN32
#if LONG_MAX != 0x7fffffffL
#error Windows allocator counters require thirty-two-bit long
#endif
typedef long MMMemoryCounter;
#else
typedef MMI32 MMMemoryCounter;
#endif
typedef struct MMMemoryStats {
    MMMemoryCounter cv, cvTot, cvRun, cb, cbRun, cactDo, cactFail;
} MMMemoryStats;
/* Caller supplies synchronization. Diagnostic arithmetic wraps explicitly;
 * native equivalence requires counters not to overflow their signed range. */
int mm_memory_stats_fail(MMMemoryStats *);
void mm_memory_stats_allocate(MMMemoryStats *, MMI32 bytes);
void mm_memory_stats_resize(MMMemoryStats *, MMI32 delta);
void mm_memory_stats_free(MMMemoryStats *, MMI32 bytes);
typedef struct MMFixedMemory { int in_liberator; } MMFixedMemory;
typedef struct MMMemoryLayout {
    MMU32 prefix, suffix, limit;
    int debug_fill;
    unsigned char garbage;
} MMMemoryLayout;
typedef struct MMMemoryHost {
    void *context;
    void *(*allocate)(void *, size_t);
    void *(*reallocate)(void *, void *, size_t);
    void (*release)(void *, void *);
    void (*enter)(void *);
    void (*leave)(void *);
    int (*has_liberator)(void *);
    MMI32 (*liberate)(void *, MMI32 bytes, MMI32 priority);
    void (*before_allocate)(void *, MMU32 payload);
    void (*allocated)(void *, void *raw, MMU32 total);
    void (*inspect_resize)(void *, void *raw);
    void (*before_resize)(void *, void *raw, MMU32 old_total, MMU32 new_total);
    void (*relocated)(void *, void *raw, const unsigned char *old_address);
    void (*resized)(void *, void *raw, MMU32 total);
    void (*before_free)(void *, void *raw);
    MMU32 (*block_size)(void *, void *raw);
    void (*validate)(void *, void *payload, MMU32 bytes);
    void (*too_large)(void *, MMU32 requested, int resizing);
    void (*failed)(void *, int resizing);
} MMMemoryHost;
/* Flags bit 1 requests clearing. Layout overhead/limit must fit signed 32-bit
 * totals and provide correct payload alignment. Null raw callbacks use libc
 * (zero-byte blocks request one byte). Prefix/suffix describe host diagnostics,
 * never serialized engine data. block_size is required for debug fill or stats.
 * Stats are optional; failure injection applies to allocate and growing resize.
 * Shared liberator state is guarded with host enter/leave, but liberation itself
 * runs unlocked. Reentrant failure suppresses liberation; positive freed bytes
 * retry. A failed resize retains pointer, payload and footer. Hooks are borrowed,
 * synchronous and must not unwind through C or destroy the owning operation.
 * Stats allocate/resize/fail use host locking; free retains the native caller-
 * synchronized convention. Free publishes the raw pointer before release and nil
 * afterward. Successful callbacks may inspect publication/stat ordering.
 * relocated runs after every successful resize, with owner still raw. Its
 * old_address argument is a borrowed sizeof(void*) byte token captured before
 * reallocation; it is not a block pointer. The host decides whether its physical
 * tracking needs relocation. C never reads an invalidated old pointer value. */
int mm_fixed_allocate(MMFixedMemory *, const MMMemoryLayout *, const MMMemoryHost *, MMMemoryStats *, void **,
                      MMU32 bytes, MMU32 flags, MMI32 priority);
int mm_fixed_resize(MMFixedMemory *, const MMMemoryLayout *, const MMMemoryHost *, MMMemoryStats *, void **,
                    MMU32 new_bytes, MMU32 old_bytes, MMU32 flags, MMI32 priority);
void mm_fixed_free(const MMMemoryLayout *, const MMMemoryHost *, MMMemoryStats *, void **);
#ifdef __cplusplus
}
#endif
#endif

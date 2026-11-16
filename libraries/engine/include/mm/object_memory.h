/* Object allocation control converted from Kauai base.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_OBJECT_MEMORY_H
#define MM_ENGINE_OBJECT_MEMORY_H
#include "memory.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMObjectLayout {
    size_t prefix;
    int debug_fill;
    unsigned char garbage;
} MMObjectLayout;
typedef struct MMObjectHost {
    void *context;
    void *(*allocate)(void *, size_t total, int clear);
    void (*release)(void *, void *raw);
    void (*enter_objects)(void *);
    void (*leave_objects)(void *);
    void (*enter_stats)(void *);
    void (*leave_stats)(void *);
    void (*allocated)(void *, void *raw, size_t total);
    void (*before_free)(void *, void *raw);
    size_t (*block_size)(void *, void *raw);
    void (*failed)(void *);
} MMObjectHost;
/* Layout supplies native diagnostic overhead/alignment. Stats are optional;
 * their payload sizes must fit signed 32-bit diagnostics. Failure injection
 * runs under object then stats locking; allocation never retries/liberates.
 * Providers supply cleared storage (or libc is used), metadata/list hooks and
 * synchronization. Allocation links metadata before updating counters, under
 * the object lock. Free unlinks, updates counters without an added stats lock,
 * poisons the complete block, then releases it. block_size is required when
 * stats or poisoning is enabled. OOM reporting runs after locks are released.
 * Hooks are synchronous, borrowed and must not unwind or destroy this operation.
 * Constructors, destructors and diagnostic graph ownership remain with the host.
 * Checked size_t overflow and null free have defined behavior in the C API. */
void *mm_object_allocate(const MMObjectLayout *, const MMObjectHost *, MMMemoryStats *, size_t bytes);
void mm_object_free(const MMObjectLayout *, const MMObjectHost *, MMMemoryStats *, void *);
#ifdef __cplusplus
}
#endif
#endif

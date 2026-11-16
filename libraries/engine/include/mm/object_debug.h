/* Object diagnostics converted from Kauai base.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_OBJECT_DEBUG_H
#define MM_ENGINE_OBJECT_DEBUG_H
#include "memory.h"
#ifdef __cplusplus
extern "C" {
#endif
#if SHRT_MAX != 32767 || SHRT_MIN != (-32767 - 1)
#error Object diagnostics require sixteen-bit short
#endif
#define MM_OBJECT_DEBUG_ALLOCATED 0x414c4f43L
#define MM_OBJECT_DEBUG_LOCAL 0x4e4f414cL
#define MM_OBJECT_REQUIRE_ALLOCATED 0x20000000UL
#define MM_OBJECT_REQUIRE_LOCAL 0x40000000UL
typedef struct MMObjectDebugState { MMMemoryCounter kind; } MMObjectDebugState;
typedef struct MMObjectDebugRecord MMObjectDebugRecord;
struct MMObjectDebugRecord {
    short magic, marks;
    char *source;
    MMMemoryCounter line, total, thread;
    MMMemoryCounter stack[10];
    MMObjectDebugRecord *next;
    MMObjectDebugRecord **previous;
};
typedef struct MMObjectDebugGraph {
    MMObjectDebugRecord *objects, *raw;
} MMObjectDebugGraph;
typedef struct MMObjectDebugLayout {
    size_t prefix, object_bytes;
    MMMemoryCounter limit;
    short magic;
} MMObjectDebugLayout;
enum {
    MM_OBJECT_DEBUG_SIZE = 1, MM_OBJECT_DEBUG_MAGIC, MM_OBJECT_DEBUG_MARKS,
    MM_OBJECT_DEBUG_UNLINKED, MM_OBJECT_DEBUG_LINKED, MM_OBJECT_DEBUG_PREVIOUS,
    MM_OBJECT_DEBUG_NEXT, MM_OBJECT_DEBUG_REFERENCES, MM_OBJECT_DEBUG_EXPECT_ALLOCATED,
    MM_OBJECT_DEBUG_EXPECT_LOCAL, MM_OBJECT_DEBUG_KIND, MM_OBJECT_DEBUG_RAW_LIST,
    MM_OBJECT_DEBUG_SIZE_BIG, MM_OBJECT_DEBUG_REFERENCES_BIG
};
typedef struct MMObjectDebugHost {
    void *context;
    void (*enter)(void *);
    void (*leave)(void *);
    void (*address)(void *, const void *, size_t);
    void (*assertion)(void *, int code, const void *value, size_t);
    MMMemoryCounter (*current_thread)(void *);
    void (*attached)(void *, void *object);
    void (*validate)(void *, void *object, MMU32 flags);
    void (*mark)(void *, void *object);
    void (*mark_root)(void *, void *object);
    int (*lost_summary)(void *, MMI32 count);
    void (*lost_object)(void *, void *object, MMObjectDebugRecord *, size_t payload);
} MMObjectDebugHost;
/* The graph owns list heads, not storage. Zero initialization is valid; init
 * requires no linked records. Its address and each record's address stay fixed
 * while linked. Prefix supplies native alignment/padding and must cover the C
 * record. Sizes/counters must fit the native signed diagnostic range. Records
 * belong to one graph until unlink; object kind lives in the owner's C state.
 * The host provides recursive synchronization, pointer checks, diagnostics,
 * current thread, native validation and virtual mark dispatch. Hooks are borrowed,
 * synchronous and cannot unwind or delete/move graph records during traversal.
 * Mark callbacks may update marks (including descendants/cycles). Lost scans
 * count first, report oldest first, and recheck marks after every callback.
 * Empty raw-list attachment deliberately avoids locking for global constructors.
 * Its initial raw-head probe precedes locking, retaining the legacy bootstrap
 * convention; callers must serialize concurrent publication around that probe.
 * Other operations preserve native recursive lock/validation order. Mark-stub
 * and mark use caller synchronization as before. No automatic finalizer runs. */
void mm_object_debug_init(MMObjectDebugGraph *);
void mm_object_debug_validate_record(const MMObjectDebugLayout *, const MMObjectDebugHost *, MMObjectDebugRecord *, int linked);
void mm_object_debug_link(const MMObjectDebugLayout *, const MMObjectDebugHost *, MMObjectDebugRecord *, MMObjectDebugRecord **head);
void mm_object_debug_unlink(const MMObjectDebugLayout *, const MMObjectDebugHost *, MMObjectDebugRecord *);
void mm_object_debug_created(MMObjectDebugGraph *, const MMObjectDebugLayout *, const MMObjectDebugHost *,
                             MMObjectDebugRecord *, char *source, MMMemoryCounter line, MMMemoryCounter total, MMMemoryCounter thread);
void mm_object_debug_attach(MMObjectDebugGraph *, const MMObjectDebugLayout *, const MMObjectDebugHost *, void *object, MMObjectDebugState *);
void mm_object_debug_validate(const MMObjectDebugLayout *, const MMObjectDebugHost *, void *object, const MMObjectDebugState *, MMI32 references, MMU32 flags);
void mm_object_debug_mark_stub(const MMObjectDebugLayout *, const MMObjectDebugHost *, void *object, const MMObjectDebugState *);
void mm_object_debug_mark(const MMObjectDebugLayout *, const MMObjectDebugHost *, void *object, const MMObjectDebugState *, MMI32 references);
void mm_object_debug_unmark(MMObjectDebugGraph *, const MMObjectDebugLayout *, const MMObjectDebugHost *);
void mm_object_debug_report(MMObjectDebugGraph *, const MMObjectDebugLayout *, const MMObjectDebugHost *);
#ifdef __cplusplus
}
#endif
#endif

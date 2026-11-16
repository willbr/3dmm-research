/* Fixed-memory diagnostics converted from Kauai utilmem.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MEMORY_DEBUG_H
#define MM_ENGINE_MEMORY_DEBUG_H
#include "memory.h"
#ifdef __cplusplus
extern "C" {
#endif
#if SHRT_MAX != 32767 || SHRT_MIN != (-32767 - 1)
#error Memory diagnostics require sixteen-bit short
#endif
typedef struct MMMemoryDebugRecord MMMemoryDebugRecord;
struct MMMemoryDebugRecord {
    MMMemoryCounter total;
    char *source;
    MMMemoryCounter line, thread;
    MMMemoryDebugRecord *previous, *next;
    MMMemoryCounter stack[5];
    short marks, magic;
};
typedef struct MMMemoryDebugFooter { short magic; } MMMemoryDebugFooter;
typedef struct MMMemoryDebugGraph { MMMemoryDebugRecord *first; } MMMemoryDebugGraph;
typedef struct MMMemoryDebugMove { unsigned char record_address[sizeof(MMMemoryDebugRecord *)]; } MMMemoryDebugMove;
typedef struct MMMemoryDebugLayout {
    MMU32 prefix, suffix, limit;
    short magic;
    int expensive;
    const MMMemoryCounter *suspended;
} MMMemoryDebugLayout;
enum {
    MM_MEMORY_DEBUG_OVERHEAD = 1, MM_MEMORY_DEBUG_FIRST_PREVIOUS, MM_MEMORY_DEBUG_OLD_NIL,
    MM_MEMORY_DEBUG_HEAD, MM_MEMORY_DEBUG_NOT_HEAD, MM_MEMORY_DEBUG_PREVIOUS_NEXT,
    MM_MEMORY_DEBUG_NEXT_PREVIOUS, MM_MEMORY_DEBUG_MAGIC, MM_MEMORY_DEBUG_SMALL,
    MM_MEMORY_DEBUG_BIG, MM_MEMORY_DEBUG_CAPACITY, MM_MEMORY_DEBUG_BAD_PREVIOUS,
    MM_MEMORY_DEBUG_FIRST_HAS_PREVIOUS, MM_MEMORY_DEBUG_BAD_NEXT, MM_MEMORY_DEBUG_TAIL,
    MM_MEMORY_DEBUG_NIL, MM_MEMORY_DEBUG_SIZE, MM_MEMORY_DEBUG_RESIZE_OVERHEAD, MM_MEMORY_DEBUG_OLD_SIZE
};
typedef struct MMMemoryDebugHost {
    void *context;
    void (*enter)(void *);
    void (*leave)(void *);
    void (*address)(void *, const void *, size_t);
    void (*assertion)(void *, int code, const void *value, size_t);
    size_t (*allocation_size)(void *, const void *);
    MMMemoryCounter (*current_thread)(void *);
    void (*lost)(void *, MMMemoryDebugRecord *);
    void (*after_report)(void *);
    void (*after_unmark)(void *);
} MMMemoryDebugHost;
/* Graph owns links; allocator owns blocks. Initialize only an empty graph;
 * zero initialization is valid. Prefix preserves host alignment and covers the
 * record; suffix covers the two-byte native-endian footer at its start. Totals,
 * payloads and physical capacities fit signed 32-bit native diagnostics. Source
 * strings are borrowed. Creation preserves cleared/poisoned initial marks.
 * Stack capture occurs between created and publish. Recursive lock, pointer,
 * diagnostic and thread services are borrowed synchronous hooks; they cannot
 * unwind or destroy/move records during traversal. Scans query the thread before
 * locking, visit newest first, filter by owner and never mark lost records.
 * Marking has no thread filter. Expensive validation honors the borrowed suspend
 * counter; fast payload validation retains its nil check even when suspended.
 * Caller serializes relocation/storage lifetime with graph publication. Prepare
 * captures a typed pointer representation before resize. Relocated receives the
 * fixed allocator's sizeof(void*) old-address token and fresh raw pointer; neither
 * old token is reconstructed as a pointer. Moved blocks unlink/relink at the head.
 * An unchanged address republishes fresh neighbour/head aliases under the lock,
 * retaining its list position. Tokens are transient, not addresses to dereference.
 * before_allocate is called under the allocator lock; mark retains caller
 * synchronization. No graph operation owns allocation/free or automatic teardown. */
void mm_memory_debug_init(MMMemoryDebugGraph *);
void mm_memory_debug_before_allocate(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *, MMU32 payload);
void mm_memory_debug_created(const MMMemoryDebugLayout *, const MMMemoryDebugHost *, MMMemoryDebugRecord *, MMU32 total, char *source, MMMemoryCounter line);
void mm_memory_debug_publish(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *, MMMemoryDebugRecord *);
void mm_memory_debug_unlink(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *, MMMemoryDebugRecord *);
void mm_memory_debug_validate_record(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *, MMMemoryDebugRecord *);
void mm_memory_debug_validate_payload(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *, void *, MMI32 bytes);
void mm_memory_debug_prepare_resize(const MMMemoryDebugLayout *, const MMMemoryDebugHost *, MMMemoryDebugRecord *, MMU32 old_total, MMU32 new_total, MMMemoryDebugMove *);
void mm_memory_debug_relocated(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *, void *raw, const MMMemoryDebugMove *, const unsigned char *old_address);
void mm_memory_debug_resized(const MMMemoryDebugLayout *, MMMemoryDebugRecord *, MMU32 total);
void mm_memory_debug_before_free(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *, MMMemoryDebugRecord *);
void mm_memory_debug_unmark(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *);
void mm_memory_debug_report(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *);
void mm_memory_debug_mark(MMMemoryDebugGraph *, const MMMemoryDebugLayout *, const MMMemoryDebugHost *, void *);
#ifdef __cplusplus
}
#endif
#endif

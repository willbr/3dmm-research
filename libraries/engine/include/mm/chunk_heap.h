/* Chunk heap policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_HEAP_H
#define MM_ENGINE_CHUNK_HEAP_H
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkFreeRange
    {
        MMU32 offset, bytes;
    } MMChunkFreeRange;
    /* Trusted sorted, nonoverlapping free-map primitives. get/put/erase cannot
     * fail; insert/ensure_map may fail allocation without changing logical entries.
     * No callback may re-enter or retain pointers into movable map storage. */
    typedef struct MMChunkHeap
    {
        void *context;
        MMU32 (*count)(void *);
        void (*get)(void *, MMU32 position, MMChunkFreeRange *);
        void (*put)(void *, MMU32 position, const MMChunkFreeRange *);
        void (*erase)(void *, MMU32 position);
        int (*insert)(void *, MMU32 position, const MMChunkFreeRange *);
        int (*ensure_map)(void *);
        MMU32 (*physical_end)(void *);
        int (*resize_file)(void *, MMU32 bytes);
    } MMChunkHeap;
    /* First fit, taking bytes from the start of a hole. Append reuses physical
     * trailing space before extending the file. Zero bytes returns offset zero.
     * Outputs remain unchanged on failure. Logical end never truncates the file. */
    int mm_chunk_heap_allocate(const MMChunkHeap *, MMU32 *logical_end, MMU32 bytes,
                                     MMU32 *offset);
    /* Merge either/both neighbors, or trim the tail and at most its adjacent last
     * free range. MEMORY means the freed interior space was dropped, matching the
     * legacy nonfatal allocation-failure policy. Invalid/overlapping ranges return
     * BAD_FILE before map mutation; no physical resize occurs. */
    int mm_chunk_heap_free(const MMChunkHeap *, MMU32 *logical_end, MMU32 offset,
                                 MMU32 bytes);
#ifdef __cplusplus
}
#endif
#endif

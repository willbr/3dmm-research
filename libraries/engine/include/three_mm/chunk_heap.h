/* Chunk heap policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_HEAP_H
#define THREE_MM_ENGINE_CHUNK_HEAP_H
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkFreeRange
    {
        ThreeMMU32 offset, bytes;
    } ThreeMMChunkFreeRange;
    /* Trusted sorted, nonoverlapping free-map primitives. get/put/erase cannot
     * fail; insert/ensure_map may fail allocation without changing logical entries.
     * No callback may re-enter or retain pointers into movable map storage. */
    typedef struct ThreeMMChunkHeap
    {
        void *context;
        ThreeMMU32 (*count)(void *);
        void (*get)(void *, ThreeMMU32 position, ThreeMMChunkFreeRange *);
        void (*put)(void *, ThreeMMU32 position, const ThreeMMChunkFreeRange *);
        void (*erase)(void *, ThreeMMU32 position);
        int (*insert)(void *, ThreeMMU32 position, const ThreeMMChunkFreeRange *);
        int (*ensure_map)(void *);
        ThreeMMU32 (*physical_end)(void *);
        int (*resize_file)(void *, ThreeMMU32 bytes);
    } ThreeMMChunkHeap;
    /* First fit, taking bytes from the start of a hole. Append reuses physical
     * trailing space before extending the file. Zero bytes returns offset zero.
     * Outputs remain unchanged on failure. Logical end never truncates the file. */
    int three_mm_chunk_heap_allocate(const ThreeMMChunkHeap *, ThreeMMU32 *logical_end, ThreeMMU32 bytes,
                                     ThreeMMU32 *offset);
    /* Merge either/both neighbors, or trim the tail and at most its adjacent last
     * free range. MEMORY means the freed interior space was dropped, matching the
     * legacy nonfatal allocation-failure policy. Invalid/overlapping ranges return
     * BAD_FILE before map mutation; no physical resize occurs. */
    int three_mm_chunk_heap_free(const ThreeMMChunkHeap *, ThreeMMU32 *logical_end, ThreeMMU32 offset,
                                 ThreeMMU32 bytes);
#ifdef __cplusplus
}
#endif
#endif

/* Embedded forests converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_FOREST_H
#define THREE_MM_ENGINE_CHUNK_FOREST_H
#include "chunk_graph.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMForestExport
    {
        void *context;
        ThreeMMChunkGraph graph;
        int (*entry)(void *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMChunkEntry *);
        int (*write)(void *, ThreeMMU32 offset, const void *, size_t bytes);
        int (*copy)(void *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 offset, ThreeMMU32 bytes);
        int big_endian, system;
    } ThreeMMForestExport;
    /* Preorder serialization repeats shared descendants and omits chunk numbers and
     * names. A NULL write selects size-only traversal (copy is not called). Actual
     * writes retain descriptor-before-payload partial output. Outputs publish only
     * on success. File references and source graph are borrowed for the whole call. */
    int three_mm_forest_write(const ThreeMMForestExport *, const ThreeMMStorageAllocator *, ThreeMMU32 type,
                              ThreeMMU32 number, ThreeMMU32 offset, ThreeMMU32 *bytes);
    typedef struct ThreeMMForestImport
    {
        void *context;
        int (*add)(void *, ThreeMMU32 type, ThreeMMU32 source_offset, ThreeMMU32 bytes, ThreeMMU32 flags, int copy,
                   ThreeMMU32 *number);
        int (*adopt)(void *, ThreeMMU32 parent_type, ThreeMMU32 parent_number, ThreeMMU32 type, ThreeMMU32 number,
                     ThreeMMU32 slot);
    } ThreeMMForestImport;
    /* Stream contains absolute source offsets. C owns the parse/parent stack and
     * sequencing; host owns destination creation/disposal and physical payload I/O.
     * Failed calls may leave partial destination nodes for host disposal. The wire
     * OSK is opaque, negative child counts are leaves, root slots are ignored, and
     * incomplete positive child counts fail. Copy imports mask flags to PACKED;
     * borrowed-data imports retain PACKED|FOREST, matching retail. Callbacks are
     * synchronous and may not re-enter; add publishes a number only on success. */
    int three_mm_forest_read(const ThreeMMReadStream *, ThreeMMU32 offset, ThreeMMU32 bytes,
                             const ThreeMMForestImport *, const ThreeMMStorageAllocator *, int copy);
#ifdef __cplusplus
}
#endif
#endif

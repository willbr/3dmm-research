/* Embedded forests converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_FOREST_H
#define MM_ENGINE_CHUNK_FOREST_H
#include "chunk_graph.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMForestExport
    {
        void *context;
        MMChunkGraph graph;
        int (*entry)(void *, MMU32 type, MMU32 number, MMChunkEntry *);
        int (*write)(void *, MMU32 offset, const void *, size_t bytes);
        int (*copy)(void *, MMU32 type, MMU32 number, MMU32 offset, MMU32 bytes);
        int big_endian, system;
    } MMForestExport;
    /* Preorder serialization repeats shared descendants and omits chunk numbers and
     * names. A NULL write selects size-only traversal (copy is not called). Actual
     * writes retain descriptor-before-payload partial output. Outputs publish only
     * on success. File references and source graph are borrowed for the whole call. */
    int mm_forest_write(const MMForestExport *, const MMStorageAllocator *, MMU32 type,
                              MMU32 number, MMU32 offset, MMU32 *bytes);
    typedef struct MMForestImport
    {
        void *context;
        int (*add)(void *, MMU32 type, MMU32 source_offset, MMU32 bytes, MMU32 flags, int copy,
                   MMU32 *number);
        int (*adopt)(void *, MMU32 parent_type, MMU32 parent_number, MMU32 type, MMU32 number,
                     MMU32 slot);
    } MMForestImport;
    /* Stream contains absolute source offsets. C owns the parse/parent stack and
     * sequencing; host owns destination creation/disposal and physical payload I/O.
     * Failed calls may leave partial destination nodes for host disposal. The wire
     * OSK is opaque, negative child counts are leaves, root slots are ignored, and
     * incomplete positive child counts fail. Copy imports mask flags to PACKED;
     * borrowed-data imports retain PACKED|FOREST, matching retail. Callbacks are
     * synchronous and may not re-enter; add publishes a number only on success. */
    int mm_forest_read(const MMReadStream *, MMU32 offset, MMU32 bytes,
                             const MMForestImport *, const MMStorageAllocator *, int copy);

    struct MMChunkOwnerRead;
    /* Import into an initialized table. Copied data is allocated in the archive.
     * Borrowed rows point into its main file. On failure dispose the partial archive. */
    int mm_chunk_reader_import_forest(const struct MMChunkOwnerRead *, const MMReadStream *,
                                      void *source, MMU32 offset, MMU32 bytes, int copy);
    /* A NULL destination measures the output. File and graph are borrowed. */
    int mm_chunk_reader_export_forest(const struct MMChunkOwnerRead *, void *destination,
                                      MMU32 type, MMU32 number, MMU32 offset, MMU32 *bytes);
#ifdef __cplusplus
}
#endif
#endif

/* Chunky archive assembly converted from Kauai chcm.cpp by ShonK.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNKY_ASSEMBLY_H
#define MM_ENGINE_CHUNKY_ASSEMBLY_H

#include "chunk_table.h"
#include "chunky_source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMChunkyAssemblyHost
{
    void *context;
    const MMStorageAllocator *allocator;

    MMU32 (*error_count)(void *);
    void (*error_detail)(void *, int error, const char *ascii);
    void (*error_limit)(void *);

    int (*exists)(void *, MMChunkRecordKey);
    int (*create)(void *, MMChunkRecordKey);
    int (*set_name)(void *, MMChunkRecordKey, const MMLexText *);
    /* Report potential reachability after a traversal failure, as well as a match. */
    int (*reaches)(void *, MMChunkRecordKey child, MMChunkRecordKey parent);
    int (*adopt)(void *, MMChunkRecordKey parent, MMChunkRecordKey child, MMU32 slot);
    void (*set_loner)(void *, MMChunkRecordKey);

    void (*clear_data)(void *);
    void (*read_prefix)(void *, void *, MMU32);
    void (*remove_prefix)(void *, MMU32);
    int (*pack_format)(void *, MMI32);
    int (*finish_data)(void *, MMChunkRecordKey, int pack, int prepacked);
    /* Collection, script, and import handlers consume their own body tokens. */
    void (*special_body)(void *, MMChunkRecordKey, int kind, int pack);
    int (*finish_archive)(void *);

    /* Create returns an owned archive without changing the active archive.
     * Activate selects the archive used by all callbacks above. */
    void *(*create_subfile)(void *);
    void (*activate)(void *, void *archive);
    void (*release)(void *, void *archive);
    int (*finish_subfile)(void *, void *parent, MMChunkRecordKey, int pack);
} MMChunkyAssemblyHost;

/* Synchronous compiler session. The caller owns the root on entry and return.
 * Child archives and scratch lists are released even on syntax and I/O errors. */
void mm_chunky_assemble(const MMChunkySource *, const MMChunkyAssemblyHost *, void *root);

typedef struct MMChunkyForestHost
{
    void *context;
    /* Archive iteration order; zero means end of entries. */
    int (*entry)(void *, MMU32 index, MMChunkRecordKey *, MMU32 *references);
    /* Measure or write one tree. The host selects the operation. */
    int (*tree)(void *, MMChunkRecordKey, MMU32 offset, MMU32 *bytes);
} MMChunkyForestHost;

/* Visit unreferenced roots in archive order, bounding the total extent. */
int mm_chunky_write_roots(const MMChunkyForestHost *, MMU32 offset, MMU32 *bytes);

#ifdef __cplusplus
}
#endif
#endif

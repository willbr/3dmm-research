/* Chunk graph operations converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_OPS_H
#define MM_ENGINE_CHUNK_OPS_H
#include "chunk_edit.h"
#include "chunk_graph.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkEndpoint
    {
        MMChunkEdit edit;
        MMChunkGraph graph;
        const MMStorageAllocator *allocator;
        void *identity, *context;
        MMU32 max_references;
        int scan_all_references;
        MMI32 (*runtime)(void *, MMU32 type, MMU32 number);
        int (*set_runtime)(void *, MMU32 type, MMU32 number, MMI32 runtime);
        /* Borrowed host scratch holds a payload/name until the next read. Transfer
         * callbacks accept the source context and use the existing block/text APIs. */
        void (*read_payload)(void *, MMU32 position);
        int (*put_payload)(void *, MMU32 type, MMU32 number, void *source_context);
        int (*add_payload)(void *, MMU32 type, void *source_context, MMU32 *number);
        int (*read_name)(void *, MMU32 position);
        int (*write_name)(void *, MMU32 position, void *source_context);
    } MMChunkEndpoint;
    MMI32 mm_chunk_runtime_highest(void);
    int mm_chunk_swap_data(const MMChunkEdit *, MMU32 type, MMU32 number, MMU32 other_type,
                                 MMU32 other_number);
    /* Distinct nodes only. Child-prefix transfers are aligned and cannot fail for
     * valid native storage; this has no rollback for a violated transfer contract. */
    int mm_chunk_swap_children(const MMChunkEdit *, MMU32 type, MMU32 number,
                                     MMU32 other_type, MMU32 other_number);
    int mm_chunk_move(const MMChunkEndpoint *, MMU32 type, MMU32 number, MMU32 new_type,
                            MMU32 new_number);
    /* Matching retains traversal/RTI semantics and candidate-number order. 1 means
     * matched; 0 includes unknown after traversal allocation failure. Output is
     * unchanged on failure. It does not invent a stronger graph-isomorphism test. */
    int mm_chunk_match(const MMChunkEndpoint *source, const MMChunkEndpoint *destination,
                             MMU32 type, MMU32 number, MMU32 *matched_number);
    /* Owns a per-copy C map; clone suppresses pre-existing graph reuse but preserves
     * sharing inside the cloned subgraph. Source numbers are retained when free.
     * Runtime allocation is process-global, as in the legacy engine. Failure retains
     * legacy partial cleanup: e.g. a first map-insert failure can leave a new loner.
     * Only successful results publish number; native failed-output poisoning stays
     * with the facade. This is not an atomic destination transaction. */
    int mm_chunk_copy(const MMChunkEndpoint *source, const MMChunkEndpoint *destination,
                            MMU32 type, MMU32 number, int clone, MMU32 *copied_number);
    /* Stage a reference to a shared chunk. Existing roots keep their loner bit.
     * Newly copied roots remain loners until adopted or discarded by the caller.
     * The two outputs are published together on success. */
    int mm_chunk_copy_reference(const MMChunkEndpoint *source, const MMChunkEndpoint *destination,
                                MMU32 type, MMU32 number, MMU32 *copied_number, int *created);
#ifdef __cplusplus
}
#endif
#endif

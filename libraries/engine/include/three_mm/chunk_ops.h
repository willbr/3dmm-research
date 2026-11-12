/* Chunk graph operations converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_OPS_H
#define THREE_MM_ENGINE_CHUNK_OPS_H
#include "chunk_edit.h"
#include "chunk_graph.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkEndpoint
    {
        ThreeMMChunkEdit edit;
        ThreeMMChunkGraph graph;
        const ThreeMMStorageAllocator *allocator;
        void *identity, *context;
        ThreeMMU32 max_references;
        int scan_all_references;
        ThreeMMI32 (*runtime)(void *, ThreeMMU32 type, ThreeMMU32 number);
        int (*set_runtime)(void *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 runtime);
        /* Borrowed host scratch holds a payload/name until the next read. Transfer
         * callbacks accept the source context and use the existing block/text APIs. */
        void (*read_payload)(void *, ThreeMMU32 position);
        int (*put_payload)(void *, ThreeMMU32 type, ThreeMMU32 number, void *source_context);
        int (*add_payload)(void *, ThreeMMU32 type, void *source_context, ThreeMMU32 *number);
        int (*read_name)(void *, ThreeMMU32 position);
        int (*write_name)(void *, ThreeMMU32 position, void *source_context);
    } ThreeMMChunkEndpoint;
    ThreeMMI32 three_mm_chunk_runtime_highest(void);
    int three_mm_chunk_swap_data(const ThreeMMChunkEdit *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 other_type,
                                 ThreeMMU32 other_number);
    /* Distinct nodes only. Child-prefix transfers are aligned and cannot fail for
     * valid native storage; this has no rollback for a violated transfer contract. */
    int three_mm_chunk_swap_children(const ThreeMMChunkEdit *, ThreeMMU32 type, ThreeMMU32 number,
                                     ThreeMMU32 other_type, ThreeMMU32 other_number);
    int three_mm_chunk_move(const ThreeMMChunkEndpoint *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 new_type,
                            ThreeMMU32 new_number);
    /* Matching retains traversal/RTI semantics and candidate-number order. 1 means
     * matched; 0 includes unknown after traversal allocation failure. Output is
     * unchanged on failure. It does not invent a stronger graph-isomorphism test. */
    int three_mm_chunk_match(const ThreeMMChunkEndpoint *source, const ThreeMMChunkEndpoint *destination,
                             ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 *matched_number);
    /* Owns a per-copy C map; clone suppresses pre-existing graph reuse but preserves
     * sharing inside the cloned subgraph. Source numbers are retained when free.
     * Runtime allocation is process-global, as in the legacy engine. Failure retains
     * legacy partial cleanup: e.g. a first map-insert failure can leave a new loner.
     * Only successful results publish number; native failed-output poisoning stays
     * with the facade. This is not an atomic destination transaction. */
    int three_mm_chunk_copy(const ThreeMMChunkEndpoint *source, const ThreeMMChunkEndpoint *destination,
                            ThreeMMU32 type, ThreeMMU32 number, int clone, ThreeMMU32 *copied_number);
#ifdef __cplusplus
}
#endif
#endif

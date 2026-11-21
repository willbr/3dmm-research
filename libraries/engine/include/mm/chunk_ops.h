/* Chunk graph operations converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_OPS_H
#define MM_ENGINE_CHUNK_OPS_H
#include "block_owner.h"
#include "chunk_edit.h"
#include "chunk_graph.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkEndpoint
    {
        MMChunkEdit edit;
        /* An immutable copy source supplies stored access in place of edit,
         * and runtime callbacks. Borrow it through the copy call. */
        const MMChunkAccess *stored;
        const MMStorageAllocator *allocator;
        const MMHandleServices *handles;
        const MMBlockFileServices *files;
        void *identity, *context;
        /* Bind stored bytes to an empty block owned by the copier. The block
         * remains valid even if writing moves the live source's index rows. */
        int (*bind_payload)(void *, MMU32 position, const MMChunkEntry *, MMOwnedBlock *);
        /* The name is borrowed through its matching write. */
        const void *(*read_name)(void *, MMU32 position);
        int (*write_name)(void *, MMU32 position, const void *name);
    } MMChunkEndpoint;
    MMI32 mm_chunk_runtime_highest(void);
    int mm_chunk_swap_data(const MMChunkEdit *, MMU32 type, MMU32 number, MMU32 other_type,
                                 MMU32 other_number);
    /* Distinct nodes only. Child-prefix transfers are aligned and cannot fail for
     * valid native storage; this has no rollback for a violated transfer contract. */
    int mm_chunk_swap_children(const MMChunkEdit *, MMU32 type, MMU32 number,
                                     MMU32 other_type, MMU32 other_number);
    int mm_chunk_move(const MMChunkEdit *, MMU32 type, MMU32 number, MMU32 new_type,
                            MMU32 new_number);
    /* Matching retains traversal/RTI semantics and candidate-number order. 1 means
     * matched; 0 includes unknown after traversal allocation failure. Output is
     * unchanged on failure. It does not invent a stronger graph-isomorphism test. */
    int mm_chunk_match(const MMChunkEndpoint *source, const MMChunkEndpoint *destination,
                             MMU32 type, MMU32 number, MMU32 *matched_number);
    /* Owns a per-copy C map; clone suppresses pre-existing graph reuse but preserves
     * sharing inside the cloned subgraph. Source numbers are retained when free.
     * Runtime allocation for live sources is process-global, as in the legacy
     * engine. Stored sources use only the per-copy map to preserve sharing.
     * Failure retains legacy partial cleanup. A first map-insert failure can
     * leave a new loner.
     * Only successful results publish number; native failed-output poisoning stays
     * with the facade. This is not an atomic destination transaction. */
    int mm_chunk_copy(const MMChunkEndpoint *source, const MMChunkEndpoint *destination,
                            MMU32 type, MMU32 number, int clone, MMU32 *copied_number);
    /* Stage a reference to a shared chunk. Existing roots keep their loner bit.
     * Newly copied roots remain loners until adopted or discarded by the caller.
     * The two outputs are published together on success. */
    int mm_chunk_copy_reference(const MMChunkEndpoint *source, const MMChunkEndpoint *destination,
                                MMU32 type, MMU32 number, MMU32 *copied_number, int *created);
    /* Copy a fresh root and defer reuse of its descendants to the type filter.
     * The filter is called only for descendants and does not mutate either graph. */
    int mm_chunk_copy_filtered(const MMChunkEndpoint *source, const MMChunkEndpoint *destination,
                               MMU32 type, MMU32 number, int (*reuse)(MMU32 type), MMU32 *copied_number);
#ifdef __cplusplus
}
#endif
#endif

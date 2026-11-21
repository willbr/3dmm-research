/* Chunk editing policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_EDIT_H
#define MM_ENGINE_CHUNK_EDIT_H
#include "chunk_graph.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkLocation
    {
        void *file;
        MMU32 offset, bytes, flags; /* Only mask 1 (on-extra) describes storage. */
    } MMChunkLocation;
    enum
    {
        MM_EDIT_MISSING_NODE,
        MM_EDIT_BAD_REFERENCE,
        MM_EDIT_LOW_MEMORY,
        MM_EDIT_MISSING_CHILD,
        MM_EDIT_CYCLE_RISK,
        MM_EDIT_INDEX_OPERATION
    };
    /* Live-index primitives; store and erase cannot fail or re-enter.
     * Readers return zero for an invalid row or unreadable child.
     * Source records are copied values, never pointers into movable host storage.
     * insert creates a blank record; store initializes all native fixed bytes only
     * when initialize is true, otherwise it preserves native-only runtime fields.
     * free_space retains the heap's nonfatal dropped-free-space policy. */
    typedef struct MMChunkEdit
    {
        void *context;
        const MMStorageAllocator *walk_allocator; /* NULL selects the C heap. */
        MMU32 max_references; /* Limit imposed by the live index record format. */
        MMU32 (*count)(void *);
        int (*entry)(void *, MMU32 position, MMChunkEntry *);
        int (*child)(void *, MMU32 parent, MMU32 position, MMChunkChild *);
        int (*insert)(void *, MMU32 position);
        void (*store)(void *, MMU32 position, const MMChunkEntry *, int initialize);
        void (*erase)(void *, MMU32 position);
        int (*allocate)(void *, MMU32 bytes, MMChunkLocation *);
        void (*free_space)(void *, int on_extra, MMU32 offset, MMU32 bytes);
        void *payload_context;
        int (*write)(void *payload_context, const MMChunkLocation *, int added);
        /* NULL location clears an output block. added distinguishes new versus
         * existing writes, including a host's temporary new-chunk output block. */
        void (*output)(void *payload_context, const MMChunkLocation *, int added);
        void (*reset_runtime)(void *, MMU32 type, MMU32 number);
        void (*issue)(void *, int kind);
        int (*insert_child)(void *, MMU32 parent, MMU32 position, const MMChunkChild *);
        void (*erase_child)(void *, MMU32 parent, MMU32 position);
        void (*put_child)(void *, MMU32 parent, MMU32 position, const MMChunkChild *);
        void (*move_entry)(void *, MMU32 source, MMU32 insertion_position);
        int (*move_bytes)(void *, MMU32 source, MMU32 offset, MMU32 destination,
                          MMU32 destination_offset, MMU32 bytes);
        MMI32 (*runtime)(void *, MMU32 type, MMU32 number);
        int (*set_runtime)(void *, MMU32 type, MMU32 number, MMI32 runtime);
        int scan_all_references; /* Also validate the stored count when moving a key. */
    } MMChunkEdit;
    /* Borrows the index with its current row count. Refresh after any edit
     * that can add, remove, or reorder rows. */
    MMChunkAccess mm_chunk_edit_access(const MMChunkEdit *);
    /* Borrows a live editor with count, entry and child readers. Each graph
     * callback resolves the current row by identity. */
    MMChunkGraph mm_chunk_edit_graph(const MMChunkEdit *);
    int mm_chunk_unique(const MMChunkEdit *, MMU32 type, MMU32 *position, MMU32 *number);
    int mm_chunk_add(const MMChunkEdit *, MMU32 type, MMU32 number, MMU32 position,
                           MMU32 bytes, MMChunkLocation *);
    int mm_chunk_put(const MMChunkEdit *, MMU32 type, MMU32 number, MMU32 bytes);
    /* PACKED/FOREST changes invalidate RTI only when changed; LONER enabling
     * preserves RTI and disabling an existing LONER delegates to graph deletion. */
    int mm_chunk_set_flag(const MMChunkEdit *, MMU32 type, MMU32 number,
                                MMU32 flag, int enabled);
    int mm_chunk_remove(const MMChunkEdit *, MMU32 position);
    int mm_chunk_decrement(const MMChunkEdit *, MMU32 position, int *should_delete);
    /* Missing root is NOT_FOUND. The walk reads the live index by chunk identity,
     * so row positions may shift when a chunk is deleted. On stack allocation
     * failure it drops child references in reverse order and leaves those child
     * chunks in the index, preserving the legacy fallback. */
    int mm_chunk_delete(const MMChunkEdit *, MMU32 type, MMU32 number);
    int mm_chunk_adopt(const MMChunkEdit *, MMU32 parent_type, MMU32 parent_number,
                             const MMChunkChild *, int clear_loner);
    int mm_chunk_unlink(const MMChunkEdit *, MMU32 parent_type, MMU32 parent_number,
                              const MMChunkChild *);
    int mm_chunk_change_slot(const MMChunkEdit *, MMU32 parent_type, MMU32 parent_number,
                                   const MMChunkChild *old_child, MMU32 new_slot);
    int mm_chunk_retarget(const MMChunkEdit *, MMU32 parent, MMU32 position,
                                const MMChunkChild *replacement);
#ifdef __cplusplus
}
#endif
#endif

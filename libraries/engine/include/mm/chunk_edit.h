/* Chunk editing policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_EDIT_H
#define MM_ENGINE_CHUNK_EDIT_H
#include "chunky_file.h"
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
        MM_DELETE_PRE = 1,
        MM_DELETE_POST = 2,
        MM_DELETE_ROOT = 4,
        MM_DELETE_ERROR = 8
    };
    enum
    {
        MM_EDIT_MISSING_NODE,
        MM_EDIT_BAD_REFERENCE,
        MM_EDIT_LOW_MEMORY,
        MM_EDIT_MISSING_CHILD,
        MM_EDIT_CYCLE_RISK
    };
    typedef struct MMChunkDeleteEvent
    {
        MMU32 type, number, flags;
    } MMChunkDeleteEvent;
    /* Trusted live-index primitives; reads/store/erase cannot fail or re-enter.
     * Source records are copied values, never pointers into movable host storage.
     * insert creates a blank record; store initializes all native fixed bytes only
     * when initialize is true, otherwise it preserves native-only runtime fields.
     * free_space retains the heap's nonfatal dropped-free-space policy. */
    typedef struct MMChunkEdit
    {
        void *context;
        MMU32 (*count)(void *);
        void (*entry)(void *, MMU32 position, MMChunkEntry *);
        void (*child)(void *, MMU32 parent, MMU32 position, MMChunkChild *);
        int (*insert)(void *, MMU32 position);
        void (*store)(void *, MMU32 position, const MMChunkEntry *, int initialize);
        void (*erase)(void *, MMU32 position);
        int (*allocate)(void *, MMU32 bytes, MMChunkLocation *);
        void (*free_space)(void *, int on_extra, MMU32 offset, MMU32 bytes);
        int (*write)(void *, const MMChunkLocation *, int added);
        /* NULL location clears an output block. added distinguishes new versus
         * existing writes, including a host's temporary new-chunk output block. */
        void (*output)(void *, const MMChunkLocation *, int added);
        void (*reset_runtime)(void *, MMU32 type, MMU32 number);
        void (*begin_delete)(void *, MMU32 type, MMU32 number);
        int (*next_delete)(void *, int skip, MMChunkDeleteEvent *);
        void (*issue)(void *, int kind);
        int (*insert_child)(void *, MMU32 parent, MMU32 position, const MMChunkChild *);
        void (*erase_child)(void *, MMU32 parent, MMU32 position);
        void (*put_child)(void *, MMU32 parent, MMU32 position, const MMChunkChild *);
        /* 0=no, 1=yes, 2=unknown, retaining the legacy reference shortcut. */
        int (*descendant)(void *, MMU32 root_type, MMU32 root_number, MMU32 target_type,
                          MMU32 target_number);
        void (*move_entry)(void *, MMU32 source, MMU32 insertion_position);
        int (*move_bytes)(void *, MMU32 source, MMU32 offset, MMU32 destination,
                          MMU32 destination_offset, MMU32 bytes);
    } MMChunkEdit;
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
    /* Missing root is NOT_FOUND. Traversal remains a host service: C selects skip,
     * decrements references and deletes on post events. An ERROR post event drops
     * child references in reverse order without recursively deleting those children,
     * matching the legacy low-memory fallback. Diagnostics are optional callbacks. */
    int mm_chunk_delete(const MMChunkEdit *, MMU32 type, MMU32 number);
    int mm_chunk_adopt_at(const MMChunkEdit *, MMU32 parent, MMU32 position,
                                const MMChunkChild *, int clear_loner, MMU32 max_references);
    int mm_chunk_adopt(const MMChunkEdit *, MMU32 parent_type, MMU32 parent_number,
                             const MMChunkChild *, int clear_loner, MMU32 max_references);
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

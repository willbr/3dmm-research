/* Chunk editing policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_EDIT_H
#define THREE_MM_ENGINE_CHUNK_EDIT_H
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkLocation
    {
        void *file;
        ThreeMMU32 offset, bytes, flags; /* Only mask 1 (on-extra) describes storage. */
    } ThreeMMChunkLocation;
    enum
    {
        THREE_MM_DELETE_PRE = 1,
        THREE_MM_DELETE_POST = 2,
        THREE_MM_DELETE_ROOT = 4,
        THREE_MM_DELETE_ERROR = 8
    };
    enum
    {
        THREE_MM_EDIT_MISSING_NODE,
        THREE_MM_EDIT_BAD_REFERENCE,
        THREE_MM_EDIT_LOW_MEMORY,
        THREE_MM_EDIT_MISSING_CHILD,
        THREE_MM_EDIT_CYCLE_RISK
    };
    typedef struct ThreeMMChunkDeleteEvent
    {
        ThreeMMU32 type, number, flags;
    } ThreeMMChunkDeleteEvent;
    /* Trusted live-index primitives; reads/store/erase cannot fail or re-enter.
     * Source records are copied values, never pointers into movable host storage.
     * insert creates a blank record; store initializes all native fixed bytes only
     * when initialize is true, otherwise it preserves native-only runtime fields.
     * free_space retains the heap's nonfatal dropped-free-space policy. */
    typedef struct ThreeMMChunkEdit
    {
        void *context;
        ThreeMMU32 (*count)(void *);
        void (*entry)(void *, ThreeMMU32 position, ThreeMMChunkEntry *);
        void (*child)(void *, ThreeMMU32 parent, ThreeMMU32 position, ThreeMMChunkChild *);
        int (*insert)(void *, ThreeMMU32 position);
        void (*store)(void *, ThreeMMU32 position, const ThreeMMChunkEntry *, int initialize);
        void (*erase)(void *, ThreeMMU32 position);
        int (*allocate)(void *, ThreeMMU32 bytes, ThreeMMChunkLocation *);
        void (*free_space)(void *, int on_extra, ThreeMMU32 offset, ThreeMMU32 bytes);
        int (*write)(void *, const ThreeMMChunkLocation *, int added);
        /* NULL location clears an output block. added distinguishes new versus
         * existing writes, including a host's temporary new-chunk output block. */
        void (*output)(void *, const ThreeMMChunkLocation *, int added);
        void (*reset_runtime)(void *, ThreeMMU32 type, ThreeMMU32 number);
        void (*begin_delete)(void *, ThreeMMU32 type, ThreeMMU32 number);
        int (*next_delete)(void *, int skip, ThreeMMChunkDeleteEvent *);
        void (*issue)(void *, int kind);
        int (*insert_child)(void *, ThreeMMU32 parent, ThreeMMU32 position, const ThreeMMChunkChild *);
        void (*erase_child)(void *, ThreeMMU32 parent, ThreeMMU32 position);
        void (*put_child)(void *, ThreeMMU32 parent, ThreeMMU32 position, const ThreeMMChunkChild *);
        /* 0=no, 1=yes, 2=unknown, retaining the legacy reference shortcut. */
        int (*descendant)(void *, ThreeMMU32 root_type, ThreeMMU32 root_number, ThreeMMU32 target_type,
                          ThreeMMU32 target_number);
    } ThreeMMChunkEdit;
    int three_mm_chunk_unique(const ThreeMMChunkEdit *, ThreeMMU32 type, ThreeMMU32 *position, ThreeMMU32 *number);
    int three_mm_chunk_add(const ThreeMMChunkEdit *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 position,
                           ThreeMMU32 bytes, ThreeMMChunkLocation *);
    int three_mm_chunk_put(const ThreeMMChunkEdit *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 bytes);
    int three_mm_chunk_remove(const ThreeMMChunkEdit *, ThreeMMU32 position);
    int three_mm_chunk_decrement(const ThreeMMChunkEdit *, ThreeMMU32 position, int *should_delete);
    /* Missing root is NOT_FOUND. Traversal remains a host service: C selects skip,
     * decrements references and deletes on post events. An ERROR post event drops
     * child references in reverse order without recursively deleting those children,
     * matching the legacy low-memory fallback. Diagnostics are optional callbacks. */
    int three_mm_chunk_delete(const ThreeMMChunkEdit *, ThreeMMU32 type, ThreeMMU32 number);
    int three_mm_chunk_adopt_at(const ThreeMMChunkEdit *, ThreeMMU32 parent, ThreeMMU32 position,
                                const ThreeMMChunkChild *, int clear_loner, ThreeMMU32 max_references);
    int three_mm_chunk_adopt(const ThreeMMChunkEdit *, ThreeMMU32 parent_type, ThreeMMU32 parent_number,
                             const ThreeMMChunkChild *, int clear_loner, ThreeMMU32 max_references);
    int three_mm_chunk_unlink(const ThreeMMChunkEdit *, ThreeMMU32 parent_type, ThreeMMU32 parent_number,
                              const ThreeMMChunkChild *);
    int three_mm_chunk_change_slot(const ThreeMMChunkEdit *, ThreeMMU32 parent_type, ThreeMMU32 parent_number,
                                   const ThreeMMChunkChild *old_child, ThreeMMU32 new_slot);
#ifdef __cplusplus
}
#endif
#endif

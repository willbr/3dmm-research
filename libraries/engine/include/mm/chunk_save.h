/* Whole-file save policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_SAVE_H
#define MM_ENGINE_CHUNK_SAVE_H
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkSaveState
    {
        void *main_file, *extra_file, *main_map, *extra_map;
        MMU32 main_end, extra_end;
        int invalid_main, add_to_extra, free_map_unread;
    } MMChunkSaveState;
    typedef struct MMChunkSaveIndex
    {
        void *context;
        MMU32 count;
        /* Trusted live index primitives: entry must fill every valid position, and
         * place cannot fail. Neither may re-enter or resize the index. Entry remains
         * stable except for the offsets/flags changed by place. No borrowed pointer
         * survives a callback, so file services may allocate/move host storage. */
        void (*entry)(void *, MMU32 position, MMChunkEntry *);
        void (*place)(void *, MMU32 position, MMU32 offset, MMU32 flags);
    } MMChunkSaveIndex;
    typedef struct MMChunkSaveServices
    {
        void *context;
        void *(*create_temp)(void *); /* Same directory as the host-resolved target. */
        int (*resize)(void *, void *file, MMU32 bytes);
        int (*copy)(void *, void *source, MMU32 source_offset, void *destination, MMU32 destination_offset,
                    MMU32 bytes);
        void (*release_file)(void *, void *file);
        void (*release_map)(void *, void *map);
        int (*is_temp)(void *, void *file);
        void (*set_temp)(void *, void *file, int temporary);
        int (*write_index)(void *, const MMChunkSaveState *, MMU32 creator);
        int (*rename)(void *, void *file); /* Existing target removal is host policy. */
        int (*swap_names)(void *, void *new_file, void *old_file);
        /* Optional facade synchronization before releasing detached references or
         * entering services after ownership changes. Must only mirror the state. */
        void (*publish)(void *, const MMChunkSaveState *);
    } MMChunkSaveServices;
    /* State owns the file/map references on entry and return, including failure.
     * Non-null release callbacks consume one reference. Named means the host has
     * resolved an explicit target different from the current name. Direct saves
     * write only the index. Relocation copies nonempty chunks before changing any
     * index/state, normalizes empty offsets to zero, and retains runtime flag bits.
     * On commit/naming failure a valid old main becomes main again and the copied
     * payload becomes extra storage; an invalid main retains its new temporary.
     * This is the legacy recovery contract, not filesystem atomicity. Callbacks are
     * synchronous, may not re-enter, and may not edit state/index independently. */
    int mm_chunk_save(MMChunkSaveState *, const MMChunkSaveIndex *, const MMChunkSaveServices *,
                            MMU32 creator, int named);
    typedef struct MMChunkCopyServices
    {
        void *context;
        void *main_file, *extra_file, *destination_file;
        void (*pin)(void *);
        void (*unpin)(void *);
        int (*copy)(void *, void *source, MMU32 source_offset, void *destination, MMU32 destination_offset,
                    MMU32 bytes);
        /* Transfer the original record/variable bytes, replacing only the C
         * supplied offset/length/flags. Source bytes must stay pinned. */
        int (*insert)(void *, MMU32 position, const MMChunkEntry *);
        int (*finish)(void *, MMU32 logical_end, MMU32 creator);
    } MMChunkCopyServices;
    /* Destination creation/disposal stays with the host. Pin covers all reads,
     * payload copies and insertions; finish runs after unpin. Empty copies still
     * invoke copy and retain the running offset (unlike whole-file relocation).
     * Source records/files stay untouched; failure may leave partial destination
     * data/index for the host to discard. index.place is unused. */
    int mm_chunk_save_copy(const MMChunkSaveIndex *, const MMChunkCopyServices *, MMU32 creator);
#ifdef __cplusplus
}
#endif
#endif

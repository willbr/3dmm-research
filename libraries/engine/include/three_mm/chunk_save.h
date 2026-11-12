/* Whole-file save policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_SAVE_H
#define THREE_MM_ENGINE_CHUNK_SAVE_H
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkSaveState
    {
        void *main_file, *extra_file, *main_map, *extra_map;
        ThreeMMU32 main_end, extra_end;
        int invalid_main, add_to_extra, free_map_unread;
    } ThreeMMChunkSaveState;
    typedef struct ThreeMMChunkSaveIndex
    {
        void *context;
        ThreeMMU32 count;
        /* Trusted live index primitives: entry must fill every valid position, and
         * place cannot fail. Neither may re-enter or resize the index. Entry remains
         * stable except for the offsets/flags changed by place. No borrowed pointer
         * survives a callback, so file services may allocate/move host storage. */
        void (*entry)(void *, ThreeMMU32 position, ThreeMMChunkEntry *);
        void (*place)(void *, ThreeMMU32 position, ThreeMMU32 offset, ThreeMMU32 flags);
    } ThreeMMChunkSaveIndex;
    typedef struct ThreeMMChunkSaveServices
    {
        void *context;
        void *(*create_temp)(void *); /* Same directory as the host-resolved target. */
        int (*resize)(void *, void *file, ThreeMMU32 bytes);
        int (*copy)(void *, void *source, ThreeMMU32 source_offset, void *destination, ThreeMMU32 destination_offset,
                    ThreeMMU32 bytes);
        void (*release_file)(void *, void *file);
        void (*release_map)(void *, void *map);
        int (*is_temp)(void *, void *file);
        void (*set_temp)(void *, void *file, int temporary);
        int (*write_index)(void *, const ThreeMMChunkSaveState *, ThreeMMU32 creator);
        int (*rename)(void *, void *file); /* Existing target removal is host policy. */
        int (*swap_names)(void *, void *new_file, void *old_file);
        /* Optional facade synchronization before releasing detached references or
         * entering services after ownership changes. Must only mirror the state. */
        void (*publish)(void *, const ThreeMMChunkSaveState *);
    } ThreeMMChunkSaveServices;
    /* State owns the file/map references on entry and return, including failure.
     * Non-null release callbacks consume one reference. Named means the host has
     * resolved an explicit target different from the current name. Direct saves
     * write only the index. Relocation copies nonempty chunks before changing any
     * index/state, normalizes empty offsets to zero, and retains runtime flag bits.
     * On commit/naming failure a valid old main becomes main again and the copied
     * payload becomes extra storage; an invalid main retains its new temporary.
     * This is the legacy recovery contract, not filesystem atomicity. Callbacks are
     * synchronous, may not re-enter, and may not edit state/index independently. */
    int three_mm_chunk_save(ThreeMMChunkSaveState *, const ThreeMMChunkSaveIndex *, const ThreeMMChunkSaveServices *,
                            ThreeMMU32 creator, int named);
    typedef struct ThreeMMChunkCopyServices
    {
        void *context;
        void *main_file, *extra_file, *destination_file;
        void (*pin)(void *);
        void (*unpin)(void *);
        int (*copy)(void *, void *source, ThreeMMU32 source_offset, void *destination, ThreeMMU32 destination_offset,
                    ThreeMMU32 bytes);
        /* Transfer the original record/variable bytes, replacing only the C
         * supplied offset/length/flags. Source bytes must stay pinned. */
        int (*insert)(void *, ThreeMMU32 position, const ThreeMMChunkEntry *);
        int (*finish)(void *, ThreeMMU32 logical_end, ThreeMMU32 creator);
    } ThreeMMChunkCopyServices;
    /* Destination creation/disposal stays with the host. Pin covers all reads,
     * payload copies and insertions; finish runs after unpin. Empty copies still
     * invoke copy and retain the running offset (unlike whole-file relocation).
     * Source records/files stay untouched; failure may leave partial destination
     * data/index for the host to discard. index.place is unused. */
    int three_mm_chunk_save_copy(const ThreeMMChunkSaveIndex *, const ThreeMMChunkCopyServices *, ThreeMMU32 creator);
#ifdef __cplusplus
}
#endif
#endif

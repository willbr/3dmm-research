/* Container ownership converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_OWNER_H
#define THREE_MM_ENGINE_CHUNK_OWNER_H
#include "chunk_container.h"
#include "chunk_save.h"
#include "chunk_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkOwner
    {
        ThreeMMChunkReopenState files;
        ThreeMMChunkRuntime runtime;
        ThreeMMU32 map_offset, map_bytes;
        int marked, add_to_extra, read_from_extra, expanded_runtime;
    } ThreeMMChunkOwner;
    /* Unique owning record: initialize before use, never copy or move it. Files/index
     * own references and maps/runtime own C storage. Host primitives may update this
     * live record synchronously; there is no second native copy. Expanded indexes
     * omit compact runtime initialization/disposal. Reopen preserves flags and the
     * last-read deferred-map offsets even on rollback, as in legacy behavior. */
    int three_mm_chunk_owner_init(ThreeMMChunkOwner *, const ThreeMMStorageAllocator *, int expanded_runtime);
    int three_mm_chunk_owner_dispose(ThreeMMChunkOwner *, const ThreeMMChunkDisposal *);
    int three_mm_chunk_owner_reopen(ThreeMMChunkOwner *, const ThreeMMChunkReopenServices *);
    int three_mm_chunk_owner_options(ThreeMMChunkOwner *, const ThreeMMChunkOptionServices *, ThreeMMU32 flags,
                                     ThreeMMU32 mask);
    /* Synchronous save transaction view: aliases existing references without
     * retaining them or creating another independent owner. Publish through the
     * save callback before replaced references are released; do not dispose both
     * this view and the owner as separate owners. */
    ThreeMMChunkSaveState three_mm_chunk_owner_save_state(const ThreeMMChunkOwner *);
    /* Save publication changes only file/map/end/invalid/add/unread state. Index,
     * mark/read flags, deferred offsets and runtime caches remain unchanged. */
    void three_mm_chunk_owner_publish_save(ThreeMMChunkOwner *, const ThreeMMChunkSaveState *);
    ThreeMMChunkStoragePolicy three_mm_chunk_owner_storage_policy(const ThreeMMChunkOwner *, ThreeMMU32 max_bytes);
    typedef struct ThreeMMChunkErrorServices
    {
        ThreeMMI32 (*get)(void *file);
        void (*set)(void *file, ThreeMMI32 level);
    } ThreeMMChunkErrorServices;
    /* Initialized owner with a main file required. Query main before extra; reset
     * only lowers levels, preserving the callback order and avoiding redundant sets.
     * Services may not re-enter or mutate owner state. */
    ThreeMMI32 three_mm_chunk_owner_error(const ThreeMMChunkOwner *, const ThreeMMChunkErrorServices *);
    void three_mm_chunk_owner_reset_error(ThreeMMChunkOwner *, const ThreeMMChunkErrorServices *, ThreeMMI32 level);
#ifdef __cplusplus
}
#endif
#endif

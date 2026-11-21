/* Container ownership converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_OWNER_H
#define MM_ENGINE_CHUNK_OWNER_H
#include "chunk_container.h"
#include "chunk_save.h"
#include "chunk_storage.h"
#include "chunk_table.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkOwner
    {
        MMChunkReopenState files;
        MMChunkRuntime runtime;
        MMU32 map_offset, map_bytes;
        int marked, add_to_extra, read_from_extra, expanded_runtime;
    } MMChunkOwner;
    /* Unique owning record: initialize before use, never copy or move it. Files/index
     * own references and maps/runtime own C storage. Host primitives may update this
     * live record synchronously; there is no second native copy. Expanded indexes
     * omit compact runtime initialization/disposal. Reopen preserves flags and the
     * last-read deferred-map offsets even on rollback, as in legacy behavior. */
    int mm_chunk_owner_init(MMChunkOwner *, const MMStorageAllocator *, int expanded_runtime);
    int mm_chunk_owner_dispose(MMChunkOwner *, const MMChunkDisposal *);
    int mm_chunk_owner_reopen(MMChunkOwner *, const MMChunkReopenServices *);
    int mm_chunk_owner_options(MMChunkOwner *, const MMChunkOptionServices *, MMU32 flags,
                                     MMU32 mask);
    /* Synchronous save transaction view: aliases existing references without
     * retaining them or creating another independent owner. Publish through the
     * save callback before replaced references are released; do not dispose both
     * this view and the owner as separate owners. */
    MMChunkSaveState mm_chunk_owner_save_state(const MMChunkOwner *);
    /* Save publication changes only file/map/end/invalid/add/unread state. Index,
     * mark/read flags, deferred offsets and runtime caches remain unchanged. */
    void mm_chunk_owner_publish_save(MMChunkOwner *, const MMChunkSaveState *);
    MMChunkStoragePolicy mm_chunk_owner_storage_policy(const MMChunkOwner *, MMU32 max_bytes);

    enum
    {
        MM_CHUNK_OWNER_MAP_PRESENT = MM_EDIT_INDEX_OPERATION + 1,
        MM_CHUNK_OWNER_FREE_RANGE,
        MM_CHUNK_OWNER_EXTENT_LIMIT,
        MM_CHUNK_OWNER_MAP_UNREAD
    };
    typedef struct MMChunkOwnerFileIO
    {
        void *context;
        void *(*create_extra)(void *); /* Transfers a reference to a new empty file. */
        MMReadStream (*read_stream)(void *file);
        MMU32 (*size)(void *file);
        int (*resize)(void *file, MMU32 bytes);
        int (*copy)(void *, const MMChunkLocation *source, const MMChunkLocation *destination);
        void (*trace)(void *, int kind, const MMChunkEntry *); /* Optional. */
        void (*issue)(void *, int kind); /* Optional storage or edit diagnostic. */
    } MMChunkOwnerFileIO;
    typedef struct MMChunkOwnerStorage
    {
        MMChunkOwner *owner;
        const MMStorageAllocator *allocator;
        MMChunkOwnerFileIO io;
        MMChunkTableEditBinding edit;
        MMU32 max_bytes;
    } MMChunkOwnerStorage;
    /* Bind once at a stable address to an initialized owner. Editing and
     * payload lookup require a live table.
     * All file services except trace/issue are required. Their file arguments
     * borrow the owner's current handles. No callback may re-enter or modify
     * the owner. The binding follows file and table replacements on all calls. */
    void mm_chunk_owner_bind_storage(MMChunkOwnerStorage *, MMChunkOwner *,
                                    const MMStorageAllocator *, MMU32 max_bytes, const MMChunkOwnerFileIO *);
    /* Attempt a pending free-map load once. Its status is advisory and the
     * unread bit clears even if the map cannot be read. */
    int mm_chunk_owner_read_free_map(MMChunkOwnerStorage *);
    int mm_chunk_owner_cache(MMChunkOwnerStorage *, MMU32 position, MMChunkLocation *);
    int mm_chunk_owner_fetch(MMChunkOwnerStorage *, MMU32 position, MMChunkLocation *);
    MMChunkEdit mm_chunk_owner_editor(MMChunkOwnerStorage *, MMU32 max_references, int scan_all_references);

    typedef struct MMChunkErrorServices
    {
        MMI32 (*get)(void *file);
        void (*set)(void *file, MMI32 level);
    } MMChunkErrorServices;
    /* Initialized owner with a main file required. Query main before extra; reset
     * only lowers levels, preserving the callback order and avoiding redundant sets.
     * Services may not re-enter or mutate owner state. */
    MMI32 mm_chunk_owner_error(const MMChunkOwner *, const MMChunkErrorServices *);
    void mm_chunk_owner_reset_error(MMChunkOwner *, const MMChunkErrorServices *, MMI32 level);
#ifdef __cplusplus
}
#endif
#endif

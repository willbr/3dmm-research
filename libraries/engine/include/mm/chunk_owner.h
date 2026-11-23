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
        MM_CHUNK_OWNER_MAP_UNREAD,
        MM_CHUNK_OWNER_TABLE_DISPOSAL
    };
    typedef struct MMChunkOwnerFileIO
    {
        void *context;
        void *(*create_extra)(void *); /* Transfers a reference to a new empty file. */
        MMReadStream (*read_stream)(void *file);
        MMU32 (*size)(void *file);
        int (*resize)(void *file, MMU32 bytes);
        int (*copy)(void *, void *source, MMU32 source_offset, void *destination, MMU32 destination_offset,
                    MMU32 bytes);
        int (*write_at)(void *file, MMU32 offset, const void *bytes, size_t count);
        void (*release)(void *file);
        int (*is_temp)(void *file);
        void (*set_temp)(void *file, int temporary);
        void (*trace)(void *, int kind, const MMChunkEntry *); /* Optional. */
        void (*issue)(void *, int kind); /* Optional storage or edit diagnostic. */
        int (*is_writable)(void *file); /* Optional physical write state for reload. */
    } MMChunkOwnerFileIO;
    typedef struct MMChunkOwnerStorage
    {
        MMChunkOwner *owner;
        const MMStorageAllocator *allocator;
        MMChunkOwnerFileIO io;
        MMChunkTableEditBinding edit;
        MMU32 max_bytes;
    } MMChunkOwnerStorage;
    typedef struct MMChunkOwnerFileFlags
    {
        MMU32 (*get)(void *file);
        int (*set)(void *file, MMU32 flags, MMU32 mask);
    } MMChunkOwnerFileFlags;
    /* Apply archive options to the current main file and load a pending free map
     * if the options enable writes. The callbacks operate on physical files. */
    int mm_chunk_owner_file_options(MMChunkOwnerStorage *, const MMChunkOwnerFileFlags *, MMU32 flags, MMU32 mask);
    /* Bind once at a stable address to an initialized owner. Editing and
     * payload lookup require a live table.
     * write_at is required for saves. Relocating saves also require release and
     * both temporary flag operations. trace and issue are optional. Other
     * services are required for storage operations. No callback may re-enter
     * or modify the owner. The binding follows file and table replacements. */
    void mm_chunk_owner_bind_storage(MMChunkOwnerStorage *, MMChunkOwner *,
                                    const MMStorageAllocator *, MMU32 max_bytes, const MMChunkOwnerFileIO *);
    /* Load a live table on a new or detached owner with a main file. Publish
     * the table, logical end and deferred free-map extent together on success.
     * The table and scratch allocators may differ. */
    int mm_chunk_owner_read_index(MMChunkOwnerStorage *, const MMStorageAllocator *table_allocator,
                                 int trash_unused, MMU32 max_name_bytes, MMChunkLegacyName, void *name_context);
    /* Clear the legacy validation mark before checking the live graph. */
    int mm_chunk_owner_validate_index(MMChunkOwnerStorage *, MMU32 max_references);
    /* Attempt a pending free-map load once. Its status is advisory and the
     * unread bit clears even if the map cannot be read. */
    int mm_chunk_owner_read_free_map(MMChunkOwnerStorage *);
    int mm_chunk_owner_cache(MMChunkOwnerStorage *, MMU32 position, MMChunkLocation *);
    int mm_chunk_owner_fetch(MMChunkOwnerStorage *, MMU32 position, MMChunkLocation *);
    MMChunkEdit mm_chunk_owner_editor(MMChunkOwnerStorage *, MMU32 max_references, int scan_all_references);
    MMChunkAccess mm_chunk_owner_access(MMChunkOwnerStorage *);
    int mm_chunk_owner_locate(MMChunkOwnerStorage *, MMU32 type, MMU32 number, MMBlockFile *, int *packed);
    int mm_chunk_owner_read_name(MMChunkOwnerStorage *, MMU32 type, MMU32 number, MMU32 max_name_bytes,
                                 void *wire, MMU32 capacity, MMU32 *written);
    int mm_chunk_owner_write_name(MMChunkOwnerStorage *, MMU32 type, MMU32 number, MMU32 max_name_bytes,
                                  const void *wire, MMU32 bytes);

    typedef struct MMChunkWriteOptions
    {
        MMU32 max_references, system;
        int scan_all_references;
        void (*invalidate_number)(MMU32 *);
    } MMChunkWriteOptions;
    typedef struct MMChunkIndexOptions
    {
        const MMStorageAllocator *table_allocator;
        MMChunkLegacyName legacy_name;
        void (*validation_scope)(int entering); /* Optional diagnostic hook. */
        int trash_unused, restore_map_on_failure;
    } MMChunkIndexOptions;
    /* Borrow a stable storage binding and platform block services. Nonzero
     * write.max_references enables a writer bound to the current table. */
    typedef struct MMChunkOwnerRead
    {
        MMChunkOwnerStorage *storage;
        MMChunkPayloadServices payloads;
        MMU32 max_name_bytes;
        MMChunkIndexOptions index;
        MMChunkWriteOptions write;
    } MMChunkOwnerRead;
    MMChunkOwnerRead mm_chunk_owner_reader(MMChunkOwnerStorage *, const MMChunkPayloadServices *, MMU32 max_name_bytes);
    int mm_chunk_reader_read_index(const MMChunkOwnerRead *);
    int mm_chunk_reader_validate_index(const MMChunkOwnerRead *);
    /* Reload an attached archive, consulting physical write state if available.
     * Failure restores its graph, extents, and runtime cache. The index profile
     * selects whether failure also restores the old deferred free-map location. */
    int mm_chunk_reader_reopen(MMChunkOwnerRead *);
    /* MMChunkTitleIO callbacks for a borrowed MMChunkOwnerRead. */
    int mm_chunk_owner_title_read(void *, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written);
    int mm_chunk_owner_title_write(void *, MMU32 type, MMU32 number, const void *wire, MMU32 bytes);
    MMArchiveWriter mm_chunk_owner_writer(const MMChunkOwnerRead *);

    typedef struct MMChunkArchive MMChunkArchive;
    /* Allocate an empty owner and stable views. The caller attaches its file
     * and table using the owner services. The archive copies its options and
     * borrows their services. It releases its file and table on close. */
    MMChunkArchive *mm_chunk_archive_new(int expanded, const MMStorageAllocator *, const MMChunkOwnerFileIO *,
                                         const MMChunkPayloadServices *, MMU32 max_name_bytes,
                                         const MMChunkIndexOptions *, const MMChunkWriteOptions *);
    MMChunkOwnerRead *mm_chunk_archive_reader(MMChunkArchive *);
    int mm_chunk_archive_close(MMChunkArchive *);

    typedef struct MMChunkOwnerSavePaths
    {
        void *context;
        int (*matches)(void *, void *file, void *path);
        /* New reference in the directory of path, or of current when path is NULL. */
        void *(*create_temp)(void *, void *current, void *path);
        int (*rename)(void *, void *file, void *path);
        int (*swap_names)(void *, void *new_file, void *old_file);
    } MMChunkOwnerSavePaths;
    /* An omitted path or a match for the current main file keeps the current name.
     * Relocation uses the supplied path operations. A direct save without a
     * path needs no path services. Path and services are borrowed for the call. */
    int mm_chunk_owner_save(MMChunkOwnerStorage *, const MMChunkOwnerSavePaths *,
                            void *path, MMU32 creator, int system);
    struct MMFileOpsHost;
    struct MMRegisteredFile;
    /* Bind save publication to the file registry. Registration maps each
     * backing token to its record, whose object is that same token. Scratch
     * is an initialized name separate from the destination and file names. */
    int mm_chunk_owner_save_registered(MMChunkOwnerStorage *, const struct MMFileOpsHost *,
                                       struct MMRegisteredFile *(*registration)(void *file),
                                       void *path, void *scratch, MMU32 creator, int system);
    /* Destination owns a fresh writable file and empty table of the same width.
     * A failed copy may leave partial output for its owner to discard. The source
     * is unchanged. Optional directory_failed reports failure after the payload
     * and rows have been copied and the source has been unpinned. */
    int mm_chunk_owner_save_copy(MMChunkOwnerStorage *source, MMChunkOwnerStorage *destination,
                                 MMU32 creator, int system, int *directory_failed);

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

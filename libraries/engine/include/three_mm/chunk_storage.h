/* Chunk storage policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_STORAGE_H
#define THREE_MM_ENGINE_CHUNK_STORAGE_H
#include "chunk_edit.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkStoragePolicy
    {
        ThreeMMU32 max_bytes;
        int add_to_extra, read_from_extra, free_map_unread;
    } ThreeMMChunkStoragePolicy;
    enum
    {
        THREE_MM_STORAGE_CACHE,
        THREE_MM_STORAGE_FETCH_EXTRA,
        THREE_MM_STORAGE_FETCH_DIRECT
    };
    /* Borrowed live primitives. Entry returns a copied value; cache_place changes
     * only offset/on-extra and preserves runtime IDs and all other fields. reserve
     * delegates to the C heap and publishes its logical end; failure retains any
     * newly created extra file. Files and free-space owners stay with the host.
     * Callbacks may allocate/relocate storage but must not edit entries or re-enter
     * this policy. release_space is the legacy nonfatal free-space operation. */
    typedef struct ThreeMMChunkStorageServices
    {
        void *context;
        void *(*file)(void *, int extra);
        int (*create_extra)(void *);
        int (*reserve)(void *, int extra, ThreeMMU32 bytes, ThreeMMU32 *offset);
        void (*release_space)(void *, int extra, ThreeMMU32 offset, ThreeMMU32 bytes);
        void (*entry)(void *, ThreeMMU32 position, ThreeMMChunkEntry *);
        void (*cache_place)(void *, ThreeMMU32 position, ThreeMMU32 offset);
        int (*copy)(void *, const ThreeMMChunkLocation *source, const ThreeMMChunkLocation *destination);
        void (*trace)(void *, int kind, const ThreeMMChunkEntry *);
    } ThreeMMChunkStorageServices;
    /* Empty allocations always choose main, offset zero, without creating extra
     * storage or reading its free map. Nonempty main allocations require the map to
     * have been read. Outputs remain unchanged on failure. */
    int three_mm_chunk_storage_allocate(const ThreeMMChunkStoragePolicy *, const ThreeMMChunkStorageServices *,
                                        ThreeMMU32 bytes, int force_extra, ThreeMMChunkLocation *);
    /* Cache copies publish the location only after successful payload transfer;
     * failed copies release the new extra extent. Original main bytes are retained.
     * Already-extra/empty records avoid I/O and RTI invalidation. An empty record's
     * ensured location uses the current extra file, possibly NULL, without setting
     * its on-extra bit: this intentionally retains the native empty-cache behavior.
     * Optional output is published only on success. */
    int three_mm_chunk_storage_cache(const ThreeMMChunkStoragePolicy *, const ThreeMMChunkStorageServices *,
                                     ThreeMMU32 position, ThreeMMChunkLocation *);
    /* Read-from-extra tries caching first, then falls back to the recorded location
     * after failure. A successful empty cache retains the NULL-extra quirk above.
     * Trace is optional and preserves native cache/fetch diagnostic ordering. */
    int three_mm_chunk_storage_fetch(const ThreeMMChunkStoragePolicy *, const ThreeMMChunkStorageServices *,
                                     ThreeMMU32 position, ThreeMMChunkLocation *);
#ifdef __cplusplus
}
#endif
#endif

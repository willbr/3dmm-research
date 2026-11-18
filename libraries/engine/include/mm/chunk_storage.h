/* Chunk storage policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_STORAGE_H
#define MM_ENGINE_CHUNK_STORAGE_H
#include "block_owner.h"
#include "chunk_edit.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkStoragePolicy
    {
        MMU32 max_bytes;
        int add_to_extra, read_from_extra, free_map_unread;
    } MMChunkStoragePolicy;
    enum
    {
        MM_STORAGE_CACHE,
        MM_STORAGE_FETCH_EXTRA,
        MM_STORAGE_FETCH_DIRECT
    };
    /* Borrowed live primitives. Entry returns a copied value; cache_place changes
     * only offset/on-extra and preserves runtime IDs and all other fields. reserve
     * delegates to the C heap and publishes its logical end; failure retains any
     * newly created extra file. Files and free-space owners stay with the host.
     * Callbacks may allocate/relocate storage but must not edit entries or re-enter
     * this policy. release_space is the legacy nonfatal free-space operation. */
    typedef struct MMChunkStorageServices
    {
        void *context;
        void *(*file)(void *, int extra);
        int (*create_extra)(void *);
        int (*reserve)(void *, int extra, MMU32 bytes, MMU32 *offset);
        void (*release_space)(void *, int extra, MMU32 offset, MMU32 bytes);
        void (*entry)(void *, MMU32 position, MMChunkEntry *);
        void (*cache_place)(void *, MMU32 position, MMU32 offset);
        int (*copy)(void *, const MMChunkLocation *source, const MMChunkLocation *destination);
        void (*trace)(void *, int kind, const MMChunkEntry *);
    } MMChunkStorageServices;
    /* Empty allocations always choose main, offset zero, without creating extra
     * storage or reading its free map. Nonempty main allocations require the map to
     * have been read. Outputs remain unchanged on failure. */
    int mm_chunk_storage_allocate(const MMChunkStoragePolicy *, const MMChunkStorageServices *,
                                        MMU32 bytes, int force_extra, MMChunkLocation *);
    /* Cache copies publish the location only after successful payload transfer;
     * failed copies release the new extra extent. Original main bytes are retained.
     * Already-extra/empty records avoid I/O and RTI invalidation. An empty record's
     * ensured location uses the current extra file, possibly NULL, without setting
     * its on-extra bit: this intentionally retains the native empty-cache behavior.
     * Optional output is published only on success. */
    int mm_chunk_storage_cache(const MMChunkStoragePolicy *, const MMChunkStorageServices *,
                                     MMU32 position, MMChunkLocation *);
    /* Read-from-extra tries caching first, then falls back to the recorded location
     * after failure. A successful empty cache retains the NULL-extra quirk above.
     * Trace is optional and preserves native cache/fetch diagnostic ordering. */
    int mm_chunk_storage_fetch(const MMChunkStoragePolicy *, const MMChunkStorageServices *,
                                     MMU32 position, MMChunkLocation *);

    typedef struct MMChunkPayloadServices
    {
        const MMHandleServices *handles;
        const MMBlockFileServices *files;
        int (*locate)(void *archive, MMU32 type, MMU32 number, MMBlockFile *, int *packed);
        void *decode_context;
        MMBlockTransform decode;
    } MMChunkPayloadServices;
    /* Locate a borrowed extent by key, own the block through the receiver call,
     * and release it afterward. A NULL, zero-length extent is the legacy empty
     * extra-cache result. The receiver's result is published only on delivery.
     * Lookup and decoding failures return a chunk status without calling it. */
    int mm_chunk_read_payload(const MMChunkPayloadServices *, void *archive, MMU32 type,
                              MMU32 number, MMReadReceiver, void *context, int *result);
#ifdef __cplusplus
}
#endif
#endif

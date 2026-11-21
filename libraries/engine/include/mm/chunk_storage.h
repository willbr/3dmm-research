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

    /* Create or replace the stored bytes using the live index and returned file
     * extent. The write completes before the replacement takes effect. A failed
     * write discards the new extent and preserves an existing chunk. */
    int mm_chunk_put_bytes(const MMChunkEdit *, const MMBlockFileServices *, MMU32 type,
                           MMU32 number, const void *data, MMU32 bytes);
    /* Create at the first unused number. A failed write removes the new chunk. */
    int mm_chunk_add_bytes(const MMChunkEdit *, const MMBlockFileServices *, MMU32 type,
                           const void *data, MMU32 bytes, MMU32 *number);
    /* Create at the first unused number and give the caller an owned file block
     * for writing. The initialized block must be empty. Success publishes the
     * number and block, and the caller controls cleanup after later writes. */
    int mm_chunk_add_block(const MMChunkEdit *, MMU32 type, MMU32 bytes, MMU32 *number, MMOwnedBlock *);

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
    /* Deliver the stored extent under the same ownership contract, without decoding. */
    int mm_chunk_visit_stored(const MMChunkPayloadServices *, void *archive, MMU32 type,
                              MMU32 number, MMReadReceiver, void *context, int *result);
    /* Read a prefix of the stored extent without invoking a decoder. */
    int mm_chunk_read_stored(const MMChunkPayloadServices *, void *archive, MMU32 type,
                             MMU32 number, void *bytes, MMU32 count);

    enum
    {
        MM_CHUNK_PAYLOAD_OK,
        MM_CHUNK_PAYLOAD_MISSING,
        MM_CHUNK_PAYLOAD_INVALID,
        MM_CHUNK_PAYLOAD_BAD_HEADER,
        MM_CHUNK_PAYLOAD_LIMIT,
        MM_CHUNK_PAYLOAD_DECODE_FAILED
    };
    /* Bind the stored extent to an initialized empty block. The caller owns
     * the block through decoding and reading. */
    int mm_chunk_payload_bind_block(const MMChunkPayloadServices *, void *archive, MMU32 type, MMU32 number,
                                    MMOwnedBlock *);
    /* Initialize once and keep the cache at a stable address. The borrowed
     * services must remain live until the cache has been cleared. */
    typedef struct MMChunkPayloadCache
    {
        MMOwnedBlock block;
        const MMChunkPayloadServices *services;
        size_t position;
    } MMChunkPayloadCache;
    typedef struct MMChunkPayloadCacheCallbacks
    {
        void *context;
        void (*invalidate)(void *);
        int (*accept)(void *);
    } MMChunkPayloadCacheCallbacks;
    int mm_chunk_payload_cache_init(MMChunkPayloadCache *, const MMChunkPayloadServices *);
    int mm_chunk_payload_cache_clear(MMChunkPayloadCache *);
    /* One cache belongs to one archive. A hit at the given position skips the
     * callbacks and I/O. On a miss invalidate runs before releasing the old
     * block. Packed index entries check both stored and advertised decoded
     * lengths. The position is published only after successful decoding and
     * acceptance. Callbacks are optional and must not re-enter this cache.
     * Clear invalidates the cache without calling the load callbacks. */
    int mm_chunk_payload_cache_load(MMChunkPayloadCache *, void *archive, size_t position,
                                    const MMChunkEntry *, MMU32 max_packed_bytes,
                                    const MMChunkPayloadCacheCallbacks *);
    /* Returns nonzero with a descriptor and stream that borrow the cache
     * until the next miss or clear. Legacy readers may borrow the block
     * without mutating it. */
    int mm_chunk_payload_cache_stream(MMChunkPayloadCache *, MMBlockIO *, MMReadStream *);

    typedef struct MMChunkStreamPayload
    {
        MMReadSlice slice;
        MMMemoryStream memory;
        unsigned char *decoded;
        const MMReadStream *stream;
    } MMChunkStreamPayload;
    /* Locate the stored bytes within the archive body. */
    int mm_chunk_stored_slice(const MMReadStream *, const MMChunkHeader *, const MMChunkEntry *, MMReadSlice *);
    MMU32 mm_chunk_packed_decoded_length(const unsigned char header[8]);
    /* Open a stored extent as a borrowed slice or an owned decoded buffer.
     * Zero exact_size allows any decoded length. max_packed_bytes bounds both
     * the stored and decoded sizes of packed chunks. Keep the output at a
     * stable address and release its decoded buffer with free after success. */
    int mm_chunk_payload_open(const MMReadStream *, const MMChunkHeader *, const MMChunkEntry *,
                              MMU32 exact_size, MMU32 max_packed_bytes, MMChunkStreamPayload *);

    typedef enum MMChunkArchiveResult
    {
        MM_CHUNK_ARCHIVE_OK,
        MM_CHUNK_ARCHIVE_INDEX,
        MM_CHUNK_ARCHIVE_GRAPH,
        MM_CHUNK_ARCHIVE_PAYLOAD
    } MMChunkArchiveResult;
    /* Validate a physical archive before publishing it to a content source.
     * An empty index is valid only when allow_empty is set. */
    MMChunkArchiveResult mm_chunk_archive_validate(const MMReadStream *, const MMChunkPolicy *,
                                                   MMU32 max_packed_bytes, int allow_empty);
#ifdef __cplusplus
}
#endif
#endif

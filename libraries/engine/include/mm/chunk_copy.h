#ifndef MM_ENGINE_CHUNK_COPY_H
#define MM_ENGINE_CHUNK_COPY_H
#include "chunk_loading.h"
#include "chunk_ops.h"
#include "chunk_storage.h"
#include "movie_title.h"
#include "resource_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMChunkFileCopy
{
    void *file;
    MMChunkEdit edit;
    const MMChunkPayloadServices *payloads;
    MMChunkTitleIO titles;
    MMMovieTitle name;
    int allow_unbacked_empty;
} MMChunkFileCopy;

/* Copy the service descriptors and borrow the live file and index. Keep the
 * binding at a stable address. File identity identifies same-file requests.
 * Legacy file copies require a backing extent even for an empty chunk. */
MMChunkEndpoint mm_chunk_file_copy_endpoint(MMChunkFileCopy *, void *file, const MMArchiveWriter *,
                                           const MMChunkTitleIO *, unsigned text_unit, int allow_unbacked_empty);

typedef struct MMStoredChunkCopy
{
    MMResourceCache *cache;
    MMChunkAccess chunks;
    MMChunkTitleIO titles;
    MMChunkLegacyName legacy_name;
    MMMovieTitle name;
} MMStoredChunkCopy;

/* The cache must describe an immutable stored archive, borrowed by the binding.
 * The codec's file read/write callbacks are unused. */
MMChunkEndpoint mm_stored_chunk_copy_endpoint(MMStoredChunkCopy *, MMResourceCache *, void *identity,
                                             const MMStorageAllocator *, const MMChunkTitleIO *,
                                             MMChunkLegacyName, unsigned text_unit);

/* Scratch for a cache whose backing may be live or immutable. */
typedef struct MMResourceChunkCopy
{
    MMChunkFileCopy live;
    MMStoredChunkCopy stored;
} MMResourceChunkCopy;

#ifdef __cplusplus
}
#endif
#endif

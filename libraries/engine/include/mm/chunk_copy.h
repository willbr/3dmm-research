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

typedef struct MMResourceCopyServices
{
    const MMStorageAllocator *allocator;
    MMChunkTitleIO titles;
    MMChunkLegacyName legacy_name;
    unsigned text_unit;
    int (*stored_writable)(void *backing); /* Optional current write state. */
} MMResourceCopyServices;

/* Scratch for a cache whose backing may be live or immutable. */
typedef struct MMResourceChunkCopy
{
    MMChunkFileCopy live;
    MMStoredChunkCopy stored;
} MMResourceChunkCopy;

/* Borrow a live or immutable archive through its cache. Stored sources whose
 * physical write access is enabled are denied. Keep the binding address stable
 * through the copy. An empty endpoint reports a refused or unavailable source. */
MMChunkEndpoint mm_resource_cache_copy_source(MMResourceCache *, MMResourceChunkCopy *);

#ifdef __cplusplus
}
#endif
#endif

#ifndef MM_PRIVATE_ARCHIVE_CACHE_H
#define MM_PRIVATE_ARCHIVE_CACHE_H
#include "private_archive.h"
#include "resource_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Populate the source-dependent callbacks. Cache construction retains the
 * archive. Use the caller's handle provider when resource decoders require it. */
void mm_private_archive_cache_services(MMPrivateArchive *, const MMHandleServices *, MMResourceServices *);
MMResourceCache *mm_private_archive_cache_create(MMPrivateArchive *, MMI32 budget,
                                                 const MMResourceServices *,
                                                 const MMStorageAllocator *owner, const MMStorageAllocator *storage);
/* Open the selected forest chunk as a read-only archive. The archive retains
 * the decoded extent independently of the source cache. The file services
 * must match those used by the source cache. */
MMPrivateArchive *mm_private_archive_open_resource_forest(MMResourceCache *, MMU32 type, MMU32 number,
                                                         MMBlockTransform decode, void *decode_context,
                                                         const MMChunkOwnerFileIO *, const MMBlockFileServices *,
                                                         const MMPrivateArchiveProfile *);

typedef struct MMSharedArchiveCacheServices
{
    const MMNamedArchiveServices *files;
    const MMResourceServices *working, *legacy;
    void *(*find_legacy)(void *name);
    int (*prepare_legacy)(void *owner);
    const MMStorageAllocator *owner_allocator, *storage_allocator;
} MMSharedArchiveCacheServices;
/* Registered working archives take precedence over legacy owners. An open
 * legacy owner or a constrained file selects the live path. A selected
 * live path can fail independently of the availability of stored reads.
 * A successful result owns the archive backing the optional borrowed reader. */
MMResourceCache *mm_shared_archive_cache_open(const MMSharedArchiveCacheServices *, void *name, int prefer_working,
                                              MMI32 budget, int *selected, MMChunkOwnerRead **reader);

#ifdef __cplusplus
}
#endif
#endif

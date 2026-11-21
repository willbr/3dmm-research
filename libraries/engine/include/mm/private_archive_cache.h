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

#ifdef __cplusplus
}
#endif
#endif

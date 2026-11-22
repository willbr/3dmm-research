#ifndef KAUAI_RESOURCE_ARCHIVE_HOST_H
#define KAUAI_RESOURCE_ARCHIVE_HOST_H

#include "mm/types.h"

struct MMChunkOwnerRead;
struct MMNamedArchiveServices;
struct MMNativeArchiveSource;
struct MMResourceCache;
class Filename;

namespace Chunky
{
MMResourceCache *OpenWorkingResourceCache(Filename *, bool create, int budget = 0);
MMResourceCache *OpenForestResourceCache(MMResourceCache *, MMU32 type, MMU32 number);
const MMNamedArchiveServices *KauaiNamedArchiveServices();

// A successful cache owns the source of the borrowed reader. When selected
// is false, the caller can open a separate immutable stream instead.
MMResourceCache *OpenSharedTypedCache(Filename *, bool preferWorking, bool &selected, MMChunkOwnerRead **reader,
                                      int budget = 0);

// Supply the host path and chunk dialect for a C-owned immutable source.
int OpenNativeArchiveSource(Filename *, MMNativeArchiveSource *);
MMResourceCache *NewTypedSourceCache(const MMNativeArchiveSource *, int budget);
}

#endif

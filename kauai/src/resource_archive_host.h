#ifndef KAUAI_RESOURCE_ARCHIVE_HOST_H
#define KAUAI_RESOURCE_ARCHIVE_HOST_H

struct MMChunkOwnerRead;
struct MMResourceCache;
class Filename;

namespace Chunky
{
// A successful cache owns the source of the borrowed reader. When selected
// is false, the caller can open a separate immutable stream instead.
MMResourceCache *OpenSharedTypedCache(Filename *, bool preferWorking, bool &selected, MMChunkOwnerRead **reader,
                                      int budget = 0);
}

#endif

#ifndef TEXT_ARGUMENT_VIEW_FIXTURE_H
#define TEXT_ARGUMENT_VIEW_FIXTURE_H
#include "mm/collections.h"

/* Use the real C readers and lock counters over test-owned record bytes. The
 * caller initializes storage and keeps these borrowed buffers alive. */
static MMGroup text_argument_view(MMCollectionStorage *storage, void *data, MMU32 bytes,
                                  MMGroupLocation *locations, MMU32 count)
{
    MMGroup rows = {0};
    MMU32 index;
    storage->buffers[0].data = bytes ? data : 0;
    storage->buffers[0].capacity = bytes;
    storage->buffers[1].data = count ? (unsigned char *)locations : 0;
    storage->buffers[1].capacity = count * sizeof(*locations);
    rows.storage = storage;
    rows.fixed_bytes = 4;
    rows.bytes = bytes;
    rows.count = count;
    for (index = 0; index < count; ++index)
        if (locations[index].offset < 0)
            ++rows.free_count;
    return rows;
}
#endif

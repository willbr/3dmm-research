#ifndef MM_ENGINE_PART_INDICES_H
#define MM_ENGINE_PART_INDICES_H

#include "collection_storage.h"
#include "movie_data.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MM_PART_INDEX_BYTES = 2, MM_PART_INDEX_HEADER_BYTES = 12 };
typedef struct MMPartIndexView
{
    const unsigned char *bytes; /* Native-order signed 16-bit indices. */
    MMI32 count;
} MMPartIndexView;

typedef struct MMPartIndexList
{
    MMPartIndexView view;
    MMCollectionStorage storage;
    int allocated;
} MMPartIndexList;

/* Use a fresh list for reading, filling or copying. The view borrows its
 * bytes until the next mutation or disposal. The allocator owns the buffers
 * and, for lists returned by new, the list record. NULL selects the C heap. */
int mm_part_indices_init(MMPartIndexList *, const MMStorageAllocator *);
MMPartIndexList *mm_part_indices_new(const MMStorageAllocator *);
void mm_part_indices_dispose(MMPartIndexList *);
int mm_part_indices_read(MMPartIndexList *, const MMReadStream *);
int mm_part_indices_copy(MMPartIndexList *, const MMPartIndexView *);
int mm_part_indices_fill(MMPartIndexList *, MMI32 count, short index);
int mm_part_indices_append(MMPartIndexList *, short index);
MMI32 mm_part_index_get(const MMPartIndexView *, MMI32 index);
MMI32 mm_part_indices_wire_size(const MMPartIndexList *);

#ifdef __cplusplus
}
#endif
#endif

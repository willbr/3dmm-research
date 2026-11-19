#ifndef MM_ENGINE_MOVIE_SOURCE_IDS_H
#define MM_ENGINE_MOVIE_SOURCE_IDS_H

#include "collection_storage.h"

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MM_MOVIE_SOURCE_ID_MEMORY = -1,
    MM_MOVIE_SOURCE_ID_KNOWN = 0,
    MM_MOVIE_SOURCE_ID_ADDED = 1
};

typedef struct MMMovieSourceIds
{
    MMCollectionStorage storage;
    MMU32 count;
    MMI32 next;
} MMMovieSourceIds;

/* Unique ownership. Initialize once. Clear releases retained host archives and
 * storage, retaining the cursor for an internal reopen. Do not copy the state. */
int mm_movie_source_ids_init(MMMovieSourceIds *, const MMStorageAllocator *);
void mm_movie_source_ids_clear(MMMovieSourceIds *, void (*release)(void *));
void mm_movie_source_ids_swap(MMMovieSourceIds *, MMMovieSourceIds *);
/* Declared sources hold no archive. A generated archive transfers ownership
 * only on ADDED. Finding a source borrows its associated archive, if any. */
int mm_movie_source_ids_add(MMMovieSourceIds *, MMI32 id, void *archive);
int mm_movie_source_ids_find(const MMMovieSourceIds *, MMI32 id, void **archive);
/* Choose the default retail alias above the declared IDs when possible.
 * With an occupied signed maximum, choose the first free alias from 2. */
int mm_movie_source_ids_retail_alias(const MMMovieSourceIds *, MMI32 *id);
/* Generated IDs descend from INT32_MAX-1 and skip low reserved IDs, the
 * retail alias and registered sources. Lower the cursor before an internal
 * reopen so that regenerated resources cannot shadow older undo snapshots. */
void mm_movie_source_ids_limit(MMMovieSourceIds *, MMI32 cursor);
int mm_movie_source_ids_take(MMMovieSourceIds *, MMI32 retail, MMI32 *id);

#ifdef __cplusplus
}
#endif
#endif

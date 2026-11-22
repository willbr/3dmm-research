#ifndef MM_ENGINE_MOVIE_PLACEMENT_BATCH_H
#define MM_ENGINE_MOVIE_PLACEMENT_BATCH_H

#include "movie_scene_journal.h"
#include "preview_objects.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMMovieWorkerPlacements
{
    MMCollectionStorage storage;
    MMU32 count;
} MMMovieWorkerPlacements;

int mm_movie_worker_placements_init(MMMovieWorkerPlacements *, const MMStorageAllocator *);
void mm_movie_worker_placements_dispose(MMMovieWorkerPlacements *);
/* The published batch is valid until successful collection or disposal. */
const MMMovieActorPlacement *mm_movie_worker_placements_data(const MMMovieWorkerPlacements *);

/* The snapshot supplies object order and evaluated translations. The live
 * scene supplies the evaluated shapes at the same ordinals. Emit only changed
 * placements. Failure leaves the prior batch available. */
int mm_movie_worker_placements_collect(MMMovieWorkerPlacements *, const MMPreviewObjectRows *,
                                      const MMPreviewLive *, const MMMoviePlacementDrafts *,
                                      const MMMoviePoseJournal *, MMU32 scene);

#ifdef __cplusplus
}
#endif
#endif

#ifndef MM_ENGINE_MOVIE_CONTENT_RESOLVER_H
#define MM_ENGINE_MOVIE_CONTENT_RESOLVER_H

#include "movie_session.h"
#include "mm/content_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMMovieContent MMMovieContent;
typedef struct MMMovieArchiveProvider
{
    void *context;
    /* On success, publish an owned handle and its borrowed stream. On failure,
     * return a movie result and leave ownership with the provider. */
    int (*open)(void *, const char *location, void **handle, MMReadStream *);
    void (*close)(void *, void *handle);
} MMMovieArchiveProvider;

/* The movie, mounted VFS and provider context are borrowed and must outlive
 * the resolver. Keep mounts stable and serialize use. Close after the last
 * query. Content indexes and stream handles are opened on demand and cached. */
MMMovieContent *mm_movie_content_open(const MMMovieSession *, const MMVfs *, const MMMovieArchiveProvider *);
void mm_movie_content_close(MMMovieContent *);

/* MMMovieSourceRead callback. Looks up the authored source name and finds the
 * exact asset in mounted .3cn files in VFS order. */
int mm_movie_content_source(void *, const MMMovieAsset *, MMReadStream *);

#ifdef __cplusplus
}
#endif
#endif

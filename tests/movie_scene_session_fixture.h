#ifndef MOVIE_SCENE_SESSION_FIXTURE_H
#define MOVIE_SCENE_SESSION_FIXTURE_H
#include "movie_scene_archive_fixture.h"
#include "mm/movie_session.h"

enum { FIXTURE_SCENE_BASE = 100, FIXTURE_MOVIE_ROOT = 7 };

static void fixture_scene_archive_init(ChunkIndexFixture *f, int count)
{
    memset(f, 0, sizeof(*f));
    fixture_scene_seed_movie(f, FIXTURE_MOVIE_ROOT, FIXTURE_SCENE_BASE, (MMU32)count);
}
static void fixture_scene_access(void *p, MMChunkAccess *access)
{
    *access = fixture_chunk_access(p);
}
static int fixture_scene_root(void *p, void *request, MMU32 position, const MMChunkEntry *entry,
                              void *bytes, MMU32 size)
{
    const unsigned char header[] = {1, 0, 3, 3, 2, 0, 2, 0};
    (void)p;
    (void)request;
    (void)position;
    if (entry->type != MM_MOVIE_CHUNK || size != sizeof(header))
        return MM_MOVIE_BAD_FILE;
    memcpy(bytes, header, sizeof(header));
    return MM_MOVIE_OK;
}
static MMMovieSessionGraph fixture_scene_graph(ChunkIndexFixture *f)
{
    MMMovieSessionGraph graph = {NULL, fixture_scene_access, fixture_scene_root, NULL, NULL};
    graph.context = f;
    return graph;
}
#endif

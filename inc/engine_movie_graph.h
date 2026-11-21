#ifndef ENGINE_MOVIE_GRAPH_H
#define ENGINE_MOVIE_GRAPH_H

#include "kauai_core.h"
#include "mm/movie_session.h"

namespace EngineMovieGraph
{
inline MMMovieSessionGraph FromFile(PChunkyFile file)
{
    return mm_movie_owner_graph(file->ReadOwner());
}
}
#endif

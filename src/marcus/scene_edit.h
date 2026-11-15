#ifndef MARCUS_SCENE_EDIT_H
#define MARCUS_SCENE_EDIT_H
#include "movie_input.h"

// Writes only a newly reserved private file; the source movie is never modified.
std::shared_ptr<MarcusMovieFile> MarcusAppendScene(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, unsigned long background, int ordinal);
#endif

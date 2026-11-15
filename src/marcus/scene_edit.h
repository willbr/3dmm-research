#ifndef MARCUS_SCENE_EDIT_H
#define MARCUS_SCENE_EDIT_H
#include "movie_input.h"

// Writes only a newly reserved private file; the source movie is never modified.
std::shared_ptr<MarcusMovieFile> MarcusAppendScene(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, unsigned long background, int ordinal);
std::shared_ptr<MarcusMovieFile> MarcusSetSceneCamera(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, unsigned long scene, long frame, unsigned long camera);
std::shared_ptr<MarcusMovieFile> MarcusAppendProp(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, const std::filesystem::path &templates,
    unsigned long scene, long frame, unsigned long prop, const char *name, const float point[3], long &actorId);
struct MarcusPropPlacement
{
    unsigned long scene, actor;
    float delta[3];
};
struct MarcusSceneRange
{
    unsigned long scene;
    long first, last;
};
// Lazy materialization: copy once for all placement and frame-range edits.
std::shared_ptr<MarcusMovieFile> MarcusMaterializeEdits(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, const std::vector<MarcusPropPlacement> &placements, const std::vector<MarcusSceneRange> &ranges);
#endif

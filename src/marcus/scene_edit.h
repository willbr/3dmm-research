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
// Lazy materialization: copy the movie once for all committed placements.
std::shared_ptr<MarcusMovieFile> MarcusPlaceProps(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, const std::vector<MarcusPropPlacement> &placements);
#endif

#ifndef MARCUS_SCENE_EDIT_H
#define MARCUS_SCENE_EDIT_H
#include "movie_input.h"
#include "live_edits.h"

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
    std::shared_ptr<MarcusMovieFile> origin, const std::vector<MarcusPropPlacement> &placements, const std::vector<MarcusSceneRange> &ranges,
    const MarcusLiveEdits &edits, const std::filesystem::path &templates);
#endif

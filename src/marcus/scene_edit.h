#ifndef MARCUS_SCENE_EDIT_H
#define MARCUS_SCENE_EDIT_H
#include "movie_input.h"
#include "live_edits.h"

struct MarcusPropPlacement
{
    unsigned long scene, actor;
    float delta[3];
    MM::PreviewObjectShape shape;
};
struct MarcusSceneRange
{
    unsigned long scene;
    long first, last;
};
// Retail text uses Windows ANSI and at most fifty characters.
std::string MarcusTextAnsi(const char *utf8);
std::shared_ptr<MarcusMovieFile> MarcusCreateEventFile();
std::shared_ptr<MarcusMovieFile> MarcusCopyMovie(const std::shared_ptr<MarcusMovieFile> &source);
std::shared_ptr<MarcusMovieFile> MarcusCreateTextTemplate(const char *utf8, unsigned long font, long source, int shape);
// Lazy materialization: copy once for all placement and frame-range edits.
std::shared_ptr<MarcusMovieFile> MarcusMaterializeEdits(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, const std::vector<MarcusPropPlacement> &placements, const std::vector<MarcusSceneRange> &ranges,
    const MarcusLiveEdits &edits, const std::filesystem::path &templates);
#endif

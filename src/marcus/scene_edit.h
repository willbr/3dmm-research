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
struct MarcusObjectClipboard
{
    std::shared_ptr<MarcusMovieFile> file;
    std::vector<MM::MovieSourceEntry> sources;
    std::uint32_t actor, sounds;
    long frame;
    std::string name;
    int browserFlags;
};
std::unique_ptr<MarcusObjectClipboard> MarcusCopyObject(const std::filesystem::path &movie, unsigned long scene,
    long frame, const MarcusObject &object);
std::shared_ptr<MarcusMovieFile> MarcusPasteObject(const std::filesystem::path &movie,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusObjectClipboard &clipboard,
    unsigned long scene, long frame, std::int32_t actorId);
std::shared_ptr<MarcusMovieFile> MarcusReframeScene(const std::filesystem::path &movie,
    const std::shared_ptr<MarcusMovieFile> &origin, unsigned long scene, long frame,
    const MarcusProjection &work, const MarcusProjection &camera,
    const std::map<std::int32_t, MM::PreviewTrackPose> &poses);
// Retail text uses Windows ANSI and at most fifty characters.
std::string MarcusTextAnsi(const char *utf8);
std::shared_ptr<MarcusMovieFile> MarcusCreateEventFile();
std::shared_ptr<MarcusMovieFile> MarcusCopyMovie(const std::shared_ptr<MarcusMovieFile> &source);
std::shared_ptr<MarcusMovieFile> MarcusCopyMovie(const std::filesystem::path &path,
                                              const std::shared_ptr<MarcusMovieFile> &source);
std::shared_ptr<MarcusMovieFile> MarcusApplyAnimation(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusMovieFile &track,
    unsigned long scene, long actorId, long previousActorFrame, int lastActorFrame, int lastEditedFrame,
    const MM::PreviewObjectShape &shape, const MarcusTextEdit *text);
std::shared_ptr<MarcusMovieFile> MarcusCreateTextTemplate(const char *utf8, unsigned long font, long source, int shape);
// Lazy materialization: copy once for all placement and frame-range edits.
std::shared_ptr<MarcusMovieFile> MarcusMaterializeEdits(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, const std::vector<MarcusPropPlacement> &placements, const std::vector<MarcusSceneRange> &ranges,
    const MarcusLiveEdits &edits, const std::filesystem::path &templates);
#endif

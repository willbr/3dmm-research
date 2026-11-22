#ifndef MARCUS_SCENE_EDIT_H
#define MARCUS_SCENE_EDIT_H
#include "movie_input.h"
#include "live_edits.h"

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
MMTextAuthorName MarcusPrepareTextName(const char *utf8);
std::shared_ptr<MarcusMovieFile> MarcusCreateEventFile();
std::shared_ptr<MarcusMovieFile> MarcusCopyMovie(const std::shared_ptr<MarcusMovieFile> &source);
std::shared_ptr<MarcusMovieFile> MarcusCopyMovie(const std::filesystem::path &path,
                                              const std::shared_ptr<MarcusMovieFile> &source);
struct MMActorArchiveAnimation;
std::shared_ptr<MarcusMovieFile> MarcusApplyAnimation(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusMovieFile &track,
    const MMActorArchiveAnimation &, const MMMovieActorTextEdit *text);
std::shared_ptr<MarcusMovieFile> MarcusCreateTextTemplate(MMTextAuthorName name, MMU32 font, MMI32 source, MMI32 shape);
// Lazy materialization: copy once for all placement and frame-range edits.
std::shared_ptr<MarcusMovieFile> MarcusMaterializeEdits(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, const MMMoviePoseJournal &poses, const MMMovieRangeJournal &ranges,
    const MarcusLiveEdits &edits, const std::filesystem::path &templates);
#endif

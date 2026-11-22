#ifndef MARCUS_LIVE_EDITS_H
#define MARCUS_LIVE_EDITS_H
#include "mm/movie_scene_journal.h"
#include "mm/preview.h"
#include "mm/reference.h"
#include "storyboard.h"
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>
struct MarcusEditFile
{
    static void Release(MarcusEditFile *lease)
    {
        mm_reference_release(&lease->references, lease, [](void *p) { delete static_cast<MarcusEditFile *>(p); });
    }
    static const MMMovieEditFileServices *Services()
    {
        static const MMMovieEditFileServices services{
            [](void *p) { mm_reference_add(&static_cast<MarcusEditFile *>(p)->references); },
            [](void *p) { Release(static_cast<MarcusEditFile *>(p)); }};
        return &services;
    }
    static auto Hold(const std::shared_ptr<MarcusMovieFile> &file)
    {
        return std::unique_ptr<MarcusEditFile, decltype(&Release)>(file ? new MarcusEditFile(file) : nullptr, Release);
    }
    static const std::shared_ptr<MarcusMovieFile> &File(const void *p)
    {
        return static_cast<const MarcusEditFile *>(p)->file;
    }

  private:
    MMReference references;
    std::shared_ptr<MarcusMovieFile> file;
    explicit MarcusEditFile(const std::shared_ptr<MarcusMovieFile> &owner) : file(owner)
    {
        mm_reference_init(&references);
    }
};
struct MarcusEditedScenes : MMMovieSceneDefinitions
{
    MarcusEditedScenes() { mm_movie_scene_definitions_init(this, nullptr); }
    MarcusEditedScenes(const MarcusEditedScenes &other) : MarcusEditedScenes()
    {
        if (!mm_movie_scene_definitions_copy(this, &other)) throw std::bad_alloc();
    }
    MarcusEditedScenes &operator=(const MarcusEditedScenes &) = delete;
    ~MarcusEditedScenes() { mm_movie_scene_definitions_dispose(this); }
    const MMPreviewLiveScene *begin() const { return mm_movie_scene_definitions_data(this); }
    const MMPreviewLiveScene *end() const { return count ? begin() + count : begin(); }
    void Append(const MMPreviewLiveScene &scene)
    {
        if (!mm_movie_scene_definitions_append(this, &scene)) throw std::bad_alloc();
    }
};
struct MarcusEditedRanges : MMMovieRangeJournal
{
    MarcusEditedRanges() { mm_movie_range_journal_init(this, nullptr); }
    MarcusEditedRanges(const MarcusEditedRanges &other) : MarcusEditedRanges()
    {
        if (!mm_movie_range_journal_copy(this, &other)) throw std::bad_alloc();
    }
    MarcusEditedRanges &operator=(const MarcusEditedRanges &) = delete;
    ~MarcusEditedRanges() { mm_movie_range_journal_dispose(this); }
};
struct MarcusEditedCameras : MMMovieCameraJournal
{
    MarcusEditedCameras() { mm_movie_camera_journal_init(this, nullptr); }
    MarcusEditedCameras(const MarcusEditedCameras &other) : MarcusEditedCameras()
    {
        if (!mm_movie_camera_journal_copy(this, &other)) throw std::bad_alloc();
    }
    MarcusEditedCameras &operator=(const MarcusEditedCameras &) = delete;
    ~MarcusEditedCameras() { mm_movie_camera_journal_dispose(this); }
    const MMMovieCameraEdit *begin() const { return mm_movie_camera_journal_data(this); }
    const MMMovieCameraEdit *end() const { return count ? begin() + count : begin(); }
    void Set(const MMMovieCameraEdit &edit)
    {
        if (!mm_movie_camera_journal_set(this, &edit)) throw std::bad_alloc();
    }
};
struct MarcusEditedTracks : MMMovieActorTrackJournal
{
    MarcusEditedTracks() { mm_movie_track_journal_init(this, nullptr, MarcusEditFile::Services()); }
    MarcusEditedTracks(const MarcusEditedTracks &other) : MarcusEditedTracks()
    {
        if (!mm_movie_track_journal_copy(this, &other)) throw std::bad_alloc();
    }
    MarcusEditedTracks &operator=(const MarcusEditedTracks &) = delete;
    ~MarcusEditedTracks() { mm_movie_track_journal_dispose(this); }
    const MMMovieActorTrackEdit *begin() const { return mm_movie_track_journal_data(this); }
    const MMMovieActorTrackEdit *end() const { return count ? begin() + count : begin(); }
    void Set(MMU32 scene, MMU32 actor, MMI32 source, const std::shared_ptr<MarcusMovieFile> &file)
    {
        const auto lease = MarcusEditFile::Hold(file);
        const MMMovieActorTrackEdit edit{scene, actor, source, lease.get()};
        if (!mm_movie_track_journal_set(this, &edit)) throw std::bad_alloc();
    }
};
struct MarcusEditedProps : MMMoviePropJournal
{
    MarcusEditedProps() { mm_movie_prop_journal_init(this, nullptr, MarcusEditFile::Services()); }
    MarcusEditedProps(const MarcusEditedProps &other) : MarcusEditedProps()
    {
        if (!mm_movie_prop_journal_copy(this, &other)) throw std::bad_alloc();
    }
    MarcusEditedProps &operator=(const MarcusEditedProps &) = delete;
    ~MarcusEditedProps() { mm_movie_prop_journal_dispose(this); }
    const MMMoviePropEdit *begin() const { return mm_movie_prop_journal_data(this); }
    const MMMoviePropEdit *end() const { return count ? begin() + count : begin(); }
    void Append(const MMPreviewLiveProp &definition, const char *name,
                const std::shared_ptr<MarcusMovieFile> &file = {})
    {
        const auto lease = MarcusEditFile::Hold(file);
        if (!mm_movie_prop_journal_append(this, &definition, name, lease.get())) throw std::bad_alloc();
    }
};
struct MarcusEditedPoses : MMMoviePoseJournal
{
    MarcusEditedPoses() { mm_movie_pose_journal_init(this, nullptr); }
    MarcusEditedPoses(const MarcusEditedPoses &other) : MarcusEditedPoses()
    {
        if (!mm_movie_pose_journal_copy(this, &other)) throw std::bad_alloc();
    }
    MarcusEditedPoses &operator=(const MarcusEditedPoses &) = delete;
    ~MarcusEditedPoses() { mm_movie_pose_journal_dispose(this); }
};
struct MarcusEditedTexts : MMMovieTextJournal
{
    MarcusEditedTexts() { mm_movie_text_journal_init(this, nullptr, MarcusEditFile::Services()); }
    MarcusEditedTexts(const MarcusEditedTexts &other) : MarcusEditedTexts()
    {
        if (!mm_movie_text_journal_copy(this, &other)) throw std::bad_alloc();
    }
    MarcusEditedTexts &operator=(const MarcusEditedTexts &) = delete;
    ~MarcusEditedTexts() { mm_movie_text_journal_dispose(this); }
    const MMMovieActorTextEdit *begin() const { return mm_movie_text_journal_data(this); }
    const MMMovieActorTextEdit *end() const { return count ? begin() + count : begin(); }
    void Set(MMMovieActorTextEdit edit, const std::shared_ptr<MarcusMovieFile> &file)
    {
        const auto lease = MarcusEditFile::Hold(file);
        edit.file = lease.get();
        if (!mm_movie_text_journal_set(this, &edit)) throw std::bad_alloc();
    }
};
struct MarcusLiveEdits
{
    using Scenes = MarcusEditedScenes;
    using Props = MarcusEditedProps;
    using Cameras = MarcusEditedCameras;
    using Texts = MarcusEditedTexts;
    std::shared_ptr<const Scenes> scenes = std::make_shared<Scenes>();
    std::shared_ptr<const Props> props = std::make_shared<Props>();
    std::shared_ptr<const Cameras> cameras = std::make_shared<Cameras>();
    std::shared_ptr<const Texts> texts = std::make_shared<Texts>();
    std::shared_ptr<const MarcusEditedTracks> costumes = std::make_shared<MarcusEditedTracks>();
    std::shared_ptr<const MarcusStoryboards> storyboards = std::make_shared<MarcusStoryboards>();
    std::uint64_t revision = 0;
};
#endif

#ifndef MARCUS_LIVE_EDITS_H
#define MARCUS_LIVE_EDITS_H
#include "mm/movie_scene_journal.h"
#include "mm/preview.h"
#include "storyboard.h"
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>
struct MarcusNamedProp
{
    MM::PreviewPropDefinition definition;
    std::string name;
    std::shared_ptr<MarcusMovieFile> file; // Generated text template, retained through undo and native export.
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
struct MarcusTextEdit
{
    MM::PreviewObjectText value;
    std::shared_ptr<MarcusMovieFile> file;
    std::int32_t source = 0;
    bool retailFont = false;
    bool hasMaterial = false;
};
struct MarcusLiveEdits
{
    using Scenes = MarcusEditedScenes;
    using Props = std::vector<MarcusNamedProp>;
    using Cameras = MarcusEditedCameras;
    using Shapes = std::map<std::pair<unsigned long, unsigned long>, MM::PreviewObjectShape>;
    using Texts = std::map<std::pair<unsigned long, unsigned long>, MarcusTextEdit>;
    // Immutable native event snapshots; undo keeps the private file alive.
    struct CostumeEdit { std::shared_ptr<MarcusMovieFile> file; std::int32_t source = 0; };
    using Costumes = std::map<std::pair<unsigned long, unsigned long>, CostumeEdit>;
    std::shared_ptr<const Scenes> scenes = std::make_shared<Scenes>();
    std::shared_ptr<const Props> props = std::make_shared<Props>();
    std::shared_ptr<const Cameras> cameras = std::make_shared<Cameras>();
    std::shared_ptr<const Shapes> shapes = std::make_shared<Shapes>();
    std::shared_ptr<const Texts> texts = std::make_shared<Texts>();
    std::shared_ptr<const Costumes> costumes = std::make_shared<Costumes>();
    std::shared_ptr<const MarcusStoryboards> storyboards = std::make_shared<MarcusStoryboards>();
    std::uint64_t revision = 0;
};
#endif

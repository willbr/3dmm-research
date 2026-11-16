#ifndef MARCUS_LIVE_EDITS_H
#define MARCUS_LIVE_EDITS_H
#include "mm/preview.h"
#include "storyboard.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
struct MarcusNamedProp
{
    MM::PreviewPropDefinition definition;
    std::string name;
    std::shared_ptr<MarcusMovieFile> file; // Generated text template, retained through undo and native export.
};
struct MarcusCameraKey
{
    unsigned long scene;
    long frame;
    bool operator<(const MarcusCameraKey &other) const
    { return scene < other.scene || (scene == other.scene && frame < other.frame); }
};
struct MarcusTextEdit
{
    MM::PreviewObjectText value;
    std::shared_ptr<MarcusMovieFile> file;
    std::int32_t source = 0;
    bool retailFont = false;
};
struct MarcusLiveEdits
{
    using Scenes = std::vector<MM::PreviewSceneDefinition>;
    using Props = std::vector<MarcusNamedProp>;
    using Cameras = std::map<MarcusCameraKey, unsigned long>;
    using Shapes = std::map<std::pair<unsigned long, unsigned long>, MM::PreviewObjectShape>;
    using Texts = std::map<std::pair<unsigned long, unsigned long>, MarcusTextEdit>;
    std::shared_ptr<const Scenes> scenes = std::make_shared<Scenes>();
    std::shared_ptr<const Props> props = std::make_shared<Props>();
    std::shared_ptr<const Cameras> cameras = std::make_shared<Cameras>();
    std::shared_ptr<const Shapes> shapes = std::make_shared<Shapes>();
    std::shared_ptr<const Texts> texts = std::make_shared<Texts>();
    std::shared_ptr<const MarcusStoryboards> storyboards = std::make_shared<MarcusStoryboards>();
    std::uint64_t revision = 0;
};
#endif

#ifndef MARCUS_SOUND_TIMELINE_H
#define MARCUS_SOUND_TIMELINE_H
#include "player.h"
#include "three_mm/preview.h"
#include <functional>
#include <memory>
#include <vector>

struct MarcusSoundSpan
{
    ThreeMM::PreviewSoundCue cue;
    MarcusWaveformInfo audio = {};
    int scene = 0;
    double start = 0, end = 0; // Seconds on the nominal movie clock.
};
struct MarcusSoundScene
{
    double start = 0;
    MarcusScene info = {};
    std::vector<std::shared_ptr<MarcusSoundSpan>> sounds;
};
// Read-only scheduling on the authored six-fps clock. Scene boundaries use the
// same lifetime policy as playback: sampled loops stop, other voices continue.
class MarcusSoundTimeline
{
    std::vector<MarcusSoundScene> scenes;
    std::vector<std::shared_ptr<MarcusSoundSpan>> active;
    double time = 0;

  public:
    void Clear();
    std::size_t Size() const { return scenes.size(); }
    const MarcusSoundScene &Scene(std::size_t index) const { return scenes.at(index); }
    void Append(const MarcusScene &scene, const std::vector<ThreeMM::PreviewSoundCue> &cues,
                const std::function<MarcusWaveformInfo(const ThreeMM::PreviewSoundCue &)> &read);
};
#endif

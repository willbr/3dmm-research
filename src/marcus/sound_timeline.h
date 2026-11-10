#ifndef MARCUS_SOUND_TIMELINE_H
#define MARCUS_SOUND_TIMELINE_H
#include "player.h"
#include "three_mm/preview.h"
#include <functional>
#include <memory>
#include <map>
#include <vector>

struct MarcusSoundSpan
{
    ThreeMM::PreviewSoundCue cue;
    MarcusWaveformInfo audio = {};
    int scene = 0;
    double start = 0, end = 0; // Seconds on the presentation clock.
};
struct MarcusSoundScene
{
    double start = 0;
    MarcusScene info = {};
    std::map<long, double> holds; // Extra seconds spent on each displayed frame.
    std::vector<std::shared_ptr<MarcusSoundSpan>> sounds;
    double TimeAt(double frame) const;
    double FrameAt(double seconds) const;
};
struct MarcusSoundTiming
{
    double transition = 0;
    std::map<long, double> scrolling;
    std::vector<ThreeMM::PreviewPause> pauses;
};
struct MarcusResumeSound
{
    ThreeMM::PreviewSoundCue cue;
    double seconds = 0;
};
// Read-only scheduling on the presentation clock. Scene boundaries use the
// same lifetime policy as playback: sampled loops stop, other voices continue.
class MarcusSoundTimeline
{
    std::vector<MarcusSoundScene> scenes;
    std::vector<std::shared_ptr<MarcusSoundSpan>> active;
    double time = 0, transition = 0;

  public:
    void Clear();
    std::size_t Size() const { return scenes.size(); }
    const MarcusSoundScene &Scene(std::size_t index) const { return scenes.at(index); }
    std::vector<MarcusResumeSound> Resume(std::size_t scene, long frame) const;
    void Append(const MarcusScene &scene, const std::vector<ThreeMM::PreviewSoundCue> &cues,
                const std::function<MarcusWaveformInfo(const ThreeMM::PreviewSoundCue &)> &read,
                const MarcusSoundTiming &timing = {});
};
#endif

#ifndef MARCUS_SOUND_TIMELINE_H
#define MARCUS_SOUND_TIMELINE_H
#include "player.h"
#include "mm/movie_sound_schedule.h"
#include "mm/preview.h"
#include <functional>
#include <memory>
#include <optional>
#include <map>
#include <vector>
#include <iosfwd>

struct MarcusSoundSpan
{
    MM::PreviewSoundCue cue;
    std::size_t scene = 0;
    double start = 0, end = 0; // Seconds on the presentation clock.
};
class MarcusSoundTimeline;
struct MarcusSoundScene
{
    double start;
    MarcusScene info;
    double TimeAt(double frame) const;
    double FrameAt(double seconds) const;
    std::size_t SoundCount() const;
    MarcusSoundSpan Sound(std::size_t row) const;

  private:
    friend class MarcusSoundTimeline;
    MarcusSoundScene(const MarcusSoundTimeline &owner, std::size_t index);
    const MarcusSoundTimeline &owner;
    std::size_t index;
};
struct MarcusSoundTiming
{
    double transition = 0;
    std::map<long, double> scrolling;
    std::vector<MM::PreviewPause> pauses;
};
struct MarcusResumeSound
{
    MM::PreviewSoundCue cue;
    double seconds = 0;
};
// Read-only scheduling on the presentation clock. Scene boundaries use the
// same lifetime policy as playback: sampled loops stop, other voices continue.
class MarcusSoundTimeline
{
    using Schedule = std::unique_ptr<MMMovieSoundSchedule, decltype(&mm_movie_sound_schedule_destroy)>;
    static Schedule Create(double frameSeconds);
    MarcusSoundTimeline(Schedule, std::vector<MarcusScene>);
    friend struct MarcusSoundScene;
    Schedule schedule;
    std::vector<MarcusScene> scenes;

  public:
    MarcusSoundTimeline();
    void Clear();
    void SetRate(double fps);
    // Reschedule cached cues/lengths after edge edits, without decoding media.
    bool Reframe(const std::vector<MarcusScene> &ranges);
    std::optional<MarcusSoundTimeline> Reframed(const std::vector<MarcusScene> &ranges) const;
    // Ephemeral IPC between matching Marcus/helper builds, never movie data.
    bool Write(std::ostream &output) const;
    bool Read(std::istream &input, const std::vector<MarcusScene> &expected);
    std::size_t Size() const { return mm_movie_sound_schedule_size(schedule.get()); }
    MarcusSoundScene Scene(std::size_t index) const { return MarcusSoundScene(*this, index); }
    std::vector<MarcusResumeSound> Resume(std::size_t scene, long frame) const;
    void Append(const MarcusScene &scene, const std::vector<MM::PreviewSoundCue> &cues,
                const std::function<MarcusWaveformInfo(const MM::PreviewSoundCue &)> &read,
                const MarcusSoundTiming &timing = {});
};
#endif

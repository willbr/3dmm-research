#ifndef MARCUS_SOUND_TIMELINE_H
#define MARCUS_SOUND_TIMELINE_H
#include "player.h"
#include "mm/movie_sound_schedule.h"
#include "mm/preview.h"
#include "sound_sheet.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <iosfwd>

struct MarcusSoundSpan
{
    MM::PreviewSoundCue cue;
    std::size_t scene = 0;
    double start = 0, end = 0; // Seconds on the presentation clock.
};
struct MarcusPlaybackScene;
MM::PreviewSoundCue MarcusPreviewCue(const MMMovieTimingCue &);
class MarcusSoundTimeline;
struct MarcusSoundScene
{
    double start;
    MMMovieTimingRange range;
    double TimeAt(double frame) const;
    double FrameAt(double seconds) const;
    std::size_t SoundCount() const;
    MarcusSoundSpan Sound(std::size_t row) const;
    int BuildSheet(MarcusSoundSheet &, const MarcusSoundSheetNames &) const;
    void ChapterTime(MarcusSceneTime &) const;
    void MovieTime(MMI32 frame, double elapsed, bool finished, MarcusMovieTime &) const;

  private:
    friend class MarcusSoundTimeline;
    MarcusSoundScene(const MarcusSoundTimeline &owner, std::size_t index);
    const MarcusSoundTimeline &owner;
    std::size_t index;
};
struct MarcusSoundTiming : MMMovieTimingHolds
{
    MarcusSoundTiming() : MMMovieTimingHolds{} {}
    ~MarcusSoundTiming() { mm_movie_timing_holds_dispose(this); }
    MarcusSoundTiming(const MarcusSoundTiming &) = delete;
    MarcusSoundTiming &operator=(const MarcusSoundTiming &) = delete;
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
    explicit MarcusSoundTimeline(Schedule);
    friend struct MarcusSoundScene;
    friend class MarcusTimingJob;
    Schedule schedule;

  public:
    using WaveformReader = std::function<MarcusWaveformInfo(const MM::PreviewSoundCue &)>;
    using PresentationReader = std::function<bool(std::size_t, MarcusPlaybackScene &, MMMovieTimingHolds &)>;
    MarcusSoundTimeline();
    void Clear();
    void SetRate(double fps);
    // Reschedule cached cues/lengths after edge edits, without decoding media.
    bool Reframe(const MarcusScene *ranges, std::size_t count);
    bool Reframe(const std::vector<MarcusScene> &ranges) { return Reframe(ranges.data(), ranges.size()); }
    std::optional<MarcusSoundTimeline> Reframed(const MarcusScene *ranges, std::size_t count) const;
    // Ephemeral IPC between matching Marcus/helper builds, never movie data.
    bool Write(std::ostream &output) const;
    bool Read(std::istream &input, const std::vector<MarcusScene> &expected);
    std::size_t Size() const { return mm_movie_sound_schedule_size(schedule.get()); }
    MarcusSoundScene Scene(std::size_t index) const { return MarcusSoundScene(*this, index); }
    bool Locate(double seconds, MMMovieSoundPosition &) const;
    std::vector<MarcusResumeSound> Resume(std::size_t scene, long frame) const;
    bool Extend(const MarcusScene *scenes, std::size_t count, const PresentationReader &, const WaveformReader &);
    void Append(const MarcusScene &scene, const std::vector<MM::PreviewSoundCue> &cues,
                const WaveformReader &read, const MMMovieTimingHolds &timing = {});
};
#endif

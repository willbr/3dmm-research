#ifndef MARCUS_PLAYBACK_H
#define MARCUS_PLAYBACK_H
#include "three_mm/preview.h"
#include "player.h"
#include "sound_timeline.h"
#include <memory>
class MarcusPlayback
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    MarcusPlayback();
    ~MarcusPlayback();
    bool Select(ThreeMM::Preview &preview, ThreeMM::Error &error, bool preserveAudio = false);
    void Seek();
    void Stop();
    static bool ReadTiming(const ThreeMM::PreviewPresentation &presentation, MarcusSoundTiming &timing,
                           ThreeMM::Error &error);
    bool RestoreAudio(ThreeMM::Preview &preview, const std::vector<MarcusResumeSound> &sounds, ThreeMM::Error &error);
    void Statistics(MarcusAudioStats &stats);
    void Mute(bool muted);
    // Advance one displayed frame only after its sounds, scrolling and waits
    // have been serviced. The host supplies elapsed milliseconds, so backend
    // timing can be exercised without an application window or UI events.
    bool Update(ThreeMM::Preview &preview, bool playing, double elapsed, bool click, bool &advance,
                ThreeMM::Error &error);
    int Wait() const;
    bool Scrolling() const;
    bool Transitioning() const;
    int Transition() const;
    void BeginTransition(int kind, const ThreeMM::PreviewImage &previous);
    bool Composite(ThreeMM::Preview &preview, bool authored, ThreeMM::PreviewImage &image, ThreeMM::Error &error);
};
#endif

#ifndef MARCUS_PLAYBACK_H
#define MARCUS_PLAYBACK_H
#include "mm/preview.h"
#include "player.h"
#include "sound_timeline.h"
#include <memory>
namespace KauaiFonts { class FontCatalog; }
class MarcusPlayback
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    MarcusPlayback();
    ~MarcusPlayback();
    bool Select(MM::Preview &preview, MM::Error &error, bool preserveAudio = false);
    void Seek(MM::Preview *preview = nullptr);
    void Stop();
    bool Audition(MM::Preview &, MM::PreviewSoundCue, MM::Error &);
    static bool ReadTiming(const MM::PreviewPresentation &presentation, MarcusSoundTiming &timing,
                           MM::Error &error, const std::shared_ptr<KauaiFonts::FontCatalog> &fonts = {});
    void SetFonts(std::shared_ptr<KauaiFonts::FontCatalog> fonts);
    bool RestoreAudio(MM::Preview &preview, const std::vector<MarcusResumeSound> &sounds, MM::Error &error);
    void Statistics(MarcusAudioStats &stats);
    void Mute(bool muted);
    void SetTrackMix(bool actorSound, long actor, long style, bool muted, bool solo);
    void TrackMix(bool actorSound, long actor, long style, int &muted, int &solo, int &audible) const;
    void ResetMix();
    bool MixActive() const;
    void SetRate(double fps);
    // Advance one displayed frame only after its sounds, scrolling and waits
    // have been serviced. The host supplies elapsed milliseconds, so backend
    // timing can be exercised without an application window or UI events.
    bool Update(MM::Preview &preview, bool playing, double elapsed, bool click, bool &advance,
                MM::Error &error);
    int Wait() const;
    bool Scrolling() const;
    bool Transitioning() const;
    // Elapsed presentation time within this frame, or a negative incoming
    // transition remainder. User click-wait time is excluded.
    double FrameSeconds() const;
    int Transition() const;
    void BeginTransition(int kind, const MM::PreviewImage &previous);
    bool Composite(MM::Preview &preview, bool authored, MM::PreviewImage &image, MM::Error &error);
};
#endif

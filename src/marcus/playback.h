#ifndef MARCUS_PLAYBACK_H
#define MARCUS_PLAYBACK_H
#include "mm/preview.h"
#include "player.h"
#include "sound_timeline.h"
#include <memory>
namespace KauaiFonts { class FontCatalog; }
struct MMPolledPlayback;
struct MMGraphicsMovieCapture;
struct MMPreviewOwnedCaption;
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
    bool Auditioning() const;
    static bool ReadTiming(const MM::PreviewPresentation &presentation, MMMovieTimingHolds &timing,
                           MM::Error &error, const std::shared_ptr<KauaiFonts::FontCatalog> &fonts = {});
    static bool ReadTiming(MM::Preview &, MM::Id, MarcusPlaybackScene &, MMMovieTimingHolds &,
                           MM::Error &, const std::shared_ptr<KauaiFonts::FontCatalog> &);
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
    const MMPolledPlayback &Clock() const;
    bool Transitioning() const;
    bool PaletteAnimating() const;
    // Elapsed presentation time within this frame, or a negative incoming
    // transition remainder. User click-wait time is excluded.
    double FrameSeconds() const;
    int Transition() const;
    void PresentMovieFrame(int scene, MM::PreviewImage &);
    void InvalidateMovieFrame();
    void ForgetMovieFrame();
    void CaptureMovieFrame(int scene, MMGraphicsMovieCapture &, const MM::Preview &);
    void BeginTransition(int kind, MMGraphicsMovieCapture &, const MM::Preview &incoming);
    bool Composite(MM::Preview &preview, bool authored, MM::PreviewImage &image, MM::Error &error,
                   const std::uint32_t *hiddenCaption = nullptr, bool editDraft = false);
    bool CaptionAt(int x, int y, std::uint32_t &chunk, const std::uint32_t *hiddenCaption) const;
    bool CaptionDraft(MMPreviewOwnedCaption &, MM::Error &);
    void ClearCaptionDraft();
    const MarcusCaptionStop *CaptionPositions(int &count, int &scroll) const;
    bool CaptionCaret(int offset);
    bool CaptionFontSample(const std::string &family, MM::PreviewImage &image);
};
#endif

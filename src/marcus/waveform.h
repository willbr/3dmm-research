#ifndef MARCUS_WAVEFORM_H
#define MARCUS_WAVEFORM_H
#include "waveform_core.h"
#include "mm/preview.h"

struct MarcusSoundScene;
struct MarcusSoundSpan;

// A min/max tree preserves short transients when reducing many samples to a
// screen column. Only peaks survive decoding; no audio device is opened.
class MarcusWaveformEnvelope
{
    MarcusWaveform wave = {};

  public:
    MarcusWaveformEnvelope() = default;
    ~MarcusWaveformEnvelope();
    MarcusWaveformEnvelope(const MarcusWaveformEnvelope &) = delete;
    MarcusWaveformEnvelope &operator=(const MarcusWaveformEnvelope &) = delete;
    void Build(const float *stereo, std::size_t frameCount);
    void Read(double first, double step, int count, MarcusWaveformColumn *columns, bool repeat = false) const;
    double Seconds() const;
};

class MarcusWaveformCache
{
    MarcusWaveformCacheState cache = {};
    const MarcusWaveform *Peaks(MM::Preview &, const MM::PreviewSoundCue &, MarcusWaveformInfo &);

  public:
    MarcusWaveformCache() = default;
    ~MarcusWaveformCache();
    MarcusWaveformCache(const MarcusWaveformCache &) = delete;
    MarcusWaveformCache &operator=(const MarcusWaveformCache &) = delete;
    void Clear();
    MarcusWaveformInfo Length(MM::Preview &preview, const MM::PreviewSoundCue &cue);
    void Read(MM::Preview &preview, const MM::PreviewSoundCue &cue, double first, double step, int count,
              MarcusWaveformColumn *columns, MarcusWaveformInfo &info);
    void ReadFrames(MM::Preview &, const MarcusSoundScene &, const MarcusSoundSpan &,
                    double first, double step, int count, MarcusWaveformColumn *, MarcusWaveformInfo &);
};
#endif

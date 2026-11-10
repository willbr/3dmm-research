#ifndef MARCUS_WAVEFORM_H
#define MARCUS_WAVEFORM_H
#include "player.h"
#include "three_mm/preview.h"
#include <map>
#include <tuple>
#include <vector>

// A min/max tree preserves short transients when reducing many samples to a
// screen column. Only peaks survive decoding; no audio device is opened.
class MarcusWaveformEnvelope
{
    std::vector<MarcusWaveformColumn> tree;
    std::size_t leaves = 0, frames = 0;

  public:
    void Build(const float *stereo, std::size_t frameCount);
    void Read(double first, double step, int count, MarcusWaveformColumn *columns, bool repeat = false) const;
    double Seconds() const;
    std::size_t Bytes() const;
};

class MarcusWaveformCache
{
    using Key = std::tuple<int, unsigned, unsigned, unsigned, unsigned>;
    struct Entry
    {
        MarcusWaveformEnvelope envelope;
        MarcusWaveformInfo info = {};
        std::uint64_t used = 0;
    };
    std::map<Key, Entry> entries;
    std::size_t bytes = 0;
    std::uint64_t clock = 0;

  public:
    void Clear();
    void Read(ThreeMM::Preview &preview, const ThreeMM::PreviewSoundCue &cue, double first, double step,
              int count, MarcusWaveformColumn *columns, MarcusWaveformInfo &info);
};
#endif

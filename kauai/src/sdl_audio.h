/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_AUDIO_H
#define KAUAI_SDL_AUDIO_H
#include "sdl_audio_output.h"
#include "mm/wave_import.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
namespace KauaiAudio
{
constexpr int SampleRate = MM_AUDIO_MIX_RATE;
struct SampleDeleter
{
    void operator()(MMAudioSample *sample) const { mm_audio_sample_release(sample); }
};
using Sample = std::unique_ptr<MMAudioSample, SampleDeleter>;
bool DecodeWave(const void *data, std::size_t bytes, Sample &sample);
// Match DecodeWave's output frame count without resampling or allocating float PCM.
bool WaveSeconds(const void *data, std::size_t bytes, double &seconds);
// Preserve compatible source bytes when compression would not save space;
// otherwise use legacy 11.025 kHz mono PCM for short clips or MS ADPCM.
bool PrepareWaveImport(const void *data, std::size_t bytes, std::vector<std::uint8_t> &wave);
extern "C" int ImportWave(void *, const MMReadStream *, MMI32 style, const MMChunkNameValue *, MMU32 *number,
                          const MMArchiveWriter *);
class LockedMixer
{
    MMAudioDeviceMixer *device;
    MMAudioMixer *core;

  public:
    explicit LockedMixer(MMSdlAudioOutput *output)
        : device(mm_sdl_audio_output_mixer(output)), core(mm_audio_device_mixer_enter(device))
    {
    }
    ~LockedMixer() { mm_audio_device_mixer_leave(device); }
    LockedMixer(const LockedMixer &) = delete;
    LockedMixer &operator=(const LockedMixer &) = delete;
    MMAudioMixer *Get() const { return core; }
};
} // namespace KauaiAudio
#endif

#include "waveform.h"
#include "sdl_audio.h"
#include "sound_timeline.h"
#include <cstdio>
#include <exception>
#include <new>

namespace
{
struct MediaReader
{
    MM::Preview &preview;
    const MM::PreviewSoundCue &cue;
    MM::PreviewAudio audio{};
    KauaiAudio::Sample sample{};
    std::exception_ptr failure{};

    template <class Effect> int Run(Effect effect)
    {
        try { return effect(); }
        catch (...) { failure = std::current_exception(); return -1; }
    }

    MarcusWaveformServices Services()
    {
        return {
            this,
            [](void *context, MarcusWaveformMedia *media, char *message, std::size_t capacity) {
                auto &reader = *static_cast<MediaReader *>(context);
                return reader.Run([&] {
                    MM::Error error;
                    const bool ok = reader.preview.ReadSound(reader.cue, reader.audio, error);
                    const auto &audio = reader.audio;
                    *media = {audio.source, audio.asset.type, audio.asset.number,
                              audio.events.empty() ? 0 : audio.events.back().milliseconds, audio.midi, audio.silent};
                    if (!ok)
                        std::snprintf(message, capacity, "%s", error.message.c_str());
                    return ok;
                });
            },
            [](void *context, double *seconds) {
                auto &reader = *static_cast<MediaReader *>(context);
                return reader.Run([&] {
                    return KauaiAudio::WaveSeconds(reader.audio.wave.data(), reader.audio.wave.size(), *seconds);
                });
            },
            [](void *context, const float **stereo, std::size_t *frames) {
                auto &reader = *static_cast<MediaReader *>(context);
                return reader.Run([&] {
                    if (!KauaiAudio::DecodeWave(reader.audio.wave.data(), reader.audio.wave.size(), reader.sample))
                        return false;
                    *stereo = mm_audio_sample_data(reader.sample.get());
                    *frames = mm_audio_sample_frames(reader.sample.get());
                    return true;
                });
            }};
    }

    void Check(int result) const
    {
        if (failure)
            std::rethrow_exception(failure);
        if (!result)
            throw std::bad_alloc();
    }
};

MarcusWaveformRequest Request(const MM::PreviewSoundCue &cue)
{
    return {cue.source, cue.sound.type, cue.sound.number, cue.child, cue.scene.number, cue.silent};
}
} // namespace

MarcusWaveformEnvelope::~MarcusWaveformEnvelope()
{
    marcus_waveform_dispose(&wave);
}
void MarcusWaveformEnvelope::Build(const float *stereo, std::size_t frameCount)
{
    if (!marcus_waveform_build(&wave, stereo, frameCount))
        throw std::bad_alloc();
}
void MarcusWaveformEnvelope::Read(double first, double step, int count, MarcusWaveformColumn *columns, bool repeat) const
{
    marcus_waveform_read(&wave, first, step, count, columns, repeat);
}
double MarcusWaveformEnvelope::Seconds() const
{
    return marcus_waveform_seconds(&wave);
}

MarcusWaveformCache::~MarcusWaveformCache()
{
    Clear();
}
void MarcusWaveformCache::Clear()
{
    marcus_waveform_cache_clear(&cache);
}
MarcusWaveformInfo MarcusWaveformCache::Length(MM::Preview &preview, const MM::PreviewSoundCue &cue)
{
    MediaReader reader{preview, cue};
    const auto services = reader.Services();
    const auto request = Request(cue);
    MarcusWaveformInfo info;
    reader.Check(marcus_waveform_cache_length(&cache, &request, &services, &info));
    return info;
}
const MarcusWaveform *MarcusWaveformCache::Peaks(MM::Preview &preview, const MM::PreviewSoundCue &cue,
                                                 MarcusWaveformInfo &info)
{
    MediaReader reader{preview, cue};
    const auto services = reader.Services();
    const auto request = Request(cue);
    const MarcusWaveform *wave;
    reader.Check(marcus_waveform_cache_peaks(&cache, &request, &services, &wave, &info));
    return wave;
}
void MarcusWaveformCache::Read(MM::Preview &preview, const MM::PreviewSoundCue &cue, double first,
                               double step, int count, MarcusWaveformColumn *columns, MarcusWaveformInfo &info)
{
    const auto wave = Peaks(preview, cue, info);
    marcus_waveform_read(wave, first, step, count, columns, cue.loop);
}
void MarcusWaveformCache::ReadFrames(MM::Preview &preview, const MarcusSoundScene &scene, const MarcusSoundSpan &span,
                                    double first, double step, int count, MarcusWaveformColumn *columns,
                                    MarcusWaveformInfo &info)
{
    const auto wave = Peaks(preview, span.cue, info);
    marcus_waveform_read_timeline(wave, span.start, span.end, first, step, count, columns, span.cue.loop,
                                  [](const void *p, double frame) {
                                      return static_cast<const MarcusSoundScene *>(p)->TimeAt(frame);
                                  }, &scene);
}

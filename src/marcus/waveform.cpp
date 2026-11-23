#include "waveform.h"
#include "sdl_wave.h"
#include "sound_timeline.h"
#include "mm/preview_media.h"
#include <cstdio>
#include <exception>
#include <new>

namespace
{
struct MediaReader
{
    MM::Preview &preview;
    const MM::PreviewSoundCue &cue;
    MMPreviewOwnedAudio audio{};
    MMAudioSample *sample = nullptr;
    std::exception_ptr failure{};

    ~MediaReader()
    {
        mm_audio_sample_release(sample);
        mm_preview_owned_audio_dispose(&audio);
    }

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
                    const bool ok = reader.preview.ReadSoundCueOwned(reader.cue.Native(), reader.audio, error);
                    const auto &audio = reader.audio;
                    const auto events = reinterpret_cast<const MMMovieAudioEvent *>(audio.payload.data);
                    *media = {audio.source, audio.type, audio.number,
                              audio.event_count ? events[audio.event_count - 1].milliseconds : 0, audio.midi, audio.silent};
                    if (!ok)
                        std::snprintf(message, capacity, "%s", error.message.c_str());
                    return ok;
                });
            },
            [](void *context, double *seconds) {
                auto &reader = *static_cast<MediaReader *>(context);
                return mm_sdl_wave_seconds(reader.audio.payload.data, reader.audio.wave_bytes, seconds);
            },
            [](void *context, const float **stereo, std::size_t *frames) {
                auto &reader = *static_cast<MediaReader *>(context);
                MMAudioSample *sample;
                if (!mm_sdl_wave_decode(reader.audio.payload.data, reader.audio.wave_bytes, &sample))
                    return 0;
                mm_audio_sample_release(reader.sample);
                reader.sample = sample;
                *stereo = mm_audio_sample_data(sample);
                *frames = mm_audio_sample_frames(sample);
                return 1;
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

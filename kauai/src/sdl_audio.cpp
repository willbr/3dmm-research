/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_audio.h"
#include "wave_codec.h"
#include "compressed_wave.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <new>
namespace KauaiAudio
{
namespace
{
void TrimFact(const void *data, std::size_t bytes, const SDL_AudioSpec &spec, Uint32 &decodedBytes)
{
    std::uint32_t frames;
    const auto frameBytes = SDL_AUDIO_BYTESIZE(spec.format) * spec.channels;
    if (frameBytes > 0 && WaveFactFrames(data, bytes, frames) &&
        frames < decodedBytes / static_cast<unsigned>(frameBytes))
        decodedBytes = frames * static_cast<unsigned>(frameBytes);
}
struct WavePCM
{
    SDL_AudioSpec source = {};
    Uint8 *sdlData = nullptr;
    Uint32 bytes = 0;
    DecodedWave compressed;
    ~WavePCM()
    {
        SDL_free(sdlData);
    }
    const Uint8 *Data() const
    {
        return sdlData ? sdlData : reinterpret_cast<const Uint8 *>(compressed.samples.data());
    }
    bool Load(const void *data, std::size_t count)
    {
        switch (DecodeCompressedWave(data, count, compressed))
        {
        case CompressedWaveResult::Invalid:
            return SDL_SetError("Invalid compressed WAVE audio");
        case CompressedWaveResult::Decoded:
            source = {SDL_AUDIO_S16, compressed.channels, compressed.sampleRate};
            bytes = static_cast<Uint32>(compressed.samples.size() * sizeof(std::int16_t));
            return true;
        case CompressedWaveResult::NotHandled:
            if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(data, count), true, &source, &sdlData, &bytes))
                return false;
            TrimFact(data, count, source, bytes);
            return true;
        }
        return false;
    }
};
} // namespace
bool PrepareWaveImport(const void *data, std::size_t bytes, std::vector<std::uint8_t> &wave)
{
    if (!data || bytes < 12 || bytes > INT_MAX)
        return false;
    WavePCM pcm;
    if (!pcm.Load(data, bytes))
        return false;
    const auto decodedBytes = pcm.bytes;
    if (!decodedBytes || decodedBytes > INT_MAX)
        return false;
    try
    {
        const auto original = static_cast<const std::uint8_t *>(data);
        const bool legacy = LegacyWaveFormat(data, bytes);
        // A complete standard ADPCM block plus its headers is at least 346 bytes.
        if (legacy && bytes <= 346)
        {
            std::vector<std::uint8_t> result(original, original + bytes);
            wave.swap(result);
            return true;
        }
        const SDL_AudioSpec destination = {SDL_AUDIO_U8, 1, 11025};
        Uint8 *converted = nullptr;
        int count = 0;
        if (!SDL_ConvertAudioSamples(&pcm.source, pcm.Data(), static_cast<int>(decodedBytes), &destination, &converted,
                                     &count))
            return false;
        std::unique_ptr<Uint8, decltype(&SDL_free)> samplesOwner(converted, SDL_free);
        if (count <= 0)
            return false;
        std::vector<std::uint8_t> samples(converted, converted + count), compressed;
        if (!LegacyImportWave(samples, compressed))
            return false;
        if (legacy && bytes <= compressed.size())
        {
            std::vector<std::uint8_t> result(original, original + bytes);
            wave.swap(result);
            return true;
        }
        wave.swap(compressed);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool DecodeWave(const void *data, std::size_t bytes, std::shared_ptr<const Sample> &sample)
{
    if (!data || bytes < 12 || bytes > INT_MAX)
        return false;
    SDL_AudioSpec destination = {SDL_AUDIO_F32, 2, SampleRate};
    WavePCM pcm;
    Uint8 *converted = nullptr;
    int convertedBytes = 0;
    if (!pcm.Load(data, bytes))
        return false;
    const bool success =
        pcm.bytes <= INT_MAX && SDL_ConvertAudioSamples(&pcm.source, pcm.Data(), static_cast<int>(pcm.bytes),
                                                        &destination, &converted, &convertedBytes);
    if (!success || convertedBytes < 0 || convertedBytes % (2 * sizeof(float)))
    {
        SDL_free(converted);
        return false;
    }
    try
    {
        auto result = std::make_shared<Sample>();
        const auto begin = reinterpret_cast<const float *>(converted);
        if (convertedBytes)
            result->stereo.assign(begin, begin + convertedBytes / sizeof(float));
        SDL_free(converted);
        converted = nullptr;
        for (auto &value : result->stereo)
            if (!std::isfinite(value))
                value = 0;
        sample = std::move(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        SDL_free(converted);
        return false;
    }
}
struct Output::Impl
{
    Mixer mixer;
    SDL_AudioStream *stream = nullptr;
    bool initialized = false;
    ~Impl()
    {
        SDL_DestroyAudioStream(stream);
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    static void SDLCALL Feed(void *data, SDL_AudioStream *stream, int additional, int)
    {
        auto self = static_cast<Impl *>(data);
        std::array<float, 1024> buffer;
        while (additional > 0)
        {
            const auto frames = std::min<std::size_t>(buffer.size() / 2, (std::size_t(additional) + 7) / 8);
            self->mixer.Render(buffer.data(), frames);
            const auto bytes = static_cast<int>(frames * 2 * sizeof(float));
            if (!SDL_PutAudioStreamData(stream, buffer.data(), bytes))
                break;
            additional -= bytes;
        }
    }
};
Output::Output() : impl(new Impl)
{
}
Output::~Output() = default;
bool Output::Open()
{
    if (impl->stream)
        return true;
    if (!impl->initialized)
    {
        SDL_SetMainReady();
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            return false;
        impl->initialized = true;
    }
    const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, SampleRate};
    impl->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Impl::Feed, impl.get());
    if (!impl->stream)
        return false;
    if (SDL_ResumeAudioStreamDevice(impl->stream))
        return true;
    SDL_DestroyAudioStream(impl->stream);
    impl->stream = nullptr;
    return false;
}
Mixer &Output::Engine()
{
    return impl->mixer;
}
} // namespace KauaiAudio

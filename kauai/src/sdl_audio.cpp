/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_audio.h"
#include "wave_codec.h"
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
} // namespace
bool PrepareWaveImport(const void *data, std::size_t bytes, std::vector<std::uint8_t> &wave)
{
    if (!data || bytes < 12 || bytes > INT_MAX)
        return false;
    SDL_AudioSpec source;
    Uint8 *decoded = nullptr;
    Uint32 decodedBytes = 0;
    if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(data, bytes), true, &source, &decoded, &decodedBytes))
        return false;
    std::unique_ptr<Uint8, decltype(&SDL_free)> owned(decoded, SDL_free);
    TrimFact(data, bytes, source, decodedBytes);
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
        if (!SDL_ConvertAudioSamples(&source, decoded, static_cast<int>(decodedBytes), &destination, &converted,
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
    SDL_AudioSpec source, destination = {SDL_AUDIO_F32, 2, SampleRate};
    Uint8 *decoded = nullptr, *converted = nullptr;
    Uint32 decodedBytes = 0;
    int convertedBytes = 0;
    if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(data, bytes), true, &source, &decoded, &decodedBytes))
        return false;
    TrimFact(data, bytes, source, decodedBytes);
    const bool success =
        decodedBytes <= INT_MAX && SDL_ConvertAudioSamples(&source, decoded, static_cast<int>(decodedBytes),
                                                           &destination, &converted, &convertedBytes);
    SDL_free(decoded);
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

/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "video_sdl.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
namespace KauaiVideo
{
namespace
{
std::atomic<std::uint64_t> opened{0}, started{0}, completed{0}, decoded{0}, queuedAudio{0}, consumedAudio{0};
std::atomic<std::uint64_t> presented{0}, audible{0};
void SDLCALL Postmix(void *, const SDL_AudioSpec *spec, float *buffer, int bytes)
{
    std::uint64_t frames = 0;
    const int count = bytes / static_cast<int>(sizeof(float));
    for (int i = 0; i + spec->channels <= count; i += spec->channels)
        for (int c = 0; c < spec->channels; ++c)
            if (buffer[i + c] > 0.00001f || buffer[i + c] < -0.00001f)
            {
                ++frames;
                break;
            }
    audible += frames;
}
} // namespace
SDLVideoStatistics SDLVideoStats()
{
    return {opened.load(),    started.load(),     completed.load(),     decoded.load(),
            presented.load(), queuedAudio.load(), consumedAudio.load(), audible.load()};
}
void SDLVideoFramePresented()
{
    ++presented;
}
struct SDLPlayback::Impl
{
    AviMovie movie;
    SDL_AudioStream *stream = nullptr;
    bool initialized = false, audio = false, playing = false;
    std::int32_t position = 0, firstFrame = 0, firstTime = 0, duration = 0;
    std::uint64_t ticks = 0, drained = 0, latency = 0;
    std::uint32_t audioRate = 0, align = 0;
    int queued = 0, lastQueued = 0;
    float gain = 1.0f;
    ~Impl()
    {
        Stop();
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    void Update()
    {
        if (!playing)
            return;
        const auto now = SDL_GetTicksNS();
        std::uint64_t elapsed = (now - ticks) / 1000000;
        if (stream)
        {
            const int remaining = SDL_GetAudioStreamQueued(stream);
            if (remaining < 0 || remaining > queued)
            {
                StopDevice();
                playing = false;
                return;
            }
            if (remaining < lastQueued)
                consumedAudio += static_cast<unsigned>(lastQueued - remaining);
            lastQueued = remaining;
            // SDL queues input bytes; this fixed-format stream never changes
            // format. Conversion/consumption therefore supplies a shared clock.
            const auto consumedNS = std::uint64_t(queued - remaining) * 1000000000 / (audioRate * align);
            auto audibleNS = consumedNS > latency ? consumedNS - latency : 0;
            if (!remaining)
            {
                if (!drained)
                    drained = now;
                const auto tail = (std::min)(now - drained, latency);
                audibleNS += tail;
                if (now - drained >= latency)
                    audibleNS = std::uint64_t(duration) * 1000000;
            }
            elapsed = audibleNS / 1000000;
        }
        position = (std::max)(
            firstFrame,
            movie.FrameForTime(firstTime + static_cast<std::int32_t>((std::min)(elapsed, std::uint64_t(duration)))));
        if (elapsed >= static_cast<std::uint64_t>(duration))
        {
            position = movie.Info().frames - 1;
            playing = false;
            StopDevice();
            ++completed;
        }
    }
    void StopDevice()
    {
        if (stream)
            SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
    void Stop()
    {
        Update();
        playing = false;
        StopDevice();
    }
    bool Play(std::int32_t frame)
    {
        Stop();
        const auto info = movie.Info();
        if (frame < 0 || frame >= info.frames)
            return false;
        firstFrame = frame;
        firstTime = movie.TimeForFrame(frame);
        duration = movie.TimeForFrame(info.frames) - firstTime;
        if (duration <= 0)
            return false;
        position = frame;
        drained = 0;
        const auto &source = movie.Audio();
        if (audio && source.rate)
        {
            if (!initialized)
            {
                if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                    return false;
                initialized = true;
            }
            audioRate = source.rate;
            align = source.channels * (source.bits / 8);
            const auto frames = (std::uint64_t(duration) * audioRate + 999) / 1000;
            if (frames * align > INT32_MAX)
                return false;
            const SDL_AudioSpec spec{source.bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE, source.channels,
                                     static_cast<int>(audioRate)};
            stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (!stream)
                return false;
            try
            {
                std::vector<std::uint8_t> pcm(static_cast<std::size_t>(frames * align), source.bits == 8 ? 128 : 0);
                const auto first = std::int64_t(firstTime) * audioRate / 1000 - source.start;
                const auto src = (std::max)(std::int64_t(0), first), dst = (std::max)(std::int64_t(0), -first);
                const auto count =
                    (std::min)(std::int64_t(source.pcm.size() / align) - src, std::int64_t(frames) - dst);
                if (count > 0)
                    std::memcpy(pcm.data() + dst * align, source.pcm.data() + src * align,
                                static_cast<std::size_t>(count * align));
                queued = lastQueued = static_cast<int>(pcm.size());
                if (!SDL_PutAudioStreamData(stream, pcm.data(), queued) || !SDL_FlushAudioStream(stream))
                {
                    StopDevice();
                    return false;
                }
            }
            catch (const std::bad_alloc &)
            {
                StopDevice();
                return false;
            }
            SDL_AudioSpec deviceSpec{};
            int deviceFrames = 0;
            if (!SDL_SetAudioPostmixCallback(SDL_GetAudioStreamDevice(stream), Postmix, nullptr) ||
                !SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &deviceSpec, &deviceFrames) ||
                deviceSpec.freq <= 0 || deviceFrames <= 0)
            {
                StopDevice();
                return false;
            }
            latency = std::uint64_t(deviceFrames) * 1000000000 / deviceSpec.freq;
            ticks = SDL_GetTicksNS();
            if (!SDL_SetAudioStreamGain(stream, gain) || !SDL_ResumeAudioStreamDevice(stream))
            {
                StopDevice();
                return false;
            }
            queuedAudio += static_cast<unsigned>(queued);
        }
        else
            ticks = SDL_GetTicksNS();
        playing = true;
        ++started;
        return true;
    }
};
SDLPlayback::SDLPlayback() : impl(new Impl)
{
}
SDLPlayback::~SDLPlayback() = default;
bool SDLPlayback::Open(const void *data, std::size_t bytes, bool withAudio)
{
    Close();
    if (!impl->movie.Read(data, bytes))
        return false;
    KauaiPixels::DibFrame first;
    if (!impl->movie.DecodeFrame(0, first))
    {
        impl->movie.Reset();
        return false;
    }
    impl->audio = withAudio;
    ++opened;
    return true;
}
void SDLPlayback::Close()
{
    impl->Stop();
    impl->movie.Reset();
    impl->position = 0;
}
AviInfo SDLPlayback::Info() const
{
    return impl->movie.Info();
}
std::int32_t SDLPlayback::TimeForFrame(std::int32_t frame) const
{
    return impl->movie.TimeForFrame(frame);
}
std::int32_t SDLPlayback::FrameForTime(std::int32_t time) const
{
    return impl->movie.FrameForTime(time);
}
bool SDLPlayback::DecodeFrame(std::int32_t frame, KauaiPixels::DibFrame &out)
{
    if (!impl->movie.DecodeFrame(frame, out))
        return false;
    ++decoded;
    return true;
}
bool SDLPlayback::Play(std::int32_t frame)
{
    return impl->Play(frame);
}
bool SDLPlayback::SetGain(float gain)
{
    if (!std::isfinite(gain) || gain < 0 || gain > 2)
        return false;
    if (gain == impl->gain)
        return true;
    if (impl->stream && !SDL_SetAudioStreamGain(impl->stream, gain))
        return false;
    impl->gain = gain;
    return true;
}
void SDLPlayback::Stop()
{
    impl->Stop();
}
bool SDLPlayback::Playing()
{
    impl->Update();
    return impl->playing;
}
std::int32_t SDLPlayback::Position()
{
    impl->Update();
    return impl->position;
}
} // namespace KauaiVideo

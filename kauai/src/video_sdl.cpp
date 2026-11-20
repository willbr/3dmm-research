/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "video_sdl.h"
#include <SDL3/SDL.h>
#include "mm/video_playback.h"
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
    bool initialized = false, audio = false;
    MMVideoDeviceClock clock{};
    float gain = 1.0f;
    ~Impl()
    {
        Stop();
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    static MMAudioCounter Now()
    {
        const auto ticks = SDL_GetTicksNS();
        return mm_audio_counter(static_cast<MMU32>(ticks), static_cast<MMU32>(ticks >> 32));
    }
    void Update()
    {
        if (!clock.playing)
            return;
        const auto now = Now();
        const int remaining = stream ? SDL_GetAudioStreamQueued(stream) : 0;
        MMU32 consumed;
        const int result = mm_video_device_update(&clock, now, remaining, &movie, [](void *context, MMI32 time) {
            return static_cast<AviMovie *>(context)->FrameForTime(time);
        }, &consumed);
        if (consumed)
            consumedAudio += consumed;
        if (result == MM_VIDEO_QUEUE_ERROR || result == MM_VIDEO_FINISHED)
            StopDevice();
        if (result == MM_VIDEO_FINISHED)
            ++completed;
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
        mm_video_device_stop(&clock);
        StopDevice();
    }
    bool Play(std::int32_t frame)
    {
        Stop();
        const auto info = movie.Info();
        if (!mm_video_device_prepare(&clock, frame, info.frames, movie.TimeForFrame(frame), movie.TimeForFrame(info.frames)))
            return false;
        const auto source = movie.Audio();
        if (audio && source.rate)
        {
            if (!initialized)
            {
                if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                    return false;
                initialized = true;
            }
            MMVideoPcmWindow window;
            const MMU32 alignment = source.channels * (source.bits / 8);
            if (!mm_video_pcm_window(clock.first_time, clock.duration, source.rate, alignment, source.start,
                                     static_cast<MMU32>(source.pcm_bytes), &window))
                return false;
            const SDL_AudioSpec spec{source.bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE, static_cast<int>(source.channels),
                                     static_cast<int>(source.rate)};
            stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (!stream)
                return false;
            try
            {
                std::vector<std::uint8_t> pcm(window.bytes, source.bits == 8 ? 128 : 0);
                if (window.copy_bytes)
                    std::memcpy(pcm.data() + window.destination_offset, source.pcm + window.source_offset,
                                window.copy_bytes);
                if (!SDL_PutAudioStreamData(stream, pcm.data(), static_cast<int>(window.bytes)) ||
                    !SDL_FlushAudioStream(stream))
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
            const auto now = Now();
            if (!SDL_SetAudioStreamGain(stream, gain) || !SDL_ResumeAudioStreamDevice(stream))
            {
                StopDevice();
                return false;
            }
            mm_video_device_begin(&clock, now, window.bytes, window.byte_rate, deviceFrames, deviceSpec.freq);
            queuedAudio += window.bytes;
        }
        else
            mm_video_device_begin(&clock, Now(), 0, 0, 0, 0);
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
    impl->clock.position = 0;
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
    return impl->clock.playing;
}
std::int32_t SDLPlayback::Position()
{
    impl->Update();
    return impl->clock.position;
}
} // namespace KauaiVideo

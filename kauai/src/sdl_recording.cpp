/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_recording.h"
#include "sdl_audio.h"
#include "recording_data.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <array>
#include <atomic>
#include <new>
namespace KauaiAudio
{
namespace
{
std::atomic<std::uint64_t> opened{0}, started{0}, completed{0}, previewed{0}, capturedFrames{0};
}
RecordingStats RecorderStats()
{
    return {opened.load(), started.load(), completed.load(), previewed.load(), capturedFrames.load()};
}
struct Recorder::Impl
{
    RecordingBuffer buffer;
    SDL_AudioStream *capture = nullptr;
    Output output;
    std::uint64_t queue = 0;
    std::vector<std::uint8_t> wave;
    bool initialized = false, recording = false, playing = false, failed = false;
    ~Impl()
    {
        SDL_DestroyAudioStream(capture);
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    static void SDLCALL Feed(void *data, SDL_AudioStream *stream, int, int)
    {
        auto self = static_cast<Impl *>(data);
        std::array<std::uint8_t, 4096> bytes;
        int count;
        while ((count = SDL_GetAudioStreamData(stream, bytes.data(), static_cast<int>(bytes.size()))) > 0)
            if (self->recording)
                self->buffer.Append(bytes.data(), static_cast<unsigned>(count));
        if (count < 0)
            self->failed = true;
    }
};
Recorder::Recorder() : impl(new Impl)
{
}
Recorder::~Recorder() = default;
bool Recorder::Open(unsigned rate, unsigned channels, unsigned bits, std::uint32_t maxMs)
{
    try
    {
        auto next = std::make_unique<Impl>();
        if (!next->buffer.Init(rate, channels, bits, maxMs))
            return false;
        SDL_SetMainReady();
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            return false;
        next->initialized = true;
        const SDL_AudioSpec spec = {bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE, static_cast<int>(channels),
                                    static_cast<int>(rate)};
        next->capture = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, Impl::Feed, next.get());
        if (!next->capture)
            return false;
        impl.swap(next);
        ++opened;
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool Recorder::Start()
{
    if (!impl->capture || impl->recording)
        return false;
    if (impl->playing && !Stop())
        return false;
    SDL_LockAudioStream(impl->capture);
    SDL_ClearAudioStream(impl->capture);
    impl->buffer.Reset();
    impl->wave.clear();
    impl->failed = false;
    impl->recording = true;
    SDL_UnlockAudioStream(impl->capture);
    if (!SDL_ResumeAudioStreamDevice(impl->capture))
    {
        SDL_LockAudioStream(impl->capture);
        impl->recording = false;
        SDL_UnlockAudioStream(impl->capture);
        return false;
    }
    ++started;
    return true;
}
bool Recorder::Stop()
{
    if (impl->playing)
    {
        impl->output.Engine().Sync(impl->queue, {});
        impl->playing = false;
    }
    if (!impl->recording)
        return true;
    if (!SDL_PauseAudioStreamDevice(impl->capture))
        return false;
    SDL_LockAudioStream(impl->capture);
    SDL_FlushAudioStream(impl->capture);
    Impl::Feed(impl.get(), impl->capture, 0, 0);
    impl->recording = false;
    const bool failed = impl->failed;
    capturedFrames += impl->buffer.Frames();
    SDL_UnlockAudioStream(impl->capture);
    if (failed)
        return false;
    if (impl->buffer.Frames() > 8)
    {
        if (!impl->buffer.Wave(impl->wave))
            return false;
        ++completed;
    }
    return true;
}
bool Recorder::Recording()
{
    if (!impl->recording)
        return false;
    SDL_LockAudioStream(impl->capture);
    const bool done = impl->failed || impl->buffer.Full();
    SDL_UnlockAudioStream(impl->capture);
    if (done)
        Stop();
    return impl->recording;
}
bool Recorder::Play(float volume)
{
    try
    {
        if (impl->recording || !HaveSound() || !Stop() || !impl->output.Open())
            return false;
        std::shared_ptr<const Sample> sample;
        if (!DecodeWave(impl->wave.data(), impl->wave.size(), sample))
            return false;
        if (!impl->queue)
            impl->queue = impl->output.Engine().AddQueue();
        Voice voice;
        voice.id = 1;
        voice.sample = sample;
        impl->output.Engine().SetVolume(volume);
        if (!impl->queue || !impl->output.Engine().Sync(impl->queue, {voice}))
            return false;
        impl->playing = true;
        ++previewed;
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
void Recorder::SuspendPreview(bool suspended)
{
    impl->output.Engine().Suspend(suspended);
}
bool Recorder::Playing()
{
    if (impl->playing && impl->output.Engine().Done(impl->queue, 1))
        Stop();
    return impl->playing;
}
bool Recorder::HaveSound() const
{
    return !impl->wave.empty();
}
const std::vector<std::uint8_t> &Recorder::Wave() const
{
    return impl->wave;
}
} // namespace KauaiAudio

/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include "midi_worker.h"
#include "midi_sdl.h"
#include "sdl_audio_output.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error SDL MIDI must not import the Windows SDK
#endif
ASSERTNAME
namespace KauaiMidi
{
namespace
{
class RendererLock
{
    MMMidiDeviceRenderer *device;
    MMMidiRenderer *core;

  public:
    explicit RendererLock(MMMidiDeviceRenderer *renderer)
        : device(renderer), core(mm_midi_device_renderer_enter(renderer)) {}
    ~RendererLock() { mm_midi_device_renderer_leave(device); }
    RendererLock(const RendererLock &) = delete;
    RendererLock &operator=(const RendererLock &) = delete;
    MMMidiRenderer *Get() const { return core; }
};
using AudioOutput = std::unique_ptr<MMSdlAudioOutput, decltype(&mm_sdl_audio_output_destroy)>;
struct Playback
{
    std::unique_ptr<MMMidiDeviceRenderer, decltype(&mm_midi_device_renderer_destroy)> renderer{
        mm_midi_device_renderer_create(), mm_midi_device_renderer_destroy};
    std::atomic<bool> active{false};
    Completion complete;
    std::uintptr_t user;
    Worker callbacks;
    static std::uint32_t ReturnBuffers(void *data)
    {
        auto &self = *static_cast<Playback *>(data);
        void *buffer;
        std::size_t tag;
        while (mm_midi_render_pop(RendererLock(self.renderer.get()).Get(), &buffer, &tag))
            self.complete(self.user, buffer, tag);
        // A seek-state prefix may complete before its main buffer is queued.
        // Deliver callbacks first: the mixer lock orders those submissions.
        mm_midi_render_idle(RendererLock(self.renderer.get()).Get());
        return UINT32_MAX;
    }
    static void Render(void *data, float *samples, std::size_t frames)
    {
        auto &self = *static_cast<Playback *>(data);
        if (mm_midi_render(RendererLock(self.renderer.get()).Get(), samples, frames))
            self.callbacks.Signal();
    }
};
std::mutex currentMutex;
std::weak_ptr<Playback> current;
std::atomic<bool> attempted{false};
class SDLStreamHost : public StreamHost
{
    std::shared_ptr<Playback> playback;
    AudioOutput output;
    LegacyLong volume = kvlmFull;

  public:
    SDLStreamHost(const std::shared_ptr<Playback> &state, AudioOutput device)
        : playback(state), output(std::move(device))
    {
    }
    ~SDLStreamHost()
    {
        mm_sdl_audio_output_close(output.get());
        playback->active.store(false);
        playback->callbacks.Stop();
    }
    const char *ProviderName() const override
    {
        return "sdl-soundfont";
    }
    void SetVlm(LegacyLong value) override
    {
        volume = std::max<LegacyLong>(0, value);
        mm_midi_render_volume(RendererLock(playback->renderer.get()).Get(), volume / float(kvlmFull));
    }
    LegacyLong VlmCur() override
    {
        return volume;
    }
    bool FActive() override
    {
        return playback->active.load();
    }
    bool FActivate(bool active) override
    {
        if (active == FActive())
            return true;
        if (!active)
        {
            if (RendererLock(playback->renderer.get()).Get()->head)
                return false;
            mm_sdl_audio_output_close(output.get());
            playback->active.store(false);
            return true;
        }
        SDL_SetMainReady();
        if (!mm_midi_render_reset(RendererLock(playback->renderer.get()).Get()))
            return false;
        if (!mm_sdl_audio_output_open(output.get(), nullptr, nullptr))
            return false;
        playback->active.store(true);
        return true;
    }
    bool FQueueBuffer(void *data, LegacyLong bytes, LegacyLong offset, LegacyLong repeats, std::uintptr_t tag) override
    {
        return FActive() && mm_midi_device_renderer_queue(playback->renderer.get(), data, bytes, offset, repeats, tag);
    }
    void StopPlaying() override
    {
        mm_midi_render_stop(RendererLock(playback->renderer.get()).Get());
        // Release the renderer mutex before touching SDL's stream lock. Client
        // callbacks run on their own worker, never on SDL's audio thread.
        mm_sdl_audio_output_clear(output.get());
        playback->callbacks.Signal();
    }
};
} // namespace
StreamHost *CreateSDLStreamHost(Completion completion, std::uintptr_t user)
{
    if (!completion)
        return nullptr;
    attempted.store(true);
    try
    {
        const auto supplied = SDL_getenv("KAUAI_SOUNDFONT");
        const auto configured = supplied && *supplied ? supplied : nullptr;
        const auto base = SDL_GetBasePath();
        if (!configured && !base)
            return nullptr;
        const std::string path = configured ? configured : std::string(base) + "assets/GeneralUserGS.sf2";
        std::size_t bytes = 0;
        std::unique_ptr<void, decltype(&SDL_free)> bank(SDL_LoadFile(path.c_str(), &bytes), SDL_free);
        auto state = std::make_shared<Playback>();
        if (!state->renderer)
            return nullptr;
        state->complete = completion;
        state->user = user;
        if (!bank || !mm_midi_device_renderer_load(state->renderer.get(), bank.get(), bytes, MM_AUDIO_MIX_RATE))
        {
            SDL_SetError("Cannot load a complete General MIDI bank from %s", path.c_str());
            std::fprintf(stderr, "SDL MIDI: %s\n", SDL_GetError());
            return nullptr;
        }
        AudioOutput output(mm_sdl_audio_output_create(state.get(), Playback::Render, 512), mm_sdl_audio_output_destroy);
        if (!output)
            return nullptr;
        if (!state->callbacks.Start(Playback::ReturnBuffers, state.get()))
            return nullptr;
        auto host = NewObj SDLStreamHost(state, std::move(output));
        if (!host)
        {
            state->callbacks.Stop();
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(currentMutex);
        current = state;
        return host;
    }
    catch (const std::bad_alloc &)
    {
        return nullptr;
    }
}
SDLStats StudioMidiStats()
{
    std::shared_ptr<Playback> state;
    {
        std::lock_guard<std::mutex> lock(currentMutex);
        state = current.lock();
    }
    SDLStats stats;
    stats.attempted = attempted.load();
    if (state)
    {
        stats.loaded = true;
        stats.active = state->active.load();
        const RendererLock lock(state->renderer.get());
        const auto r = lock.Get();
        const auto number = [](MMAudioCounter n) { return (std::uint64_t(n.high) << 32) | n.low; };
        stats.rendering = {number(r->queued), number(r->completed), number(r->messages),
                           number(r->notes), number(r->frames), number(r->audible_frames)};
    }
    return stats;
}
} // namespace KauaiMidi

/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include "midi_worker.h"
#include "midi_sdl.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error SDL MIDI must not import the Windows SDK
#endif
ASSERTNAME
namespace KauaiMidi
{
namespace
{
constexpr int sampleRate = 48000;
struct Playback
{
    Renderer renderer;
    std::atomic<bool> active{false};
    Completion complete;
    std::uintptr_t user;
    Worker callbacks;
    static std::uint32_t ReturnBuffers(void *data)
    {
        auto &self = *static_cast<Playback *>(data);
        ReturnedBuffer buffer;
        while (self.renderer.PopReturned(buffer))
            self.complete(self.user, buffer.data, buffer.tag);
        // A seek-state prefix may complete before its main buffer is queued.
        // Deliver callbacks first: the mixer lock orders those submissions.
        self.renderer.SilenceIfIdle();
        return UINT32_MAX;
    }
    static void SDLCALL Feed(void *data, SDL_AudioStream *stream, int additional, int)
    {
        auto &self = *static_cast<Playback *>(data);
        std::array<float, 1024> samples;
        while (additional > 0)
        {
            const auto frames = std::min<std::size_t>(512, (static_cast<std::size_t>(additional) + 7) / 8);
            if (self.renderer.Render(samples.data(), frames))
                self.callbacks.Signal();
            const auto bytes = static_cast<int>(frames * 8);
            if (!SDL_PutAudioStreamData(stream, samples.data(), bytes))
                break;
            additional -= bytes;
        }
    }
};
std::mutex currentMutex;
std::weak_ptr<Playback> current;
std::atomic<bool> attempted{false};
class SDLStreamHost : public StreamHost
{
    std::shared_ptr<Playback> playback;
    SDL_AudioStream *stream = nullptr;
    bool initialized = false;
    LegacyLong volume = kvlmFull;

  public:
    explicit SDLStreamHost(const std::shared_ptr<Playback> &state) : playback(state)
    {
    }
    ~SDLStreamHost()
    {
        SDL_DestroyAudioStream(stream);
        playback->active.store(false);
        playback->callbacks.Stop();
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    const char *ProviderName() const override
    {
        return "sdl-soundfont";
    }
    void SetVlm(LegacyLong value) override
    {
        volume = std::max<LegacyLong>(0, value);
        playback->renderer.SetVolume(volume / float(kvlmFull));
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
            if (playback->renderer.Pending())
                return false;
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
            playback->active.store(false);
            return true;
        }
        SDL_SetMainReady();
        if (!playback->renderer.Reset())
            return false;
        if (!initialized)
        {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                return false;
            initialized = true;
        }
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, sampleRate};
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Playback::Feed, playback.get());
        if (!stream)
            return false;
        if (!SDL_ResumeAudioStreamDevice(stream))
        {
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
            return false;
        }
        playback->active.store(true);
        return true;
    }
    bool FQueueBuffer(void *data, LegacyLong bytes, LegacyLong offset, LegacyLong repeats, std::uintptr_t tag) override
    {
        return FActive() && playback->renderer.Queue(data, bytes, offset, repeats, tag);
    }
    void StopPlaying() override
    {
        playback->renderer.Stop();
        // Release the renderer mutex before touching SDL's stream lock. Client
        // callbacks run on their own worker, never on SDL's audio thread.
        if (stream)
            SDL_ClearAudioStream(stream);
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
        state->complete = completion;
        state->user = user;
        if (!bank || !state->renderer.Load(bank.get(), bytes, sampleRate))
        {
            SDL_SetError("Cannot load a complete General MIDI bank from %s", path.c_str());
            std::fprintf(stderr, "SDL MIDI: %s\n", SDL_GetError());
            return nullptr;
        }
        if (!state->callbacks.Start(Playback::ReturnBuffers, state.get()))
            return nullptr;
        auto host = NewObj SDLStreamHost(state);
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
        stats.rendering = state->renderer.Stats();
    }
    return stats;
}
} // namespace KauaiMidi

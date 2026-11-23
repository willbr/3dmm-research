/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_sdl.h"
#include "midi_sdl_device.h"
#include <mutex>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error SDL MIDI must not import the Windows SDK
#endif
ASSERTNAME
namespace KauaiMidi
{
namespace
{
std::mutex statsMutex;
MMSdlMidiDevice *current;
bool attempted;
static_assert(MM_SDL_MIDI_FULL_VOLUME == kvlmFull, "MIDI volume units differ");

class SDLStreamHost : public StreamHost
{
    MMSdlMidiDevice *device = nullptr;
    Completion complete;
    std::uintptr_t user;

    static void Return(void *context, void *buffer, size_t tag) noexcept
    {
        auto host = static_cast<SDLStreamHost *>(context);
        host->complete(host->user, buffer, tag);
    }

  public:
    SDLStreamHost(Completion completion, std::uintptr_t user) : complete(completion), user(user) {}
    bool Init()
    {
        device = mm_sdl_midi_create(Return, this, nullptr);
        if (!device)
            return false;
        std::lock_guard<std::mutex> lock(statsMutex);
        current = device;
        return true;
    }
    ~SDLStreamHost()
    {
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            if (current == device)
                current = nullptr;
        }
        mm_sdl_midi_destroy(device);
    }
    const char *ProviderName() const override { return "sdl-soundfont"; }
    void SetVlm(LegacyLong value) override { mm_sdl_midi_set_volume(device, value); }
    LegacyLong VlmCur() override { return mm_sdl_midi_volume(device); }
    bool FActive() override { return mm_sdl_midi_active(device) != 0; }
    bool FActivate(bool active) override { return mm_sdl_midi_activate(device, active) != 0; }
    bool FQueueBuffer(void *data, LegacyLong bytes, LegacyLong offset, LegacyLong repeats, std::uintptr_t tag) override
    {
        return mm_sdl_midi_queue(device, data, bytes, offset, repeats, tag) != 0;
    }
    void StopPlaying() override { mm_sdl_midi_stop(device); }
};
} // namespace

StreamHost *CreateSDLStreamHost(Completion completion, std::uintptr_t user)
{
    if (!completion)
        return nullptr;
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        attempted = true;
    }
    auto host = NewObj SDLStreamHost(completion, user);
    if (host && !host->Init())
        ReleasePpo(&host);
    return host;
}

SDLStats StudioMidiStats()
{
    std::lock_guard<std::mutex> lock(statsMutex);
    SDLStats stats;
    stats.attempted = attempted;
    if (current)
    {
        MMSdlMidiStats native;
        mm_sdl_midi_stats(current, &native);
        stats.loaded = true;
        stats.active = native.active != 0;
        const auto number = [](MMAudioCounter n) { return (std::uint64_t(n.high) << 32) | n.low; };
        stats.rendering = {number(native.queued), number(native.completed), number(native.messages),
                           number(native.notes), number(native.frames), number(native.audible_frames)};
    }
    return stats;
}
} // namespace KauaiMidi

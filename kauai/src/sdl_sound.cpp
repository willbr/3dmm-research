/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_sound.h"
#include "sdl_wave.h"
#include "sdl_wave_device.h"
#include "mm/sound_payload.h"
#include <memory>
#include "kauai_core.h"
#include "handle_services.h"
#include "sndm.h"
#undef size
#ifdef _WINDOWS_
#error The SDL sound adapter must not import the Windows SDK
#endif
ASSERTNAME

#define SDLWave_PAR BaseCacheableObject
#define kclsSDLWave 'swav'
class SDLWave : public SDLWave_PAR
{
    RTCLASS_INLINE(SDLWave)
  public:
    MMAudioSample *sample = nullptr;

    ~SDLWave() { mm_audio_sample_release(sample); }

    static int Read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **result, MMI32 *bytes)
    {
        // Preserve the cache charge of the former shared PCM handle.
        constexpr auto cacheBytes = sizeof(SDLWave) - sizeof(MMAudioSample *) + sizeof(std::shared_ptr<void>);
        MMSoundPayloadServices s = {};
        s.object_bytes = cacheBytes;
        s.create = [](void *) -> void * { return NewObj SDLWave; };
        s.release = [](void *, void *object) { static_cast<SDLWave *>(object)->Release(); };
        s.decode = [](void *, void *object, const void *encoded, MMU32 bytes) -> int {
            return mm_sdl_wave_decode(encoded, bytes, &static_cast<SDLWave *>(object)->sample);
        };
        s.allocation_size = [](void *, void *object, MMI32 *bytes) -> int {
            const auto frames = mm_audio_sample_frames(static_cast<SDLWave *>(object)->sample);
            if (frames > (INT32_MAX - cacheBytes) / (2 * sizeof(float)))
                return 0;
            *bytes = static_cast<MMI32>(frames * 2 * sizeof(float) + cacheBytes);
            return 1;
        };
        s.decoded = [](void *) { mm_sdl_wave_note_decode(); };
        return mm_wave_payload_fetch(block, result, bytes, &s);
    }
};

#define SDLWaveQueue_PAR SoundQueue
#define kclsSDLWaveQueue 'swqu'
class SDLWaveQueue : public SDLWaveQueue_PAR
{
    RTCLASS_INLINE(SDLWaveQueue)
    MMSdlWaveQueue queue = {};

  protected:
    void _Enter() override { mm_sdl_wave_queue_enter(&queue); }
    void _Leave() override { mm_sdl_wave_queue_leave(&queue); }
    void _Flush() override
    {
        const auto services = _Services();
        mm_sdl_wave_queue_flush(&queue, &services);
    }
    void _Queue(LegacyLong) override { mm_sdl_wave_queue_sync(&queue); }
    void _PauseQueue(LegacyLong) override { mm_sdl_wave_queue_sync(&queue); }
    void _ResumeQueue(LegacyLong) override { mm_sdl_wave_queue_sync(&queue); }
    ResourceReader _Reader() override { return SDLWave::Read; }

  public:
    ~SDLWaveQueue() { mm_sdl_wave_queue_dispose(&queue); }
    static SDLWaveQueue *Create(MMSdlWaveDevice *device)
    {
        auto host = NewObj SDLWaveQueue;
        const auto sample = [](void *object) -> const MMAudioSample * {
            return static_cast<SDLWave *>(static_cast<PBaseCacheableObject>(object))->sample;
        };
        if (host && (!mm_sdl_wave_queue_init(&host->queue, device, &host->_entries, sample) || !host->_FInit()))
            ReleasePpo(&host);
        return host;
    }
};

#define SDLWaveDevice_PAR SoundManagerQueue
#define kclsSDLWaveDevice 'swde'
class SDLWaveDevice : public SDLWaveDevice_PAR
{
    RTCLASS_INLINE(SDLWaveDevice)
    MMSdlWaveDevice *device = nullptr;

  protected:
    PSoundQueue _PsnqueNew() override { return SDLWaveQueue::Create(device); }
    void _Suspend(bool suspended) override { mm_sdl_wave_device_suspend(device, suspended); }

  public:
    ~SDLWaveDevice() { mm_sdl_wave_device_release(device); }
    void SetVlm(LegacyLong value) override { mm_sdl_wave_device_set_volume(device, value); }
    LegacyLong VlmCur() override { return mm_sdl_wave_device_volume(device); }
    void BeginSynch() override { mm_sdl_wave_device_begin(device); }
    void EndSynch() override { mm_sdl_wave_device_end(device); }

    static SDLWaveDevice *Create()
    {
        auto host = NewObj SDLWaveDevice;
        if (!host)
            return nullptr;
        host->device = mm_sdl_wave_device_create(kvlmFull);
        if (!host->device || !host->_FInit() || !mm_sdl_wave_device_open(host->device))
            ReleasePpo(&host);
        return host;
    }
};

namespace KauaiAudio
{
SoundDevice *NewWaveDevice()
{
    return SDLWaveDevice::Create();
}

StudioSoundStats StudioStats()
{
    MMSdlWaveStats stats;
    mm_sdl_wave_stats(&stats);
    const auto number = [](MMAudioCounter count) { return (std::uint64_t(count.high) << 32) | count.low; };
    return {stats.active != 0, number(stats.decoded), number(stats.started), number(stats.completed)};
}
} // namespace KauaiAudio

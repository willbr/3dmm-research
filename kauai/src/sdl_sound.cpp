/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_audio.h"
#include "sdl_sound.h"
#include "mm/sound_payload.h"
#include <algorithm>
#include <new>
#include "kauai_core.h"
#include "handle_services.h"
#include "sndm.h"
#undef size
#ifdef _WINDOWS_
#error The SDL sound adapter must not import the Windows SDK
#endif
ASSERTNAME
namespace
{
std::weak_ptr<MMSdlAudioOutput> studioOutput;
std::uint64_t decodedSounds = 0;
} // namespace
#define SDLWave_PAR BaseCacheableObject
#define kclsSDLWave 'swav'
class SDLWave : public SDLWave_PAR
{
    RTCLASS_INLINE(SDLWave)
  public:
    KauaiAudio::Sample sample;
    static int Read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **result, MMI32 *bytes)
    {
        // Preserve the cache charge of the former shared PCM handle.
        constexpr auto cacheBytes = sizeof(SDLWave) - sizeof(KauaiAudio::Sample) + sizeof(std::shared_ptr<void>);
        MMSoundPayloadServices s = {};
        s.object_bytes = cacheBytes;
        s.decode_block = KauaiDecodeBlockHandle;
        s.create = [](void *) -> void * { return NewObj SDLWave; };
        s.release = [](void *, void *object) { static_cast<SDLWave *>(object)->Release(); };
        s.decode = [](void *, void *object, const void *encoded, MMU32 bytes) -> int {
            try
            {
                return KauaiAudio::DecodeWave(encoded, bytes, static_cast<SDLWave *>(object)->sample);
            }
            catch (const std::bad_alloc &)
            {
                return false;
            }
        };
        s.allocation_size = [](void *, void *object, MMI32 *bytes) -> int {
            const auto frames = mm_audio_sample_frames(static_cast<SDLWave *>(object)->sample.get());
            if (frames > (INT32_MAX - cacheBytes) / (2 * sizeof(float)))
                return 0;
            *bytes = static_cast<MMI32>(frames * 2 * sizeof(float) + cacheBytes);
            return 1;
        };
        s.decoded = [](void *) { ++decodedSounds; };
        return mm_wave_payload_fetch(block, result, bytes, &s);
    }
};
#define SDLWaveQueue_PAR SoundQueue
#define kclsSDLWaveQueue 'swqu'
class SDLWaveQueue : public SDLWaveQueue_PAR
{
    RTCLASS_INLINE(SDLWaveQueue)
    std::shared_ptr<MMSdlAudioOutput> output;
    MMAudioCounter id = {};
    explicit SDLWaveQueue(const std::shared_ptr<MMSdlAudioOutput> &device) : output(device)
    {
    }
    void Sync()
    {
        mm_audio_device_mixer_sync_queue(mm_sdl_audio_output_mixer(output.get()), id, &_entries, kvlmFull,
            [](void *object) -> const MMAudioSample * {
                return static_cast<SDLWave *>(static_cast<PBaseCacheableObject>(object))->sample.get();
            });
    }

  protected:
    void _Enter() override
    {
        auto mixer = mm_audio_device_mixer_enter(mm_sdl_audio_output_mixer(output.get()));
        while (_entries.current < mm_sound_queue_count(&_entries))
        {
            const auto item = mm_sound_queue_get(&_entries, _entries.current);
            if (!mm_audio_mixer_done(mixer, id, item.id))
                break;
            ++_entries.current;
        }
    }
    void _Leave() override
    {
        mm_audio_device_mixer_leave(mm_sdl_audio_output_mixer(output.get()));
    }
    void _Flush() override
    {
        KauaiAudio::LockedMixer lock(output.get());
        SoundQueue::_Flush();
        Sync();
    }
    void _Queue(LegacyLong) override
    {
        Sync();
    }
    void _PauseQueue(LegacyLong) override
    {
        Sync();
    }
    void _ResumeQueue(LegacyLong) override
    {
        Sync();
    }
    ResourceReader _Reader() override
    {
        return SDLWave::Read;
    }

  public:
    ~SDLWaveQueue()
    {
        if (id.low || id.high)
            mm_audio_mixer_remove(KauaiAudio::LockedMixer(output.get()).Get(), id);
    }
    static SDLWaveQueue *Create(const std::shared_ptr<MMSdlAudioOutput> &output)
    {
        auto queue = NewObj SDLWaveQueue(output);
        if (!queue)
            return nullptr;
        if (queue->_FInit())
            queue->id = mm_audio_mixer_add(KauaiAudio::LockedMixer(output.get()).Get());
        if (!queue->id.low && !queue->id.high)
        {
            queue->Release();
            return nullptr;
        }
        return queue;
    }
};
#define SDLWaveDevice_PAR SoundManagerQueue
#define kclsSDLWaveDevice 'swde'
class SDLWaveDevice : public SDLWaveDevice_PAR
{
    RTCLASS_INLINE(SDLWaveDevice)
    std::shared_ptr<MMSdlAudioOutput> output;
    LegacyLong volume = kvlmFull;

  protected:
    PSoundQueue _PsnqueNew() override
    {
        return SDLWaveQueue::Create(output);
    }
    void _Suspend(bool suspended) override
    {
        mm_audio_mixer_suspend(KauaiAudio::LockedMixer(output.get()).Get(), suspended);
    }

  public:
    void SetVlm(LegacyLong value) override
    {
        volume = std::max<LegacyLong>(0, value);
        mm_audio_mixer_volume(KauaiAudio::LockedMixer(output.get()).Get(), volume / float(kvlmFull));
    }
    LegacyLong VlmCur() override
    {
        return volume;
    }
    void BeginSynch() override
    {
        mm_audio_mixer_begin(KauaiAudio::LockedMixer(output.get()).Get());
    }
    void EndSynch() override
    {
        mm_audio_mixer_end(KauaiAudio::LockedMixer(output.get()).Get());
    }
    static SDLWaveDevice *Create()
    {
        auto device = NewObj SDLWaveDevice;
        if (!device)
            return nullptr;
        auto nativeOutput = mm_sdl_audio_output_create_mixer();
        if (!nativeOutput)
        {
            device->Release();
            return nullptr;
        }
        try
        {
            device->output = std::shared_ptr<MMSdlAudioOutput>(nativeOutput, mm_sdl_audio_output_destroy);
        }
        catch (const std::bad_alloc &)
        {
            device->Release();
            return nullptr;
        }
        if (!device->_FInit() || !mm_sdl_audio_output_open(device->output.get(), nullptr, nullptr))
        {
            device->Release();
            return nullptr;
        }
        studioOutput = device->output;
        return device;
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
    auto output = studioOutput.lock();
    if (!output)
        return {};
    const LockedMixer lock(output.get());
    const auto number = [](MMAudioCounter count) { return (std::uint64_t(count.high) << 32) | count.low; };
    return {true, decodedSounds, number(lock.Get()->started), number(lock.Get()->completed)};
}
} // namespace KauaiAudio

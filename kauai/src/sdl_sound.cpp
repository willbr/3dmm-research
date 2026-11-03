/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_audio.h"
#include "sdl_sound.h"
#include <algorithm>
#include <new>
#include "kauai_core.h"
#include "sndm.h"
#undef size
#ifdef _WINDOWS_
#error The SDL sound adapter must not import the Windows SDK
#endif
ASSERTNAME
namespace
{
std::weak_ptr<KauaiAudio::Output> studioOutput;
std::uint64_t decodedSounds = 0;
} // namespace
#define SDLWave_PAR BaseCacheableObject
#define kclsSDLWave 'swav'
class SDLWave : public SDLWave_PAR
{
    RTCLASS_INLINE(SDLWave)
  public:
    std::shared_ptr<const KauaiAudio::Sample> sample;
    static bool Read(PChunkyResourceFile, ChunkTagOrType, ChunkNumber, PDataBlock block, PBaseCacheableObject *result,
                     LegacyLong *bytes)
    {
        *bytes = sizeof(SDLWave);
        if (!result)
            return true;
        *result = nullptr;
        if (!block->FUnpackData())
            return false;
        const auto count = block->Cb();
        if (count < 12)
            return false;
        try
        {
            std::vector<byte> encoded(count);
            if (!block->FReadRgb(encoded.data(), count, 0))
                return false;
            auto wave = NewObj SDLWave;
            if (!wave)
                return false;
            if (!KauaiAudio::DecodeWave(encoded.data(), encoded.size(), wave->sample))
            {
                wave->Release();
                return false;
            }
            const auto allocated = wave->sample->stereo.size() * sizeof(float) + sizeof(SDLWave);
            if (allocated > INT32_MAX)
            {
                wave->Release();
                return false;
            }
            *bytes = static_cast<LegacyLong>(allocated);
            *result = wave;
            ++decodedSounds;
            return true;
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
    }
};
#define SDLWaveQueue_PAR SoundQueue
#define kclsSDLWaveQueue 'swqu'
class SDLWaveQueue : public SDLWaveQueue_PAR
{
    RTCLASS_INLINE(SDLWaveQueue)
    std::shared_ptr<KauaiAudio::Output> output;
    std::uint64_t id = 0;
    explicit SDLWaveQueue(const std::shared_ptr<KauaiAudio::Output> &device) : output(device)
    {
    }
    void Sync()
    {
        try
        {
            std::vector<KauaiAudio::Voice> voices;
            for (LegacyLong i = _isndinCur; i < _pglsndin->IvMac(); ++i)
            {
                SNDIN item;
                _pglsndin->Get(i, &item);
                KauaiAudio::Voice voice;
                voice.id = item.sii;
                voice.sample = static_cast<SDLWave *>(item.pbaco)->sample;
                voice.volume = std::max<LegacyLong>(0, item.vlm) / float(kvlmFull);
                voice.plays = std::max<LegacyLong>(0, item.cactPlay);
                voice.offsetMs = item.dtsStart;
                voice.paused = item.cactPause > 0;
                voice.stopped = item.cactPause < 0;
                voices.push_back(std::move(voice));
            }
            output->Engine().Sync(id, voices);
        }
        catch (const std::bad_alloc &)
        {
        }
    }

  protected:
    void _Enter() override
    {
        output->Engine().Lock();
        while (_pglsndin && _isndinCur < _pglsndin->IvMac())
        {
            SNDIN item;
            _pglsndin->Get(_isndinCur, &item);
            if (!output->Engine().Done(id, item.sii))
                break;
            ++_isndinCur;
        }
    }
    void _Leave() override
    {
        output->Engine().Unlock();
    }
    void _Flush() override
    {
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
    PBaseCacheableObject _PbacoFetch(PResourceCache cache, ChunkTagOrType type, ChunkNumber number) override
    {
        return cache->PbacoFetch(type, number, SDLWave::Read);
    }

  public:
    ~SDLWaveQueue()
    {
        if (id)
            output->Engine().RemoveQueue(id);
    }
    static SDLWaveQueue *Create(const std::shared_ptr<KauaiAudio::Output> &output)
    {
        auto queue = NewObj SDLWaveQueue(output);
        if (!queue)
            return nullptr;
        if (!queue->_FInit() || !(queue->id = output->Engine().AddQueue()))
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
    std::shared_ptr<KauaiAudio::Output> output;
    LegacyLong volume = kvlmFull;

  protected:
    PSoundQueue _PsnqueNew() override
    {
        return SDLWaveQueue::Create(output);
    }
    void _Suspend(bool suspended) override
    {
        output->Engine().Suspend(suspended);
    }

  public:
    void SetVlm(LegacyLong value) override
    {
        volume = std::max<LegacyLong>(0, value);
        output->Engine().SetVolume(volume / float(kvlmFull));
    }
    LegacyLong VlmCur() override
    {
        return volume;
    }
    void BeginSynch() override
    {
        output->Engine().BeginGroup();
    }
    void EndSynch() override
    {
        output->Engine().EndGroup();
    }
    static SDLWaveDevice *Create()
    {
        auto device = NewObj SDLWaveDevice;
        if (!device)
            return nullptr;
        try
        {
            device->output = std::make_shared<KauaiAudio::Output>();
        }
        catch (const std::bad_alloc &)
        {
            device->Release();
            return nullptr;
        }
        if (!device->_FInit() || !device->output->Open())
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
    const auto stats = output->Engine().Stats();
    return {true, decodedSounds, stats.started, stats.completed};
}
} // namespace KauaiAudio

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Audioman based sound classes.

***************************************************************************/
#include "frame.h"
#include "mm/sound_payload.h"
#include "audioman.h"
#include "sndampri.h"
#include "handle_services.h"
ASSERTNAME

// CHECK_AUDIO_DEVCAPS turns on using waveOutGetDevCaps to check for
// audio device characteristics, to workaround AudioMan 1.0 always returning
// whatever device was asked for, regardless if device actually supports format.
// In case of asking for a 16 bit device, and there is only an 8 bit device, the
// open will succeed, causing sub-optimal audioman performance on an 8 bit card.
// With AudioMan 1.5, this should go away.
#define CHECK_AUDIO_DEVCAPS

// Initialize the maximum mem footprint of wave sounds.
long AudioManSoundDevice::vcbMaxMemWave = 40 * 1024;

static IAMMixer *_pamix; // the audioman mixer
static MMAudioManGroup _group;

static ulong _luFormat;    // format mixer is in
static ulong _luCacheTime; // buffer size for mixer

RTCLASS(AudioManSoundDevice)
RTCLASS(CachedAudioManSound)
RTCLASS(AudioManQueue)

/***************************************************************************
    Constructor for a streamed block.
***************************************************************************/
DataBlockStream::DataBlockStream(void)
{
    AssertThisMem();

    // WARNING: this is not allocated using our NewObj because DataBlockStream is not
    // based on BASE. So fields are not automatically initialized to 0.
    mm_reference_init(&_references);
    AssertDo(mm_audioman_stream_init(&_stream, KauaiHandleServices(), KauaiBlockFileServices()) == MM_DATA_OK,
             "invalid AudioMan stream services");
}

/***************************************************************************
    Destructor for a streamed block.
***************************************************************************/
DataBlockStream::~DataBlockStream(void)
{
    AssertThisMem();
    mm_audioman_stream_dispose(&_stream);
}

/***************************************************************************
    QueryInterface for DataBlockStream.
***************************************************************************/
STDMETHODIMP DataBlockStream::QueryInterface(REFIID riid, void **ppv)
{
    AssertThis(0);

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IStream))
    {
        *ppv = (void *)this;
        AddRef();
        return S_OK;
    }

    *ppv = pvNil;
    return E_NOINTERFACE;
}

/***************************************************************************
    Increment the reference count.
***************************************************************************/
STDMETHODIMP_(ULONG) DataBlockStream::AddRef(void)
{
    AssertThis(0);
    return mm_reference_add(&_references);
}

/***************************************************************************
    Decrement the reference count.
***************************************************************************/
STDMETHODIMP_(ULONG) DataBlockStream::Release(void)
{
    AssertThis(0);
    const auto cactRef = mm_reference_drop(&_references);
    if (!cactRef)
        delete this;
    return cactRef;
}

/***************************************************************************
    Read some stuff.
***************************************************************************/
STDMETHODIMP DataBlockStream::Read(void *pv, ULONG cb, ULONG *pcb)
{
    AssertThis(0);
    AssertPvCb(pv, cb);
    AssertNilOrVarMem(pcb);

    MMU32 read;
    const int status = mm_audioman_stream_read(&_stream, pv, cb, &read);
    if (pcb)
        *pcb = read;
    return status == MM_DATA_OK ? NOERROR : ResultFromScode(STG_E_READFAULT);
}

/***************************************************************************
    Seek to a place.
***************************************************************************/
STDMETHODIMP DataBlockStream::Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition)
{
    AssertThis(0);
    AssertNilOrVarMem(plibNewPosition);

    static_assert(STREAM_SEEK_SET == MM_AUDIOMAN_SEEK_SET && STREAM_SEEK_CUR == MM_AUDIOMAN_SEEK_CUR &&
                      STREAM_SEEK_END == MM_AUDIOMAN_SEEK_END,
                  "AudioMan seek origins must match IStream");
    const MMWide offset = {static_cast<MMU32>(dlibMove.LowPart), static_cast<MMI32>(dlibMove.HighPart)};
    MMU32 position;
    const int status = mm_audioman_stream_seek(&_stream, offset, dwOrigin, &position);
    if (plibNewPosition)
        plibNewPosition->QuadPart = position;
    return status == MM_DATA_OK ? S_OK : E_INVALIDARG;
}

/***************************************************************************
    Create a stream with backing that outlives the cache callback.
***************************************************************************/
PDataBlockStream DataBlockStream::PstblNew(MMOwnedBlock *source)
{
    auto stream = new DataBlockStream;
    if (!stream)
        return pvNil;

    bool removable = false;
    if (!source->view.packed)
    {
        Filename filename;
        static_cast<PFileObject>(source->file)->GetFni(&filename);
        removable = (filename.Grfvk() & (fvkFloppy | fvkCD | fvkRemovable)) != 0;
    }
    if (!mm_wave_stream_prepare(source, &stream->_stream.block, removable, kcbCacheCharge,
                                AudioManSoundDevice::vcbMaxMemWave, KauaiDecodeBlockHandle, KauaiCopyAllocator()))
    {
        delete stream;
        return pvNil;
    }
    AssertPo(stream, 0);
    return stream;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a DataBlockStream.
***************************************************************************/
void DataBlockStream::AssertValid(ulong grf)
{
    AssertThisMem();
    Assert(mm_owned_block_check(&_stream.block) == MM_DATA_OK, "invalid AudioMan stream block");
    Assert(_stream.position <= _stream.block.view.length, "AudioMan stream position past EOF");
    if (_stream.block.view.kind == MM_BLOCK_FILE)
        AssertPo(static_cast<PFileObject>(_stream.block.file), 0);
    else if (_stream.block.view.kind == MM_BLOCK_MEMORY)
        AssertHq(_stream.block.view.memory);
}

/***************************************************************************
    Mark memory for the DataBlockStream.
***************************************************************************/
void DataBlockStream::MarkMem(void)
{
    AssertValid(0);
    MarkHq(_stream.block.view.memory);
}
#endif // DEBUG

/***************************************************************************
    Constructor for a cached AudioMan sound.
***************************************************************************/
CachedAudioManSound::CachedAudioManSound(void)
{
    AssertBaseThis(fobjAllocated);
}

/***************************************************************************
    Destructor for a cached AudioMan sound.
***************************************************************************/
CachedAudioManSound::~CachedAudioManSound(void)
{
    AssertBaseThis(fobjAllocated);
    ReleasePpo(&psnd);
    ReleasePpo(&_pstbl);
}

/***************************************************************************
    Static BaseCacheableObject reader method to put together a Cached AudioMan sound.
***************************************************************************/
int CachedAudioManSound::FReadCams(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block,
                                  void **out, MMI32 *bytes)
{
    MMSoundPayloadServices s = {};
    s.object_bytes = size(CachedAudioManSound) + DataBlockStream::kcbCacheCharge;
    s.open_stream = [](void *, MMOwnedBlock *root) -> void * { return DataBlockStream::PstblNew(root); };
    s.release_stream = [](void *, void *stream) { static_cast<PDataBlockStream>(stream)->Release(); };
    s.stream_size = [](void *, void *stream) -> MMI32 {
        return size(CachedAudioManSound) + static_cast<PDataBlockStream>(stream)->CbMem();
    };
    s.create = [](void *) -> void * { return NewObj CachedAudioManSound; };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.decode_stream = [](void *, void *object, void *stream) -> int {
        return SUCCEEDED(AllocSoundFromStream(&static_cast<PCachedAudioManSound>(object)->psnd,
                                              static_cast<PDataBlockStream>(stream), fTrue, pvNil));
    };
    s.adopt_stream = [](void *, void *object, void *stream) {
        static_cast<PCachedAudioManSound>(object)->_pstbl = static_cast<PDataBlockStream>(stream);
    };
    return mm_wave_stream_fetch(block, out, bytes, &s);
}

/***************************************************************************
    Static BaseCacheableObject reader method to put together a Cached AudioMan sound.
***************************************************************************/
PCachedAudioManSound CachedAudioManSound::PcamsNewLoop(PCachedAudioManSound pcamsSrc, long cactPlay)
{
    AssertPo(pcamsSrc, 0);
    Assert(cactPlay != 1, "bad loop count");
    PCachedAudioManSound pcams = pvNil;

    if (pvNil == (pcams = NewObj CachedAudioManSound) ||
        FAILED(AllocLoopFilter(&pcams->psnd, pcamsSrc->psnd, cactPlay - 1)))
    {
        ReleasePpo(&pcams);
    }

    if (pvNil != pcams)
    {
        pcams->_pstbl = pcamsSrc->_pstbl;
        pcams->_pstbl->AddRef();
    }

    AssertNilOrPo(pcams, 0);
    return pcams;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a CachedAudioManSound.
***************************************************************************/
void CachedAudioManSound::AssertValid(ulong grf)
{
    CachedAudioManSound_PAR::AssertValid(0);
    AssertPo(_pstbl, 0);
    Assert(psnd != pvNil, 0);
}

/***************************************************************************
    Mark memory for the CachedAudioManSound.
***************************************************************************/
void CachedAudioManSound::MarkMem(void)
{
    AssertValid(0);
    CachedAudioManSound_PAR::MarkMem();
    if (pvNil != _pstbl)
        _pstbl->MarkMem();
}
#endif // DEBUG

/***************************************************************************
    Constructor for our notify sink.
***************************************************************************/
AudioManNotifySink::AudioManNotifySink(void)
{
    mm_reference_init(&_references);
    _pamque = pvNil;
}

/***************************************************************************
    Set the AudioManQueue that we're to notify.
***************************************************************************/
void AudioManNotifySink::Set(PAudioManQueue pamque)
{
    AssertNilOrVarMem(pamque);
    _pamque = pamque;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a AudioManNotifySink.
***************************************************************************/
void AudioManNotifySink::AssertValid(ulong grf)
{
    AssertThisMem();
    AssertNilOrVarMem(_pamque);
}
#endif // DEBUG

/***************************************************************************
    QueryInterface for AudioManNotifySink.
***************************************************************************/
STDMETHODIMP AudioManNotifySink::QueryInterface(REFIID riid, void **ppv)
{
    AssertThis(0);

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IAMNotifySink))
    {
        *ppv = (void *)this;
        AddRef();
        return S_OK;
    }

    *ppv = pvNil;
    return E_NOINTERFACE;
}

/***************************************************************************
    Increment the reference count.
***************************************************************************/
STDMETHODIMP_(ULONG) AudioManNotifySink::AddRef(void)
{
    AssertThis(0);
    return mm_reference_add(&_references);
}

/***************************************************************************
    Decrement the reference count.
***************************************************************************/
STDMETHODIMP_(ULONG) AudioManNotifySink::Release(void)
{
    AssertThis(0);
    const auto cactRef = mm_reference_drop(&_references);
    if (!cactRef)
        delete this;
    return cactRef;
}

/***************************************************************************
    The indicated sound is done. Just tell the AudioManQueue that we got a notify.
***************************************************************************/
STDMETHODIMP_(void) AudioManNotifySink::OnCompletion(LPSOUND pSound, DWORD dwPosition)
{
    AssertThis(0);

    if (pvNil != _pamque)
        _pamque->Notify(pSound);
}

/***************************************************************************
    Constructor for an audioman queue.
***************************************************************************/
AudioManQueue::AudioManQueue(void)
{
}

/***************************************************************************
    Destructor for an audioman queue.
***************************************************************************/
AudioManQueue::~AudioManQueue(void)
{
    if (pvNil != _pchan)
        StopAll();
    ReleasePpo(&_pchan);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a AudioManQueue.
***************************************************************************/
void AudioManQueue::AssertValid(ulong grf)
{
    AudioManQueue_PAR::AssertValid(0);
    Assert(pvNil != _pchan, 0);
}
#endif // DEBUG

/***************************************************************************
    Static method to create a new audioman queue.
***************************************************************************/
PAudioManQueue AudioManQueue::PamqueNew(void)
{
    PAudioManQueue pamque;

    if (pvNil == (pamque = NewObj AudioManQueue))
        return pvNil;

    if (!pamque->_FInit())
        ReleasePpo(&pamque);

    AssertNilOrPo(pamque, 0);
    return pamque;
}

/***************************************************************************
    Initialize the audioman queue. Allocate the audioman channel and the
    _pglsndin.
***************************************************************************/
bool AudioManQueue::_FInit(void)
{
    AssertBaseThis(0);

    if (!AudioManQueue_PAR::_FInit())
        return fFalse;

    if (FAILED(_pamix->AllocChannel(&_pchan)))
    {
        _pchan = pvNil;
        return fFalse;
    }
    if (pvNil == _pchan)
    {
        Bug("Audioman messed up!");
        return fFalse;
    }

    _amnot.Set(this);
    if (FAILED(_pchan->RegisterNotify(&_amnot, NOTIFYSINK_ONCOMPLETION)))
        return fFalse;

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Enter the critical section protecting member variables.
***************************************************************************/
void AudioManQueue::_Enter(void)
{
    _mutx.Enter();
}

/***************************************************************************
    Leave the critical section protecting member variables.
***************************************************************************/
void AudioManQueue::_Leave(void)
{
    _mutx.Leave();
}

/***************************************************************************
    Fetch the given sound chunk as a CachedAudioManSound.
***************************************************************************/
PBaseCacheableObject AudioManQueue::_PbacoFetch(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    AssertPo(prca, 0);

    return prca->PbacoFetch(ctg, cno, &CachedAudioManSound::FReadCams);
}

/***************************************************************************
    An item was added to or deleted from the queue.
***************************************************************************/
void AudioManQueue::_Queue(long isndinMin)
{
    AssertThis(0);
    const auto s = _QueueServices();
    mm_audioman_queue_run(&_tsStart, isndinMin, &_group, &s);
}

/***************************************************************************
    One or more items in the queue were paused.
***************************************************************************/
void AudioManQueue::_PauseQueue(long isndinMin)
{
    AssertThis(0);
    const auto s = _QueueServices();
    mm_audioman_queue_pause(&_tsStart, isndinMin, &_group, &s);
}

/***************************************************************************
    One or more items in the queue were resumed.
***************************************************************************/
void AudioManQueue::_ResumeQueue(long isndinMin)
{
    AssertThis(0);

    _Queue(isndinMin);
}

/***************************************************************************
    Called by our notify sink to tell us that the indicated sound is done.
    WARNING: this is called in an auxillary thread.
***************************************************************************/
void AudioManQueue::Notify(LPSOUND psnd)
{
    AssertThis(0);
    const auto s = _QueueServices();
    mm_audioman_queue_notify(&_tsStart, psnd, &_group, &s);
}

MMAudioManQueueServices AudioManQueue::_QueueServices()
{
    MMAudioManQueueServices s = {};
    s.entries = _Services();
    s.entries.count = [](void *p) -> MMI32 {
        auto queue = static_cast<AudioManQueue *>(static_cast<SoundQueue *>(p));
        return queue->_pglsndin ? queue->_pglsndin->IvMac() : -1;
    };
    s.context = this;
    s.loop = [](void *, void *object, MMI32 plays) -> void * {
        return CachedAudioManSound::PcamsNewLoop(static_cast<PCachedAudioManSound>(object), plays);
    };
    s.identity = [](void *, void *object) -> void * { return static_cast<PCachedAudioManSound>(object)->psnd; };
    s.in_memory = [](void *, void *object) -> int { return static_cast<PCachedAudioManSound>(object)->FInMemory(); };
    s.stop = [](void *p) { static_cast<AudioManQueue *>(p)->_pchan->Stop(); };
    s.play = [](void *p) { static_cast<AudioManQueue *>(p)->_pchan->Play(); };
    s.volume = [](void *p, MMU32 volume) { static_cast<AudioManQueue *>(p)->_pchan->SetVolume(volume); };
    s.seek = [](void *p, MMU32 offset) { static_cast<AudioManQueue *>(p)->_pchan->SetTimePos(offset); };
    s.source = [](void *p, void *object) {
        auto sound = static_cast<PCachedAudioManSound>(object);
        static_cast<AudioManQueue *>(p)->_pchan->SetSoundSrc(sound ? sound->psnd : nullptr);
    };
    s.cached_source = [](void *p, void *object) {
        CacheConfig cc;
        cc.dwSize = size(cc);
        cc.fSrcFormat = fTrue;
        cc.lpFormat = pvNil;
        cc.dwFormat = _luFormat;
        cc.dwCacheTime = 2 * _luCacheTime;
        static_cast<AudioManQueue *>(p)->_pchan->SetCachedSrc(static_cast<PCachedAudioManSound>(object)->psnd, &cc);
    };
    s.enlist = [](void *p, MMU32 group) -> int {
        return SUCCEEDED(_pamix->EnlistGroup(static_cast<AudioManQueue *>(p)->_pchan, group));
    };
    s.clock = [](void *) -> MMU32 { return TsCurrentSystem(); };
    return s;
}

/***************************************************************************
    Constructor for the audioman device.
***************************************************************************/
AudioManSoundDevice::AudioManSoundDevice(void)
{
    _vlm = kvlmFull;
    _luVolSys = (ulong)(-1);
}

/***************************************************************************
    Destructor for the audioman device.
***************************************************************************/
AudioManSoundDevice::~AudioManSoundDevice(void)
{
    AssertBaseThis(0);

    if (_fAudioManInited && 0 == _pamix->Release())
        _pamix = pvNil;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a AudioManSoundDevice.
***************************************************************************/
void AudioManSoundDevice::AssertValid(ulong grf)
{
    AudioManSoundDevice_PAR::AssertValid(0);
    Assert(_pamix != pvNil, 0);
}
#endif // DEBUG

/***************************************************************************
    Static method to create the audioman device.
***************************************************************************/
PAudioManSoundDevice AudioManSoundDevice::PsdamNew(long wav)
{
    PAudioManSoundDevice psdam;

    if (pvNil == (psdam = NewObj AudioManSoundDevice))
        return pvNil;

    if (!psdam->_FInit(wav))
        ReleasePpo(&psdam);

    AssertNilOrPo(psdam, 0);
    return psdam;
}

static long _mpwavfmt[] = {
    WAVE_FORMAT_1M08, WAVE_FORMAT_2M08, WAVE_FORMAT_4M08, WAVE_FORMAT_1S08, WAVE_FORMAT_2S08, WAVE_FORMAT_4S08,
    WAVE_FORMAT_1M16, WAVE_FORMAT_2M16, WAVE_FORMAT_4M16, WAVE_FORMAT_1S16, WAVE_FORMAT_2S16, WAVE_FORMAT_4S16,
};

#ifdef CHECK_AUDIO_DEVCAPS
/******************************************************************************

@func   WORD | wHaveWaveDevice |

        Do we have a wave device capable of playing the passed PCM format(s).

@parm  DWORD | dwFormats | WAVE formats needed to be supported.  These can be
                            a bitwise combination of WAVE_FORMAT_???? flags
                            which are defined in mmsystem.h.  If you don't
                            care what formats are supported you can pass zero.

******************************************************************************/

bool FHaveWaveDevice(DWORD dwReqFormats)
{
    WORD wNumWavDev;
    WAVEOUTCAPS WOC;
    WORD wDevID;
    WORD wErr;

    // Determine how many WAVE devices are in the user's system
    wNumWavDev = waveOutGetNumDevs();

    // If there are none, return indicating that
    if (0 == wNumWavDev)
        return (fFalse);

    // Cycle through the WAVE devices to determine if any support
    // the desired format.
    for (wDevID = 0; wDevID < wNumWavDev; wDevID++)
    {
        wErr = waveOutGetDevCaps(wDevID, &WOC, sizeof(WAVEOUTCAPS));

        // If we obtain a WAVE device's capabilities OK
        // and it supports the desired format
        if ((0 == wErr) && ((WOC.dwFormats & dwReqFormats) == dwReqFormats))
        {
            // then return success - we have a device that supports what we want
            return fTrue;
        }
    }

    // it doesn't support this device
    return fFalse;
}
#endif

/***************************************************************************
    Initialize the audioman device.
***************************************************************************/
bool AudioManSoundDevice::_FInit(long wav)
{
    AssertBaseThis(0);
    MIXERCONFIG mixc;
    ADVMIXCONFIG amxc;

    if (!AudioManSoundDevice_PAR::_FInit())
        return fFalse;

    // get IAMMixer interface
    if (pvNil != _pamix)
    {
        _pamix->AddRef();
        _fAudioManInited = fTrue;
    }
    else
    {
        if (pvNil == (_pamix = GetAudioManMixer()))
            return fFalse;
        _fAudioManInited = fTrue;

        // REVIEW shonk: what values should we use?
        mixc.dwSize = size(mixc);
        mixc.lpFormat = pvNil;
        if (!FIn(wav, 0, kwavLim))
            wav = kwav22M16;
        mixc.dwFormat = _mpwavfmt[wav];
        amxc.dwSize = size(amxc);
        amxc.uVoices = 12;
        amxc.fRemixEnabled = fTrue;
        amxc.uBufferTime = 600;

#ifdef CHECK_AUDIO_DEVCAPS
        // if we don't have a device of this format...
        if (!FHaveWaveDevice(mixc.dwFormat))
        {
            // failed, so try dropping to 8 bit
            wav += kwav22M8 - kwav22M16;
            if (!FIn(wav, 0, kwavLim))
                return fFalse;
            mixc.dwFormat = _mpwavfmt[wav];
            // we'll try to open at 8, cause if the card doesn't
            // support it, then WAVE_MAPPER will actually convert
            // to the 8 bit format.
        }

        _luFormat = mixc.dwFormat;
        _luCacheTime = amxc.uBufferTime;

        // initialize it (done only once...)
        if (FAILED(_pamix->Init(vwig.hinst, pvNil, &mixc, &amxc)))
            return fFalse;
#else
        _luFormat = mixc.dwFormat;
        _luCacheTime = amxc.uBufferTime;

        // initialize it (done only once...)
        if (FAILED(_pamix->Init(vwig.hinst, pvNil, &mixc, &amxc)))
        {
            // failed, so try at 8 bit
            wav += kwav22M8 - kwav22M16;
            if (!FIn(wav, 0, kwavLim))
                return fFalse;
            if (FAILED(_pamix->Init(vwig.hinst, pvNil, &mixc, &amxc)))
                return fFalse;
        }
#endif
        if (FAILED(_pamix->Activate(fTrue)))
            return fFalse;
    }

    _Suspend(_control.suspended > 0 || !_control.active);

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Allocate a new audioman queue.
***************************************************************************/
PSoundQueue AudioManSoundDevice::_PsnqueNew(void)
{
    AssertThis(0);

    return AudioManQueue::PamqueNew();
}

/***************************************************************************
    Activate or deactivate audioman.
***************************************************************************/
void AudioManSoundDevice::_Suspend(bool fSuspend)
{
    AssertThis(0);

    if (fSuspend)
        _pamix->SetMixerVolume(_luVolSys);

    if (FAILED(_pamix->Suspend(fSuspend)) && !fSuspend)
        PushErc(ercSndamWaveDeviceBusy);
    else if (!fSuspend)
    {
        // becoming active
        _pamix->GetMixerVolume(&_luVolSys);
        vluSysVolFake = _luVolSys;
        _pamix->SetMixerVolume(LuVolScale(_luVolSys, _vlm));
    }
}

/***************************************************************************
    Set the volume.
***************************************************************************/
void AudioManSoundDevice::SetVlm(long vlm)
{
    AssertThis(0);

    if (_vlm != vlm)
    {
        _vlm = vlm;
        if (_control.suspended <= 0 && _control.active)
            _pamix->SetMixerVolume(LuVolScale(_luVolSys, vlm));
    }
}

/***************************************************************************
    Get the current volume.
***************************************************************************/
long AudioManSoundDevice::VlmCur(void)
{
    AssertThis(0);

    return _vlm;
}

/***************************************************************************
    Begin a synchronization group.
***************************************************************************/
void AudioManSoundDevice::BeginSynch(void)
{
    AssertThis(0);
    MMAudioManGroupServices s = {};
    s.allocate = [](void *, MMU32 *id) -> int {
        DWORD group = *id;
        const auto result = _pamix->AllocGroup(&group);
        *id = group;
        return SUCCEEDED(result);
    };
    mm_audioman_group_begin(&_group, &s);
}

/***************************************************************************
    End a synchronization group.
***************************************************************************/
void AudioManSoundDevice::EndSynch(void)
{
    AssertThis(0);
    MMAudioManGroupServices s = {};
    s.start = [](void *, MMU32 group) { _pamix->StartGroup(group, fTrue); };
    s.release = [](void *, MMU32 group) { _pamix->FreeGroup(group); };
    mm_audioman_group_end(&_group, &s);
}

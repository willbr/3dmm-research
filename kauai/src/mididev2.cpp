/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    The midi player device using a Midi Stream.

***************************************************************************/
#include "midi_stream_host.h"
#include "sndm.h"
#include "midi.h"
#include "mididev2.h"
#include "mdev2pri.h"
#include "handle_services.h"
#include <cstdlib>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The MIDI mixer must not import the Windows SDK
#endif
ASSERTNAME

RTCLASS(MidiStreamCached)
RTCLASS(MidiStreamQueue)
RTCLASS(MidiStreamPlayer)
RTCLASS(MidiStreamMixer)

const LegacyLong kdtsMinSlip = kdtsSecond / 30;

/***************************************************************************
    Constructor for the midi stream device.
***************************************************************************/
MidiStreamPlayer::MidiStreamPlayer(void)
{
}

/***************************************************************************
    Destructor for the midi stream device.
***************************************************************************/
MidiStreamPlayer::~MidiStreamPlayer(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pmsmix);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MidiStreamPlayer.
***************************************************************************/
void MidiStreamPlayer::AssertValid(LegacyUlong grf)
{
    MidiStreamPlayer_PAR::AssertValid(0);
    AssertPo(_pmsmix, 0);
}

/***************************************************************************
    Mark memory for the MidiStreamPlayer.
***************************************************************************/
void MidiStreamPlayer::MarkMem(void)
{
    AssertValid(0);
    MidiStreamPlayer_PAR::MarkMem();
    MarkMemObj(_pmsmix);
}
#endif // DEBUG

/***************************************************************************
    Static method to create the midi stream device.
***************************************************************************/
PMidiStreamPlayer MidiStreamPlayer::PmdpsNew(KauaiMidi::HostFactory factory)
{
    PMidiStreamPlayer pmdps;

    if (pvNil == (pmdps = NewObj MidiStreamPlayer))
        return pvNil;

    if (!pmdps->_FInit(factory))
        ReleasePpo(&pmdps);

    AssertNilOrPo(pmdps, 0);
    return pmdps;
}

/***************************************************************************
    Initialize the midi stream device.
***************************************************************************/
bool MidiStreamPlayer::_FInit(KauaiMidi::HostFactory factory)
{
    AssertBaseThis(0);

    if (!MidiStreamPlayer_PAR::_FInit())
        return fFalse;

    // Create the midi stream output scheduler
    if (pvNil == (_pmsmix = MidiStreamMixer::PmsmixNew(factory)))
        return fFalse;

    _Suspend(_control.suspended > 0 || !_control.active);

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Allocate a new midi stream queue.
***************************************************************************/
PSoundQueue MidiStreamPlayer::_PsnqueNew(void)
{
    AssertThis(0);

    return MidiStreamQueue::PmsqueNew(_pmsmix);
}

/***************************************************************************
    Activate or deactivate the midi stream device.
***************************************************************************/
void MidiStreamPlayer::_Suspend(bool fSuspend)
{
    AssertThis(0);

    _pmsmix->Suspend(fSuspend);
}

/***************************************************************************
    Set the volume.
***************************************************************************/
void MidiStreamPlayer::SetVlm(LegacyLong vlm)
{
    AssertThis(0);

    _pmsmix->SetVlm(vlm);
}

/***************************************************************************
    Get the current volume.
***************************************************************************/
LegacyLong MidiStreamPlayer::VlmCur(void)
{
    AssertThis(0);

    return _pmsmix->VlmCur();
}

/***************************************************************************
    Constructor for a midi stream object.
***************************************************************************/
MidiStreamCached::MidiStreamCached(void)
{
}

/***************************************************************************
    Destructor for a Win95 midi stream object.
***************************************************************************/
MidiStreamCached::~MidiStreamCached(void)
{
    AssertDo(mm_midi_stream_dispose(&_stream) == MM_STORAGE_OK, "MIDI stream still pinned");
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MidiStreamCached.
***************************************************************************/
void MidiStreamCached::AssertValid(LegacyUlong grf)
{
    MidiStreamCached_PAR::AssertValid(0);
    Assert(_stream.events.storage == &_stream.storage && _stream.events.count >= 2, "invalid MIDI stream");
}

/***************************************************************************
    Mark memory for the MidiStreamCached.
***************************************************************************/
void MidiStreamCached::MarkMem(void)
{
    AssertValid(0);
    MidiStreamCached_PAR::MarkMem();
    MarkPv(_stream.storage.buffers[0].data);
}
#endif // DEBUG

/***************************************************************************
    A baco reader for a midi stream.
***************************************************************************/
int MidiStreamCached::FReadMdws(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    *bytes = static_cast<MMI32>(block->view.length);
    if (!out)
        return fTrue;

    auto stream = NewObj MidiStreamCached;
    if (!stream || !stream->_FInit(block))
    {
        delete stream;
        TrashVar(out);
        TrashVar(bytes);
        return fFalse;
    }
    *bytes = mm_midi_stream_bytes(stream->Stream()) + size(MidiStreamCached);
    *out = stream;
    return fTrue;
}

/***************************************************************************
    Initialize the cached stream with a packed or unpacked MIDI track.
***************************************************************************/
bool MidiStreamCached::_FInit(MMOwnedBlock *block)
{
    const auto allocator = Group::CollectionAllocator();
    return mm_midi_stream_init(&_stream, &allocator, DebugShip(1, 0)) &&
           mm_midi_stream_read(&_stream, block, KauaiDecodeBlockHandle, nullptr);
}

/***************************************************************************
    Constructor for a midi stream queue.
***************************************************************************/
MidiStreamQueue::MidiStreamQueue(void)
{
}

/***************************************************************************
    Destructor for a midi stream queue.
***************************************************************************/
MidiStreamQueue::~MidiStreamQueue(void)
{
    if (pvNil != _pmsmix)
    {
        _pmsmix->FPlay(this);
        ReleasePpo(&_pmsmix);
    }
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MidiStreamQueue.
***************************************************************************/
void MidiStreamQueue::AssertValid(LegacyUlong grf)
{
    MidiStreamQueue_PAR::AssertValid(0);
    AssertPo(_pmsmix, 0);
}

/***************************************************************************
    Mark memory for the MidiStreamQueue.
***************************************************************************/
void MidiStreamQueue::MarkMem(void)
{
    AssertValid(0);
    MidiStreamQueue_PAR::MarkMem();
    MarkMemObj(_pmsmix);
}
#endif // DEBUG

/***************************************************************************
    Static method to create a new midi stream queue.
***************************************************************************/
PMidiStreamQueue MidiStreamQueue::PmsqueNew(PMidiStreamMixer pmsmix)
{
    AssertPo(pmsmix, 0);
    PMidiStreamQueue pmsque;

    if (pvNil == (pmsque = NewObj MidiStreamQueue))
        return pvNil;

    if (!pmsque->_FInit(pmsmix))
        ReleasePpo(&pmsque);

    AssertNilOrPo(pmsque, 0);
    return pmsque;
}

/***************************************************************************
    Initialize the midi stream queue.
***************************************************************************/
bool MidiStreamQueue::_FInit(PMidiStreamMixer pmsmix)
{
    AssertPo(pmsmix, 0);
    AssertBaseThis(0);

    if (!MidiStreamQueue_PAR::_FInit())
        return fFalse;

    _pmsmix = pmsmix;
    _pmsmix->AddRef();

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Enter the critical section protecting member variables.
***************************************************************************/
void MidiStreamQueue::_Enter(void)
{
    _mutx.Enter();
}

/***************************************************************************
    Leave the critical section protecting member variables.
***************************************************************************/
void MidiStreamQueue::_Leave(void)
{
    _mutx.Leave();
}

/***************************************************************************
    Fetch the given sound chunk as an MidiStreamCached.
***************************************************************************/
ResourceReader MidiStreamQueue::_Reader()
{
    AssertThis(0);
    return &MidiStreamCached::FReadMdws;
}

/***************************************************************************
    An item was added to or deleted from the queue.
***************************************************************************/
void MidiStreamQueue::_Queue(LegacyLong isndinMin)
{
    if (!_entries.rows.storage)
        return;
    auto s = _QueueServices();
    mm_midi_queue_run(&s, isndinMin);
}

/***************************************************************************
    One or more items in the queue were paused.
***************************************************************************/
void MidiStreamQueue::_PauseQueue(LegacyLong isndinMin)
{
    if (!_entries.rows.storage)
        return;
    auto s = _QueueServices();
    mm_midi_queue_pause(&s, isndinMin);
}

/***************************************************************************
    One or more items in the queue were resumed.
***************************************************************************/
void MidiStreamQueue::_ResumeQueue(LegacyLong isndinMin)
{
    AssertThis(0);

    _Queue(isndinMin);
}

/***************************************************************************
    Called by the MidiStreamMixer to tell us that the indicated sound is done.
    WARNING: this is called in an auxillary thread.
***************************************************************************/
void MidiStreamQueue::Notify(PMidiStreamCached pmdws)
{
    if (!_entries.rows.storage)
        return;
    auto s = _QueueServices();
    mm_midi_queue_notify(&s, pmdws);
}

/***************************************************************************
    Constructor for the midi stream output object.
***************************************************************************/
MidiStreamMixer::MidiStreamMixer(void)
{
}

/***************************************************************************
    Destructor for the midi stream output object.
***************************************************************************/
MidiStreamMixer::~MidiStreamMixer(void)
{
    Assert(pvNil == _pmisi || !_pmisi->FActive(), "MidiStreamInterface still active!");

    mm_midi_worker_stop(_worker);
    Assert(!_mix.voices.count, "MidiStreamMixer still has active sounds");
    ReleasePpo(&_pmisi);
    mm_midi_mix_dispose(&_mix);
    mm_native_event_destroy(_buffersReturned);
    mm_midi_worker_destroy(_worker);
}

/***************************************************************************
    Static method to create a new MidiStreamMixer.
***************************************************************************/
PMidiStreamMixer MidiStreamMixer::PmsmixNew(KauaiMidi::HostFactory factory)
{
    PMidiStreamMixer pmsmix;

    if (pvNil == (pmsmix = NewObj MidiStreamMixer))
        return pvNil;

    if (!pmsmix->_FInit(factory))
        ReleasePpo(&pmsmix);

    AssertNilOrPo(pmsmix, 0);
    return pmsmix;
}

/***************************************************************************
    Initialize the voice state, return event and MIDI stream device.
***************************************************************************/
bool MidiStreamMixer::_FInit(KauaiMidi::HostFactory factory)
{
    AssertBaseThis(0);
    if (!factory)
        return fFalse;

    const auto allocator = Group::CollectionAllocator();
    if (!mm_midi_mix_init(&_mix, &allocator, kvlmFull, DebugShip(1, 0)))
        return fFalse;
    _buffersReturned = mm_native_event_create(nullptr);
    if (!_buffersReturned)
        return fFalse;

    if (pvNil == (_pmisi = factory(_MidiProc, reinterpret_cast<std::uintptr_t>(this))))
    {
        return fFalse;
    }

    _worker = mm_midi_worker_start(_ThreadProc, this);
    return _worker != nullptr;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MidiStreamMixer.
***************************************************************************/
void MidiStreamMixer::AssertValid(LegacyUlong grf)
{
    MidiStreamMixer_PAR::AssertValid(0);
    _mutx.Enter();
    Assert(mm_midi_worker_running(_worker), "no MIDI expiration worker");
    Assert(_mix.voices.storage == &_mix.storage, "MIDI voice state uninitialized");
    AssertPo(_pmisi, 0);
    _mutx.Leave();
}

/***************************************************************************
    Mark memory for the MidiStreamMixer.
***************************************************************************/
void MidiStreamMixer::MarkMem(void)
{
    AssertValid(0);

    MidiStreamMixer_PAR::MarkMem();

    _mutx.Enter();
    MarkPv(_mix.storage.buffers[0].data);
    MarkMemObj(_pmisi);
    MarkPv(_mix.prefix.storage.buffers[0].data);
    _mutx.Leave();
}
#endif // DEBUG

/***************************************************************************
    Suspend or resume the midi stream mixer.
***************************************************************************/
void MidiStreamMixer::Suspend(bool fSuspend)
{
    _mutx.Enter();
    auto s = _Services();
    mm_midi_mix_suspend(&_mix, &s, fSuspend);
    _mutx.Leave();
}

/***************************************************************************
    Set the volume for the midi stream output device.
***************************************************************************/
void MidiStreamMixer::SetVlm(LegacyLong vlm)
{
    _mutx.Enter();
    auto s = _Services();
    mm_midi_mix_volume(&_mix, &s, vlm);
    _mutx.Leave();
}

/***************************************************************************
    Get the current volume.
***************************************************************************/
LegacyLong MidiStreamMixer::VlmCur(void)
{
    AssertThis(0);

    return _mix.base_volume;
}

/***************************************************************************
    Play the given midi stream from the indicated queue.
***************************************************************************/
bool MidiStreamMixer::FPlay(PMidiStreamQueue pmsque, PMidiStreamCached pmdws, LegacyLong sii, LegacyLong spr,
                            LegacyLong cactPlay, LegacyUlong dtsStart, LegacyLong vlm)
{
    AssertPo(pmsque, 0);
    AssertNilOrPo(pmdws, 0);
    MMMidiMixVoice v = {pmsque, pmdws, pmdws ? pmdws->Stream() : nullptr,
                        sii, spr, cactPlay, 0, dtsStart, vlm, 0};
    _mutx.Enter();
    auto s = _Services();
    bool ok = mm_midi_mix_play(&_mix, &s, &v) != 0;
    _mutx.Leave();
    return ok;
}

/***************************************************************************
    Call back from the midi stream stuff.
***************************************************************************/
void MidiStreamMixer::_MidiProc(std::uintptr_t luUser, void *pvData, std::uintptr_t lUserDataa)
{
    PMidiStreamMixer pmsmix;
    PMidiStreamCached pmdws;

    pmsmix = (PMidiStreamMixer)luUser;
    AssertPo(pmsmix, 0);
    pmdws = (PMidiStreamCached)lUserDataa;
    AssertNilOrPo(pmdws, 0);

    pmsmix->_Notify(pvData, pmdws);
}

/***************************************************************************
    The midi stream is done with the given header.
***************************************************************************/
void MidiStreamMixer::_Notify(void *pvData, PMidiStreamCached pmdws)
{
    _mutx.Enter();
    Assert(_mix.outstanding > 0, "what buffer is this?");
    auto stream = pmdws ? pmdws->Stream() : &_mix.prefix;
    Assert(pvData == stream->storage.buffers[0].data && stream->storage.buffers[0].locks, "Wrong MIDI buffer");
    Assert(!pmdws || (_mix.voices.count && mm_midi_mix_voice_at(&_mix, 0)->sound == pmdws), "Wrong pmdws");
    auto s = _Services();
    mm_midi_mix_return(&_mix, &s, stream);
    _mutx.Leave();
}

/***************************************************************************
    AT: Timed worker callback for the MidiStreamMixer object.
***************************************************************************/
MMU32 MidiStreamMixer::_ThreadProc(void *pv) noexcept
{
    PMidiStreamMixer pmsmix = (PMidiStreamMixer)pv;

    AssertPo(pmsmix, 0);

    return pmsmix->_ExpireSounds();
}

/***************************************************************************
    AT: Expire non-playing sounds and return the delay until the next check.
***************************************************************************/
std::uint32_t MidiStreamMixer::_ExpireSounds(void)
{
    _mutx.Enter();
    auto s = _Services();
    auto wait = mm_midi_mix_expire(&_mix, &s);
    _mutx.Leave();
    return wait;
}

MMMidiMixServices MidiStreamMixer::_Services()
{
    MMMidiMixServices s = {};
    s.context = this;
    s.clock = [](void *) -> MMU32 { return TsCurrentSystem(); };
    s.enter = [](void *p) { static_cast<MidiStreamMixer *>(p)->_mutx.Enter(); };
    s.leave = [](void *p) { static_cast<MidiStreamMixer *>(p)->_mutx.Leave(); };
    s.stop_device = [](void *p) { static_cast<MidiStreamMixer *>(p)->_pmisi->StopPlaying(); };
    s.wait_return = [](void *p) {
        auto m = static_cast<MidiStreamMixer *>(p);
        m->_mutx.Leave();
        int signaled;
        if (mm_native_event_wait(m->_buffersReturned, 0xffffffffUL, &signaled))
            std::abort();
        m->_mutx.Enter();
    };
    s.signal_return = [](void *p) {
        if (mm_native_event_signal(static_cast<MidiStreamMixer *>(p)->_buffersReturned))
            std::abort();
    };
    s.signal_worker = [](void *p) { mm_midi_worker_signal(static_cast<MidiStreamMixer *>(p)->_worker); };
    s.active = [](void *p) -> int { return static_cast<MidiStreamMixer *>(p)->_pmisi->FActive(); };
    s.activate = [](void *p, int a) -> int { return static_cast<MidiStreamMixer *>(p)->_pmisi->FActivate(a != 0); };
    s.busy = [](void *) { PushErc(ercSndMidiDeviceBusy); };
    s.volume = [](void *p, MMI32 v) { static_cast<MidiStreamMixer *>(p)->_pmisi->SetVlm(v); };
    s.submit = [](void *p, void *data, MMI32 bytes, MMI32 skip, MMI32 repeats, void *sound) -> int {
        return static_cast<MidiStreamMixer *>(p)->_pmisi->FQueueBuffer(data, bytes, skip, repeats,
                                                                       reinterpret_cast<std::uintptr_t>(sound));
    };
    s.notify = [](void *, void *queue, void *sound) {
        static_cast<PMidiStreamQueue>(queue)->Notify(static_cast<PMidiStreamCached>(sound));
    };
    return s;
}

MMMidiQueueServices MidiStreamQueue::_QueueServices()
{
    MMMidiQueueServices s = {};
    s.entries = _Services();
    s.context = this;
    s.clock = [](void *) -> MMU32 { return TsCurrentSystem(); };
    s.start = [](void *p) -> MMU32 { return static_cast<MidiStreamQueue *>(p)->_tsStart; };
    s.set_start = [](void *p, MMU32 t) { static_cast<MidiStreamQueue *>(p)->_tsStart = t; };
    s.play = [](void *p, const MMSoundEntry *e) -> int {
        auto q = static_cast<MidiStreamQueue *>(p);
        if (!e)
            return q->_pmsmix->FPlay(q);
        return q->_pmsmix->FPlay(q, static_cast<PMidiStreamCached>(e->object), e->id, e->priority, e->plays, e->offset,
                                 e->volume);
    };
    return s;
}

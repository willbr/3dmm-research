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
    ReleasePpo(&_pglmev);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MidiStreamCached.
***************************************************************************/
void MidiStreamCached::AssertValid(LegacyUlong grf)
{
    MidiStreamCached_PAR::AssertValid(0);
    AssertPo(_pglmev, 0);
}

/***************************************************************************
    Mark memory for the MidiStreamCached.
***************************************************************************/
void MidiStreamCached::MarkMem(void)
{
    AssertValid(0);
    MidiStreamCached_PAR::MarkMem();
    MarkMemObj(_pglmev);
}
#endif // DEBUG

/***************************************************************************
    A baco reader for a midi stream.
***************************************************************************/
bool MidiStreamCached::FReadMdws(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                                 PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, fblckReadable);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);
    PMidiStreamCached pmdws;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;

    if (!pblck->FUnpackData())
        goto LFail;

    if (pvNil == (pmdws = PmdwsRead(pblck)))
    {
    LFail:
        TrashVar(ppbaco);
        TrashVar(pcb);
        return fFalse;
    }
    *pcb = pmdws->_pglmev->IvMac() * size(MEV) + size(MidiStreamCached);

    *ppbaco = pmdws;
    return fTrue;
}

/***************************************************************************
    Read a midi stream from the given block.
***************************************************************************/
PMidiStreamCached MidiStreamCached::PmdwsRead(PDataBlock pblck)
{
    AssertPo(pblck, 0);

    PMidiStream pmids;
    PMidiStreamCached pmdws;

    if (pvNil == (pmids = MidiStream::PmidsRead(pblck)))
        return pvNil;

    if (pvNil != (pmdws = NewObj MidiStreamCached) && !pmdws->_FInit(pmids))
        ReleasePpo(&pmdws);

    ReleasePpo(&pmids);
    AssertNilOrPo(pmdws, 0);

    return pmdws;
}

/***************************************************************************
    Initialize the MidiStreamCached with the midi data in *pmids.
***************************************************************************/
bool MidiStreamCached::_FInit(PMidiStream pmids)
{
    AssertPo(pmids, 0);
    if (!(_pglmev = DynamicArray::PglNew(size(MEV))))
        return false;
    struct Context
    {
        MidiStreamParser parser;
        PDynamicArray output;
    } context;
    context.parser.Init(pmids, 0);
    context.output = _pglmev;
    ThreeMMMidiStreamBuildServices s = {};
    s.context = &context;
    s.next = [](void *p, ThreeMMMidiEvent *out) -> int {
        MIDEV e;
        auto c = static_cast<Context *>(p);
        if (!c->parser.FGetEvent(&e))
            return 0;
        ClearPb(out, sizeof(*out));
        out->timestamp = e.ts;
        out->count = e.cb;
        out->send[0] = (byte)e.lwSend;
        out->send[1] = (byte)(e.lwSend >> 8);
        out->send[2] = (byte)(e.lwSend >> 16);
        return 1;
    };
    s.append = [](void *p, const MEV *events, ThreeMMI32 count) -> int {
        auto output = static_cast<Context *>(p)->output;
        auto index = output->IvMac();
        if (count > klwMax / (LegacyLong)sizeof(MEV) - index || !output->FSetIvMac(index + count))
            return 0;
        if (count)
            CopyPb(const_cast<PMEV>(events), output->QvGet(index), count * sizeof(MEV));
        return 1;
    };
    ThreeMMU32 duration;
    if (!three_mm_midi_stream_build(&s, &duration))
        return false;
    _pglmev->FEnsureSpace(0, fgrpShrink);
    _dts = duration;
    return true;
}

/***************************************************************************
    Return a locked pointer to the data.
***************************************************************************/
void *MidiStreamCached::PvLockData(LegacyLong *pcb)
{
    AssertThis(0);
    AssertVarMem(pcb);

    *pcb = LwMul(_pglmev->IvMac(), size(MEV));
    return _pglmev->PvLock(0);
}

/***************************************************************************
    Balance a call to PvLockData.
***************************************************************************/
void MidiStreamCached::UnlockData(void)
{
    AssertThis(0);

    _pglmev->Unlock();
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
PBaseCacheableObject MidiStreamQueue::_PbacoFetch(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    AssertPo(prca, 0);

    return prca->PbacoFetch(ctg, cno, &MidiStreamCached::FReadMdws);
}

/***************************************************************************
    An item was added to or deleted from the queue.
***************************************************************************/
void MidiStreamQueue::_Queue(LegacyLong isndinMin)
{
    if (!_pglsndin)
        return;
    auto s = _QueueServices();
    three_mm_midi_queue_run(&s, isndinMin);
}

/***************************************************************************
    One or more items in the queue were paused.
***************************************************************************/
void MidiStreamQueue::_PauseQueue(LegacyLong isndinMin)
{
    if (!_pglsndin)
        return;
    auto s = _QueueServices();
    three_mm_midi_queue_pause(&s, isndinMin);
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
    if (!_pglsndin)
        return;
    auto s = _QueueServices();
    three_mm_midi_queue_notify(&s, pmdws);
}

/***************************************************************************
    Constructor for the midi stream output object.
***************************************************************************/
MidiStreamMixer::MidiStreamMixer(void)
{
    _mix.base_volume = kvlmFull;
    _mix.sound_volume = kvlmFull;
}

/***************************************************************************
    Destructor for the midi stream output object.
***************************************************************************/
MidiStreamMixer::~MidiStreamMixer(void)
{
    Assert(pvNil == _pmisi || !_pmisi->FActive(), "MidiStreamInterface still active!");

    _worker.Stop();

    if (pvNil != _pglmsos)
    {
        Assert(_pglmsos->IvMac() == 0, "MidiStreamMixer still has active sounds");
        ReleasePpo(&_pglmsos);
    }
    ReleasePpo(&_pmisi);
    ReleasePpo(&_pglmevKey);
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
    Initialize the MidiStreamMixer - allocate the pglmsos and the midi stream api
    object.
***************************************************************************/
bool MidiStreamMixer::_FInit(KauaiMidi::HostFactory factory)
{
    AssertBaseThis(0);
    if (!factory)
        return fFalse;

    if (pvNil == (_pglmsos = DynamicArray::PglNew(size(MSOS))))
        return fFalse;
    _pglmsos->SetMinGrow(1);

    if (pvNil == (_pmisi = factory(_MidiProc, reinterpret_cast<std::uintptr_t>(this))))
    {
        return fFalse;
    }

    return _worker.Start(_ThreadProc, this);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MidiStreamMixer.
***************************************************************************/
void MidiStreamMixer::AssertValid(LegacyUlong grf)
{
    MidiStreamMixer_PAR::AssertValid(0);
    _mutx.Enter();
    Assert(_worker.Running(), "no MIDI expiration worker");
    AssertPo(_pglmsos, 0);
    AssertPo(_pmisi, 0);
    AssertNilOrPo(_pglmevKey, 0);
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
    MarkMemObj(_pglmsos);
    MarkMemObj(_pmisi);
    MarkMemObj(_pglmevKey);
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
    three_mm_midi_mix_suspend(&_mix, &s, fSuspend);
    _mutx.Leave();
}

/***************************************************************************
    If we're currently playing a midi stream stop it. Assumes the mutx is
    already checked out exactly once.
***************************************************************************/
void MidiStreamMixer::_StopStream(void)
{
    auto s = _Services();
    three_mm_midi_mix_stop(&_mix, &s);
}

/***************************************************************************
    Set the volume for the midi stream output device.
***************************************************************************/
void MidiStreamMixer::SetVlm(LegacyLong vlm)
{
    _mutx.Enter();
    auto s = _Services();
    three_mm_midi_mix_volume(&_mix, &s, vlm);
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
    ThreeMMMidiMixVoice v = {pmsque, pmdws, sii, spr, cactPlay, 0, dtsStart, vlm, 0};
    _mutx.Enter();
    auto s = _Services();
    bool ok = three_mm_midi_mix_play(&_mix, &s, &v) != 0;
    _mutx.Leave();
    return ok;
}

/***************************************************************************
    The sound list changed so make sure we're playing the first tune.
    Assumes the mutx is already checked out.
***************************************************************************/
void MidiStreamMixer::_Restart(bool fNew)
{
    auto s = _Services();
    three_mm_midi_mix_restart(&_mix, &s, fNew);
}

/***************************************************************************
    Submit the buffer(s) for the current MSOS. Assumes the mutx is already
    checked out.
***************************************************************************/
void MidiStreamMixer::_SubmitBuffers(LegacyUlong tsCur)
{
    auto s = _Services();
    three_mm_midi_mix_submit(&_mix, &s, tsCur);
}

/***************************************************************************
    Seek into the pmdws the given amount of time, and accumulate key events
    in _pglmevKey.
***************************************************************************/
bool MidiStreamMixer::_FGetKeyEvents(PMidiStreamCached pmdws, LegacyUlong dtsSeek, LegacyLong *pcbSkip)
{
    if (!_pglmevKey && !(_pglmevKey = DynamicArray::PglNew(size(MEV))))
        return false;
    _pglmevKey->FSetIvMac(0);
    LegacyLong bytes;
    auto events = static_cast<const MEV *>(pmdws->PvLockData(&bytes));
    ThreeMMI32 skip;
    const bool ok = three_mm_midi_stream_seek(
                        events, bytes / sizeof(MEV), dtsSeek, _pglmevKey,
                        [](void *p, const MEV *events, ThreeMMI32 count) -> int {
                            auto output = static_cast<PDynamicArray>(p);
                            auto old = output->IvMac();
                            if (count > klwMax / (LegacyLong)sizeof(MEV) - old || !output->FSetIvMac(old + count))
                                return 0;
                            auto data = static_cast<PMEV>(output->QvGet(0));
                            BltPb(data, data + count, old * sizeof(MEV));
                            CopyPb(const_cast<PMEV>(events), data, count * sizeof(MEV));
                            return 1;
                        },
                        &skip) != 0;
    if (!ok)
        _pglmevKey->FSetIvMac(0);
    _pglmevKey->FEnsureSpace(0, fgrpShrink);
    pmdws->UnlockData();
    if (ok)
        *pcbSkip = skip;
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
    Assert(pmdws || (_pglmevKey && pvData == _pglmevKey->QvGet(0)), "Bad pvData/pmdws combo");
    Assert(!pmdws || (_pglmsos->IvMac() > 0 && static_cast<MSOS *>(_pglmsos->QvGet(0))->pmdws == pmdws), "Wrong pmdws");
    auto s = _Services();
    three_mm_midi_mix_return(&_mix, &s, pmdws);
    _mutx.Leave();
}

/***************************************************************************
    AT: Timed worker callback for the MidiStreamMixer object.
***************************************************************************/
std::uint32_t MidiStreamMixer::_ThreadProc(void *pv)
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
    auto wait = three_mm_midi_mix_expire(&_mix, &s);
    _mutx.Leave();
    return wait;
}

ThreeMMMidiMixServices MidiStreamMixer::_Services()
{
    ThreeMMMidiMixServices s = {};
    s.context = this;
    s.count = [](void *p) -> ThreeMMI32 { return static_cast<MidiStreamMixer *>(p)->_pglmsos->IvMac(); };
    s.get = [](void *p, ThreeMMI32 i, ThreeMMMidiMixVoice *out) {
        MSOS v;
        static_cast<MidiStreamMixer *>(p)->_pglmsos->Get(i, &v);
        *out = {v.pmsque, v.pmdws, v.sii, v.spr, v.cactPlay, v.dts, v.dtsStart, v.vlm, v.tsStart};
    };
    s.put = [](void *p, ThreeMMI32 i, const ThreeMMMidiMixVoice *in) {
        MSOS v = {static_cast<PMidiStreamQueue>(in->queue),
                  static_cast<PMidiStreamCached>(in->sound),
                  in->id,
                  in->priority,
                  in->plays,
                  in->duration,
                  in->offset,
                  in->volume,
                  in->start};
        static_cast<MidiStreamMixer *>(p)->_pglmsos->Put(i, &v);
    };
    s.insert = [](void *p, ThreeMMI32 i, const ThreeMMMidiMixVoice *in) -> int {
        MSOS v = {static_cast<PMidiStreamQueue>(in->queue),
                  static_cast<PMidiStreamCached>(in->sound),
                  in->id,
                  in->priority,
                  in->plays,
                  in->duration,
                  in->offset,
                  in->volume,
                  in->start};
        return static_cast<MidiStreamMixer *>(p)->_pglmsos->FInsert(i, &v);
    };
    s.erase = [](void *p, ThreeMMI32 i) { static_cast<MidiStreamMixer *>(p)->_pglmsos->Delete(i); };
    s.rotate = [](void *p, ThreeMMI32 n) {
        auto list = static_cast<MidiStreamMixer *>(p)->_pglmsos;
        SwapBlocks(list->QvGet(0), n * sizeof(MSOS), (list->IvMac() - n) * sizeof(MSOS));
    };
    s.clock = [](void *) -> ThreeMMU32 { return TsCurrentSystem(); };
    s.duration = [](void *, void *p) -> ThreeMMU32 { return static_cast<PMidiStreamCached>(p)->Dts(); };
    s.enter = [](void *p) { static_cast<MidiStreamMixer *>(p)->_mutx.Enter(); };
    s.leave = [](void *p) { static_cast<MidiStreamMixer *>(p)->_mutx.Leave(); };
    s.stop_device = [](void *p) { static_cast<MidiStreamMixer *>(p)->_pmisi->StopPlaying(); };
    s.wait_return = [](void *p) {
        auto m = static_cast<MidiStreamMixer *>(p);
        m->_mutx.Leave();
        m->_buffersReturned.Wait();
        m->_mutx.Enter();
    };
    s.signal_return = [](void *p) { static_cast<MidiStreamMixer *>(p)->_buffersReturned.Signal(); };
    s.signal_worker = [](void *p) { static_cast<MidiStreamMixer *>(p)->_worker.Signal(); };
    s.active = [](void *p) -> int { return static_cast<MidiStreamMixer *>(p)->_pmisi->FActive(); };
    s.activate = [](void *p, int a) -> int { return static_cast<MidiStreamMixer *>(p)->_pmisi->FActivate(a != 0); };
    s.busy = [](void *) { PushErc(ercSndMidiDeviceBusy); };
    s.volume = [](void *p, ThreeMMI32 v) { static_cast<MidiStreamMixer *>(p)->_pmisi->SetVlm(v); };
    s.seek = [](void *p, void *sound, ThreeMMU32 time, ThreeMMI32 *skip) -> int {
        LegacyLong bytes;
        if (!static_cast<MidiStreamMixer *>(p)->_FGetKeyEvents(static_cast<PMidiStreamCached>(sound), time, &bytes))
            return 0;
        *skip = bytes;
        return 1;
    };
    s.lock_key = [](void *p, ThreeMMI32 *bytes) -> void * {
        auto list = static_cast<MidiStreamMixer *>(p)->_pglmevKey;
        *bytes = list->IvMac() * sizeof(MEV);
        return *bytes > 0 ? list->PvLock(0) : nullptr;
    };
    s.unlock_key = [](void *p) { static_cast<MidiStreamMixer *>(p)->_pglmevKey->Unlock(); };
    s.lock_sound = [](void *, void *p, ThreeMMI32 *bytes) -> void * {
        LegacyLong n;
        auto data = static_cast<PMidiStreamCached>(p)->PvLockData(&n);
        *bytes = n;
        return data;
    };
    s.unlock_sound = [](void *, void *p) { static_cast<PMidiStreamCached>(p)->UnlockData(); };
    s.submit = [](void *p, void *data, ThreeMMI32 bytes, ThreeMMI32 skip, ThreeMMI32 repeats, void *sound) -> int {
        return static_cast<MidiStreamMixer *>(p)->_pmisi->FQueueBuffer(data, bytes, skip, repeats,
                                                                       reinterpret_cast<std::uintptr_t>(sound));
    };
    s.notify = [](void *, void *queue, void *sound) {
        static_cast<PMidiStreamQueue>(queue)->Notify(static_cast<PMidiStreamCached>(sound));
    };
    return s;
}

ThreeMMMidiQueueServices MidiStreamQueue::_QueueServices()
{
    ThreeMMMidiQueueServices s = {};
    s.entries = _Services();
    s.context = this;
    s.clock = [](void *) -> ThreeMMU32 { return TsCurrentSystem(); };
    s.start = [](void *p) -> ThreeMMU32 { return static_cast<MidiStreamQueue *>(p)->_tsStart; };
    s.set_start = [](void *p, ThreeMMU32 t) { static_cast<MidiStreamQueue *>(p)->_tsStart = t; };
    s.play = [](void *p, const ThreeMMSoundEntry *e) -> int {
        auto q = static_cast<MidiStreamQueue *>(p);
        if (!e)
            return q->_pmsmix->FPlay(q);
        return q->_pmsmix->FPlay(q, static_cast<PMidiStreamCached>(e->object), e->id, e->priority, e->plays, e->offset,
                                 e->volume);
    };
    return s;
}

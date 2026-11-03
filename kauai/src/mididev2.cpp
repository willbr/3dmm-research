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

    _Suspend(_cactSuspend > 0 || !_fActive);

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
bool MidiStreamCached::FReadMdws(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
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

    MidiStreamParser mstp;
    LegacyUlong tsCur;
    MEV rgmev[100];
    PMEV pmev, pmevLim;
    MIDEV midev;
    bool fEvt;

    if (pvNil == (_pglmev = DynamicArray::PglNew(size(MEV))))
        return fFalse;

    Assert(KauaiMidi::shortMessage == 0, "this code assumes KauaiMidi::shortMessage is 0 and it's not");

    ClearPb(rgmev, size(rgmev));
    pmev = rgmev;
    pmevLim = rgmev + CvFromRgv(rgmev);

    // use 1 second per quarter. We'll use 1000 ticks per quarter when
    // setting up the stream. The net result is that milliseconds correspond
    // to ticks, so no conversion is necessary here.
    pmev->eventWord = ((LegacyUlong)KauaiMidi::tempo << 24) | 1000000;
    pmev++;

    mstp.Init(pmids, 0);
    tsCur = 0;
    for (;;)
    {
        fEvt = mstp.FGetEvent(&midev);
        if (pmev >= pmevLim || !fEvt)
        {
            // append the MEVs in rgmev to the _pglmev
            LegacyLong imev, cmev;

            imev = _pglmev->IvMac();
            cmev = pmev - rgmev;
            if (!_pglmev->FSetIvMac(imev + cmev))
                return fFalse;
            CopyPb(rgmev, _pglmev->QvGet(imev), LwMul(cmev, size(MEV)));
            if (!fEvt)
            {
                // Add a final NOP so when we seek and there's only one
                // event left, it's not an important one.
                rgmev[0].deltaTicks = 0;
                rgmev[0].streamId = 0;
                rgmev[0].eventWord = (LegacyUlong)KauaiMidi::noOperation << 24;
                if (!_pglmev->FAdd(&rgmev[0]))
                    return fFalse;

                _pglmev->FEnsureSpace(0, fgrpShrink);
                break;
            }
            pmev = rgmev;
        }

        if (midev.cb > 0)
        {
            pmev->deltaTicks = midev.ts - tsCur;
            pmev->eventWord = midev.lwSend & 0x00FFFFFF;
            pmev++;
            tsCur = midev.ts;
        }
    }

    _dts = tsCur;
    return fTrue;
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
    AssertThis(0);
    SNDIN sndin;

    _mutx.Enter();

    if (_isndinCur == isndinMin && pvNil != _pglsndin)
    {
        for (; _isndinCur < _pglsndin->IvMac(); _isndinCur++)
        {
            _pglsndin->Get(_isndinCur, &sndin);
            if (0 < sndin.cactPause)
                break;

            if (0 == sndin.cactPause && _pmsmix->FPlay(this, (PMidiStreamCached)sndin.pbaco, sndin.sii, sndin.spr, sndin.cactPlay,
                                                       sndin.dtsStart, sndin.vlm))
            {
                _tsStart = TsCurrentSystem() - sndin.dtsStart;
                goto LDone;
            }
        }

        _pmsmix->FPlay(this);
    }

LDone:
    _mutx.Leave();
}

/***************************************************************************
    One or more items in the queue were paused.
***************************************************************************/
void MidiStreamQueue::_PauseQueue(LegacyLong isndinMin)
{
    AssertThis(0);
    SNDIN sndin;

    _mutx.Enter();

    if (_isndinCur == isndinMin && _pglsndin->IvMac() > _isndinCur)
    {
        _pglsndin->Get(_isndinCur, &sndin);
        sndin.dtsStart = TsCurrentSystem() - _tsStart;
        _pglsndin->Put(_isndinCur, &sndin);

        _Queue(isndinMin);
    }

    _mutx.Leave();
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
    AssertThis(0);
    SNDIN sndin;

    _mutx.Enter();

    if (pvNil != _pglsndin && _pglsndin->IvMac() > _isndinCur)
    {
        _pglsndin->Get(_isndinCur, &sndin);
        if (pmdws == sndin.pbaco)
        {
            _isndinCur++;
            _Queue(_isndinCur);
        }
    }

    _mutx.Leave();
}

/***************************************************************************
    Constructor for the midi stream output object.
***************************************************************************/
MidiStreamMixer::MidiStreamMixer(void)
{
    _vlmBase = kvlmFull;
    _vlmSound = kvlmFull;
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
    AssertThis(0);

    _mutx.Enter();

    if (fSuspend)
        _StopStream();
    if (!_pmisi->FActivate(!fSuspend) && !fSuspend)
        PushErc(ercSndMidiDeviceBusy);
    _Restart();

    _mutx.Leave();
}

/***************************************************************************
    If we're currently playing a midi stream stop it. Assumes the mutx is
    already checked out exactly once.
***************************************************************************/
void MidiStreamMixer::_StopStream(void)
{
    AssertThis(0);

    if (!_fPlaying)
        return;

    // set _fPlaying to false first so the call back knows that we're
    // aborting the current stream - so it doesn't notify us.
    _fPlaying = fFalse;

    _pmisi->StopPlaying();

    // Wait for the buffers to be returned
    _fWaiting = fTrue;
    while (_cpvOut > 0)
    {
        _mutx.Leave();
        _buffersReturned.Wait();
        _mutx.Enter();
    }
    _fWaiting = fFalse;
}

/***************************************************************************
    Set the volume for the midi stream output device.
***************************************************************************/
void MidiStreamMixer::SetVlm(LegacyLong vlm)
{
    AssertThis(0);
    LegacyUlong luHigh, luLow;

    _mutx.Enter();

    _vlmBase = vlm;
    MulLu(_vlmBase, _vlmSound, &luHigh, &luLow);
    _pmisi->SetVlm(LuHighLow(SuLow(luHigh), SuHigh(luLow)));

    _mutx.Leave();
}

/***************************************************************************
    Get the current volume.
***************************************************************************/
LegacyLong MidiStreamMixer::VlmCur(void)
{
    AssertThis(0);

    return _vlmBase;
}

/***************************************************************************
    Play the given midi stream from the indicated queue.
***************************************************************************/
bool MidiStreamMixer::FPlay(PMidiStreamQueue pmsque, PMidiStreamCached pmdws, LegacyLong sii, LegacyLong spr, LegacyLong cactPlay, LegacyUlong dtsStart, LegacyLong vlm)
{
    AssertThis(0);
    AssertPo(pmsque, 0);
    AssertNilOrPo(pmdws, 0);
    LegacyLong imsos;
    MSOS msos;
    bool fNew = fFalse;
    bool fRet = fTrue;

    if (pvNil != pmdws && pmdws->Dts() == 0)
        return fFalse;

    _mutx.Enter();

    // stop any current midi stream on this msque
    for (imsos = _pglmsos->IvMac(); imsos-- > 0;)
    {
        _pglmsos->Get(imsos, &msos);
        if (msos.pmsque == pmsque)
        {
            if (0 == imsos)
                _StopStream();
            _pglmsos->Get(imsos, &msos);
            _pglmsos->Delete(imsos);
            break;
        }
    }

    // start up the new midi stream
    if (pvNil != pmdws)
    {
        // find the position to insert the new one
        for (imsos = 0; imsos < _pglmsos->IvMac(); imsos++)
        {
            _pglmsos->Get(imsos, &msos);
            if (msos.spr < spr || msos.spr == spr && msos.sii < sii)
            {
                // insert before the current one
                break;
            }
        }

        if (0 == imsos)
        {
            fNew = fTrue;
            _StopStream();
        }

        ClearPb(&msos, size(msos));
        msos.pmsque = pmsque;
        msos.pmdws = pmdws;
        msos.sii = sii;
        msos.spr = spr;
        msos.cactPlay = cactPlay;
        msos.dts = pmdws->Dts();
        msos.dtsStart = dtsStart;
        msos.vlm = vlm;
        msos.tsStart = TsCurrentSystem() - msos.dtsStart;

        if (!_pglmsos->FInsert(imsos, &msos))
        {
            fRet = fFalse;
            fNew = fFalse;
        }
    }

    _Restart(fNew);

    _mutx.Leave();
    return fRet;
}

/***************************************************************************
    The sound list changed so make sure we're playing the first tune.
    Assumes the mutx is already checked out.
***************************************************************************/
void MidiStreamMixer::_Restart(bool fNew)
{
    AssertThis(0);

    if (_pmisi->FActive() && !_fPlaying && _pglmsos->IvMac() > 0)
    {
        // start playing the first MSOS
        MSOS msos;
        LegacyUlong tsCur = TsCurrentSystem();

        if (fNew)
        {
            _pglmsos->Get(0, &msos);
            msos.tsStart = tsCur - msos.dtsStart;
            _pglmsos->Put(0, &msos);
        }
        _SubmitBuffers(tsCur);
    }

    // signal the aux thread that the list changed
    _worker.Signal();
}

/***************************************************************************
    Submit the buffer(s) for the current MSOS. Assumes the mutx is already
    checked out.
***************************************************************************/
void MidiStreamMixer::_SubmitBuffers(LegacyUlong tsCur)
{
    Assert(!_fPlaying, "already playing!");
    LegacyLong cb, cbSkip;
    MSOS msos;
    LegacyLong imsos;
    void *pvData;
    LegacyLong cactSkip;

    for (imsos = 0; imsos < _pglmsos->IvMac(); imsos++)
    {
        _pglmsos->Get(imsos, &msos);

        cactSkip = (tsCur - msos.tsStart) / msos.dts;
        if (cactSkip > 0)
        {
            LegacyUlong dtsSeek;

            // we need to skip at least one loop of this sound
            if (msos.cactPlay > 0 && (msos.cactPlay -= cactSkip) <= 0)
                goto LTryNext;

            dtsSeek = (tsCur - msos.tsStart) % msos.dts;
            msos.tsStart = tsCur - dtsSeek;
            _pglmsos->Put(imsos, &msos);
        }

        // Calling SetVlm causes us to tell the MidiStreamInterface about the new volume
        _vlmSound = msos.vlm;
        SetVlm(_vlmBase);

        cbSkip = 0;
        if (tsCur != msos.tsStart)
        {
            // have to seek into the stream
            if (!_FGetKeyEvents(msos.pmdws, tsCur - msos.tsStart, &cbSkip))
                goto LTryNext;

            cb = LwMul(_pglmevKey->IvMac(), size(MEV));
            if (0 < cb)
            {
                pvData = _pglmevKey->PvLock(0);
                if (!_pmisi->FQueueBuffer(pvData, cb, 0, 1, 0))
                {
                    // streaming the key events failed
                    _pglmevKey->Unlock();
                    goto LTryNext;
                }

                _cpvOut++;
                _fPlaying = fTrue;
            }
        }

        _cpvOut++;
        pvData = msos.pmdws->PvLockData(&cb);
        if (_pmisi->FQueueBuffer(pvData, cb, cbSkip, msos.cactPlay, (std::uintptr_t)msos.pmdws))
        {
            // it worked!
            _fPlaying = fTrue;
            break;
        }

        // submitting the buffer failed
        msos.pmdws->UnlockData();
        _cpvOut--;

    LTryNext:
        // make this one disappear
        // stop the seek buffer from playing
        _StopStream();

        // make this MSOS have lowest possible priority and 0 time
        // remaining to play - we'll move it to the end of _pglmsos
        // in the code below.
        msos.tsStart = tsCur - msos.dtsStart;
        msos.cactPlay = 1;
        msos.sii = klwMin;
        msos.spr = klwMin;
        _pglmsos->Put(imsos, &msos);
    }

    if (_fPlaying && imsos > 0)
    {
        // move the skipped ones to the end of the list
        LegacyLong cmsos = _pglmsos->IvMac();

        AssertIn(imsos, 1, cmsos);
        SwapBlocks(_pglmsos->QvGet(0), LwMul(imsos, size(MSOS)), LwMul(cmsos - imsos, size(MSOS)));
    }
}

/***************************************************************************
    Seek into the pmdws the given amount of time, and accumulate key events
    in _pglmevKey.
***************************************************************************/
bool MidiStreamMixer::_FGetKeyEvents(PMidiStreamCached pmdws, LegacyUlong dtsSeek, LegacyLong *pcbSkip)
{
    AssertPo(pmdws, 0);
    AssertVarMem(pcbSkip);

    // This keeps track of which events we've seen (so we only record the
    // most recent one. We record tempo changes, program changes and
    // controller changes.
    struct MKEY
    {
        ushort grfbitProgram;
        ushort grfbitChannelPressure;
        ushort grfbitPitchWheel;
        ushort fTempo : 1;

        ushort rggrfbitControl[120];
    };

    MKEY mkey;
    PMEV pmev;
    PMEV pmevMin;
    PMEV pmevLim;

    MEV rgmev[100];
    PMEV pmevDst;
    PMEV pmevLimDst;

    LegacyLong cb;
    LegacyUlong dts;
    LegacyLong igrfbit;
    ushort fbit;
    ushort *pgrfbit;
    byte bT;

    ClearPb(&mkey, size(mkey));
    ClearPb(rgmev, size(rgmev));

    if (pvNil == _pglmevKey && (pvNil == (_pglmevKey = DynamicArray::PglNew(size(MEV)))))
        return fFalse;
    _pglmevKey->FSetIvMac(0);

    pmevMin = (PMEV)pmdws->PvLockData(&cb);
    cb = LwRoundToward(cb, size(MEV));
    pmevLim = (PMEV)PvAddBv(pmevMin, cb);

    dts = 0;
    for (pmev = pmevMin; pmev < pmevLim; pmev++)
    {
        dts += pmev->deltaTicks;
        if (dts >= dtsSeek)
            break;
    }

    if (pmev + 1 >= pmevLim)
    {
        // dtsSeek goes past the end!
        goto LFail;
    }

    // get the destination pointer - this walks backwards
    pmevLimDst = rgmev + CvFromRgv(rgmev);
    pmevDst = pmevLimDst;

    // put the first event in the key frame list with a smaller time
    --pmevDst;
    pmevDst->deltaTicks = dts - dtsSeek;
    pmevDst->eventWord = pmev->eventWord;
    *pcbSkip = BvSubPvs(pmev + 1, pmevMin);

    for (;;)
    {
        if (pmevDst <= rgmev || pmev <= pmevMin)
        {
            // destination buffer is full - write it out
            LegacyLong cmev, cmevNew;
            PMEV qrgmev;

            cmev = _pglmevKey->IvMac();
            cmevNew = pmevLimDst - pmevDst;

            if (!_pglmevKey->FSetIvMac(cmev + cmevNew))
            {
            LFail:
                _pglmevKey->FSetIvMac(0);
                _pglmevKey->FEnsureSpace(0, fgrpShrink);
                pmdws->UnlockData();
                return fFalse;
            }

            qrgmev = (PMEV)_pglmevKey->QvGet(0);
            BltPb(qrgmev, qrgmev + cmevNew, LwMul(cmev, size(MEV)));
            CopyPb(pmevDst, qrgmev, LwMul(cmevNew, size(MEV)));

            if (pmev <= pmevMin)
                break;

            pmevDst = pmevLimDst;
        }

        --pmev;
        switch (pmev->eventWord >> 24)
        {
        case KauaiMidi::shortMessage:
            bT = (byte)pmev->eventWord;

            // The high nibble of bT is the status value
            // The low nibble is the channel
            switch (bT & 0xF0)
            {
            case 0xB0: // control change
                igrfbit = BHigh(SuLow(pmev->eventWord));
                if (!FIn(igrfbit, 0, CvFromRgv(mkey.rggrfbitControl)))
                    break;
                pgrfbit = &mkey.rggrfbitControl[igrfbit];
                goto LTest;

            case 0xC0: // program change
                pgrfbit = &mkey.grfbitProgram;
                goto LTest;

            case 0xD0: // channel pressure
                pgrfbit = &mkey.grfbitChannelPressure;
                goto LTest;

            case 0xE0: // pitch wheel
                pgrfbit = &mkey.grfbitPitchWheel;
            LTest:
                fbit = 1 << (bT & 0x0F);
                if (!(*pgrfbit & fbit))
                {
                    // first time we've seen this event on this channel
                    *pgrfbit |= fbit;
                    goto LCopy;
                }
                break;
            }
            break;

        case KauaiMidi::tempo:
            if (!mkey.fTempo)
            {
                mkey.fTempo = fTrue;
            LCopy:
                pmevDst--;
                pmevDst->deltaTicks = 0;
                pmevDst->eventWord = pmev->eventWord;
            }
            break;
        }
    }

    _pglmevKey->FEnsureSpace(0, fgrpShrink);
    pmdws->UnlockData();
    return fTrue;
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
    AssertNilOrPo(pmdws, 0);
    MSOS msos;

    _mutx.Enter();

    Assert(_cpvOut > 0, "what buffer is this?");
    _cpvOut--;
    if (_cpvOut == 0)
        _buffersReturned.Signal();
    if (pvNil != pmdws)
    {
        AssertVar(_pglmsos->IvMac() > 0 && ((MSOS *)_pglmsos->QvGet(0))->pmdws == pmdws, "Wrong pmdws", &pmdws);
        pmdws->UnlockData();
    }
    else if (pvNil != _pglmevKey && pvData == _pglmevKey->QvGet(0))
        _pglmevKey->Unlock();
    else
    {
        Bug("Bad pvData/pmdws combo");
    }

    if (!_fPlaying)
    {
        // we don't need to notify or start the next sound.
        _mutx.Leave();
        return;
    }

    if (_fPlaying && _cpvOut == 0)
    {
        // all headers are in and we're supposed to be playing - so notify the
        // previous pmdws and start up the next one.
        _fPlaying = fFalse;
        if (0 < _pglmsos->IvMac())
        {
            _pglmsos->Get(0, &msos);
            _pglmsos->Delete(0);

            _mutx.Leave();

            // do the notify
            msos.pmsque->Notify(msos.pmdws);

            _mutx.Enter();
        }

        if (_pglmsos->IvMac() > 0)
            _Restart();
    }

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
    AssertThis(0);
    LegacyUlong tsCur;
    LegacyLong imsos;
    MSOS msos;
    LegacyLong cactSkip;
    LegacyUlong dtsNextStop = kluMax;

    _mutx.Enter();

    if (_fWaiting)
    {
        // we're waiting for buffers to be returned, so don't touch
        // anything!
        dtsNextStop = 1;
    }
    else
    {
        // See if any sounds have expired...
        tsCur = TsCurrentSystem();
        dtsNextStop = kluMax;
        for (imsos = _pglmsos->IvMac(); imsos-- > 0;)
        {
            if (imsos == 0 && _fPlaying)
                break;
            _pglmsos->Get(imsos, &msos);

            cactSkip = (tsCur - msos.tsStart) / msos.dts;
            if (cactSkip > 0)
            {
                LegacyUlong dtsSeek;

                if (msos.cactPlay > 0 && (msos.cactPlay -= cactSkip) <= 0)
                {
                    // this sound is done
                    _pglmsos->Delete(imsos);
                    _mutx.Leave();

                    // do the notify
                    msos.pmsque->Notify(msos.pmdws);

                    _mutx.Enter();
                    dtsNextStop = 0;
                    break;
                }

                // adjust the values in the MSOS
                dtsSeek = (tsCur - msos.tsStart) % msos.dts;
                msos.tsStart = tsCur - dtsSeek;
                _pglmsos->Put(imsos, &msos);
            }

            dtsNextStop = LuMin(dtsNextStop, msos.dts - (tsCur - msos.tsStart));
        }
    }

    _mutx.Leave();
    return dtsNextStop;
}

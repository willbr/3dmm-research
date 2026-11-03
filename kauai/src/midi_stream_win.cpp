/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_stream_host.h"
#include "frame.h"
#include "midi_stream_win.h"
ASSERTNAME
RTCLASS(MidiStreamInterface)
RTCLASS(WindowsMidiStream)
RTCLASS(OurMidiStream)
const long kdtsMinSlip = kdtsSecond / 30;
const long klwInfinite = klwMax;
const long kcbMaxWmsBuffer = 0x0000FFFF / size(MEV) * size(MEV);
KauaiMidi::StreamHost *KauaiMidi::CreateStreamHost(Completion completion, std::uintptr_t user)
{
    if (auto host = WindowsMidiStream::PwmsNew(completion, user))
        return host;
    return OurMidiStream::PomsNew(completion, user);
}

/***************************************************************************
    Constructor for the MIDI stream interface.
***************************************************************************/
MidiStreamInterface::MidiStreamInterface(PFNMIDI pfn, std::uintptr_t luUser)
{
    AssertBaseThis(0);
    Assert(pvNil != pfn, 0);

    _pfnCall = pfn;
    _luUser = luUser;

    _tBogusDriver = tMaybe;
    _luVolSys = (ulong)(-1);
    _vlmBase = kvlmFull;
}

/***************************************************************************
    Reset the midi device.
***************************************************************************/
void MidiStreamInterface::_Reset(void)
{
    Assert(hNil != _hms, 0);
    long iv;

    midiOutReset(_hms);

    // Reset channel pressure and pitch wheel on all channels.
    // We shouldn't have to do this, but some drivers don't reset these.
    for (iv = 0; iv < 16; iv++)
    {
        midiOutShortMsg(_hms, 0xD0 | iv);
        midiOutShortMsg(_hms, 0x004000E0 | iv);
    }
}

/***************************************************************************
    Get the system volume level.
***************************************************************************/
void MidiStreamInterface::_GetSysVol(void)
{
    Assert(hNil != _hms, "calling _GetSysVol with nil _hms");
    ulong lu0, lu1, lu2;

    switch (_tBogusDriver)
    {
    case tYes:
        // just use vluSysVolFake...
        _luVolSys = vluSysVolFake;
        return;

    case tMaybe:
        // need to determine if midiOutGetVolume really works for this
        // driver.

        // Some drivers will only ever tell us what we last gave them -
        // irregardless of what the user has set the value to. Those drivers
        // will always give us full volume the first time we ask.

        // We also look for drivers that give us nonsense values.

        if (0 != midiOutGetVolume(_hms, &_luVolSys) || _luVolSys == ULONG_MAX || 0 != midiOutSetVolume(_hms, 0ul) ||
            0 != midiOutGetVolume(_hms, &lu0) || 0 != midiOutSetVolume(_hms, 0x7FFF7FFFul) ||
            0 != midiOutGetVolume(_hms, &lu1) || 0 != midiOutSetVolume(_hms, 0xFFFFFFFFul) ||
            0 != midiOutGetVolume(_hms, &lu2) || lu0 >= lu1 || lu1 >= lu2)
        {
            _tBogusDriver = tYes;
            _luVolSys = vluSysVolFake;
        }
        else
        {
            _tBogusDriver = tNo;
            vluSysVolFake = _luVolSys;
        }
        midiOutSetVolume(_hms, _luVolSys);
        break;

    default:
        if (0 != midiOutGetVolume(_hms, &_luVolSys))
        {
            // failed - use the fake value
            _luVolSys = vluSysVolFake;
        }
        else
            vluSysVolFake = _luVolSys;
        break;
    }
}

/***************************************************************************
    Set the system volume level.
***************************************************************************/
void MidiStreamInterface::_SetSysVol(ulong luVol)
{
    Assert(hNil != _hms, "calling _SetSysVol with nil _hms");
    midiOutSetVolume(_hms, luVol);
}

/***************************************************************************
    Set the system volume level from the current values of _vlmBase
    and _luVolSys. We set the system volume to the result of scaling
    _luVolSys by _vlmBase.
***************************************************************************/
void MidiStreamInterface::_SetSysVlm(void)
{
    ulong luVol;

    luVol = LuVolScale(_luVolSys, _vlmBase);
    _SetSysVol(luVol);
}

/***************************************************************************
    Set the volume for the midi stream output device.
***************************************************************************/
void MidiStreamInterface::SetVlm(long vlm)
{
    AssertThis(0);

    if (vlm != _vlmBase)
    {
        _vlmBase = vlm;
        if (hNil != _hms)
            _SetSysVlm();
    }
}

/***************************************************************************
    Get the current volume.
***************************************************************************/
long MidiStreamInterface::VlmCur(void)
{
    AssertThis(0);

    return _vlmBase;
}

/***************************************************************************
    Return whether the midi stream output device is active.
***************************************************************************/
bool MidiStreamInterface::FActive(void)
{
    return hNil != _hms;
}

/***************************************************************************
    Activate or deactivate the Midi stream output object.
***************************************************************************/
bool MidiStreamInterface::FActivate(bool fActivate)
{
    AssertThis(0);

    return fActivate ? _FOpen() : _FClose();
}

/***************************************************************************
    Constructor for the Win95 Midi stream class.
***************************************************************************/
WindowsMidiStream::WindowsMidiStream(PFNMIDI pfn, std::uintptr_t luUser) : MidiStreamInterface(pfn, luUser)
{
}

/***************************************************************************
    Destructor for the Win95 Midi stream class.
***************************************************************************/
WindowsMidiStream::~WindowsMidiStream(void)
{
    if (hNil != _hth)
    {
        // tell the thread to end and wait for it to finish
        _fDone = fTrue;
        SetEvent(_hevt);
        WaitForSingleObject(_hth, INFINITE);
    }

    if (hNil != _hevt)
        CloseHandle(_hevt);

    if (pvNil != _pglpmsir)
    {
        Assert(0 == _pglpmsir->IvMac(), "WindowsMidiStream still has some active buffers");
        ReleasePpo(&_pglpmsir);
    }
    if (hNil != _hlib)
    {
        FreeLibrary(_hlib);
        _hlib = hNil;
    }
}

/***************************************************************************
    Create a new WindowsMidiStream.
***************************************************************************/
PWindowsMidiStream WindowsMidiStream::PwmsNew(PFNMIDI pfn, std::uintptr_t luUser)
{
    PWindowsMidiStream pwms;

    if (pvNil == (pwms = NewObj WindowsMidiStream(pfn, luUser)))
        return pvNil;

    if (!pwms->_FInit())
        ReleasePpo(&pwms);

    return pwms;
}

/***************************************************************************
    Initialize the WindowsMidiStream: get the addresses of the stream API.
***************************************************************************/
bool WindowsMidiStream::_FInit(void)
{
    OSVERSIONINFO osv;
    ulong luThread;

    // Make sure we're on Win95 and not NT, since the API exists on NT 3.51
    // but it fails.
    osv.dwOSVersionInfoSize = size(osv);
    if (!GetVersionEx(&osv))
        return fFalse;

// Old header files don't have this defined!
#ifndef VER_PLATFORM_WIN32_WINDOWS
#define VER_PLATFORM_WIN32_WINDOWS 1
#endif //! VER_PLATFORM_WIN32_WINDOWS

    if (VER_PLATFORM_WIN32_WINDOWS != osv.dwPlatformId)
    {
        // don't bother trying - NT's scheduler works fine anyway.
        return fFalse;
    }

    if (hNil == (_hlib = LoadLibrary(PszLit("WINMM.DLL"))))
        return fFalse;

#define _Get(n)                                                                                                        \
    if (pvNil == (*(void **)&_pfn##n = (void *)GetProcAddress(_hlib, "midiStream" #n)))                                \
    {                                                                                                                  \
        return fFalse;                                                                                                 \
    }

    _Get(Open);
    _Get(Close);
    _Get(Property);
    _Get(Position);
    _Get(Out);
    _Get(Pause);
    _Get(Restart);
    _Get(Stop);

#undef _Get

    if (pvNil == (_pglpmsir = DynamicArray::PglNew(size(PMSIR))))
        return fFalse;
    _pglpmsir->SetMinGrow(1);

    if (hNil == (_hevt = CreateEvent(pvNil, fFalse, fFalse, pvNil)))
        return fFalse;

    // create the thread
    if (hNil == (_hth = CreateThread(pvNil, 1024, WindowsMidiStream::_ThreadProc, this, 0, &luThread)))
    {
        return fFalse;
    }

    AssertThis(0);
    return fTrue;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a WindowsMidiStream.
***************************************************************************/
void WindowsMidiStream::AssertValid(ulong grf)
{
    WindowsMidiStream_PAR::AssertValid(0);
    Assert(hNil != _hlib, 0);
    long cpmsir;

    _mutx.Enter();
    Assert(hNil != _hevt, "nil event");
    Assert(hNil != _hth, "nil thread");
    AssertPo(_pglpmsir, 0);
    cpmsir = _pglpmsir->IvMac();
    Assert(_cmhOut >= 0, "negative _cmhOut");
    AssertIn(_ipmsirCur, 0, cpmsir + 1);
    _mutx.Leave();
}

/***************************************************************************
    Mark memory for the WindowsMidiStream.
***************************************************************************/
void WindowsMidiStream::MarkMem(void)
{
    AssertValid(0);
    PMSIR pmsir;
    long ipmsir;

    WindowsMidiStream_PAR::MarkMem();

    _mutx.Enter();
    for (ipmsir = _pglpmsir->IvMac(); ipmsir-- > 0;)
    {
        _pglpmsir->Get(ipmsir, &pmsir);
        AssertVarMem(pmsir);
        MarkPv(pmsir);
    }
    MarkMemObj(_pglpmsir);
    _mutx.Leave();
}
#endif // DEBUG

/***************************************************************************
    Opens the midi stream and sets the time division to 1000 ticks per
    quarter note. It is assumed that the midi data has a tempo record
    indicating 1 quarter note per second (1000000 microseconds per quarter).
    The end result is that ticks are milliseconds.
***************************************************************************/
bool WindowsMidiStream::_FOpen(void)
{
    AssertThis(0);

    // MIDIPROPTIMEDIV struct
    struct MT
    {
        DWORD cbStruct;
        DWORD dwTimeDiv;
    };

    MT mt;
    UINT uT = MIDI_MAPPER;

    _mutx.Enter();

    if (hNil != _hms)
        goto LDone;

    if (MMSYSERR_NOERROR != (*_pfnOpen)(&_hms, &uT, 1, (DWORD_PTR)_MidiProc, (DWORD_PTR)this, CALLBACK_FUNCTION))
    {
        goto LFail;
    }

    // We set the time division to 1000 ticks per beat, so clients can
    // use 1 beat per second and just use milliseconds for timing.
    // We also un-pause the stream.
    mt.cbStruct = size(mt);
    mt.dwTimeDiv = 1000;

    if (MMSYSERR_NOERROR != (*_pfnProperty)(_hms, (byte *)&mt, MIDIPROP_SET | MIDIPROP_TIMEDIV))
    {
        (*_pfnClose)(_hms);
    LFail:
        _hms = hNil;
        _mutx.Leave();
        return fFalse;
    }

    // we know there are no buffers submitted
    AssertVar(_cmhOut == 0, "why is _cmhOut non-zero?", &_cmhOut);
    _cmhOut = 0;

    // get the system volume level
    _GetSysVol();

    // set our volume level
    _SetSysVlm();

LDone:
    _mutx.Leave();

    return fTrue;
}

/***************************************************************************
    Close the midi stream.
***************************************************************************/
bool WindowsMidiStream::_FClose(void)
{
    AssertThis(0);

    _mutx.Enter();

    if (hNil == _hms)
    {
        _mutx.Leave();
        return fTrue;
    }

    if (0 < _cmhOut)
    {
        BugVar("closing a stream that still has buffers!", &_cmhOut);
        _mutx.Leave();
        return fFalse;
    }

    // reset the device
    _Reset();

    // restore the volume level
    _SetSysVol(_luVolSys);

    // free the device
    (*_pfnClose)(_hms);
    _hms = hNil;

    _mutx.Leave();

    return fTrue;
}

#ifdef STREAM_BUG
/***************************************************************************
    Just return the value of our flag, not (hNil != _hms).
***************************************************************************/
bool WindowsMidiStream::FActive(void)
{
    return _fActive;
}

/***************************************************************************
    Need to set _fActive as well.
***************************************************************************/
bool WindowsMidiStream::FActivate(bool fActivate)
{
    bool fRet;

    fRet = WindowsMidiStream_PAR::FActivate(fActivate);
    if (fRet)
        _fActive = FPure(fActivate);
    return fRet;
}
#endif // STREAM_BUG

/***************************************************************************
    Reset the midi stream so it's ready to accept new input. Assumes we
    already have the mutx.
***************************************************************************/
void WindowsMidiStream::_ResetStream(void)
{
    if (!FActive())
        return;

#ifdef STREAM_BUG
    if (hNil == _hms)
        _FOpen();
    else
    {
        (*_pfnStop)(_hms);
        _FClose();
        _FOpen();
    }
#else  //! STREAM_BUG
    (*_pfnStop)(_hms);
    _Reset();
#endif //! STREAM_BUG
}

/***************************************************************************
    This submits a buffer and restarts the midi stream. If the data is
    bigger than 64K, this (in conjunction with _Notify) deals with it.
***************************************************************************/
bool WindowsMidiStream::FQueueBuffer(void *pvData, long cb, long ibStart, long cactPlay, std::uintptr_t lUserDataa)
{
    AssertThis(0);
    AssertPvCb(pvData, cb);
    AssertIn(ibStart, 0, cb);
    Assert(cb % size(MEV) == 0, "bad cb");
    Assert(ibStart % size(MEV) == 0, "bad cb");

    long ipmsir;
    PMSIR pmsir = pvNil;
    bool fRet = fTrue;

    _mutx.Enter();

    if (hNil == _hms)
        goto LFail;

    if (!FAllocPv((void **)&pmsir, size(MSIR), fmemClear, mprNormal))
        goto LFail;

    pmsir->pvData = pvData;
    pmsir->cb = cb;
    pmsir->cactPlay = cactPlay;
    pmsir->lUserDataa = lUserDataa;
    pmsir->ibNext = ibStart;

    if (_hms == hNil || !_pglpmsir->FAdd(&pmsir, &ipmsir))
        goto LFail;

    if (0 == _CmhSubmitBuffers() && ipmsir == 0)
    {
        // submitting the buffers failed
        _pglpmsir->Delete(0);
        _ipmsirCur = 0;
    LFail:
        FreePpv((void **)pmsir);
        fRet = fFalse;
    }

    _mutx.Leave();

    return fRet;
}

/***************************************************************************
    Submits buffers. Assumes the _mutx is already ours.
***************************************************************************/
long WindowsMidiStream::_CmhSubmitBuffers(void)
{
    PMSIR pmsir;
    long cbMh;
    PMH pmh;
    long imh;
    long cmh = 0;

    while (_ipmsirCur < _pglpmsir->IvMac())
    {
        _pglpmsir->Get(_ipmsirCur, &pmsir);
        if (pmsir->ibNext >= pmsir->cb)
        {
            // see if the sound should be repeated
            if (pmsir->cactPlay == 1)
            {
                _ipmsirCur++;
                continue;
            }
            pmsir->cactPlay--;
            pmsir->ibNext = 0;
        }

        // see if one of the buffers is free
        for (imh = 0;; imh++)
        {
            if (imh >= kcmhMsir)
            {
                // all buffers are busy
                Assert(_cmhOut >= kcmhMsir, 0);
                return cmh;
            }
            if (pmsir->rgibLim[imh] == 0)
                break;
        }

        // fill the buffer and submit it
        pmh = &pmsir->rgmh[imh];
        pmh->lpData = static_cast<LPSTR>(PvAddBv(pmsir->pvData, pmsir->ibNext));
        cbMh = LwMin(pmsir->cb - pmsir->ibNext, kcbMaxWmsBuffer);
        pmh->dwBufferLength = cbMh;
        pmh->dwBytesRecorded = cbMh;
        pmh->dwUser = (DWORD_PTR)pmsir;
        pmsir->ibNext += cbMh;
        pmsir->rgibLim[imh] = pmsir->ibNext;

        if (_FSubmit(pmh))
            cmh++;
        else
        {
            // just play the previous buffers and forget about this one
            pmsir->ibNext = pmsir->cb;
            pmsir->rgibLim[imh] = 0;
            pmsir->cactPlay = 1;
            _ipmsirCur++;
        }
    }

    return cmh;
}

/***************************************************************************
    Prepare and submit the given buffer. Assumes the mutx is ours.
***************************************************************************/
bool WindowsMidiStream::_FSubmit(PMH pmh)
{
    bool fRestart = (0 == _cmhOut);

    if (hNil == _hms)
        return fFalse;

    // prepare and submit the buffer
    if (MMSYSERR_NOERROR != midiOutPrepareHeader(_hms, (PMHO)pmh, sizeof(*pmh)))
        return fFalse;

    if (MMSYSERR_NOERROR != (*_pfnOut)(_hms, (PMHO)pmh, size(*pmh)))
    {
        midiOutUnprepareHeader(_hms, (PMHO)pmh, size(*pmh));
        return fFalse;
    }
    _cmhOut++;

    if (fRestart)
        (*_pfnRestart)(_hms);

    return fTrue;
}

/***************************************************************************
    Stop the midi stream.
***************************************************************************/
void WindowsMidiStream::StopPlaying(void)
{
    AssertThis(0);

    _mutx.Enter();

    if (hNil != _hms && _cmhOut > 0)
    {
        (*_pfnStop)(_hms);
        _ipmsirCur = _pglpmsir->IvMac();
    }

    _mutx.Leave();
}

/***************************************************************************
    Call back from the midi stream. If the number of active buffers returns
    to 0, this stops the midi stream. If the indicated sound is done,
    we notify the client.
***************************************************************************/
void __stdcall WindowsMidiStream::_MidiProc(HMS hms, UINT msg, DWORD_PTR luUser, DWORD_PTR lu1, DWORD_PTR lu2)
{
    PWindowsMidiStream pwms;
    PMH pmh;

    if (msg != MOM_DONE)
        return;

    pwms = (PWindowsMidiStream)luUser;
    AssertPo(pwms, 0);
    pmh = (PMH)lu1;
    AssertVarMem(pmh);
    pwms->_Notify(hms, pmh);
}

/***************************************************************************
    The this-based callback.

    The mmsys guys claim that it's illegal to call midiOutUnprepareHeader,
    midiStreamStop and midiOutReset. So we just signal another thread to
    do this work.
***************************************************************************/
void WindowsMidiStream::_Notify(HMS hms, PMH pmh)
{
    AssertThis(0);
    Assert(hNil != hms, 0);
    AssertVarMem(pmh);

    PMSIR pmsir;
    long imh;

    _mutx.Enter();

    Assert(hms == _hms, "wrong hms");

    midiOutUnprepareHeader(hms, (PMHO)pmh, size(*pmh));
    pmsir = (PMSIR)pmh->dwUser;
    AssertVarMem(pmsir);
    AssertPvCb(pmsir->pvData, pmsir->cb);

    for (imh = 0;; imh++)
    {
        if (imh >= kcmhMsir)
        {
            Bug("corrupt msir");
            _mutx.Leave();
            return;
        }

        if (pmh == &pmsir->rgmh[imh])
            break;
    }

    Assert(pmh->lpData == PvAddBv(pmsir->pvData, pmsir->rgibLim[imh] - pmh->dwBufferLength), "pmh->lpData is wrong");

    // mark this buffer free
    pmsir->rgibLim[imh] = 0;

    // fill and submit buffers
    _CmhSubmitBuffers();

    // update the submitted buffer count
    --_cmhOut;

    // wake up the auxillary thread to do callbacks and stop and reset
    // the device it there's nothing more to play
    SetEvent(_hevt);

    _mutx.Leave();
}

/***************************************************************************
    AT: Static method. Thread function for the WindowsMidiStream object. This thread
    just waits for the event to be triggered, indicating that we got
    a callback from the midiStream stuff and it's time to do our callbacks.
***************************************************************************/
ulong __stdcall WindowsMidiStream::_ThreadProc(void *pv)
{
    PWindowsMidiStream pwms = (PWindowsMidiStream)pv;

    AssertPo(pwms, 0);

    return pwms->_LuThread();
}

/***************************************************************************
    AT: This thread just sleeps until the next sound is due to expire, then
    wakes up and nukes any expired sounds.
***************************************************************************/
ulong WindowsMidiStream::_LuThread(void)
{
    AssertThis(0);

    for (;;)
    {
        WaitForSingleObject(_hevt, INFINITE);

        if (_fDone)
            return 0;

        _mutx.Enter();

        if (hNil != _hms && 0 == _cmhOut)
            _ResetStream();

        _DoCallBacks();

        _mutx.Leave();
    }
}

/***************************************************************************
    Check for MSIRs that are done and do the callback on them and free them.
    Assumes the _mutx is checked out exactly once.
***************************************************************************/
void WindowsMidiStream::_DoCallBacks()
{
    PMSIR pmsir;

    AssertIn(_ipmsirCur, 0, _pglpmsir->IvMac() + 1);
    while (0 < _ipmsirCur)
    {
        _pglpmsir->Get(0, &pmsir);

        if (_cmhOut > 0)
        {
            // see if the MSIR is done
            long imh;

            for (imh = 0; imh < kcmhMsir; imh++)
            {
                if (0 < pmsir->rgibLim[imh])
                {
                    // this one is busy
                    return;
                }
            }
        }

        // this one is done
        _pglpmsir->Delete(0);
        _ipmsirCur--;
        _mutx.Leave();

        // notify the client that we're done with the sound
        (*_pfnCall)(_luUser, pmsir->pvData, pmsir->lUserDataa);
        FreePpv((void **)&pmsir);

        _mutx.Enter();
        AssertIn(_ipmsirCur, 0, _pglpmsir->IvMac() + 1);
    }
}

/***************************************************************************
    Constructor for our own midi stream api implementation.
***************************************************************************/
OurMidiStream::OurMidiStream(PFNMIDI pfn, std::uintptr_t luUser) : MidiStreamInterface(pfn, luUser)
{
}

/***************************************************************************
    Destructor for our midi stream.
***************************************************************************/
OurMidiStream::~OurMidiStream(void)
{
    if (hNil != _hth)
    {
        // tell the thread to end and wait for it to finish
        _fDone = fTrue;
        SetEvent(_hevt);
        WaitForSingleObject(_hth, INFINITE);
    }

    _mutx.Enter();

    if (hNil != _hevt)
        CloseHandle(_hevt);

    Assert(_hms == hNil, "Still have an HMS");
    Assert(_pglmsb->IvMac() == 0, "Still have some buffers");
    ReleasePpo(&_pglmsb);

    _mutx.Leave();
}

/***************************************************************************
    Create a new OurMidiStream.
***************************************************************************/
POurMidiStream OurMidiStream::PomsNew(PFNMIDI pfn, std::uintptr_t luUser)
{
    POurMidiStream poms;

    if (pvNil == (poms = NewObj OurMidiStream(pfn, luUser)))
        return pvNil;

    if (!poms->_FInit())
        ReleasePpo(&poms);

    return poms;
}

/***************************************************************************
    Initialize the OurMidiStream.
***************************************************************************/
bool OurMidiStream::_FInit(void)
{
    AssertBaseThis(0);
    ulong luThread;

    if (pvNil == (_pglmsb = DynamicArray::PglNew(size(MSB))))
        return fFalse;
    _pglmsb->SetMinGrow(1);

    if (hNil == (_hevt = CreateEvent(pvNil, fFalse, fFalse, pvNil)))
        return fFalse;

    // create the thread in a suspended state
    if (hNil == (_hth = CreateThread(pvNil, 1024, OurMidiStream::_ThreadProc, this, CREATE_SUSPENDED, &luThread)))
    {
        return fFalse;
    }

    SetThreadPriority(_hth, THREAD_PRIORITY_TIME_CRITICAL);

    // start the thread
    ResumeThread(_hth);

    return fTrue;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a OurMidiStream.
***************************************************************************/
void OurMidiStream::AssertValid(ulong grf)
{
    OurMidiStream_PAR::AssertValid(0);

    _mutx.Enter();
    Assert(hNil != _hth, "nil thread");
    Assert(hNil != _hevt, "nil event");
    AssertPo(_pglmsb, 0);
    _mutx.Leave();
}

/***************************************************************************
    Mark memory for the OurMidiStream.
***************************************************************************/
void OurMidiStream::MarkMem(void)
{
    AssertValid(0);
    OurMidiStream_PAR::MarkMem();

    _mutx.Enter();
    MarkMemObj(_pglmsb);
    _mutx.Leave();
}
#endif // DEBUG

/***************************************************************************
    Open the stream.
***************************************************************************/
bool OurMidiStream::_FOpen(void)
{
    AssertThis(0);

    _mutx.Enter();

    if (hNil != _hms)
        goto LDone;

    _fChanged = _fStop = fFalse;
    if (MMSYSERR_NOERROR != midiOutOpen(&_hms, MIDI_MAPPER, 0, 0, CALLBACK_NULL))
    {
        _hms = hNil;
        _mutx.Leave();
        return fFalse;
    }

    // get the system volume level
    _GetSysVol();

    // set our volume level
    _SetSysVlm();

LDone:
    _mutx.Leave();

    return fTrue;
}

/***************************************************************************
    Close the stream.
***************************************************************************/
bool OurMidiStream::_FClose(void)
{
    AssertThis(0);

    _mutx.Enter();

    if (hNil == _hms)
    {
        _mutx.Leave();
        return fTrue;
    }

    if (_pglmsb->IvMac() > 0)
    {
        Bug("closing a stream that still has buffers!");
        _mutx.Leave();
        return fFalse;
    }

    // reset the device
    _Reset();

    // restore the volume level
    _SetSysVol(_luVolSys);

    midiOutClose(_hms);
    _hms = hNil;

    _mutx.Leave();

    return fTrue;
}

/***************************************************************************
    Queue a buffer to the midi stream.
***************************************************************************/
bool OurMidiStream::FQueueBuffer(void *pvData, long cb, long ibStart, long cactPlay, std::uintptr_t lUserDataa)
{
    AssertThis(0);
    AssertPvCb(pvData, cb);
    AssertIn(ibStart, 0, cb);
    Assert(cb % size(MEV) == 0, "bad cb");
    Assert(ibStart % size(MEV) == 0, "bad cb");

    MSB msb;

    _mutx.Enter();

    if (hNil == _hms)
        goto LFail;

    msb.pvData = pvData;
    msb.cb = cb;
    msb.ibStart = ibStart;
    msb.cactPlay = cactPlay;
    msb.lUserDataa = lUserDataa;

    if (!_pglmsb->FAdd(&msb))
    {
    LFail:
        _mutx.Leave();
        return fFalse;
    }

    if (1 == _pglmsb->IvMac())
    {
        // Start the buffer
        SetEvent(_hevt);
        _fChanged = fTrue;
    }

    _mutx.Leave();

    return fTrue;
}

/***************************************************************************
    Stop the stream and release all buffers. The buffer notifies are
    asynchronous.
***************************************************************************/
void OurMidiStream::StopPlaying(void)
{
    AssertThis(0);

    _mutx.Enter();

    if (hNil != _hms)
    {
        _fStop = fTrue;
        SetEvent(_hevt);
        _fChanged = fTrue;
    }

    _mutx.Leave();
}

/***************************************************************************
    AT: Static method. Thread function for the midi stream object.
***************************************************************************/
ulong __stdcall OurMidiStream::_ThreadProc(void *pv)
{
    POurMidiStream poms = (POurMidiStream)pv;

    AssertPo(poms, 0);

    return poms->_LuThread();
}

/***************************************************************************
    AT: The midi stream playback thread.
***************************************************************************/
ulong OurMidiStream::_LuThread(void)
{
    AssertThis(0);
    MSB msb;
    bool fChanged; // whether the event went off
    ulong tsCur;
    const long klwInfinite = klwMax;
    long dtsWait = klwInfinite;

    for (;;)
    {
        fChanged =
            dtsWait > 0 && WAIT_TIMEOUT != WaitForSingleObject(_hevt, dtsWait == klwInfinite ? INFINITE : dtsWait);

        if (_fDone)
            return 0;

        _mutx.Enter();
        if (_fChanged && !fChanged)
        {
            // the event went off before we got the mutx.
            dtsWait = klwInfinite;
            goto LLoop;
        }

        _fChanged = fFalse;
        if (!fChanged)
        {
            // play the event
            if (_pmev < _pmevLim)
            {
                if (MEVT_SHORTMSG == (_pmev->eventWord >> 24))
                    midiOutShortMsg(_hms, _pmev->eventWord & 0x00FFFFFF);

                _pmev++;
                if (_pmev >= _pmevLim)
                    dtsWait = 0;
                else
                {
                    ulong tsNew = TsCurrentSystem();

                    tsCur += _pmev->deltaTicks;
                    dtsWait = tsCur - tsNew;
                    if (dtsWait < -kdtsMinSlip)
                    {
                        tsCur = tsNew;
                        dtsWait = 0;
                    }
                }
                goto LLoop;
            }

            // ran out of events in the current buffer - see if we should
            // repeat it
            _pglmsb->Get(0, &msb);
            if (msb.cactPlay == 1)
            {
                _imsbCur = 1;
                _ReleaseBuffers();
            }
            else
            {
                // repeat the current buffer
                if (msb.cactPlay > 0)
                    msb.cactPlay--;
                msb.ibStart = 0;
                _pglmsb->Put(0, &msb);
            }
        }
        else if (_fStop)
        {
            // release all buffers
            _fStop = fFalse;
            _imsbCur = _pglmsb->IvMac();
            _ReleaseBuffers();
        }

        if (0 == _pglmsb->IvMac())
        {
            // no buffers to play
            dtsWait = klwInfinite;
        }
        else
        {
            // start playing the new buffers
            _pglmsb->Get(0, &msb);
            _pmev = (PMEV)PvAddBv(msb.pvData, msb.ibStart);
            _pmevLim = (PMEV)PvAddBv(msb.pvData, msb.cb);
            if (_pmev >= _pmevLim)
                dtsWait = 0;
            else
            {
                dtsWait = _pmev->deltaTicks;
                tsCur = TsCurrentSystem() + dtsWait;
            }
        }
    LLoop:
        _mutx.Leave();
    }
}

/***************************************************************************
    Release all buffers up to _imsbCur. Assumes that we have the mutx
    checked out exactly once.
***************************************************************************/
void OurMidiStream::_ReleaseBuffers(void)
{
    MSB msb;

    if (_imsbCur >= _pglmsb->IvMac() && hNil != _hms)
        _Reset();

    while (_imsbCur > 0)
    {
        _pglmsb->Get(0, &msb);
        _pglmsb->Delete(0);
        _imsbCur--;

        _mutx.Leave();

        // call the notify proc
        (*_pfnCall)(_luUser, msb.pvData, msb.lUserDataa);

        _mutx.Enter();
    }
}

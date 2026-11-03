/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_STREAM_WIN_H
#define KAUAI_MIDI_STREAM_WIN_H
// Private native declarations; the Windows SDK must be included first.
#include "midi_stream_host.h"
using MEV = KauaiMidi::StreamEvent;
typedef MEV *PMEV;
using MH = MIDIHDR;
typedef MH *PMH;
typedef MIDIHDR *PMHO;
typedef HMIDIOUT HMS;
/***************************************************************************
    The midi stream interface.
***************************************************************************/
// luUser/lUserDataa are pointer-sized: PwmsNew callers pass `this` to round-trip
// through Win32 midiStreamOpen's dwInstance slot, which is DWORD_PTR (8 bytes
// on Win64). bare `ulong` (4 bytes on Win64 LLP64) silently truncates pointers.
using PFNMIDI = KauaiMidi::Completion;

#define MidiStreamInterface_PAR KauaiMidi::StreamHost
#define kclsMidiStreamInterface 'MISI'
class MidiStreamInterface : public MidiStreamInterface_PAR
{
    RTCLASS_DEC

  protected:
    HMS _hms;               // the midi stream handle
    PFNMIDI _pfnCall;       // call back function
    std::uintptr_t _luUser; // user data to send back (pointer-sized; holds `this`)

    // system volume level - to be saved and restored. The volume we set
    // is always relative to this
    tribool _tBogusDriver; // to indicate whether midiOutGetVolume really works
    ulong _luVolSys;
    long _vlmBase; // our current volume relative to _luVolSys.

    MidiStreamInterface(PFNMIDI pfn, std::uintptr_t luUser);

    virtual bool _FOpen(void) = 0;
    virtual bool _FClose(void) = 0;

    void _Reset(void);
    void _GetSysVol(void);
    void _SetSysVol(ulong luVol);
    void _SetSysVlm(void);

  public:
    virtual void SetVlm(long vlm);
    virtual long VlmCur(void);

    virtual bool FActive(void);
    virtual bool FActivate(bool fActivate);

    virtual bool FQueueBuffer(void *pvData, long cb, long ibStart, long cactPlay, std::uintptr_t lUserDataa) = 0;
    virtual void StopPlaying(void) = 0;
};

/***************************************************************************
    The midiStreamStop API has a bug in it where it doesn't reset the
    current "buffer position" so that after calling midiStreamStop, then
    midiStreamOut and midiStreamRestart, the new buffer isn't played
    immediately, but the system waits until the previous buffer position
    expires before playing the new buffer.

    When this bug is fixed, STREAM_BUG can be undefined.
***************************************************************************/
#define STREAM_BUG

/***************************************************************************
    The real midi stream interface.
***************************************************************************/
typedef class WindowsMidiStream *PWindowsMidiStream;
#define WindowsMidiStream_PAR MidiStreamInterface
#define kclsWindowsMidiStream 'WMS'
class WindowsMidiStream : public WindowsMidiStream_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
#define kcmhMsir 2
    struct MSIR
    {
        void *pvData;
        long cb;
        long cactPlay;
        std::uintptr_t lUserDataa;
        long ibNext;

        MH rgmh[kcmhMsir];
        long rgibLim[kcmhMsir];
    };
    typedef MSIR *PMSIR;

    Mutex _mutx;
    HINSTANCE _hlib;
    PDynamicArray _pglpmsir;
    long _ipmsirCur;
    long _cmhOut;

    HN _hevt; // event to wake up the thread
    HN _hth;  // thread to do callbacks and cleanup after a notify

#ifdef STREAM_BUG
    bool _fActive : 1;
#endif               // STREAM_BUG
    bool _fDone : 1; // tells the aux thread to terminate

    // Win32 midiStreamOpen takes DWORD_PTR (pointer-sized) for the callback +
    // instance slots, not DWORD. Was 4 bytes everywhere on x86; on Win64 the
    // callback function pointer and our `this` user-instance are both 8 bytes.
    // Storing them in a 4-byte DWORD silently truncates and crashes WINMM the
    // moment it tries to invoke the half-pointer.
    MMRESULT(WINAPI *_pfnOpen)
    (HMS *phms, LPUINT puDeviceID, DWORD cMidi, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen);
    MMRESULT(WINAPI *_pfnClose)(HMS hms);
    MMRESULT(WINAPI *_pfnProperty)(HMS hms, LPBYTE lpb, DWORD dwProperty);
    MMRESULT(WINAPI *_pfnPosition)(HMS hms, LPMMTIME lpmmt, UINT cbmmt);
    MMRESULT(WINAPI *_pfnOut)(HMS hms, LPMIDIHDR pmh, UINT cbmh);
    MMRESULT(WINAPI *_pfnPause)(HMS hms);
    MMRESULT(WINAPI *_pfnRestart)(HMS hms);
    MMRESULT(WINAPI *_pfnStop)(HMS hms);

    WindowsMidiStream(PFNMIDI pfn, std::uintptr_t luUser);
    bool _FInit(void);

    virtual bool _FOpen(void);
    virtual bool _FClose(void);

    bool _FSubmit(PMH pmh);
    void _DoCallBacks(void);
    long _CmhSubmitBuffers(void);
    void _ResetStream(void);

    // The callback signature must match Win32 midiOutProc: dwInstance/dwParam1/
    // dwParam2 are DWORD_PTR (pointer-sized) so the user-data round-trips
    // intact on Win64.
    static void __stdcall _MidiProc(HMS hms, UINT msg, DWORD_PTR luUser, DWORD_PTR lu1, DWORD_PTR lu2);
    void _Notify(HMS hms, PMH pmh);

    static ulong __stdcall _ThreadProc(void *pv);
    ulong _LuThread(void);

  public:
    static PWindowsMidiStream PwmsNew(PFNMIDI pfn, std::uintptr_t luUser);
    ~WindowsMidiStream(void);
    const char *ProviderName() const override
    {
        return "winmm-stream";
    }

#ifdef STREAM_BUG
    virtual bool FActive(void);
    virtual bool FActivate(bool fActivate);
#endif // STREAM_BUG

    virtual bool FQueueBuffer(void *pvData, long cb, long ibStart, long cactPlay, std::uintptr_t lUserDataa);
    virtual void StopPlaying(void);
};

/***************************************************************************
    Our fake midi stream class.
***************************************************************************/
typedef class OurMidiStream *POurMidiStream;
#define OurMidiStream_PAR MidiStreamInterface
#define kclsOurMidiStream 'OMS'
class OurMidiStream : public OurMidiStream_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    struct MSB
    {
        void *pvData;
        long cb;
        long ibStart;
        long cactPlay;

        std::uintptr_t lUserDataa;
    };

    Mutex _mutx;
    HN _hevt; // event to notify the thread that the stream data has changed
    HN _hth;  // thread to play the stream data

    bool _fChanged : 1; // the event has been signalled
    bool _fStop : 1;    // tells the aux thread to stop all buffers
    bool _fDone : 1;    // tells the aux thread to return

    long _imsbCur;
    PDynamicArray _pglmsb;
    PMEV _pmev;
    PMEV _pmevLim;
    ulong _tsCur;

    OurMidiStream(PFNMIDI pfn, std::uintptr_t luUser);
    bool _FInit(void);

    virtual bool _FOpen(void);
    virtual bool _FClose(void);

    static ulong __stdcall _ThreadProc(void *pv);
    ulong _LuThread(void);
    void _ReleaseBuffers(void);

  public:
    static POurMidiStream PomsNew(PFNMIDI pfn, std::uintptr_t luUser);
    ~OurMidiStream(void);
    const char *ProviderName() const override
    {
        return "winmm-scheduled";
    }

    virtual bool FQueueBuffer(void *pvData, long cb, long ibStart, long cactPlay, std::uintptr_t lUserDataa);
    virtual void StopPlaying(void);
};

#endif //! KAUAI_MIDI_STREAM_WIN_H

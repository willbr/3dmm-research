/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Private declarations for mididev2 (streamed midi).

***************************************************************************/
#ifndef MDEV2PRI_H
#define MDEV2PRI_H

#include "midi_stream_host.h"
#include "midi_worker.h"
#include "three_mm/midi_mix.h"
using MEV = KauaiMidi::StreamEvent;
typedef MEV *PMEV;

/***************************************************************************
    This is the midi stream cached object.
***************************************************************************/
typedef class MidiStreamCached *PMidiStreamCached;
#define MidiStreamCached_PAR BaseCacheableObject
#define kclsMidiStreamCached 'MDWS'
class MidiStreamCached : public MidiStreamCached_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PDynamicArray _pglmev;
    LegacyUlong _dts;

    MidiStreamCached(void);
    bool _FInit(PMidiStream pmids);

  public:
    static bool FReadMdws(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                          PBaseCacheableObject *ppbaco, LegacyLong *pcb);
    static PMidiStreamCached PmdwsRead(PDataBlock pblck);

    ~MidiStreamCached(void);

    LegacyUlong Dts(void)
    {
        return _dts;
    }
    void *PvLockData(LegacyLong *pcb);
    void UnlockData(void);
};

// forward declaration
typedef class MidiStreamMixer *PMidiStreamMixer;
typedef KauaiMidi::StreamHost *PMidiStreamInterface;

/***************************************************************************
    Midi stream queue.
***************************************************************************/
typedef class MidiStreamQueue *PMidiStreamQueue;
#define MidiStreamQueue_PAR SoundQueue
#define kclsMidiStreamQueue 'msqu'
class MidiStreamQueue : public MidiStreamQueue_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    Mutex _mutx;          // restricts access to member variables
    LegacyUlong _tsStart; // when we started the current sound
    PMidiStreamMixer _pmsmix;

    ThreeMMMidiQueueServices _QueueServices();
    MidiStreamQueue(void);

    virtual void _Enter(void);
    virtual void _Leave(void);

    virtual bool _FInit(PMidiStreamMixer pmsmix);
    virtual PBaseCacheableObject _PbacoFetch(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno);
    virtual void _Queue(LegacyLong isndinMin);
    virtual void _PauseQueue(LegacyLong isndinMin);
    virtual void _ResumeQueue(LegacyLong isndinMin);

  public:
    static PMidiStreamQueue PmsqueNew(PMidiStreamMixer pmsmix);
    ~MidiStreamQueue(void);

    void Notify(PMidiStreamCached pmdws);
};

/***************************************************************************
    Midi Stream "mixer". It really just chooses which midi stream to play
    (based on the (spr, sii) priority).
***************************************************************************/
typedef class MidiStreamMixer *PMidiStreamMixer;
#define MidiStreamMixer_PAR BASE
#define kclsMidiStreamMixer 'msmx'
class MidiStreamMixer : public MidiStreamMixer_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    struct MSOS
    {
        PMidiStreamQueue pmsque; // the "channel" or queue that the sound is on
        PMidiStreamCached pmdws; // the sound
        LegacyLong sii;          // its sound id (for a priority tie breaker)
        LegacyLong spr;          // its priority
        LegacyLong cactPlay;     // how many times to play the sound
        LegacyUlong dts;         // length of this sound
        LegacyUlong dtsStart;    // position to start at
        LegacyLong vlm;          // volume to play at
        LegacyUlong tsStart;     // when we "started" the sound (minus dtsStart)
    };

    // Mutex to protect our member variables
    Mutex _mutx;
    KauaiMidi::Worker _worker;
    KauaiPlatform::AutoResetEvent _buffersReturned;

    PMidiStreamInterface _pmisi; // the midi stream interface
    PDynamicArray _pglmsos;      // the list of current sounds, in priority order

    PDynamicArray _pglmevKey; // to accumulate state events for seeking

    ThreeMMMidiMixState _mix;
    ThreeMMMidiMixServices _Services();
    MidiStreamMixer(void);
    bool _FInit(KauaiMidi::HostFactory factory);
    void _StopStream(void);
    bool _FGetKeyEvents(PMidiStreamCached pmdws, LegacyUlong dtsSeek, LegacyLong *pcbSkip);
    void _Restart(bool fNew = fFalse);
    void _WaitForBuffers(void);
    void _SubmitBuffers(LegacyUlong tsCur);

    static void _MidiProc(std::uintptr_t luUser, void *pvData, std::uintptr_t lUserDataa);
    void _Notify(void *pvData, PMidiStreamCached pmdws);

    static std::uint32_t _ThreadProc(void *pv);
    std::uint32_t _ExpireSounds(void);

  public:
    static PMidiStreamMixer PmsmixNew(KauaiMidi::HostFactory factory);
    ~MidiStreamMixer(void);

    bool FPlay(PMidiStreamQueue pmsque, PMidiStreamCached pmdws = pvNil, LegacyLong sii = siiNil, LegacyLong spr = 0,
               LegacyLong cactPlay = 1, LegacyUlong dtsStart = 0, LegacyLong vlm = kvlmFull);

    void Suspend(bool fSuspend);
    void SetVlm(LegacyLong vlm);
    LegacyLong VlmCur(void);
};

#endif //! MDEV2PRI_H

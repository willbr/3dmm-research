/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Sound Management class for WAVE and MIDI support

***************************************************************************/
#ifndef SNDM_H
#define SNDM_H

const LegacyLong siiNil = 0;
const FileType kftgMidi = MacWin('MIDI', 'MID'); // REVIEW shonk: Mac: file type
const FileType kftgWave = MacWin('WAVE', 'WAV'); // REVIEW shonk: Mac: file type

/***************************************************************************
    Sound device - like audioman or our midi player.
***************************************************************************/
typedef class SoundDevice *PSoundDevice;
#define SoundDevice_PAR BASE
#define kclsSoundDevice 'SNDV'
class SoundDevice : public SoundDevice_PAR
{
    RTCLASS_DEC

  protected:
    static LegacyLong _siiLast;

    static LegacyLong _SiiAlloc(void)
    {
        return ++_siiLast;
    }

  public:
    virtual bool FActive(void) = 0;
    virtual void Activate(bool fActive) = 0; // boolean state
    virtual void Suspend(bool fSuspend) = 0; // reference count
    virtual void SetVlm(LegacyLong vlm) = 0;
    virtual LegacyLong VlmCur(void) = 0;

    virtual LegacyLong SiiPlay(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno, LegacyLong sqn = ksqnNone, LegacyLong vlm = kvlmFull, LegacyLong cactPlay = 1,
                         LegacyUlong dtsStart = 0, LegacyLong spr = 0, LegacyLong scl = sclNil) = 0;

    virtual void Stop(LegacyLong sii) = 0;
    virtual void StopAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil) = 0;

    virtual void Pause(LegacyLong sii) = 0;
    virtual void PauseAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil) = 0;

    virtual void Resume(LegacyLong sii) = 0;
    virtual void ResumeAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil) = 0;

    virtual bool FPlaying(LegacyLong sii) = 0;
    virtual bool FPlayingAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil) = 0;

    virtual void Flush(void) = 0;
    virtual void BeginSynch(void);
    virtual void EndSynch(void);
};

/****************************************
    Sound manager class
****************************************/
typedef class SoundManager *PSoundManager;
#define SoundManager_PAR SoundDevice
#define kclsSoundManager 'SNDM'
class SoundManager : public SoundManager_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    void (*_playObserver)(ChunkTagOrType, ChunkNumber, LegacyLong, LegacyLong, LegacyLong, LegacyUlong, LegacyLong,
                          LegacyLong) = nullptr;
#endif
    struct SNDMPE
    {
        ChunkTagOrType ctg;
        PSoundDevice psndv;
    };

    PDynamicArray _pglsndmpe; // sound type to device mapper
    LegacyLong _sclFirst = klwMin, _sclLast = klwMax;

    LegacyLong _cactSuspend;  // nesting level for suspending
    bool _fActive : 1;  // whether the app is active
    bool _fFreeing : 1; // we're in the destructor

    SoundManager(void);
    bool _FInit(void);
    bool _FFindCtg(ChunkTagOrType ctg, SNDMPE *psndmpe, LegacyLong *pisndmpe = pvNil);

  public:
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    void ObservePlays(void (*observer)(ChunkTagOrType, ChunkNumber, LegacyLong, LegacyLong, LegacyLong, LegacyUlong,
                                       LegacyLong, LegacyLong))
    {
        _playObserver = observer;
    }
#endif
    static PSoundManager PsndmNew(void);
    ~SoundManager(void);

    // new methods
    virtual bool FAddDevice(ChunkTagOrType ctg, PSoundDevice psndv);
    virtual PSoundDevice PsndvFromCtg(ChunkTagOrType ctg);
    void SetSoundClassRange(LegacyLong first, LegacyLong last)
    {
        _sclFirst = first;
        _sclLast = last;
    }
    virtual void RemoveSndv(ChunkTagOrType ctg);

    // inherited methods
    virtual bool FActive(void);
    bool FSuspended() const { return _cactSuspend > 0; }
    virtual void Activate(bool fActive);
    virtual void Suspend(bool fSuspend);
    virtual void SetVlm(LegacyLong vlm);
    virtual LegacyLong VlmCur(void);

    virtual LegacyLong SiiPlay(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno, LegacyLong sqn = ksqnNone, LegacyLong vlm = kvlmFull, LegacyLong cactPlay = 1,
                         LegacyUlong dtsStart = 0, LegacyLong spr = 0, LegacyLong scl = sclNil);

    virtual void Stop(LegacyLong sii);
    virtual void StopAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil);

    virtual void Pause(LegacyLong sii);
    virtual void PauseAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil);

    virtual void Resume(LegacyLong sii);
    virtual void ResumeAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil);

    virtual bool FPlaying(LegacyLong sii);
    virtual bool FPlayingAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil);

    virtual void Flush(void);
    virtual void BeginSynch(void);
    virtual void EndSynch(void);
};

/***************************************************************************
    A useful base class for devices that support multiple queues.
***************************************************************************/
typedef class SoundQueue *PSoundQueue;

typedef class SoundManagerQueue *PSoundManagerQueue;
#define SoundManagerQueue_PAR SoundDevice
#define kclsSoundManagerQueue 'snmq'
class SoundManagerQueue : public SoundManagerQueue_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // queue descriptor
    struct SNQD
    {
        PSoundQueue psnque;
        LegacyLong sqn;
    };

    PDynamicArray _pglsnqd; // the queues

    LegacyLong _cactSuspend;
    bool _fActive : 1;

    virtual bool _FInit(void);
    virtual bool _FEnsureQueue(LegacyLong sqn, SNQD *psnqd, LegacyLong *pisnqd);

    virtual PSoundQueue _PsnqueNew(void) = 0;
    virtual void _Suspend(bool fSuspend) = 0;

  public:
    ~SoundManagerQueue(void);

    // inherited methods
    virtual bool FActive(void);
    virtual void Activate(bool fActive);
    virtual void Suspend(bool fSuspend);

    virtual LegacyLong SiiPlay(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno, LegacyLong sqn = ksqnNone, LegacyLong vlm = kvlmFull, LegacyLong cactPlay = 1,
                         LegacyUlong dtsStart = 0, LegacyLong spr = 0, LegacyLong scl = sclNil);

    virtual void Stop(LegacyLong sii);
    virtual void StopAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil);

    virtual void Pause(LegacyLong sii);
    virtual void PauseAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil);

    virtual void Resume(LegacyLong sii);
    virtual void ResumeAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil);

    virtual bool FPlaying(LegacyLong sii);
    virtual bool FPlayingAll(LegacyLong sqn = sqnNil, LegacyLong scl = sclNil);

    virtual void Flush(void);
};

/***************************************************************************
    The sound instance structure.
***************************************************************************/
struct SNDIN
{
    PBaseCacheableObject pbaco;    // the sound to play
    LegacyLong sii;       // the sound instance id
    LegacyLong vlm;       // volume to play at
    LegacyLong cactPlay;  // how many times to play
    LegacyUlong dtsStart; // offset to start at
    LegacyLong spr;       // sound priority
    LegacyLong scl;       // sound class

    // 0 means play, < 0 means skip, > 0 means pause
    LegacyLong cactPause;
};

/***************************************************************************
    Sound queue for a SoundManagerQueue
***************************************************************************/
#define SoundQueue_PAR BASE
#define kclsSoundQueue 'snqu'
class SoundQueue : public SoundQueue_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PDynamicArray _pglsndin;   // the queue
    LegacyLong _isndinCur; // SNDIN that we should be playing

    SoundQueue(void);

    virtual bool _FInit(void);
    virtual void _Queue(LegacyLong isndinMin) = 0;
    virtual void _PauseQueue(LegacyLong isndinMin) = 0;
    virtual void _ResumeQueue(LegacyLong isndinMin) = 0;
    virtual PBaseCacheableObject _PbacoFetch(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno) = 0;

    virtual void _Enter(void);
    virtual void _Leave(void);
    virtual void _Flush(void);

  public:
    ~SoundQueue(void);

    void Enqueue(LegacyLong sii, PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno, LegacyLong vlm, LegacyLong cactPlay, LegacyUlong dtsStart, LegacyLong spr, LegacyLong scl);

    LegacyLong SprCur(void);
    void Stop(LegacyLong sii);
    void StopAll(LegacyLong scl = sclNil);
    void Pause(LegacyLong sii);
    void PauseAll(LegacyLong scl = sclNil);
    void Resume(LegacyLong sii);
    void ResumeAll(LegacyLong scl = sclNil);
    bool FPlaying(LegacyLong sii);
    bool FPlayingAll(LegacyLong scl = sclNil);
    void Flush(void);
};

extern LegacyUlong LuVolScale(LegacyUlong luVolSys, LegacyLong vlm);
extern LegacyUlong vluSysVolFake;

#endif //! SNDM_H

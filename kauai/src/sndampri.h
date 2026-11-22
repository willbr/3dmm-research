/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Private audioman sound device header file.

***************************************************************************/
#ifndef SNDAMPRI_H
#define SNDAMPRI_H
#include "mm/audioman.h"
#include "mm/reference.h"

/***************************************************************************
    IStream interface for a C-owned sound block.
***************************************************************************/
typedef class DataBlockStream *PDataBlockStream;
#define DataBlockStream_PAR IStream
class DataBlockStream : public DataBlockStream_PAR
{
    ASSERT
    MARKMEM

  protected:
    MMReference _references;
    MMAudioManStream _stream;

    DataBlockStream(void);
    ~DataBlockStream(void);

  public:
    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // IStream methods
    STDMETHODIMP Read(void *pv, ULONG cb, ULONG *pcb);
    STDMETHODIMP Write(VOID const *pv, ULONG cb, ULONG *pcb)
    {
        if (pvNil != pcb)
            *pcb = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition);
    STDMETHODIMP SetSize(ULARGE_INTEGER libNewSize)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP CopyTo(IStream *pStm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten)
    {
        if (pvNil != pcbRead)
            pcbRead->LowPart = pcbRead->HighPart = 0;
        if (pvNil != pcbWritten)
            pcbWritten->LowPart = pcbWritten->HighPart = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP Commit(DWORD grfCommitFlags)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP Revert(void)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP Stat(STATSTG *pstatstg, DWORD grfStatFlag)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP Clone(THIS_ IStream **ppstm)
    {
        *ppstm = pvNil;
        return E_NOTIMPL;
    }

    static PDataBlockStream PstblNew(MMOwnedBlock *);
    // Retain the previous DataBlock-based charge in the cache and spill threshold.
#ifdef DEBUG
    static constexpr MMI32 kcbCacheCharge = sizeof(void *) == 8 ? 88 : 60;
#else
    static constexpr MMI32 kcbCacheCharge = sizeof(void *) == 8 ? 88 : 56;
#endif
    long CbMem(void)
    {
        return kcbCacheCharge + mm_audioman_stream_resident(&_stream);
    }
    bool FInMemory(void)
    {
        return mm_audioman_stream_resident(&_stream) > 0;
    }
};

/***************************************************************************
    Cached AudioMan Sound.
***************************************************************************/
typedef class CachedAudioManSound *PCachedAudioManSound;
#define CachedAudioManSound_PAR BaseCacheableObject
#define kclsCachedAudioManSound 'CAMS'
class CachedAudioManSound : public CachedAudioManSound_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // this is just so we can do a MarkMemObj on it while AudioMan has it
    PDataBlockStream _pstbl;

    CachedAudioManSound(void);

  public:
    ~CachedAudioManSound(void);
    static PCachedAudioManSound PcamsNewLoop(PCachedAudioManSound pcamsSrc, long cactPlay);

    IAMSound *psnd; // the sound to use

    static int FReadCams(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *,
                           void **object, MMI32 *bytes);
    bool FInMemory(void)
    {
        return _pstbl->FInMemory();
    }
};

/***************************************************************************
    Notify sink class.
***************************************************************************/
typedef class AudioManQueue *PAudioManQueue; // forward declaration

typedef class AudioManNotifySink *PAudioManNotifySink;
#define AudioManNotifySink_PAR IAMNotifySink
class AudioManNotifySink : public AudioManNotifySink_PAR
{
    ASSERT

  protected:
    MMReference _references;
    PAudioManQueue _pamque; // the amque to notify

  public:
    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // IAMNotifySink methods
    STDMETHODIMP_(void) OnStart(LPSOUND pSound, DWORD dwPosition)
    {
    }
    STDMETHODIMP_(void) OnCompletion(LPSOUND pSound, DWORD dwPosition);
    STDMETHODIMP_(void) OnError(LPSOUND pSound, DWORD dwPosition, HRESULT hrError)
    {
    }
    STDMETHODIMP_(void) OnSyncObject(LPSOUND pSound, DWORD dwPosition, void *pvObject)
    {
    }

    AudioManNotifySink(void);
    void Set(PAudioManQueue pamque);
};

/***************************************************************************
    Audioman queue.
***************************************************************************/
#define AudioManQueue_PAR SoundQueue
#define kclsAudioManQueue 'amqu'
class AudioManQueue : public AudioManQueue_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    Mutex _mutx;         // restricts access to member variables
    IAMChannel *_pchan; // the audioman channel
    MMU32 _tsStart;     // when we started the current sound
    AudioManNotifySink _amnot;       // notify sink

    AudioManQueue(void);
    MMAudioManQueueServices _QueueServices();

    virtual void _Enter(void);
    virtual void _Leave(void);

    virtual bool _FInit(void);
    ResourceReader _Reader() override;
    virtual void _Queue(long isndinMin);
    virtual void _PauseQueue(long isndinMin);
    virtual void _ResumeQueue(long isndinMin);

  public:
    static PAudioManQueue PamqueNew(void);
    ~AudioManQueue(void);

    void Notify(LPSOUND psnd);
};

#endif //! SNDAMPRI_H

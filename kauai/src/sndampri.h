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

/***************************************************************************
    IStream interface for a DataBlock.
***************************************************************************/
typedef class DataBlockStream *PDataBlockStream;
#define DataBlockStream_PAR IStream
class DataBlockStream : public DataBlockStream_PAR
{
    ASSERT
    MARKMEM

  protected:
    long _cactRef;
    long _ib;
    DataBlock _blck;

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

    static PDataBlockStream PstblNew(FileLocation *pflo, bool fPacked);
    long CbMem(void)
    {
        return size(DataBlockStream) + _blck.CbMem();
    }
    bool FInMemory(void)
    {
        return _blck.CbMem() > 0;
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

    static bool FReadCams(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);
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
    long _cactRef;
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
    ulong _tsStart;     // when we started the current sound
    AudioManNotifySink _amnot;       // notify sink

    AudioManQueue(void);

    virtual void _Enter(void);
    virtual void _Leave(void);

    virtual bool _FInit(void);
    virtual PBaseCacheableObject _PbacoFetch(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno);
    virtual void _Queue(long isndinMin);
    virtual void _PauseQueue(long isndinMin);
    virtual void _ResumeQueue(long isndinMin);

  public:
    static PAudioManQueue PamqueNew(void);
    ~AudioManQueue(void);

    void Notify(LPSOUND psnd);
};

#endif //! SNDAMPRI_H

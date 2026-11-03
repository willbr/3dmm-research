/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Audioman sound support. Contains the audioman sound sink, audioman
    channel and audioman cached sound classes.

***************************************************************************/
#ifndef SNDAM_H
#define SNDAM_H

#include "legacy_wave_formats.h"

/***************************************************************************
    Audioman sound device class.
***************************************************************************/
typedef class AudioManSoundDevice *PAudioManSoundDevice;
#define AudioManSoundDevice_PAR SoundManagerQueue
#define kclsAudioManSoundDevice 'SDAM'
class AudioManSoundDevice : public AudioManSoundDevice_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    ulong _luVolSys;
    long _vlm;
    bool _fAudioManInited : 1;

    AudioManSoundDevice(void);
    virtual bool _FInit(long wav);

    // inherited methods
    virtual PSoundQueue _PsnqueNew(void);
    virtual void _Suspend(bool fSuspend);

  public:
    static long vcbMaxMemWave;

    static PAudioManSoundDevice PsdamNew(long wav);
    ~AudioManSoundDevice(void);

    virtual void SetVlm(long vlm);
    virtual long VlmCur(void);
    virtual void BeginSynch(void);
    virtual void EndSynch(void);
};

#endif //! SNDAM_H

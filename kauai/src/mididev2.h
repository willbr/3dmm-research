/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    The midi player sound device.

***************************************************************************/
#ifndef MIDIDEV2_H
#define MIDIDEV2_H

typedef class MidiStreamMixer *PMidiStreamMixer;

/***************************************************************************
    The midi player using a Midi stream.
***************************************************************************/
typedef class MidiStreamPlayer *PMidiStreamPlayer;
#define MidiStreamPlayer_PAR SNDMQ
#define kclsMidiStreamPlayer 'MDPS'
class MidiStreamPlayer : public MidiStreamPlayer_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PMidiStreamMixer _pmsmix;

    MidiStreamPlayer(void);

    virtual bool _FInit(void);
    virtual PSoundQueue _PsnqueNew(void);
    virtual void _Suspend(bool fSuspend);

  public:
    static PMidiStreamPlayer PmdpsNew(void);
    ~MidiStreamPlayer(void);

    // inherited methods
    virtual void SetVlm(long vlm);
    virtual long VlmCur(void);
};

#endif //! MIDIDEV2_H

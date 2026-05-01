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
#ifndef MIDIDEV_H
#define MIDIDEV_H

/***************************************************************************
    The midi player.
***************************************************************************/
typedef class MidiPlayer *PMidiPlayer;
#define MidiPlayer_PAR SoundManagerQueue
#define kclsMidiPlayer 'MIDP'
class MidiPlayer : public MidiPlayer_PAR
{
    RTCLASS_DEC

  protected:
    MidiPlayer(void);

    virtual PSoundQueue _PsnqueNew(void);
    virtual void _Suspend(bool fSuspend);

  public:
    static PMidiPlayer PmidpNew(void);
    ~MidiPlayer(void);

    // inherited methods
    virtual void SetVlm(long vlm);
    virtual long VlmCur(void);
};

#endif //! MIDIDEV_H

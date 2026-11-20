/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Clock class. See comments in clok.cpp.

***************************************************************************/
#ifndef CLOK_H
#define CLOK_H
#include "mm/clock.h"

enum
{
    fclokNil = 0,
    fclokReset = MM_CLOCK_RESET_ON_INPUT,
    fclokNoSlip = MM_CLOCK_NO_SLIP,
};

typedef class Clock *PClock;
#define Clock_PAR CommandHandler
#define kclsClock 'CLOK'
class Clock : public Clock_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(Clock)

  protected:
    struct Host;
    static MMAlarmClock *_clocks;
    MMAlarmClock _clock;

  public:
    Clock(LegacyLong hid, LegacyUlong grfclok = fclokNil);
    ~Clock(void);
    static PClock PclokFromHid(LegacyLong hid);
    static void BuryCmh(PCommandHandler pcmh);
    void RemoveCmh(PCommandHandler pcmh);

    void Start(LegacyUlong tim);
    void Stop(void);
    LegacyUlong TimCur(bool fAdjustForDelay = fFalse);
    LegacyUlong DtimAlarm(void)
    {
        return _clock.alarm_delay;
    }

    bool FSetAlarm(LegacyLong dtim, PCommandHandler pcmhNotify = pvNil, LegacyLong lwUser = 0, bool fAdjustForDelay = fFalse);

    // idle handling
    virtual bool FCmdAll(PCommand pcmd);

#ifdef DEBUG
    static void MarkAllCloks(void);
#endif // DEBUG
};

#endif //! CLOK_H

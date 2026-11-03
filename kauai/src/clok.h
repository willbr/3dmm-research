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

enum
{
    fclokNil = 0,
    fclokReset = 1,
    fclokNoSlip = 2,
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
    // alarm descriptor
    struct AlarmDescriptor
    {
        PCommandHandler pcmh;
        LegacyUlong tim;
        LegacyLong lw;
    };

    static PClock _pclokFirst;

    PClock _pclokNext;
    LegacyUlong _tsBase;
    LegacyUlong _timBase;
    LegacyUlong _timCur;    // current time
    LegacyUlong _dtimAlarm; // processing alarms up to _timCur + _dtimAlarm
    LegacyUlong _timNext;   // next alarm time to process (for speed)
    LegacyUlong _grfclok;
    PDynamicArray _pglalad; // the registered alarms

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
        return _dtimAlarm;
    }

    bool FSetAlarm(LegacyLong dtim, PCommandHandler pcmhNotify = pvNil, LegacyLong lwUser = 0, bool fAdjustForDelay = fFalse);

    // idle handling
    virtual bool FCmdAll(PCommand pcmd);

#ifdef DEBUG
    static void MarkAllCloks(void);
#endif // DEBUG
};

#endif //! CLOK_H

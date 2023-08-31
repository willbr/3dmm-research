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
#define Clock_PAR CMH
#define kclsClock 'CLOK'
class Clock : public Clock_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(Clock)

  protected:
    // alarm descriptor
    struct ALAD
    {
        PCMH pcmh;
        ulong tim;
        long lw;
    };

    static PClock _pclokFirst;

    PClock _pclokNext;
    ulong _tsBase;
    ulong _timBase;
    ulong _timCur;    // current time
    ulong _dtimAlarm; // processing alarms up to _timCur + _dtimAlarm
    ulong _timNext;   // next alarm time to process (for speed)
    ulong _grfclok;
    PDynamicArray _pglalad; // the registered alarms

  public:
    Clock(long hid, ulong grfclok = fclokNil);
    ~Clock(void);
    static PClock PclokFromHid(long hid);
    static void BuryCmh(PCMH pcmh);
    void RemoveCmh(PCMH pcmh);

    void Start(ulong tim);
    void Stop(void);
    ulong TimCur(bool fAdjustForDelay = fFalse);
    ulong DtimAlarm(void)
    {
        return _dtimAlarm;
    }

    bool FSetAlarm(long dtim, PCMH pcmhNotify = pvNil, long lwUser = 0, bool fAdjustForDelay = fFalse);

    // idle handling
    virtual bool FCmdAll(PCMD pcmd);

#ifdef DEBUG
    static void MarkAllCloks(void);
#endif // DEBUG
};

#endif //! CLOK_H

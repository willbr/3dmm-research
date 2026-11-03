/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    These are globals common to the util layer.

***************************************************************************/
#ifndef UTILGLOB_H
#define UTILGLOB_H

/***************************************************************************
    Universal scalable application clock and other time stuff
***************************************************************************/
const LegacyUlong kdtsSecond = 1000;
const LegacyUlong kluTimeScaleNormal = 0x00010000;

typedef class UniversalScalableApplicationClock *PUniversalScalableApplicationClock;
#define UniversalScalableApplicationClock_PAR BASE
#define kclsUniversalScalableApplicationClock 'USAC'
class UniversalScalableApplicationClock : public UniversalScalableApplicationClock_PAR
{
    RTCLASS_DEC

  private:
    LegacyUlong _tsBaseSys; // base system time
    LegacyUlong _tsBaseApp; // base application time
    LegacyUlong _luScale;

  public:
    UniversalScalableApplicationClock(void);

    LegacyUlong TsCur(void);
    void Scale(LegacyUlong luScale);
    LegacyUlong LuScale(void)
    {
        return _luScale;
    }
    void Jump(LegacyUlong dtsJump)
    {
        _tsBaseApp += dtsJump;
    }
};

extern PUniversalScalableApplicationClock vpusac;

inline LegacyUlong TsCurrent(void)
{
    return vpusac->TsCur();
}
inline LegacyUlong TsCurrentSystem(void)
{
    return KauaiPlatform::SystemTicks();
}
// Caret blink timing belongs to the GUI runtime.
LegacyUlong DtsCaret(void);

/***************************************************************************
    Mutexes to protect various global linked lists, etc.
***************************************************************************/
#ifdef DEBUG
extern Mutex vmutxBase;
#endif // DEBUG
extern Mutex vmutxMem;

/***************************************************************************
    Global random number generator and shuffler. These are used by the
    script interpreter.
***************************************************************************/
extern Shuffler vsflUtil;
extern Random vrndUtil;

/***************************************************************************
    Global standard Kauai codec, compression manager, and pointer to
    a compression manager. The blck-level compression uses vpcodmUtil.
    Clients are free to redirect this to their own compression manager.
***************************************************************************/
extern KauaiCodec vkcdcUtil;
extern CodecManager vcodmUtil;
extern PCodecManager vpcodmUtil;

/***************************************************************************
    Debug memory globals
***************************************************************************/
#ifdef DEBUG
extern DebugMemoryGlobals vdmglob;
#endif // DEBUG

#endif //! UTILGLOB_H

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
#include "mm/clock.h"

/***************************************************************************
    Universal scalable application clock and other time stuff
***************************************************************************/
const LegacyUlong kdtsSecond = MM_CLOCK_MILLISECONDS_PER_SECOND;
const LegacyUlong kluTimeScaleNormal = MM_CLOCK_NORMAL_SCALE;

typedef class UniversalScalableApplicationClock *PUniversalScalableApplicationClock;
#define UniversalScalableApplicationClock_PAR BASE
#define kclsUniversalScalableApplicationClock 'USAC'
class UniversalScalableApplicationClock : public UniversalScalableApplicationClock_PAR
{
    RTCLASS_DEC

  private:
    MMScaledClock _clock;

  public:
    UniversalScalableApplicationClock(void);

    LegacyUlong TsCur(void);
    void Scale(LegacyUlong luScale);
    LegacyUlong LuScale(void)
    {
        return _clock.scale;
    }
    void Jump(LegacyUlong dtsJump)
    {
        mm_scaled_clock_jump(&_clock, dtsJump);
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
extern Mutex &vmutxBase;
#endif // DEBUG
extern Mutex &vmutxMem;

/***************************************************************************
    Global random number generator and shuffler. These are used by the
    script interpreter.
***************************************************************************/
extern Shuffler vsflUtil;
extern Random vrndUtil;

/***************************************************************************
    Global standard compression manager and pointer to
    a compression manager. The blck-level compression uses vpcodmUtil.
    Clients are free to redirect this to their own compression manager.
***************************************************************************/
extern CodecManager vcodmUtil;
extern PCodecManager vpcodmUtil;

/***************************************************************************
    Debug memory globals
***************************************************************************/
#ifdef DEBUG
extern DebugMemoryGlobals vdmglob;
#endif // DEBUG

#endif //! UTILGLOB_H

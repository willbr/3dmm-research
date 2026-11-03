/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Some "random" stuff

***************************************************************************/
#include "util.h"

ASSERTNAME

RTCLASS(Random)
RTCLASS(Shuffler)

/***************************************************************************
    Constructs a pseudo-random number generator.  If luSeed is zero,
    generates a seed from the current system time (TsCurrentSystem).
***************************************************************************/
Random::Random(LegacyUlong luSeed)
{
    if (0 == luSeed)
    {
        luSeed = TsCurrentSystem();
        SwapBytesRglw((LegacyLong *)&luSeed, 1);
    }
    _luSeed = luSeed;
    AssertThis(0);
}

/***************************************************************************
    Return the next pseudo-random number within the range 0 to lwLim - 1,
    inclusive.
***************************************************************************/
LegacyLong Random::LwNext(LegacyLong lwLim)
{
    AssertThis(0);
    AssertIn(lwLim, 1, kcbMax);

    // high bits are more random than the low ones
    // See Knuth vol 2, page 102, line 24 of table 1.
    // value of kdluRand doesn't matter much
    const LegacyUlong kluRandMul = 1566083941L;
    const LegacyLong kdluRand = 2531011L;
    LegacyLong lw;

    _luSeed = _luSeed * kluRandMul + kdluRand;

    // multiply lw by lwLim and divide by 2^32
#ifdef IN_80386
    LegacyUlong luSeedT = _luSeed;
    __asm
    {
		mov		eax,luSeedT
		mul		lwLim
		mov		lw,edx
    }
#else  //! IN_80386
    double dou;

    dou = (double)_luSeed * lwLim / (double)0x40000000 / 4;
    lw = (LegacyLong)dou;
#endif //! IN_80386

    Assert(lw < lwLim, "random number out of range");
    return lw;
}

/***************************************************************************
    Constructs a shuffled array.
***************************************************************************/
Shuffler::Shuffler(LegacyUlong luSeed) : Random(luSeed)
{
    _clw = 0;
    _ilw = 0;
    _fCustom = fFalse;
    _hqrglw = hqNil;
    AssertThis(0);
}

/***************************************************************************
    Destructs a shuffled array.
***************************************************************************/
Shuffler::~Shuffler(void)
{
    AssertThis(0);
    FreePhq(&_hqrglw);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a Shuffler.
***************************************************************************/
void Shuffler::AssertValid(LegacyUlong grf)
{
    Shuffler_PAR::AssertValid(0);
    if (_hqrglw != hqNil)
    {
        AssertHq(_hqrglw);
        Assert(CbOfHq(_hqrglw) == LwMul(_clw, size(LegacyLong)), "HQ wrong size");
    }
    else
        Assert(0 == _clw, "_clw wrong");
}

/***************************************************************************
    Mark memory for the Shuffler.
***************************************************************************/
void Shuffler::MarkMem(void)
{
    AssertValid(0);
    Shuffler_PAR::MarkMem();
    MarkHq(_hqrglw);
}
#endif // DEBUG

/***************************************************************************
    Shuffle the numbers [0, lwLim).
***************************************************************************/
void Shuffler::Shuffle(LegacyLong lwLim)
{
    AssertThis(0);
    AssertIn(lwLim, 0, kcbMax);
    LegacyLong ilw;
    LegacyLong *qrglw;

    if (!_FEnsureHq(lwLim))
        return;
    _ilw = 0;
    Assert(_clw == lwLim, "wrong _clw");

    qrglw = (LegacyLong *)QvFromHq(_hqrglw);
    // fill the array with [0, _clw)
    for (ilw = 0; ilw < _clw; ilw++)
        qrglw[ilw] = ilw;

    _fCustom = fFalse;
    _ShuffleCore();
}

/***************************************************************************
    Fill the Shuffler with the values in prglw and shuffle them.
***************************************************************************/
void Shuffler::ShuffleRglw(LegacyLong clw, LegacyLong *prglw)
{
    AssertThis(0);
    AssertIn(clw, 0, kcbMax);
    AssertPvCb(prglw, LwMul(clw, size(LegacyLong)));

    if (!_FEnsureHq(clw))
        return;
    _ilw = 0;
    Assert(_clw == clw, "wrong _clw");

    // fill the HQ with the stuff in prglw
    CopyPb(prglw, QvFromHq(_hqrglw), LwMul(clw, size(LegacyLong)));

    _fCustom = fTrue;
    _ShuffleCore();
}

/***************************************************************************
    Shuffle the entries in the HQ.
***************************************************************************/
void Shuffler::_ShuffleCore(void)
{
    AssertThis(0);
    Assert(_clw > 0, 0);
    LegacyLong lw;
    LegacyLong ilw, ilwSwap;
    LegacyLong *qrglw;

    // swap stuff
    qrglw = (LegacyLong *)QvFromHq(_hqrglw);
    for (ilw = _clw; --ilw > 0;)
    {
        ilwSwap = Random::LwNext(ilw + 1);
        if (ilwSwap < ilw)
        {
            lw = qrglw[ilw];
            qrglw[ilw] = qrglw[ilwSwap];
            qrglw[ilwSwap] = lw;
        }
    }
}

/***************************************************************************
    Make sure the HQ is the correct size and set clw appropriately.
***************************************************************************/
bool Shuffler::_FEnsureHq(LegacyLong clw)
{
    AssertThis(0);
    AssertIn(clw, 0, kcbMax);

    if (clw <= 0)
        goto LFail;

    if (_hqrglw == hqNil && !FAllocHq(&_hqrglw, LwMul(_clw = clw, size(LegacyLong)), fmemNil, mprNormal))
    {
        goto LFail;
    }
    if (clw != _clw && !FResizePhq(&_hqrglw, LwMul(_clw = clw, size(LegacyLong)), fmemNil, mprNormal))
    {
    LFail:
        // we are low on memory, so be nice and give some up
        FreePhq(&_hqrglw);
        _clw = 0;
        AssertThis(0);
        return fFalse;
        ;
    }
    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Returns the next number in a shuffled array of numbers.  If lwLim is
    zero, uses the numbers already in the Shuffler.  Otherwise, numbers
    range from 0 to (lwLim - 1), inclusive.
***************************************************************************/
LegacyLong Shuffler::LwNext(LegacyLong lwLim)
{
    AssertThis(0);
    AssertIn(lwLim, 0, kcbMax);

    if (lwLim > 0 && (lwLim != _clw || _fCustom))
    {
        // need to reshuffle with standard values
        Shuffle(lwLim);
        _ilw = 0;
        if (0 == _clw)
        {
            // shuffling failed, just use the regular random number
            return Random::LwNext(lwLim);
        }
    }
    else if (_clw == 0)
    {
        // no values in the HQ, just return 0
        _ilw = 0;
        return 0;
    }
    else if (_ilw >= _clw)
    {
        // need to reshuffle the values already in the HQ
        _ShuffleCore();
        _ilw = 0;
    }

    AssertIn(_ilw, 0, _clw);
    return ((LegacyLong *)QvFromHq(_hqrglw))[_ilw++];
}

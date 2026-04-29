/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Random number generator and shuffler stuff.

***************************************************************************/
#ifndef UTILRND_H
#define UTILRND_H

/***************************************************************************
    A pseudo-random number generator. LwNext returns values from 0 to
    (lwLim - 1), inclusive.
***************************************************************************/
typedef class Random *PRandom;
#define Random_PAR BASE
#define kclsRandom 'RND'
class Random : public Random_PAR
{
    RTCLASS_DEC
    NOCOPY(Random)

  protected:
    ulong _luSeed;

  public:
    Random(ulong luSeed = 0L);
    virtual long LwNext(long lwLim);
};

/***************************************************************************
    A shuffled array of numbers.
***************************************************************************/
typedef class Shuffler *PShuffler;
#define Shuffler_PAR Random
#define kclsShuffler 'SFL'
class Shuffler : public Shuffler_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(Shuffler)

  protected:
    long _clw;
    long _ilw;
    HQ _hqrglw;
    bool _fCustom; // false iff the values in the hq are [0, _clw)

    bool _FEnsureHq(long clw);
    void _ShuffleCore(void);

  public:
    Shuffler(ulong luSeed = 0L);
    ~Shuffler(void);
    void Shuffle(long lwLim);
    void ShuffleRglw(long clw, long *prglw);

    virtual long LwNext(long lwLim = 0);
};

#endif // UTILRND_H

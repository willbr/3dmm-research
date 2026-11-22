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
#include "mm/random.h"

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
    MMRandom _random;

  public:
    Random(LegacyUlong luSeed = 0L);
    virtual LegacyLong LwNext(LegacyLong lwLim);
    MMRandom *RandomCore()
    {
        return &_random;
    }
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
    MMShuffle _deck;

  public:
    Shuffler(LegacyUlong luSeed = 0L);
    ~Shuffler(void);
    void Shuffle(LegacyLong lwLim);
    void ShuffleRglw(LegacyLong clw, const void *words);

    virtual LegacyLong LwNext(LegacyLong lwLim = 0);
    MMShuffle *ShuffleCore()
    {
        return &_deck;
    }
};

#endif // UTILRND_H

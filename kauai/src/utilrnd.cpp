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
#include "handle_services.h"

ASSERTNAME

RTCLASS(Random)
RTCLASS(Shuffler)

/***************************************************************************
    Constructs a pseudo-random number generator.  If luSeed is zero,
    generates a seed from the current system time (TsCurrentSystem).
***************************************************************************/
Random::Random(LegacyUlong luSeed)
{
    _random.seed = luSeed ? luSeed : mm_random_seed_from_ticks(TsCurrentSystem());
    AssertThis(0);
}

LegacyLong Random::LwNext(LegacyLong lwLim)
{
    AssertThis(0);
    AssertIn(lwLim, 1, kcbMax);
    return mm_random_bounded(&_random, static_cast<MMU32>(lwLim));
}

Shuffler::Shuffler(LegacyUlong luSeed) : Random(luSeed)
{
    AssertDo(mm_shuffle_init(&_deck, &_random, KauaiCopyAllocator()) == MM_STORAGE_OK, 0);
    AssertThis(0);
}

Shuffler::~Shuffler(void)
{
    AssertThis(0);
    mm_shuffle_dispose(&_deck);
}

#ifdef DEBUG
void Shuffler::AssertValid(LegacyUlong)
{
    Shuffler_PAR::AssertValid(0);
    Assert(_deck.storage.buffers[0].capacity == _deck.count * sizeof(MMI32), "wrong deck size");
    Assert(_deck.next <= _deck.count, "bad deck cursor");
}

void Shuffler::MarkMem(void)
{
    AssertValid(0);
    Shuffler_PAR::MarkMem();
    MarkPv(_deck.storage.buffers[0].data);
}
#endif // DEBUG

void Shuffler::Shuffle(LegacyLong lwLim)
{
    AssertThis(0);
    AssertIn(lwLim, 0, kcbMax);
    mm_shuffle_set(&_deck, lwLim, nullptr);
}

void Shuffler::ShuffleRglw(LegacyLong clw, const void *words)
{
    AssertThis(0);
    AssertIn(clw, 0, kcbMax);
    AssertPvCb(const_cast<void *>(words), LwMul(clw, size(LegacyLong)));
    mm_shuffle_set(&_deck, clw, words);
}

LegacyLong Shuffler::LwNext(LegacyLong lwLim)
{
    AssertThis(0);
    AssertIn(lwLim, 0, kcbMax);
    return mm_shuffle_next(&_deck, lwLim);
}

/* Legacy pseudo-random draws and shuffled decks. */
#ifndef MM_ENGINE_RANDOM_H
#define MM_ENGINE_RANDOM_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMRandom
    {
        MMU32 seed;
    } MMRandom;

    MMU32 mm_random_seed_from_ticks(MMU32 ticks);
    MMU32 mm_random_step(MMU32 seed);
    /* Advance the seed and return floor(seed * limit / 2^32). */
    MMU32 mm_random_bounded(MMRandom *, MMU32 limit);

    typedef struct MMShuffle
    {
        MMCollectionStorage storage;
        MMRandom *random;
        MMU32 count, next;
        int custom;
    } MMShuffle;

    /* The generator is borrowed for the lifetime of the deck. */
    int mm_shuffle_init(MMShuffle *, MMRandom *, const MMStorageAllocator *);
    void mm_shuffle_dispose(MMShuffle *);
    /* Copy count native 32-bit words from independent storage. NULL words
     * selects [0, count). An invalid size or failed allocation clears the deck. */
    int mm_shuffle_set(MMShuffle *, MMI32 count, const void *words);
    /* Zero draws from the existing deck. A positive limit selects a standard
     * deck of that size, falling back to one random draw if allocation fails. */
    MMI32 mm_shuffle_next(MMShuffle *, MMI32 limit);
#ifdef __cplusplus
}
#endif
#endif

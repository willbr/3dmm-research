/* Random and shuffle algorithms converted from Kauai utilrnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/random.h"
#include "mm/runtime_math.h"
#include <string.h>

MMU32 mm_random_seed_from_ticks(MMU32 ticks)
{
    return (ticks << 24) | ((ticks << 8) & 0x00ff0000UL) |
           ((ticks >> 8) & 0x0000ff00UL) | (ticks >> 24);
}

MMU32 mm_random_step(MMU32 seed)
{
    return seed * (MMU32)1566083941UL + (MMU32)2531011UL;
}

MMU32 mm_random_bounded(MMRandom *random, MMU32 limit)
{
    random->seed = mm_random_step(random->seed);
    return mm_runtime_counter_multiply(random->seed, limit).high;
}

int mm_shuffle_init(MMShuffle *deck, MMRandom *random, const MMStorageAllocator *allocator)
{
    int status;
    if (!deck || !random)
        return MM_STORAGE_ARGUMENT;
    status = mm_storage_init(&deck->storage, allocator);
    if (status != MM_STORAGE_OK)
        return status;
    deck->random = random;
    deck->count = deck->next = 0;
    deck->custom = 0;
    return MM_STORAGE_OK;
}

void mm_shuffle_dispose(MMShuffle *deck)
{
    if (mm_storage_dispose(&deck->storage) == MM_STORAGE_OK)
    {
        deck->count = deck->next = 0;
        deck->custom = 0;
    }
}

static void shuffle_words(MMShuffle *deck)
{
    unsigned char *data = deck->storage.buffers[0].data;
    MMU32 remaining = deck->count, pick;
    unsigned char *last, *chosen;
    MMI32 saved;
    while (remaining > 1)
    {
        pick = mm_random_bounded(deck->random, remaining);
        --remaining;
        if (pick != remaining)
        {
            last = data + remaining * sizeof(saved);
            chosen = data + pick * sizeof(saved);
            memcpy(&saved, last, sizeof(saved));
            memcpy(last, chosen, sizeof(saved));
            memcpy(chosen, &saved, sizeof(saved));
        }
    }
    deck->next = 0;
}

int mm_shuffle_set(MMShuffle *deck, MMI32 count, const void *words)
{
    MMU32 bytes;
    MMI32 word;
    unsigned char *data;
    if (count <= 0 || (MMU32)count > (MM_STORAGE_LIMIT - 1) / sizeof(word))
    {
        mm_shuffle_dispose(deck);
        return 0;
    }
    bytes = (MMU32)count * sizeof(word);
    if (deck->count != (MMU32)count &&
        mm_storage_resize_exact(&deck->storage, 0, bytes, MM_STORAGE_NORMAL) != MM_STORAGE_OK)
    {
        mm_shuffle_dispose(deck);
        return 0;
    }
    data = deck->storage.buffers[0].data;
    if (words)
        memcpy(data, words, bytes);
    else
        for (word = 0; word < count; ++word)
            memcpy(data + (MMU32)word * sizeof(word), &word, sizeof(word));
    deck->count = (MMU32)count;
    deck->custom = words != NULL;
    shuffle_words(deck);
    return 1;
}

MMI32 mm_shuffle_next(MMShuffle *deck, MMI32 limit)
{
    MMI32 word;
    if (limit > 0 && ((MMU32)limit != deck->count || deck->custom))
    {
        if (!mm_shuffle_set(deck, limit, NULL))
            return (MMI32)mm_random_bounded(deck->random, (MMU32)limit);
    }
    if (!deck->count)
        return 0;
    if (deck->next == deck->count)
        shuffle_words(deck);
    memcpy(&word, deck->storage.buffers[0].data + deck->next * sizeof(word), sizeof(word));
    ++deck->next;
    return word;
}

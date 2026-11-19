#include "mm/movie_source_ids.h"
#include "mm/collections.h"

typedef struct SourceId
{
    MMI32 id;
    void *archive;
} SourceId;

static const SourceId *items(const MMMovieSourceIds *sources)
{
    return (const SourceId *)sources->storage.buffers[0].data;
}

static MMU32 lower(const MMMovieSourceIds *sources, MMI32 id)
{
    const SourceId *entries = items(sources);
    MMU32 first = 0, last = sources->count, middle;
    while (first < last)
    {
        middle = first + (last - first) / 2;
        if (entries[middle].id < id)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

int mm_movie_source_ids_init(MMMovieSourceIds *sources, const MMStorageAllocator *allocator)
{
    if (mm_storage_init(&sources->storage, allocator) != MM_STORAGE_OK)
        return 0;
    sources->count = 0;
    sources->next = 0x7ffffffeL;
    sources->storage.min_grow[0] = 8 * sizeof(SourceId);
    return 1;
}

void mm_movie_source_ids_clear(MMMovieSourceIds *sources, void (*release)(void *))
{
    const SourceId *entries = items(sources);
    MMU32 i;
    for (i = 0; i < sources->count; ++i)
        if (entries[i].archive)
            release(entries[i].archive);
    mm_storage_dispose(&sources->storage);
    sources->count = 0;
}

void mm_movie_source_ids_swap(MMMovieSourceIds *first, MMMovieSourceIds *second)
{
    MMMovieSourceIds saved = *first;
    *first = *second;
    *second = saved;
}

int mm_movie_source_ids_find(const MMMovieSourceIds *sources, MMI32 id, void **archive)
{
    const SourceId *entries = items(sources);
    MMU32 position = lower(sources, id);
    if (position == sources->count || entries[position].id != id)
        return 0;
    if (archive)
        *archive = entries[position].archive;
    return 1;
}

int mm_movie_source_ids_add(MMMovieSourceIds *sources, MMI32 id, void *archive)
{
    SourceId entry;
    MMArray array = {0};
    MMU32 position = lower(sources, id);
    MMU32 growth = sources->storage.buffers[0].capacity / 2;
    if (position < sources->count && items(sources)[position].id == id)
        return MM_MOVIE_SOURCE_ID_KNOWN;
    entry.id = id;
    entry.archive = archive;
    array.storage = &sources->storage;
    array.width = sizeof(entry);
    array.count = sources->count;
    array.free_count = -1;
    if (growth > sources->storage.min_grow[0])
        sources->storage.min_grow[0] = growth;
    if (mm_array_insert(&array, position, &entry, 1) != MM_STORAGE_OK)
        return MM_MOVIE_SOURCE_ID_MEMORY;
    sources->count = array.count;
    return MM_MOVIE_SOURCE_ID_ADDED;
}

int mm_movie_source_ids_retail_alias(const MMMovieSourceIds *sources, MMI32 *id)
{
    const MMI32 maximum = 0x7fffffffL;
    const SourceId *entries = items(sources);
    const MMI32 last = sources->count ? entries[sources->count - 1].id : 0;
    MMI32 candidate = 2;
    MMU32 i;
    if (last < maximum)
        candidate = last >= 2 ? last + 1 : 2;
    else
        for (i = lower(sources, candidate); i < sources->count && entries[i].id == candidate; ++i)
        {
            if (candidate == maximum)
                return 0;
            ++candidate;
        }
    *id = candidate;
    return 1;
}

void mm_movie_source_ids_limit(MMMovieSourceIds *sources, MMI32 cursor)
{
    if (cursor < sources->next)
        sources->next = cursor;
}

int mm_movie_source_ids_take(MMMovieSourceIds *sources, MMI32 retail, MMI32 *id)
{
    MMI32 candidate = sources->next;
    while (candidate > 2 && (candidate == retail || mm_movie_source_ids_find(sources, candidate, NULL)))
        --candidate;
    if (candidate <= 2)
        return 0;
    sources->next = candidate - 1;
    *id = candidate;
    return 1;
}

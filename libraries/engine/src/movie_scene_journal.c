#include "mm/movie_scene_journal.h"

static MMArray array(MMCollectionStorage *storage, MMU32 width, MMU32 count)
{
    MMArray records = {0};
    records.storage = storage;
    records.width = width;
    records.count = count;
    records.free_count = -1;
    return records;
}

static int insert(MMArray *records, MMU32 index, const void *value)
{
    MMU32 growth = records->storage->buffers[0].capacity / 2;
    records->storage->min_grow[0] = growth < records->width * 8 ? records->width * 8 : growth;
    return mm_array_insert(records, index, value, 1) == MM_STORAGE_OK;
}

int mm_movie_scene_definitions_init(MMMovieSceneDefinitions *definitions, const MMStorageAllocator *allocator)
{
    definitions->count = 0;
    return mm_storage_init(&definitions->storage, allocator) == MM_STORAGE_OK;
}

void mm_movie_scene_definitions_dispose(MMMovieSceneDefinitions *definitions)
{
    mm_storage_dispose(&definitions->storage);
    definitions->count = 0;
}

int mm_movie_scene_definitions_copy(MMMovieSceneDefinitions *destination, const MMMovieSceneDefinitions *source)
{
    if (destination != source &&
        mm_storage_copy(&destination->storage, &source->storage, source->count * sizeof(MMPreviewLiveScene), 0) != MM_STORAGE_OK)
        return 0;
    destination->count = source->count;
    return 1;
}

const MMPreviewLiveScene *mm_movie_scene_definitions_data(const MMMovieSceneDefinitions *definitions)
{
    return (const MMPreviewLiveScene *)definitions->storage.buffers[0].data;
}

int mm_movie_scene_definitions_uses_source(const MMMovieSceneDefinitions *definitions, MMI32 source)
{
    const MMPreviewLiveScene *items = mm_movie_scene_definitions_data(definitions);
    MMU32 i;
    for (i = 0; i < definitions->count; ++i)
        if (items[i].source == source)
            return 1;
    return 0;
}

int mm_movie_scene_definitions_append(MMMovieSceneDefinitions *definitions, const MMPreviewLiveScene *input)
{
    MMArray records = array(&definitions->storage, sizeof(*input), definitions->count);
    MMPreviewLiveScene value = *input;
    if (!insert(&records, records.count, &value))
        return 0;
    definitions->count = records.count;
    return 1;
}

void mm_movie_scene_definitions_retarget(MMMovieSceneDefinitions *definitions, const MMPreviewLiveScene *input)
{
    MMPreviewLiveScene *items = (MMPreviewLiveScene *)definitions->storage.buffers[0].data;
    MMPreviewLiveScene value = *input;
    MMU32 i;
    for (i = 0; i < definitions->count; ++i)
        if (items[i].number == value.number)
            items[i] = value;
}

int mm_movie_camera_journal_init(MMMovieCameraJournal *journal, const MMStorageAllocator *allocator)
{
    journal->count = 0;
    return mm_storage_init(&journal->storage, allocator) == MM_STORAGE_OK;
}

void mm_movie_camera_journal_dispose(MMMovieCameraJournal *journal)
{
    mm_storage_dispose(&journal->storage);
    journal->count = 0;
}

int mm_movie_camera_journal_copy(MMMovieCameraJournal *destination, const MMMovieCameraJournal *source)
{
    if (destination != source &&
        mm_storage_copy(&destination->storage, &source->storage, source->count * sizeof(MMMovieCameraEdit), 0) != MM_STORAGE_OK)
        return 0;
    destination->count = source->count;
    return 1;
}

const MMMovieCameraEdit *mm_movie_camera_journal_data(const MMMovieCameraJournal *journal)
{
    return (const MMMovieCameraEdit *)journal->storage.buffers[0].data;
}

static MMU32 lower(const MMMovieCameraJournal *journal, MMU32 scene, MMI32 frame)
{
    const MMMovieCameraEdit *items = mm_movie_camera_journal_data(journal);
    MMU32 first = 0, last = journal->count, middle;
    while (first < last)
    {
        middle = first + (last - first) / 2;
        if (items[middle].scene < scene || (items[middle].scene == scene && items[middle].frame < frame))
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

int mm_movie_camera_journal_set(MMMovieCameraJournal *journal, const MMMovieCameraEdit *input)
{
    MMMovieCameraEdit *items = (MMMovieCameraEdit *)journal->storage.buffers[0].data;
    MMMovieCameraEdit value = *input;
    MMArray records;
    MMU32 position = lower(journal, value.scene, value.frame);
    if (position < journal->count && items[position].scene == value.scene && items[position].frame == value.frame)
    {
        items[position].camera = value.camera;
        return 1;
    }
    records = array(&journal->storage, sizeof(value), journal->count);
    if (!insert(&records, position, &value))
        return 0;
    journal->count = records.count;
    return 1;
}

const MMMovieCameraEdit *mm_movie_camera_journal_find(const MMMovieCameraJournal *journal, MMU32 scene, MMU32 *count)
{
    const MMI32 first_frame = (-2147483647 - 1);
    MMU32 first = lower(journal, scene, first_frame);
    MMU32 end = scene == (MMU32)-1 ? journal->count : lower(journal, scene + 1, first_frame);
    *count = end - first;
    return *count ? mm_movie_camera_journal_data(journal) + first : NULL;
}

static void preview_camera(void *context, MMU32 index, MMPreviewLiveCamera *out)
{
    const MMMovieCameraEdit *items = (const MMMovieCameraEdit *)context;
    out->frame = items[index].frame;
    out->camera = items[index].camera;
}

MMPreviewLiveCameraEdits mm_movie_camera_journal_preview(const MMMovieCameraJournal *journal, MMU32 scene)
{
    MMPreviewLiveCameraEdits result;
    result.context = (void *)mm_movie_camera_journal_find(journal, scene, &result.count);
    result.get = preview_camera;
    return result;
}

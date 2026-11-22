#include "mm/movie_scene_journal.h"
#include "mm/movie_placement_batch.h"
#include "mm/material_resource.h"
#include <math.h>
#include <string.h>

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

int mm_movie_range_journal_init(MMMovieRangeJournal *journal, const MMStorageAllocator *allocator)
{
    journal->count = 0;
    return mm_storage_init(&journal->storage, allocator) == MM_STORAGE_OK;
}

void mm_movie_range_journal_dispose(MMMovieRangeJournal *journal)
{
    mm_storage_dispose(&journal->storage);
    journal->count = 0;
}

int mm_movie_range_journal_copy(MMMovieRangeJournal *destination, const MMMovieRangeJournal *source)
{
    if (destination != source &&
        mm_storage_copy(&destination->storage, &source->storage, source->count * sizeof(MMMovieSceneRange), 0) != MM_STORAGE_OK)
        return 0;
    destination->count = source->count;
    return 1;
}

const MMMovieSceneRange *mm_movie_range_journal_data(const MMMovieRangeJournal *journal)
{
    return (const MMMovieSceneRange *)journal->storage.buffers[0].data;
}

static MMU32 range_lower(const MMMovieRangeJournal *journal, MMU32 scene)
{
    const MMMovieSceneRange *items = mm_movie_range_journal_data(journal);
    MMU32 first = 0, last = journal->count, middle;
    while (first < last)
    {
        middle = first + (last - first) / 2;
        if (items[middle].scene < scene)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

const MMMovieSceneRange *mm_movie_range_journal_find(const MMMovieRangeJournal *journal, MMU32 scene)
{
    const MMMovieSceneRange *items = mm_movie_range_journal_data(journal);
    MMU32 position = range_lower(journal, scene);
    return position < journal->count && items[position].scene == scene ? items + position : NULL;
}

int mm_movie_range_journal_set(MMMovieRangeJournal *journal, const MMMovieSceneRange *input)
{
    MMMovieSceneRange *items = (MMMovieSceneRange *)journal->storage.buffers[0].data;
    MMMovieSceneRange value = *input;
    MMU32 position = range_lower(journal, value.scene);
    MMArray records;
    if (value.first > value.last)
        return 0;
    if (position < journal->count && items[position].scene == value.scene)
    {
        items[position] = value;
        return 1;
    }
    records = array(&journal->storage, sizeof(value), journal->count);
    if (!insert(&records, position, &value))
        return 0;
    journal->count = records.count;
    return 1;
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

int mm_movie_prop_journal_init(MMMoviePropJournal *journal, const MMStorageAllocator *allocator,
                               const MMMovieEditFileServices *files)
{
    journal->count = 0;
    journal->files = files;
    return mm_storage_init(&journal->storage, allocator) == MM_STORAGE_OK;
}

const MMMoviePropEdit *mm_movie_prop_journal_data(const MMMoviePropJournal *journal)
{
    return (const MMMoviePropEdit *)journal->storage.buffers[0].data;
}

void mm_movie_prop_journal_dispose(MMMoviePropJournal *journal)
{
    const MMMoviePropEdit *items = mm_movie_prop_journal_data(journal);
    MMU32 i;
    for (i = 0; i < journal->count; ++i)
        if (items[i].file)
            journal->files->release(items[i].file);
    mm_storage_dispose(&journal->storage);
    journal->count = 0;
}

int mm_movie_prop_journal_copy(MMMoviePropJournal *destination, const MMMoviePropJournal *source)
{
    MMMoviePropJournal copy, old;
    const MMMoviePropEdit *items;
    MMU32 i;
    if (destination == source)
        return 1;
    if (destination->files != source->files ||
        !mm_movie_prop_journal_init(&copy, &destination->storage.allocator, destination->files))
        return 0;
    if (mm_storage_copy(&copy.storage, &source->storage, source->count * sizeof(*items), 0) != MM_STORAGE_OK)
    {
        mm_movie_prop_journal_dispose(&copy);
        return 0;
    }
    items = mm_movie_prop_journal_data(&copy);
    for (i = 0; i < source->count; ++i)
        if (items[i].file)
            copy.files->retain(items[i].file);
    copy.count = source->count;
    old = *destination;
    *destination = copy;
    mm_movie_prop_journal_dispose(&old);
    return 1;
}

int mm_movie_prop_journal_append(MMMoviePropJournal *journal, const MMPreviewLiveProp *definition,
                                const char *name, void *file)
{
    MMMoviePropEdit value;
    MMArray records = array(&journal->storage, sizeof(value), journal->count);
    size_t length = strlen(name);
    if (length >= sizeof(value.name) || (file && !journal->files))
        return 0;
    memset(&value, 0, sizeof(value));
    value.definition = *definition;
    value.file = file;
    memcpy(value.name, name, length + 1);
    if (!insert(&records, records.count, &value))
        return 0;
    if (file)
        journal->files->retain(file);
    journal->count = records.count;
    return 1;
}

int mm_movie_prop_journal_started(const MMMoviePropJournal *journal, MMU32 scene, MMI32 frame)
{
    const MMMoviePropEdit *items = mm_movie_prop_journal_data(journal);
    MMU32 i;
    for (i = 0; i < journal->count; ++i)
        if (items[i].definition.scene == scene && items[i].definition.first <= frame)
            return 1;
    return 0;
}

int mm_movie_prop_placement_valid(const float point[3])
{
    int i;
    for (i = 0; i < 3; ++i)
        if (!(point[i] >= -32700.0f && point[i] <= 32700.0f))
            return 0;
    return 1;
}

static void preview_scene(void *context, MMU32 index, MMPreviewLiveScene *out)
{
    const MMMovieLiveDefinitions *definitions = (const MMMovieLiveDefinitions *)context;
    *out = mm_movie_scene_definitions_data(definitions->scenes)[index];
}

static void preview_prop(void *context, MMU32 index, MMPreviewLiveProp *out)
{
    const MMMovieLiveDefinitions *definitions = (const MMMovieLiveDefinitions *)context;
    *out = mm_movie_prop_journal_data(definitions->props)[index].definition;
}

MMPreviewLiveDefinitions mm_movie_definitions_preview(MMMovieLiveDefinitions *definitions)
{
    MMPreviewLiveDefinitions view;
    view.context = definitions;
    view.scene_count = definitions->scenes->count;
    view.prop_count = definitions->props->count;
    view.scene = preview_scene;
    view.prop = preview_prop;
    return view;
}

int mm_movie_track_journal_init(MMMovieActorTrackJournal *journal, const MMStorageAllocator *allocator,
                               const MMMovieEditFileServices *files)
{
    journal->count = 0;
    journal->files = files;
    return mm_storage_init(&journal->storage, allocator) == MM_STORAGE_OK;
}

const MMMovieActorTrackEdit *mm_movie_track_journal_data(const MMMovieActorTrackJournal *journal)
{
    return (const MMMovieActorTrackEdit *)journal->storage.buffers[0].data;
}

void mm_movie_track_journal_dispose(MMMovieActorTrackJournal *journal)
{
    const MMMovieActorTrackEdit *items = mm_movie_track_journal_data(journal);
    MMU32 i;
    for (i = 0; i < journal->count; ++i)
        journal->files->release(items[i].file);
    mm_storage_dispose(&journal->storage);
    journal->count = 0;
}

int mm_movie_track_journal_copy(MMMovieActorTrackJournal *destination, const MMMovieActorTrackJournal *source)
{
    MMMovieActorTrackJournal copy, old;
    const MMMovieActorTrackEdit *items;
    MMU32 i;
    if (destination == source)
        return 1;
    if (destination->files != source->files ||
        !mm_movie_track_journal_init(&copy, &destination->storage.allocator, destination->files))
        return 0;
    if (mm_storage_copy(&copy.storage, &source->storage, source->count * sizeof(*items), 0) != MM_STORAGE_OK)
    {
        mm_movie_track_journal_dispose(&copy);
        return 0;
    }
    items = mm_movie_track_journal_data(&copy);
    for (i = 0; i < source->count; ++i)
        copy.files->retain(items[i].file);
    copy.count = source->count;
    old = *destination;
    *destination = copy;
    mm_movie_track_journal_dispose(&old);
    return 1;
}

static MMU32 track_lower(const MMMovieActorTrackJournal *journal, MMU32 scene, MMU32 actor)
{
    const MMMovieActorTrackEdit *items = mm_movie_track_journal_data(journal);
    MMU32 first = 0, last = journal->count, middle;
    while (first < last)
    {
        middle = first + (last - first) / 2;
        if (items[middle].scene < scene || (items[middle].scene == scene && items[middle].actor < actor))
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

int mm_movie_track_journal_set(MMMovieActorTrackJournal *journal, const MMMovieActorTrackEdit *input)
{
    MMMovieActorTrackEdit *items = (MMMovieActorTrackEdit *)journal->storage.buffers[0].data;
    MMMovieActorTrackEdit value = *input;
    MMU32 position = track_lower(journal, value.scene, value.actor);
    MMArray records;
    if (position < journal->count && items[position].scene == value.scene && items[position].actor == value.actor)
    {
        void *old = items[position].file;
        journal->files->retain(value.file);
        items[position] = value;
        journal->files->release(old);
        return 1;
    }
    records = array(&journal->storage, sizeof(value), journal->count);
    if (!insert(&records, position, &value))
        return 0;
    journal->files->retain(value.file);
    journal->count = records.count;
    return 1;
}

const MMMovieActorTrackEdit *mm_movie_track_journal_find(const MMMovieActorTrackJournal *journal, MMU32 scene, MMU32 actor)
{
    const MMMovieActorTrackEdit *items = mm_movie_track_journal_data(journal);
    MMU32 position = track_lower(journal, scene, actor);
    return position < journal->count && items[position].scene == scene && items[position].actor == actor
        ? items + position : NULL;
}

int mm_movie_track_journal_has_scene(const MMMovieActorTrackJournal *journal, MMU32 scene)
{
    const MMMovieActorTrackEdit *items = mm_movie_track_journal_data(journal);
    MMU32 position = track_lower(journal, scene, 0);
    return position < journal->count && items[position].scene == scene;
}

int mm_movie_text_journal_init(MMMovieTextJournal *journal, const MMStorageAllocator *allocator,
                               const MMMovieEditFileServices *files)
{
    journal->count = 0;
    journal->files = files;
    return mm_storage_init(&journal->storage, allocator) == MM_STORAGE_OK;
}

const MMMovieActorTextEdit *mm_movie_text_journal_data(const MMMovieTextJournal *journal)
{
    return (const MMMovieActorTextEdit *)journal->storage.buffers[0].data;
}

void mm_movie_text_journal_dispose(MMMovieTextJournal *journal)
{
    const MMMovieActorTextEdit *items = mm_movie_text_journal_data(journal);
    MMU32 i;
    for (i = 0; i < journal->count; ++i)
        journal->files->release(items[i].file);
    mm_storage_dispose(&journal->storage);
    journal->count = 0;
}

int mm_movie_text_journal_copy(MMMovieTextJournal *destination, const MMMovieTextJournal *source)
{
    MMMovieTextJournal copy, old;
    const MMMovieActorTextEdit *items;
    MMU32 i;
    if (destination == source)
        return 1;
    if (destination->files != source->files ||
        !mm_movie_text_journal_init(&copy, &destination->storage.allocator, destination->files))
        return 0;
    if (mm_storage_copy(&copy.storage, &source->storage, source->count * sizeof(*items), 0) != MM_STORAGE_OK)
    {
        mm_movie_text_journal_dispose(&copy);
        return 0;
    }
    items = mm_movie_text_journal_data(&copy);
    for (i = 0; i < source->count; ++i)
        copy.files->retain(items[i].file);
    copy.count = source->count;
    old = *destination;
    *destination = copy;
    mm_movie_text_journal_dispose(&old);
    return 1;
}

static MMU32 text_lower(const MMMovieTextJournal *journal, MMU32 scene, MMU32 actor)
{
    const MMMovieActorTextEdit *items = mm_movie_text_journal_data(journal);
    MMU32 first = 0, last = journal->count, middle;
    while (first < last)
    {
        middle = first + (last - first) / 2;
        if (items[middle].scene < scene || (items[middle].scene == scene && items[middle].actor < actor))
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

const MMMovieActorTextEdit *mm_movie_text_journal_find(const MMMovieTextJournal *journal, MMU32 scene, MMU32 actor)
{
    const MMMovieActorTextEdit *items = mm_movie_text_journal_data(journal);
    MMU32 position = text_lower(journal, scene, actor);
    return position < journal->count && items[position].scene == scene && items[position].actor == actor
        ? items + position : NULL;
}

int mm_movie_text_journal_set(MMMovieTextJournal *journal, const MMMovieActorTextEdit *input)
{
    MMMovieActorTextEdit *items = (MMMovieActorTextEdit *)journal->storage.buffers[0].data;
    MMMovieActorTextEdit value = *input;
    MMU32 position = text_lower(journal, value.scene, value.actor);
    MMArray records;
    if (!value.file || !journal->files)
        return 0;
    if (position < journal->count && items[position].scene == value.scene && items[position].actor == value.actor)
    {
        void *old = items[position].file;
        journal->files->retain(value.file);
        items[position] = value;
        journal->files->release(old);
        return 1;
    }
    records = array(&journal->storage, sizeof(value), journal->count);
    if (!insert(&records, position, &value))
        return 0;
    journal->files->retain(value.file);
    journal->count = records.count;
    return 1;
}

static int same_font(const MMMovieTextDescription *before, const MMMovieTextDescription *after)
{
    return before->font_source == after->font_source && before->font == after->font;
}

int mm_movie_text_font_change(const MMMovieTextDescription *before, const MMMovieTextDescription *after,
                              MMI32 retail_source)
{
    if (same_font(before, after))
        return MM_MOVIE_TEXT_KEEP_FONT;
    return after->font_source == retail_source ? MM_MOVIE_TEXT_RETAIL_FONT : MM_MOVIE_TEXT_UNAVAILABLE_FONT;
}

int mm_movie_text_change(const MMMovieTextDescription *before, const MMMovieTextDescription *after,
                         const MMMaterialAppearance *current, const MMMaterialAppearance *requested,
                         int preset, const MMMovieActorTextEdit *previous)
{
    int material = requested && (preset || !mm_material_appearance_equal(requested, current));
    if (!material && same_font(before, after) && before->shape == after->shape && before->bytes == after->bytes &&
        !memcmp(before->utf8, after->utf8, before->bytes))
        return 0;
    return MM_MOVIE_TEXT_CHANGED | ((material || (previous && previous->custom_material)) ? MM_MOVIE_TEXT_WRITE_MATERIAL : 0);
}

int mm_movie_pose_journal_init(MMMoviePoseJournal *journal, const MMStorageAllocator *allocator)
{
    journal->count = 0;
    return mm_storage_init(&journal->storage, allocator) == MM_STORAGE_OK;
}

void mm_movie_pose_journal_dispose(MMMoviePoseJournal *journal)
{
    mm_storage_dispose(&journal->storage);
    journal->count = 0;
}

int mm_movie_pose_journal_copy(MMMoviePoseJournal *destination, const MMMoviePoseJournal *source)
{
    if (destination != source &&
        mm_storage_copy(&destination->storage, &source->storage, source->count * sizeof(MMMovieActorPoseEdit), 0) !=
            MM_STORAGE_OK)
        return 0;
    destination->count = source->count;
    return 1;
}

const MMMovieActorPoseEdit *mm_movie_pose_journal_data(const MMMoviePoseJournal *journal)
{
    return (const MMMovieActorPoseEdit *)journal->storage.buffers[0].data;
}

static MMU32 pose_lower(const MMMoviePoseJournal *journal, MMU32 scene, MMU32 actor)
{
    const MMMovieActorPoseEdit *items = mm_movie_pose_journal_data(journal);
    MMU32 first = 0, last = journal->count, middle;
    while (first < last)
    {
        middle = first + (last - first) / 2;
        if (items[middle].scene < scene || (items[middle].scene == scene && items[middle].actor < actor))
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

static const MMMovieActorPoseEdit *pose_at(const MMMoviePoseJournal *journal, MMU32 index, MMU32 scene, MMU32 actor)
{
    const MMMovieActorPoseEdit *items = mm_movie_pose_journal_data(journal);
    return index < journal->count && items[index].scene == scene && items[index].actor == actor ? items + index : NULL;
}

const MMMovieActorPoseEdit *mm_movie_pose_journal_find(const MMMoviePoseJournal *journal, MMU32 scene, MMU32 actor)
{
    return pose_at(journal, pose_lower(journal, scene, actor), scene, actor);
}

int mm_movie_pose_journal_has_scene(const MMMoviePoseJournal *journal, MMU32 scene)
{
    const MMMovieActorPoseEdit *items = mm_movie_pose_journal_data(journal);
    MMU32 position = pose_lower(journal, scene, 0);
    return position < journal->count && items[position].scene == scene;
}

static MMMovieActorPoseEdit identity_pose(MMU32 scene, MMU32 actor)
{
    MMMovieActorPoseEdit pose = {0};
    pose.scene = scene;
    pose.actor = actor;
    pose.shape.rotation[0] = pose.shape.rotation[4] = pose.shape.rotation[8] = 1;
    pose.shape.scale[0] = pose.shape.scale[1] = pose.shape.scale[2] = 1;
    return pose;
}

static int put_pose(MMMoviePoseJournal *journal, MMU32 index, int present, const MMMovieActorPoseEdit *pose)
{
    MMArray records = array(&journal->storage, sizeof(*pose), journal->count);
    if (present)
        ((MMMovieActorPoseEdit *)journal->storage.buffers[0].data)[index] = *pose;
    else if (!insert(&records, index, pose))
        return 0;
    journal->count = records.count;
    return 1;
}

int mm_movie_pose_journal_move(MMMoviePoseJournal *journal, MMU32 scene, MMU32 actor,
                               const float delta[3], float offset[3])
{
    MMU32 index = pose_lower(journal, scene, actor);
    const MMMovieActorPoseEdit *existing = pose_at(journal, index, scene, actor);
    MMMovieActorPoseEdit pose = existing ? *existing : identity_pose(scene, actor);
    unsigned axis;
    for (axis = 0; axis < 3; ++axis)
        pose.offset[axis] += delta[axis];
    if (!pose.has_shape && pose.offset[0] == 0 && pose.offset[1] == 0 && pose.offset[2] == 0)
    {
        if (existing)
        {
            MMArray records = array(&journal->storage, sizeof(pose), journal->count);
            if (mm_array_delete(&records, index, 1) != MM_STORAGE_OK)
                return 0;
            journal->count = records.count;
        }
    }
    else if (!put_pose(journal, index, existing != NULL, &pose))
        return 0;
    memcpy(offset, pose.offset, sizeof(pose.offset));
    return 1;
}

int mm_movie_pose_journal_shape(MMMoviePoseJournal *journal, MMU32 scene, MMU32 actor, const MMPreviewActorShape *shape)
{
    MMU32 index = pose_lower(journal, scene, actor);
    const MMMovieActorPoseEdit *existing = pose_at(journal, index, scene, actor);
    MMMovieActorPoseEdit pose = existing ? *existing : identity_pose(scene, actor);
    pose.shape = *shape;
    pose.has_shape = 1;
    return put_pose(journal, index, existing != NULL, &pose);
}

int mm_movie_pose_journal_include(MMMoviePoseJournal *journal, MMU32 scene, MMU32 actor)
{
    MMU32 index = pose_lower(journal, scene, actor);
    MMMovieActorPoseEdit pose;
    if (pose_at(journal, index, scene, actor))
        return 1;
    pose = identity_pose(scene, actor);
    return put_pose(journal, index, 0, &pose);
}

int mm_movie_pose_journal_export(MMMoviePoseJournal *destination, const MMMoviePoseJournal *poses,
                                 const MMMovieTextJournal *texts, const MMMovieActorTrackJournal *tracks)
{
    const MMMovieActorTextEdit *text = mm_movie_text_journal_data(texts);
    const MMMovieActorTrackEdit *track = mm_movie_track_journal_data(tracks);
    MMMoviePoseJournal result;
    MMU32 i;
    if (!mm_movie_pose_journal_init(&result, &destination->storage.allocator))
        return 0;
    if (!mm_movie_pose_journal_copy(&result, poses))
        goto failure;
    for (i = 0; i < texts->count; ++i)
        if (!mm_movie_pose_journal_include(&result, text[i].scene, text[i].actor))
            goto failure;
    for (i = 0; i < tracks->count; ++i)
        if (!mm_movie_pose_journal_include(&result, track[i].scene, track[i].actor))
            goto failure;
    mm_movie_pose_journal_dispose(destination);
    *destination = result;
    return 1;

failure:
    mm_movie_pose_journal_dispose(&result);
    return 0;
}

static const MMPreviewActorShape identity_shape = {{1, 0, 0, 0, 1, 0, 0, 0, 1}, {1, 1, 1}};

static int replay_transform(MMU32 index, const float *offset, const MMPreviewActorShape *shape,
                            const MMMovieActorReplayServices *s)
{
    static const float zero[3] = {0, 0, 0};
    return s->offset(s->context, (MMI32)index, offset ? offset : zero) &&
           s->shape(s->context, (MMI32)index, shape ? shape : &identity_shape);
}

static MMU32 replay_actor_count(const MMPreviewLive *live)
{
    return live->status.ready ? live->actors.count : 0;
}

int mm_movie_actor_replay(const MMPreviewLive *live, const MMMovieActorEdits *edits, const MMMovieActorReplayServices *s)
{
    MMU32 index, count = replay_actor_count(live);
    for (index = 0; index < count; ++index)
    {
        const MMPreviewActor *actor = mm_preview_live_actor_view(live, index);
        const MMMovieActorTrackEdit *track;
        const MMMovieActorTextEdit *text;
        const MMMovieActorPoseEdit *pose;
        MMI32 ordinal = (MMI32)index, id;
        int has_lifetime;
        if (!actor)
            return MM_MOVIE_REPLAY_FAILED;
        id = actor->header.actor_id;
        has_lifetime = !actor->inactive;
        track = mm_movie_track_journal_find(edits->tracks, edits->scene, actor->number);
        text = mm_movie_text_journal_find(edits->texts, edits->scene, actor->number);
        pose = mm_movie_pose_journal_find(edits->poses, edits->scene, actor->number);
        if (track && !s->track(s->context, ordinal, track->source))
            return MM_MOVIE_REPLAY_FAILED;
        if (text && (!s->text(s->context, ordinal, text->source) || !s->name(s->context, id, text->name.utf8) ||
                     (text->custom_material && !s->material(s->context, ordinal, text->source))))
            return MM_MOVIE_REPLAY_FAILED;
        if (has_lifetime && !replay_transform(index, pose ? pose->offset : NULL, pose ? &pose->shape : NULL, s))
            return MM_MOVIE_REPLAY_FAILED;
    }
    return MM_MOVIE_REPLAY_OK;
}

int mm_movie_actor_replay_names(const MMPreviewLive *live, const MMMovieTextJournal *texts, void *context,
                                MMMovieActorNamePublish *publish)
{
    MMU32 index, count = replay_actor_count(live);
    for (index = 0; index < count; ++index)
    {
        const MMPreviewActor *actor = mm_preview_live_actor_view(live, index);
        const MMMovieActorTextEdit *text;
        if (!actor)
            return MM_MOVIE_REPLAY_FAILED;
        text = mm_movie_text_journal_find(texts, live->status.asset_number, actor->number);
        if (text && !publish(context, actor->header.actor_id, text->name.utf8))
            return MM_MOVIE_REPLAY_FAILED;
    }
    return MM_MOVIE_REPLAY_OK;
}

int mm_movie_actor_replay_placements(const MMPreviewLive *live, const MMMovieActorPlacement *placements,
                                     size_t size, const MMMovieActorReplayServices *s)
{
    MMU32 index, count = replay_actor_count(live);
    size_t next;
    MMI32 previous = -1;
    if (size > count || (size && !placements))
        return MM_MOVIE_REPLAY_BAD_PLACEMENTS;
    for (next = 0; next < size; ++next)
    {
        MMI32 object = placements[next].object;
        const MMPreviewActor *actor;
        if (object <= previous || (MMU32)object >= count)
            return MM_MOVIE_REPLAY_BAD_PLACEMENTS;
        actor = mm_preview_live_actor_view(live, (MMU32)object);
        if (!actor || actor->inactive)
            return MM_MOVIE_REPLAY_BAD_PLACEMENTS;
        previous = object;
    }
    next = 0;
    for (index = 0; index < count; ++index)
    {
        const MMPreviewActor *actor = mm_preview_live_actor_view(live, index);
        const MMMovieActorPlacement *entry = NULL;
        if (!actor)
            return MM_MOVIE_REPLAY_FAILED;
        if (actor->inactive)
            continue;
        if (next < size && placements[next].object == (MMI32)index)
            entry = placements + next++;
        if (!replay_transform(index, entry ? entry->offset : NULL, entry ? &entry->shape : NULL, s))
            return MM_MOVIE_REPLAY_FAILED;
    }
    return MM_MOVIE_REPLAY_OK;
}

enum { PLACEMENT_POSITION = 1, PLACEMENT_SHAPE = 2 };
typedef struct PlacementDraft
{
    MMI32 object;
    int flags;
    float position[3];
    MMPreviewActorShape shape;
} PlacementDraft;

int mm_movie_placement_init(MMMoviePlacementDrafts *drafts, const MMStorageAllocator *allocator)
{
    drafts->count = 0;
    return mm_storage_init(&drafts->storage, allocator) == MM_STORAGE_OK;
}

void mm_movie_placement_clear(MMMoviePlacementDrafts *drafts)
{
    drafts->count = 0;
}

void mm_movie_placement_dispose(MMMoviePlacementDrafts *drafts)
{
    mm_storage_dispose(&drafts->storage);
    mm_movie_placement_clear(drafts);
}

static MMU32 placement_lower(const MMMoviePlacementDrafts *drafts, MMI32 object)
{
    const PlacementDraft *entries = (const PlacementDraft *)drafts->storage.buffers[0].data;
    MMU32 first = 0, last = drafts->count, middle;
    while (first < last)
    {
        middle = first + (last - first) / 2;
        if (entries[middle].object < object)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

static PlacementDraft *placement_at(const MMMoviePlacementDrafts *drafts, MMU32 row)
{
    return (PlacementDraft *)drafts->storage.buffers[0].data + row;
}

static PlacementDraft *placement_find(const MMMoviePlacementDrafts *drafts, MMU32 row, MMI32 object)
{
    return row < drafts->count && placement_at(drafts, row)->object == object ? placement_at(drafts, row) : NULL;
}

static PlacementDraft *placement_reserve(MMMoviePlacementDrafts *drafts, MMU32 row, MMI32 object)
{
    PlacementDraft *entry = placement_find(drafts, row, object);
    PlacementDraft blank = {0};
    MMArray records;
    if (entry)
        return entry;
    blank.object = object;
    records = array(&drafts->storage, sizeof(blank), drafts->count);
    if (!insert(&records, row, &blank))
        return NULL;
    drafts->count = records.count;
    return placement_at(drafts, row);
}

static void placement_remove(MMMoviePlacementDrafts *drafts, MMU32 row, int flag)
{
    MMArray records;
    PlacementDraft *entry = placement_at(drafts, row);
    entry->flags &= ~flag;
    if (entry->flags)
        return;
    records = array(&drafts->storage, sizeof(*entry), drafts->count);
    mm_array_delete(&records, row, 1);
    drafts->count = records.count;
}

static void difference(const float position[3], const float origin[3], float delta[3])
{
    unsigned axis;
    for (axis = 0; axis < 3; ++axis)
        delta[axis] = position[axis] - origin[axis];
}

static int near_zero(const float delta[3], float tolerance)
{
    return fabs(delta[0]) < tolerance && fabs(delta[1]) < tolerance && fabs(delta[2]) < tolerance;
}

static int same_shape(const MMPreviewActorShape *left, const MMPreviewActorShape *right)
{
    unsigned i;
    for (i = 0; i < 9; ++i)
        if (left->rotation[i] != right->rotation[i])
            return 0;
    return left->scale[0] == right->scale[0] && left->scale[1] == right->scale[1] && left->scale[2] == right->scale[2];
}

int mm_movie_placement_valid_position(const float position[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i)
        if (!(position[i] >= -32700 && position[i] <= 32700))
            return 0;
    return 1;
}

int mm_movie_placement_preview_position(MMMoviePlacementDrafts *drafts, MMI32 object, const float current[3],
                                        const float requested[3], const MMMoviePlacementServices *s)
{
    MMU32 row;
    PlacementDraft *entry;
    float delta[3], after[3];
    difference(requested, current, delta);
    if (!delta[0] && !delta[1] && !delta[2])
        return MM_MOVIE_PLACEMENT_OK;
    row = placement_lower(drafts, object);
    entry = placement_reserve(drafts, row, object);
    if (!entry)
        return MM_MOVIE_PLACEMENT_MEMORY;
    if (!(entry->flags & PLACEMENT_POSITION))
        memcpy(entry->position, current, sizeof(entry->position));
    if (!s->apply_position(s->context, object, delta, 0, after))
    {
        placement_remove(drafts, row, 0);
        return MM_MOVIE_PLACEMENT_FAILED;
    }
    entry->flags |= PLACEMENT_POSITION;
    difference(after, entry->position, delta);
    if (near_zero(delta, .0001f))
        placement_remove(drafts, row, PLACEMENT_POSITION);
    s->invalidate_frame(s->context);
    return MM_MOVIE_PLACEMENT_OK;
}

int mm_movie_placement_preview_shape(MMMoviePlacementDrafts *drafts, MMI32 object, const MMPreviewActorShape *current,
                                     const MMPreviewActorShape *requested, const MMMoviePlacementServices *s)
{
    MMU32 row = placement_lower(drafts, object);
    PlacementDraft *entry = placement_reserve(drafts, row, object);
    if (!entry)
        return MM_MOVIE_PLACEMENT_MEMORY;
    if (!s->apply_shape(s->context, object, requested))
    {
        placement_remove(drafts, row, 0);
        return MM_MOVIE_PLACEMENT_FAILED;
    }
    if (!(entry->flags & PLACEMENT_SHAPE))
        entry->shape = *current;
    entry->flags |= PLACEMENT_SHAPE;
    if (same_shape(requested, &entry->shape))
        placement_remove(drafts, row, PLACEMENT_SHAPE);
    s->invalidate_frame(s->context);
    return MM_MOVIE_PLACEMENT_OK;
}

int mm_movie_placement_commit_position(MMMoviePlacementDrafts *drafts, MMI32 object, MMU32 scene, MMU32 actor,
                                       const float current[3], const float requested[3],
                                       const MMMoviePlacementServices *s)
{
    MMU32 row = placement_lower(drafts, object);
    PlacementDraft *entry = placement_find(drafts, row, object);
    int previewed = entry && (entry->flags & PLACEMENT_POSITION);
    const float *original = previewed ? entry->position : current;
    MMMoviePoseJournal *poses;
    float delta[3], offset[3];
    difference(requested, original, delta);
    if (near_zero(delta, 1e-6f))
    {
        if (previewed)
        {
            difference(original, current, delta);
            if (!s->apply_position(s->context, object, delta, 0, NULL))
                return MM_MOVIE_PLACEMENT_FAILED;
            placement_remove(drafts, row, PLACEMENT_POSITION);
            s->invalidate_frame(s->context);
        }
        return MM_MOVIE_PLACEMENT_OK;
    }
    poses = s->prepare_commit(s->context);
    if (!poses || !mm_movie_pose_journal_move(poses, scene, actor, delta, offset))
        return MM_MOVIE_PLACEMENT_MEMORY;
    if (!s->apply_position(s->context, object, offset, 1, NULL))
        return MM_MOVIE_PLACEMENT_FAILED;
    if (previewed)
        placement_remove(drafts, row, PLACEMENT_POSITION);
    s->invalidate_frame(s->context);
    s->publish_commit(s->context, 1);
    return MM_MOVIE_PLACEMENT_OK;
}

int mm_movie_placement_commit_shape(MMMoviePlacementDrafts *drafts, MMI32 object, MMU32 scene, MMU32 actor,
                                    const MMPreviewActorShape *current, const MMPreviewActorShape *requested,
                                    const MMMoviePlacementServices *s)
{
    MMU32 row = placement_lower(drafts, object);
    PlacementDraft *entry = placement_find(drafts, row, object);
    int previewed = entry && (entry->flags & PLACEMENT_SHAPE);
    const MMPreviewActorShape *original = previewed ? &entry->shape : current;
    int changed = !same_shape(requested, original);
    MMMoviePoseJournal *poses;
    if (changed)
    {
        poses = s->prepare_commit(s->context);
        if (!poses || !mm_movie_pose_journal_shape(poses, scene, actor, requested))
            return MM_MOVIE_PLACEMENT_MEMORY;
    }
    if (!s->apply_shape(s->context, object, changed ? requested : original))
        return MM_MOVIE_PLACEMENT_FAILED;
    if (previewed)
        placement_remove(drafts, row, PLACEMENT_SHAPE);
    s->invalidate_frame(s->context);
    if (changed)
        s->publish_commit(s->context, 0);
    return MM_MOVIE_PLACEMENT_OK;
}

static int placement_restore(MMMoviePlacementDrafts *drafts, int flag, const MMMoviePlacementServices *s)
{
    MMU32 row = 0;
    while (row < drafts->count)
    {
        const PlacementDraft *entry = placement_at(drafts, row);
        if (entry->flags & flag)
        {
            MMU32 count = drafts->count;
            if (flag == PLACEMENT_SHAPE)
            {
                if (!s->apply_shape(s->context, entry->object, &entry->shape))
                    return 0;
            }
            else
            {
                float position[3], delta[3];
                if (!s->read_position(s->context, entry->object, position))
                    return 0;
                difference(entry->position, position, delta);
                if (!s->apply_position(s->context, entry->object, delta, 0, NULL))
                    return 0;
            }
            placement_remove(drafts, row, flag);
            s->invalidate_frame(s->context);
            if (count != drafts->count)
                continue;
        }
        ++row;
    }
    return 1;
}

int mm_movie_placement_cancel(MMMoviePlacementDrafts *drafts, const MMMoviePlacementServices *s)
{
    return placement_restore(drafts, PLACEMENT_SHAPE, s) && placement_restore(drafts, PLACEMENT_POSITION, s);
}

int mm_movie_placement_worker(const MMMoviePlacementDrafts *drafts, MMI32 object, const float current[3],
                              const MMMovieActorPoseEdit *pose, float offset[3], int *shape)
{
    const PlacementDraft *entry = placement_find(drafts, placement_lower(drafts, object), object);
    float delta[3];
    unsigned axis;
    for (axis = 0; axis < 3; ++axis)
        offset[axis] = pose ? pose->offset[axis] : 0;
    if (entry && (entry->flags & PLACEMENT_POSITION))
    {
        difference(current, entry->position, delta);
        for (axis = 0; axis < 3; ++axis)
            offset[axis] += delta[axis];
    }
    *shape = (pose && pose->has_shape) || (entry && (entry->flags & PLACEMENT_SHAPE));
    return *shape || offset[0] || offset[1] || offset[2];
}

int mm_movie_worker_placements_init(MMMovieWorkerPlacements *batch, const MMStorageAllocator *allocator)
{
    batch->count = 0;
    return mm_storage_init(&batch->storage, allocator) == MM_STORAGE_OK;
}

void mm_movie_worker_placements_dispose(MMMovieWorkerPlacements *batch)
{
    mm_storage_dispose(&batch->storage);
    batch->count = 0;
}

const MMMovieActorPlacement *mm_movie_worker_placements_data(const MMMovieWorkerPlacements *batch)
{
    return (const MMMovieActorPlacement *)batch->storage.buffers[0].data;
}

int mm_movie_worker_placements_collect(MMMovieWorkerPlacements *batch, const MMPreviewObjectRows *objects,
                                      const MMPreviewLive *live, const MMMoviePlacementDrafts *drafts,
                                      const MMMoviePoseJournal *poses, MMU32 scene)
{
    const MMPreviewActorInfo *rows = mm_preview_object_rows_data(objects);
    MMU32 i, count = 0;
    for (i = 0; i < objects->count; ++i)
    {
        const MMMovieActorPoseEdit *pose = mm_movie_pose_journal_find(poses, scene, rows[i].number);
        MMMovieActorPlacement placement;
        float position[3];
        int shaped;
        placement.object = (MMI32)i;
        mm_preview_object_position(rows + i, position);
        if (!mm_movie_placement_worker(drafts, placement.object, position, pose, placement.offset, &shaped))
            continue;
        placement.shape = identity_shape;
        if (shaped)
        {
            const MMPreviewActor *actor = mm_preview_live_actor_view(live, i);
            if (!live->status.ready || !actor || !actor->body)
                return MM_MOVIE_PLACEMENT_FAILED;
            placement.shape = actor->shape;
        }
        batch->storage.min_grow[1] = batch->storage.buffers[1].capacity / 2;
        if (count >= (MM_STORAGE_LIMIT - 1) / sizeof(placement) ||
            mm_storage_ensure(&batch->storage, 0, (MMU32)((count + 1) * sizeof(placement)), 0) != MM_STORAGE_OK)
            return MM_MOVIE_PLACEMENT_MEMORY;
        ((MMMovieActorPlacement *)batch->storage.buffers[1].data)[count++] = placement;
    }
    if (count)
    {
        MMStorageBuffer previous = batch->storage.buffers[0];
        batch->storage.buffers[0] = batch->storage.buffers[1];
        batch->storage.buffers[1] = previous;
    }
    batch->count = count;
    return MM_MOVIE_PLACEMENT_OK;
}

/* Movie scene-container control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_scene.h"
#include "mm/movie_save.h"

int mm_movie_scene_cursor_select(MMI32 scene, size_t count, unsigned flags, const MMMovieSceneCursorServices *s)
{
    MMMovieSceneCursorRequest request;
    const int initial = (flags & MM_MOVIE_SCENE_INITIAL) != 0;
    int status;
    if (scene < 0 || (size_t)scene >= count)
        return 0;
    request.scene = scene;
    request.frame = request.first = request.last = 0;
    request.keep_framing = (flags & MM_MOVIE_SCENE_KEEP_FRAMING) != 0;
    request.keep_audio = !initial && (flags & MM_MOVIE_SCENE_KEEP_AUDIO);

    if (!initial)
    {
        *s->selected = -1;
        status = s->effect(s->context, MM_MOVIE_SCENE_CLEAR_VIEW, &request);
        if (status <= 0)
            return status;
        if (!request.keep_audio)
        {
            status = s->effect(s->context, MM_MOVIE_SCENE_STOP_PLAYBACK, &request);
            if (status <= 0)
                return status;
        }
    }
    status = s->effect(s->context, MM_MOVIE_SCENE_LOAD, &request);
    if (status <= 0)
        goto failed_load;
    status = s->effect(s->context, MM_MOVIE_SCENE_BACKGROUND, &request);
    if (status <= 0)
        return status;
    if (!initial)
    {
        status = s->effect(s->context, MM_MOVIE_SCENE_READ_RANGE, &request);
        if (status > 0)
            status = s->effect(s->context, MM_MOVIE_SCENE_SET_RANGE, &request);
        if (status > 0)
            status = s->effect(s->context, MM_MOVIE_SCENE_READ_FRAME, &request);
        if (status > 0 && request.frame != request.first)
        {
            request.frame = request.first;
            status = s->effect(s->context, MM_MOVIE_SCENE_SET_FRAME, &request);
        }
        if (status > 0)
            status = s->effect(s->context, MM_MOVIE_SCENE_APPLY_CAMERAS, &request);
        if (status > 0)
            status = s->effect(s->context, MM_MOVIE_SCENE_APPLY_PLACEMENTS, &request);
        if (status > 0 && !request.keep_framing)
            status = s->effect(s->context, MM_MOVIE_SCENE_RESET_FRAMING, &request);
        if (status <= 0)
            return status;
    }
    status = s->effect(s->context, MM_MOVIE_SCENE_SET_PLAYBACK, &request);
    if (status <= 0)
        goto failed_load;
    status = s->effect(s->context, MM_MOVIE_SCENE_SYNC_OBJECTS, &request);
    if (status <= 0)
        return status;
    status = s->effect(s->context, MM_MOVIE_SCENE_READ_SOUNDS, &request);
    if (status <= 0)
        goto failed_load;
    *s->selected = scene;
    mm_polled_movie_reset(s->playback, request.keep_audio);
    return 1;

failed_load:
    if (!initial && !status)
    {
        status = s->effect(s->context, MM_MOVIE_SCENE_STOP_PLAYBACK, &request);
        return status < 0 ? status : 0;
    }
    return status;
}

int mm_movie_scene_cursor_seek(long frame, const MMMovieSceneCursorServices *s)
{
    MMMovieSceneCursorRequest request = {0, 0, 0, 0, 0, 0};
    int status = s->effect(s->context, MM_MOVIE_SCENE_CANCEL_PREVIEWS, &request);
    if (status <= 0)
        return status;
    if (*s->selected < 0 || frame < (-2147483647L - 1) || frame > 2147483647L)
        return 0;
    request.scene = *s->selected;
    request.frame = (MMI32)frame;
    status = s->effect(s->context, MM_MOVIE_SCENE_SET_FRAME, &request);
    if (status > 0)
        status = s->effect(s->context, MM_MOVIE_SCENE_SEEK_PLAYBACK, &request);
    if (status <= 0)
        return status;
    mm_polled_movie_reset(s->playback, 0);
    return s->effect(s->context, MM_MOVIE_SCENE_SYNC_OBJECTS, &request);
}

int mm_movie_scene_extend(int direction, const MMMovieRangeServices *s)
{
    MMMovieRangeEdit before, after;
    if (*s->selected < 0 || (direction != -1 && direction != 1))
        return MM_MOVIE_RANGE_DIRECTION;
    if (!s->effect(s->context, MM_MOVIE_RANGE_CANCEL_PREVIEWS, NULL, 0))
        return 0;
    s->position(s->context, &before);
    after = before;
    if (direction < 0)
    {
        if (before.frame != before.first || before.first <= -2147483647L)
            return MM_MOVIE_RANGE_BOUNDARY;
        after.frame = --after.first;
    }
    else
    {
        if (before.frame != before.last || before.last >= 2147483647L)
            return MM_MOVIE_RANGE_BOUNDARY;
        after.frame = ++after.last;
    }
    if (!s->effect(s->context, MM_MOVIE_RANGE_PREPARE, &after, 0) ||
        !s->effect(s->context, MM_MOVIE_RANGE_SET_BOUNDS, &after, 0))
        return 0;
    if (!s->effect(s->context, MM_MOVIE_RANGE_SET_FRAME, &after, 0) ||
        !s->effect(s->context, MM_MOVIE_RANGE_PREPARE_FRAME, &after, 0))
    {
        if (!s->effect(s->context, MM_MOVIE_RANGE_SET_BOUNDS, &before, 1) ||
            !s->effect(s->context, MM_MOVIE_RANGE_SET_FRAME, &before, 1))
        {
            *s->selected = -1;
            mm_polled_movie_reset(s->playback, 0);
            s->effect(s->context, MM_MOVIE_RANGE_ABANDON, NULL, 1);
        }
        return 0;
    }
    mm_polled_movie_reset(s->playback, 0);
    s->publish(s->context);
    return 1;
}

void mm_movie_scenes_init(MMMovieScenes *state)
{
    state->open = 0;
    state->index = -1;
}

int mm_movie_scene_autosave(const MMMovieSceneServices *s)
{
    return s->working_save ? mm_movie_autosave(NULL, 0, s->working_save) : s->autosave(s->context);
}

int mm_movie_scene_temporary(const MMMovieSceneServices *s)
{
    return s->working_save ? mm_movie_save_temporary(s->working_save) : s->use_temporary(s->context);
}

MMChunkEdit mm_movie_scene_editor(void *resources, const MMMovieSceneServices *s)
{
    return s->edit ? s->edit(resources) : mm_resource_cache_writer(resources).edit;
}

static int prepare_working_archive(const MMMovieSceneServices *s)
{
    const int ready = s->working_save ? mm_movie_save_resources(s->working_save) : s->make_resources(s->context);
    return ready && mm_movie_scene_temporary(s);
}

static void publish_working_archive(const MMMovieSceneServices *s)
{
    if (s->working_save)
        s->working_save->save(s->movie->resources, NULL);
    else
        s->save(s->movie->resources);
}

static int copy_scene_snapshot(const MMSceneSnapshot *snapshot, MMU32 *number, const MMMovieSceneServices *s)
{
    void *resources = s->movie->resources;
    return s->copy_snapshot ? s->copy_snapshot(resources, snapshot, number)
                            : mm_scene_snapshot_copy_cache(snapshot, resources, &s->snapshot_text, number);
}

void mm_movie_scene_close_open(const MMMovieSceneServices *s)
{
    s->close(s->state->open);
    s->state->open = 0;
    s->state->index = -1;
}
int mm_movie_scene_close(const MMMovieSceneServices *s)
{
    if (s->state->open)
    {
        if (!mm_movie_scene_autosave(s))
            return 0;
        mm_movie_scene_close_open(s);
    }
    return 1;
}
static int scene_number(MMI32 index, MMU32 *number, const MMMovieSceneServices *s)
{
    return mm_movie_state_scene_number(s->movie, (MMU32)index, number, &s->session);
}
static void *read_scene(MMI32 index, const MMMovieSceneServices *s)
{
    void *resources = s->movie->resources;
    MMU32 number;
    if (!scene_number(index, &number, s))
        return NULL;
    return s->read(s->context, resources, number, s->movie->session);
}
int mm_movie_scene_switch(MMI32 index, const MMMovieSceneServices *s)
{
    MMI32 old;
    void *scene;
    int result = 1;
    if (index == s->state->index)
        return 1;
    old = s->state->index;
    if (!mm_movie_scene_close(s))
        return 0;
    s->stop_loops(s->context);
    s->memory_cleanup(s->context);
    if (index == -1)
        return 1;
    for (;;)
    {
        scene = read_scene(index, s);
        if (!scene || !s->start(scene))
        {
            s->state->open = 0;
            s->state->index = -1;
            if (scene)
                s->close(scene);
            if (old != -1)
            {
                index = old;
                old = -1;
                result = 0;
                continue;
            }
            s->scene_change(s->context);
            return 0;
        }
        s->state->open = scene;
        s->scene_change(s->context);
        s->state->index = index;
        if (!s->seek_first(scene))
        {
            s->state->open = 0;
            s->state->index = -1;
            s->close(scene);
            if (old != -1)
            {
                index = old;
                old = -1;
                result = 0;
                continue;
            }
            return 0;
        }
        s->sound_frame(scene);
        s->invalidate(s->context);
        return result;
    }
}
static void move_child(MMI32 from, MMI32 to, const MMChunkEdit *edit, const MMMovieSceneServices *s)
{
    const MMChunkAccess chunks = mm_chunk_edit_access(edit);
    const MMU32 movie = s->movie->movieNumber;
    MMChunkChild child;
    child.type = MM_SCENE_CHUNK;
    child.slot = (MMU32)from;
    if (mm_chunk_link_number(&chunks, MM_MOVIE_CHUNK, movie, child.slot, child.type, &child.number) != MM_CHUNK_OK)
    {
        if (s->missing_link)
            s->missing_link(s->context);
        return;
    }
    if (mm_chunk_change_slot(edit, MM_MOVIE_CHUNK, movie, &child, (MMU32)to) != MM_CHUNK_OK && s->archive_issue)
        s->archive_issue(s->context, MM_MOVIE_ARCHIVE_MOVE_FAILED);
}
static void shift_scenes(MMI32 index, MMI32 count, int down, const MMChunkEdit *edit,
                         const MMMovieSceneServices *s)
{
    MMI32 i;
    if (down)
    {
        for (i = count; i > index;)
        {
            --i;
            move_child(i, i + 1, edit, s);
        }
    }
    else
        for (i = index; i < count; ++i)
            move_child(i + 1, i, edit, s);
}
static void unlink_scene(const MMChunkEdit *edit, const MMChunkChild *child, const MMMovieSceneServices *s)
{
    if (mm_chunk_unlink(edit, MM_MOVIE_CHUNK, s->movie->movieNumber, child) != MM_CHUNK_OK && s->archive_issue)
        s->archive_issue(s->context, MM_MOVIE_ARCHIVE_UNLINK_FAILED);
}
static int publish_scene(MMI32 index, MMU32 number, const MMMovieSceneServices *s)
{
    MMMovieState *movie = s->movie;
    const MMChunkEdit edit = mm_movie_scene_editor(movie->resources, s);
    const MMI32 previous_count = movie->sceneCount;
    MMChunkChild child;
    int status;
    child.type = MM_SCENE_CHUNK;
    child.number = number;
    child.slot = (MMU32)index;
    shift_scenes(index, previous_count, 1, &edit, s);
    ++movie->sceneCount;
    status = mm_chunk_adopt(&edit, MM_MOVIE_CHUNK, movie->movieNumber, &child, 1);
    if (status != MM_CHUNK_OK)
    {
        if (status == MM_CHUNK_NOT_FOUND && s->archive_issue)
            s->archive_issue(s->context, MM_MOVIE_ARCHIVE_MISSING_CHILD);
        shift_scenes(index, previous_count, 0, &edit, s);
        if (mm_chunk_delete(&edit, child.type, child.number) != MM_CHUNK_OK && s->archive_issue)
            s->archive_issue(s->context, MM_MOVIE_ARCHIVE_DELETE_FAILED);
        return 0;
    }
    publish_working_archive(s);
    if (!mm_movie_scene_switch(index, s))
    {
        --movie->sceneCount;
        unlink_scene(&edit, &child, s);
        shift_scenes(index, movie->sceneCount, 0, &edit, s);
        publish_working_archive(s);
        if (movie->sceneCount > 0)
            mm_movie_scene_switch(index < movie->sceneCount ? index : movie->sceneCount - 1, s);
        return 0;
    }
    s->add_actors(s->state->open);
    s->dirty(s->context);
    s->rollcall_changed(s->context);
    if (s->sounds_enabled(s->context))
        s->play_sound(s->context);
    else
        s->flush_sound(s->context);
    return 1;
}
int mm_movie_scene_insert(MMI32 index, void *scene, const MMMovieSceneServices *s)
{
    MMU32 number;
    if (!prepare_working_archive(s) || !mm_movie_scene_close(s) ||
        !s->write(s->context, scene, &number))
        return 0;
    s->retain(scene);
    s->close(scene);
    return publish_scene(index, number, s);
}
int mm_movie_scene_insert_snapshot(MMI32 index, const MMSceneSnapshot *snapshot, const MMMovieSceneServices *s)
{
    MMChunkEdit edit;
    MMU32 number;
    if (!snapshot->archive || !prepare_working_archive(s) ||
        !copy_scene_snapshot(snapshot, &number, s))
        return 0;
    if (mm_movie_scene_close(s))
        return publish_scene(index, number, s);
    edit = mm_movie_scene_editor(s->movie->resources, s);
    if (mm_chunk_delete(&edit, MM_SCENE_CHUNK, number) != MM_CHUNK_OK && s->archive_issue)
        s->archive_issue(s->context, MM_MOVIE_ARCHIVE_DELETE_FAILED);
    return 0;
}
int mm_movie_scene_new(MMI32 index, const MMMovieSceneServices *s)
{
    int result;
    void *scene = s->create(s->context);
    if (!scene)
        return 0;
    result = mm_movie_scene_insert(index, scene, s);
    s->close(scene);
    return result;
}
int mm_movie_scene_remove(MMI32 index, const MMMovieSceneServices *s)
{
    MMChunkEdit edit;
    MMI32 old;
    MMChunkChild child;
    MMMovieState *movie = s->movie;
    void *scene;
    if (!mm_movie_scene_temporary(s))
        return 0;
    old = s->state->index;
    if (old != index && !mm_movie_scene_switch(index, s))
    {
        mm_movie_scene_switch(old, s);
        return 0;
    }
    scene = s->state->open;
    child.type = MM_SCENE_CHUNK;
    child.slot = (MMU32)index;
    if (!scene_number(index, &child.number, s))
    {
        if (s->missing_link)
            s->missing_link(s->context);
        goto restore_scene;
    }
    edit = mm_movie_scene_editor(movie->resources, s);
    if (!mm_movie_archive_preserve_scene_assets(&edit, child.number))
        goto restore_scene;
    s->retain(scene);
    mm_movie_scene_close_open(s);
    s->remove_actors(scene);
    s->release(scene);
    unlink_scene(&edit, &child, s);
    --movie->sceneCount;
    shift_scenes(index, movie->sceneCount, 0, &edit, s);
    publish_working_archive(s);
    if (movie->sceneCount == 0)
        return 1;
    mm_movie_scene_switch(index < movie->sceneCount ? index : index - 1, s);
    s->dirty(s->context);
    if (!s->state->open)
        s->switch_error(s->context);
    return 1;
restore_scene:
    if (old != index)
        mm_movie_scene_switch(old, s);
    return 0;
}
/* Removal distinguishes allocation failure from a failed history commit. */
static int record_undo(MMI32 index, void *scene, const MMTag *background, const MMMovieSceneServices *s)
{
    MMMovieSceneUndoRecord *record;
    void *undo = s->new_undo(s->context, &record);
    int added;
    if (!undo)
        return -1;
    record->index = index;
    if (background)
    {
        record->operation = MM_MOVIE_INSERT_SCENE;
        record->saved.background = *background;
        if (!background->sid && background->cache)
            mm_tag_dup(background, s->undo_tags);
    }
    else
    {
        const MMSceneSnapshot empty = {0};
        record->operation = MM_MOVIE_REMOVE_SCENE;
        record->saved.snapshot = empty;
        if (!mm_scene_snapshot_save(&record->saved.snapshot, scene, &s->snapshot))
        {
            s->release_undo(undo);
            return 0;
        }
    }
    added = s->add_undo(s->context, undo);
    s->release_undo(undo);
    return added != 0;
}
int mm_movie_scene_record_insertion(MMI32 index, const MMTag *tag, const MMMovieSceneServices *s)
{
    return record_undo(index, NULL, tag, s) > 0;
}
void mm_movie_scene_undo_dispose(MMMovieSceneUndoRecord *p, const MMTagServices *tags)
{
    MMMovieSceneUndoRecord record = *p;
    p->operation = MM_MOVIE_SCENE_UNDO_EMPTY;
    if (record.operation == MM_MOVIE_REMOVE_SCENE)
        mm_scene_snapshot_dispose(&record.saved.snapshot);
    else if (record.operation == MM_MOVIE_INSERT_SCENE)
    {
        MMTag tag = record.saved.background;
        if (!tag.sid && tag.cache)
            mm_tag_close(&tag, tags);
    }
}
int mm_movie_scene_remove_undoable(MMI32 index, const MMMovieSceneServices *s)
{
    void *scene;
    int added;
    if (s->state->index == index)
    {
        scene = s->state->open;
        s->retain(scene);
    }
    else
        scene = read_scene(index, s);
    if (!scene)
        return 0;
    added = record_undo(index, scene, NULL, s);
    s->release(scene);
    if (added <= 0)
        return added < 0;
    if (!mm_movie_scene_remove(index, s))
    {
        s->clear_undo(s->context);
        return 0;
    }
    return 1;
}
int mm_movie_scene_undo(const MMMovieSceneUndoRecord *record, int redo, const MMMovieSceneUndoServices *s)
{
    const MMMovieSceneServices *scenes = s->scenes;
    void *context = scenes->context;
    const int kind = record->operation;
    const MMI32 index = record->index;
    int removed;
    if (kind != MM_MOVIE_INSERT_SCENE && kind != MM_MOVIE_REMOVE_SCENE)
    {
        s->unknown(context);
        goto fail;
    }
    removed = (kind == MM_MOVIE_REMOVE_SCENE) == !!redo;
    if (removed)
    {
        if (!mm_movie_scene_remove(index, scenes))
            goto fail;
    }
    else if (redo)
    {
        MMSceneEditServices edit;
        MMTag old;
        if (!mm_movie_scene_new(index, scenes))
            goto fail;
        edit = s->scene_edit(scenes->state->open);
        if (!mm_scene_background_edit(&record->saved.background, &old, &edit))
        {
            mm_movie_scene_remove(index, scenes);
            goto fail;
        }
    }
    else if (!mm_movie_scene_insert_snapshot(index, &record->saved.snapshot, scenes))
        goto fail;
    s->notify(context, removed);
    scenes->flush_sound(context);
    return 1;
fail:
    scenes->flush_sound(context);
    scenes->clear_undo(context);
    return 0;
}

/* Movie scene-container control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_scene.h"

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
        if (!s->autosave(s->context))
            return 0;
        mm_movie_scene_close_open(s);
    }
    return 1;
}
static int scene_number(MMI32 index, void *file, MMU32 *number, const MMMovieSceneServices *s)
{
    return mm_movie_state_scene_number(s->movie, (MMU32)index, number, file, s->context, s->open_session);
}
static void *read_scene(MMI32 index, const MMMovieSceneServices *s)
{
    void *resources = s->movie->resources;
    MMU32 number;
    if (!resources || !scene_number(index, s->file(resources), &number, s))
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
static void move_child(MMI32 from, MMI32 to, void *file, const MMMovieSceneServices *s)
{
    const MMChunkAccess chunks = s->chunks(file);
    const MMU32 movie = s->movie->movieNumber;
    MMChunkChild child;
    child.type = MM_SCENE_CHUNK;
    child.slot = (MMU32)from;
    if (mm_chunk_link_number(&chunks, MM_MOVIE_CHUNK, movie, child.slot, child.type, &child.number) == MM_CHUNK_OK)
        s->move_child(file, MM_MOVIE_CHUNK, movie, &child, (MMU32)to);
    else if (s->missing_link)
        s->missing_link(s->context);
}
static void shift_scenes(MMI32 index, MMI32 count, int down, void *file, const MMMovieSceneServices *s)
{
    MMI32 i;
    if (down)
    {
        for (i = count; i > index;)
        {
            --i;
            move_child(i, i + 1, file, s);
        }
    }
    else
        for (i = index; i < count; ++i)
            move_child(i + 1, i, file, s);
}
int mm_movie_scene_insert(MMI32 index, void *scene, const MMMovieSceneServices *s)
{
    MMChunkChild child;
    MMMovieState *movie = s->movie;
    MMI32 previous_count;
    void *file;
    if (!s->make_resources(s->context))
        return 0;
    file = s->file(movie->resources);
    child.type = MM_SCENE_CHUNK;
    child.slot = (MMU32)index;
    if (!s->use_temporary(s->context) || !mm_movie_scene_close(s) || !s->write(s->context, scene, &child.number))
        return 0;
    s->retain(scene);
    s->close(scene);
    previous_count = movie->sceneCount;
    shift_scenes(index, previous_count, 1, file, s);
    ++movie->sceneCount;
    if (!s->adopt(file, MM_MOVIE_CHUNK, movie->movieNumber, &child))
    {
        shift_scenes(index, previous_count, 0, file, s);
        s->delete_chunk(file, child.type, child.number);
        return 0;
    }
    s->save(file);
    if (!mm_movie_scene_switch(index, s))
    {
        --movie->sceneCount;
        s->delete_child(file, MM_MOVIE_CHUNK, movie->movieNumber, &child);
        shift_scenes(index, movie->sceneCount, 0, file, s);
        s->save(file);
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
    MMI32 old;
    int found;
    MMChunkChild child;
    MMMovieState *movie = s->movie;
    void *scene, *file = s->file(movie->resources);
    if (!s->use_temporary(s->context))
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
    found = scene_number(index, file, &child.number, s);
    if (!found && s->missing_link)
        s->missing_link(s->context);
    if (!found || (s->preserve_scene_assets && !s->preserve_scene_assets(file, child.number)))
    {
        if (old != index)
            mm_movie_scene_switch(old, s);
        return 0;
    }
    s->retain(scene);
    mm_movie_scene_close_open(s);
    s->remove_actors(scene);
    s->release(scene);
    s->delete_child(file, MM_MOVIE_CHUNK, movie->movieNumber, &child);
    --movie->sceneCount;
    shift_scenes(index, movie->sceneCount, 0, file, s);
    s->save(file);
    if (movie->sceneCount == 0)
        return 1;
    mm_movie_scene_switch(index < movie->sceneCount ? index : index - 1, s);
    s->dirty(s->context);
    if (!s->state->open)
        s->switch_error(s->context);
    return 1;
}
int mm_movie_scene_remove_undoable(MMI32 index, const MMMovieSceneServices *s)
{
    void *scene, *undo;
    if (s->state->index == index)
    {
        scene = s->state->open;
        s->retain(scene);
    }
    else
    {
        scene = read_scene(index, s);
        if (!scene || !s->start(scene))
        {
            s->close(scene);
            return 0;
        }
        s->hide(scene);
    }
    undo = s->new_undo(s->context);
    if (!undo)
    {
        s->release(scene);
        return 1;
    }
    s->configure_undo(s->context, undo, index, scene);
    if (!s->add_undo(s->context, undo))
    {
        s->release_undo(undo);
        s->release(scene);
        return 0;
    }
    s->release_undo(undo);
    s->release(scene);
    if (!mm_movie_scene_remove(index, s))
    {
        s->clear_undo(s->context);
        return 0;
    }
    return 1;
}
int mm_movie_scene_undo(int kind, MMI32 index, void *scene, const MMTag *tag, int redo,
                        const MMMovieSceneUndoServices *s)
{
    const MMMovieSceneServices *scenes = s->scenes;
    void *context = scenes->context;
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
        if (!mm_scene_background_edit(tag, &old, &edit))
        {
            mm_movie_scene_remove(index, scenes);
            goto fail;
        }
    }
    else if (!mm_movie_scene_insert(index, scene, scenes))
        goto fail;
    s->notify(context, removed);
    scenes->flush_sound(context);
    return 1;
fail:
    scenes->flush_sound(context);
    scenes->clear_undo(context);
    return 0;
}

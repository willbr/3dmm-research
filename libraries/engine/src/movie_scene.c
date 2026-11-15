/* Movie scene-container control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_scene.h"

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
        scene = s->read(s->context, index);
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
void mm_movie_scene_move(MMI32 index, int down, void *file, const MMMovieSceneServices *s)
{
    MMI32 i;
    if (down)
    {
        for (i = *s->count; i > index;)
        {
            --i;
            s->move_child(s->context, file, i, i + 1);
        }
    }
    else
        for (i = index; i < *s->count; ++i)
            s->move_child(s->context, file, i + 1, i);
}
int mm_movie_scene_insert(MMI32 index, void *scene, const MMMovieSceneServices *s)
{
    MMU32 number;
    void *file;
    if (!s->make_resources(s->context))
        return 0;
    file = s->file(s->context);
    if (!s->use_temporary(s->context) || !mm_movie_scene_close(s) || !s->write(s->context, scene, &number))
        return 0;
    s->retain(scene);
    s->close(scene);
    mm_movie_scene_move(index, 1, file, s);
    ++*s->count;
    if (!s->adopt(s->context, file, number, index))
    {
        mm_movie_scene_move(index, 0, file, s);
        s->delete_chunk(s->context, file, number);
        return 0;
    }
    s->save(file);
    if (!mm_movie_scene_switch(index, s))
    {
        --*s->count;
        s->delete_child(s->context, file, number, index);
        mm_movie_scene_move(index, 0, file, s);
        s->save(file);
        if (*s->count > 0)
            mm_movie_scene_switch(index < *s->count ? index : *s->count - 1, s);
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
    MMU32 number;
    void *scene, *file = s->file(s->context);
    if (!s->use_temporary(s->context))
        return 0;
    old = s->state->index;
    if (old != index && !mm_movie_scene_switch(index, s))
    {
        mm_movie_scene_switch(old, s);
        return 0;
    }
    scene = s->state->open;
    s->retain(scene);
    mm_movie_scene_close_open(s);
    s->remove_actors(scene);
    s->release(scene);
    number = s->scene_number(s->context, file, index);
    s->delete_child(s->context, file, number, index);
    --*s->count;
    mm_movie_scene_move(index, 0, file, s);
    s->save(file);
    if (*s->count == 0)
        return 1;
    mm_movie_scene_switch(index < *s->count ? index : index - 1, s);
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
        scene = s->read(s->context, index);
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
    int removed;
    if (kind != MM_MOVIE_INSERT_SCENE && kind != MM_MOVIE_REMOVE_SCENE)
    {
        s->unknown(s->context);
        goto fail;
    }
    removed = (kind == MM_MOVIE_REMOVE_SCENE) == !!redo;
    if (removed)
    {
        if (!s->remove(s->context, index))
            goto fail;
    }
    else if (redo)
    {
        if (!s->create(s->context, index))
            goto fail;
        if (!s->background(s->context, tag))
        {
            s->remove(s->context, index);
            goto fail;
        }
    }
    else if (!s->insert(s->context, index, scene))
        goto fail;
    s->notify(s->context, removed);
    s->flush(s->context);
    return 1;
fail:
    s->flush(s->context);
    s->clear(s->context);
    return 0;
}

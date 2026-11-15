/* Movie scene-container control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/movie_scene.h"

void three_mm_movie_scenes_init(ThreeMMMovieScenes *state)
{
    state->open = 0;
    state->index = -1;
}
void three_mm_movie_scene_close_open(const ThreeMMMovieSceneServices *s)
{
    s->close(s->state->open);
    s->state->open = 0;
    s->state->index = -1;
}
int three_mm_movie_scene_close(const ThreeMMMovieSceneServices *s)
{
    if (s->state->open)
    {
        if (!s->autosave(s->context))
            return 0;
        three_mm_movie_scene_close_open(s);
    }
    return 1;
}
int three_mm_movie_scene_switch(ThreeMMI32 index, const ThreeMMMovieSceneServices *s)
{
    ThreeMMI32 old;
    void *scene;
    int result = 1;
    if (index == s->state->index)
        return 1;
    old = s->state->index;
    if (!three_mm_movie_scene_close(s))
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
void three_mm_movie_scene_move(ThreeMMI32 index, int down, void *file, const ThreeMMMovieSceneServices *s)
{
    ThreeMMI32 i;
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
int three_mm_movie_scene_insert(ThreeMMI32 index, void *scene, const ThreeMMMovieSceneServices *s)
{
    ThreeMMU32 number;
    void *file;
    if (!s->make_resources(s->context))
        return 0;
    file = s->file(s->context);
    if (!s->use_temporary(s->context) || !three_mm_movie_scene_close(s) || !s->write(s->context, scene, &number))
        return 0;
    s->retain(scene);
    s->close(scene);
    three_mm_movie_scene_move(index, 1, file, s);
    ++*s->count;
    if (!s->adopt(s->context, file, number, index))
    {
        three_mm_movie_scene_move(index, 0, file, s);
        s->delete_chunk(s->context, file, number);
        return 0;
    }
    s->save(file);
    if (!three_mm_movie_scene_switch(index, s))
    {
        --*s->count;
        s->delete_child(s->context, file, number, index);
        three_mm_movie_scene_move(index, 0, file, s);
        s->save(file);
        if (*s->count > 0)
            three_mm_movie_scene_switch(index < *s->count ? index : *s->count - 1, s);
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
int three_mm_movie_scene_new(ThreeMMI32 index, const ThreeMMMovieSceneServices *s)
{
    int result;
    void *scene = s->create(s->context);
    if (!scene)
        return 0;
    result = three_mm_movie_scene_insert(index, scene, s);
    s->close(scene);
    return result;
}
int three_mm_movie_scene_remove(ThreeMMI32 index, const ThreeMMMovieSceneServices *s)
{
    ThreeMMI32 old;
    ThreeMMU32 number;
    void *scene, *file = s->file(s->context);
    if (!s->use_temporary(s->context))
        return 0;
    old = s->state->index;
    if (old != index && !three_mm_movie_scene_switch(index, s))
    {
        three_mm_movie_scene_switch(old, s);
        return 0;
    }
    scene = s->state->open;
    s->retain(scene);
    three_mm_movie_scene_close_open(s);
    s->remove_actors(scene);
    s->release(scene);
    number = s->scene_number(s->context, file, index);
    s->delete_child(s->context, file, number, index);
    --*s->count;
    three_mm_movie_scene_move(index, 0, file, s);
    s->save(file);
    if (*s->count == 0)
        return 1;
    three_mm_movie_scene_switch(index < *s->count ? index : index - 1, s);
    s->dirty(s->context);
    if (!s->state->open)
        s->switch_error(s->context);
    return 1;
}
int three_mm_movie_scene_remove_undoable(ThreeMMI32 index, const ThreeMMMovieSceneServices *s)
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
    if (!three_mm_movie_scene_remove(index, s))
    {
        s->clear_undo(s->context);
        return 0;
    }
    return 1;
}
int three_mm_movie_scene_undo(int kind, ThreeMMI32 index, void *scene, const ThreeMMTag *tag, int redo,
                              const ThreeMMMovieSceneUndoServices *s)
{
    int removed;
    if (kind != THREE_MM_MOVIE_INSERT_SCENE && kind != THREE_MM_MOVIE_REMOVE_SCENE)
    {
        s->unknown(s->context);
        goto fail;
    }
    removed = (kind == THREE_MM_MOVIE_REMOVE_SCENE) == !!redo;
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

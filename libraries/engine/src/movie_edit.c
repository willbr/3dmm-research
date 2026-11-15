/* Movie editing and common undo control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/movie_edit.h"
#include "three_mm/actor_motion.h"
#include "three_mm/wide.h"
int three_mm_movie_undo_add(void *undo, const ThreeMMMovieUndoServices *s)
{
    s->bind_movie(s->context, undo);
    s->bind_scene(undo, s->scene_index(s->context));
    if (s->scene_index(s->context) != -1)
        s->bind_frame(undo, s->frame(s->context));
    if (!s->commit(s->context, undo))
    {
        s->notify(s->context, THREE_MM_MOVIE_UNDO_DISABLED);
        return 0;
    }
    s->notify(s->context, THREE_MM_MOVIE_UNDO_READY);
    return 1;
}
void three_mm_movie_undo_clear(const ThreeMMMovieUndoServices *s)
{
    s->clear(s->context);
    s->notify(s->context, THREE_MM_MOVIE_UNDO_DISABLED);
}
static void changed(int bars, const ThreeMMMovieEditServices *s)
{
    s->dirty(s->context);
    s->invalidate(s->context, bars);
}
ThreeMMI32 three_mm_movie_selected_actor(const ThreeMMMovieEditServices *s)
{
    void *actor;
    if (!s->scenes->open || !(actor = s->selected_actor(s->scenes->open)))
        return -1;
    return s->rollcall.actor_id(actor);
}
int three_mm_movie_choose_actor(ThreeMMI32 id, const ThreeMMMovieEditServices *s)
{
    void *actor, *snapshot, *view = s->first_view(s->context);
    ThreeMMRollCallActor row;
    ThreeMMTag tag;
    if (!view)
        return 0;
    actor = s->actor_from_id(s->scenes->open, id);
    if (actor)
    {
        if (!s->actor_visible(actor))
        {
            snapshot = s->duplicate_actor(actor, 1);
            if (!snapshot)
                return 0;
            if (!s->core_onstage(actor))
            {
                s->release_actor(snapshot);
                return 0;
            }
            s->select_actor(s->scenes->open, actor);
            if (!three_mm_movie_rollcall_add(actor, 0, &s->rollcall))
                s->diagnostic(s->context, THREE_MM_MOVIE_EDIT_ROLLCALL_FAILED);
            s->place_actor(view, 0);
            s->placement_undo(view, snapshot);
        }
        s->select_actor(s->scenes->open, actor);
        s->invalidate(s->context, 0);
        return 1;
    }
    if (three_mm_movie_rollcall_find(id, &row, &s->rollcall) == -1)
        return 0;
    tag = three_mm_movie_rollcall_tag(&row);
    actor = s->new_actor(s->context, &tag);
    if (!actor)
        return 0;
    s->rollcall.set_actor_id(actor, id);
    if (!s->add_actor(s->scenes->open, actor))
    {
        s->release_actor(actor);
        return 0;
    }
    s->place_actor(view, 0);
    s->release_actor(actor);
    return 1;
}
static int cache(const ThreeMMTag *tag, int background, const ThreeMMMovieEditServices *s)
{
    int result;
    s->long_op(s->context, 1);
    result = s->cache_tag(s->context, tag, background);
    s->long_op(s->context, 0);
    return result;
}
int three_mm_movie_insert_actor(const ThreeMMTag *tag, const ThreeMMMovieEditServices *s)
{
    void *actor;
    if (!cache(tag, 0, s))
        return 0;
    actor = s->new_actor(s->context, tag);
    if (!actor)
        return 0;
    if (!s->add_actor(s->scenes->open, actor))
    {
        s->release_actor(actor);
        return 0;
    }
    s->release_actor(actor);
    changed(1, s);
    return 1;
}
int three_mm_movie_onstage(ThreeMMI32 id, const ThreeMMMovieEditServices *s)
{
    void *actor;
    ThreeMMMovieEditRequest request = {0};
    if (id == -1)
        actor = s->selected_actor(s->scenes->open);
    else
    {
        actor = s->actor_from_id(s->scenes->open, id);
        s->select_actor(s->scenes->open, actor);
    }
    if (!actor)
    {
        s->diagnostic(s->context, THREE_MM_MOVIE_EDIT_NO_ACTOR);
        return 0;
    }
    /* This operation consumes no request fields. */
    request.tag = 0;
    if (!s->apply_actor(actor, THREE_MM_MOVIE_EDIT_ONSTAGE, &request))
        return 0;
    changed(0, s);
    return 1;
}
int three_mm_movie_edit_actor(int kind, const ThreeMMMovieEditRequest *input, const ThreeMMMovieEditServices *s)
{
    ThreeMMMovieEditRequest request = *input;
    void *actor, *undo;
    if (kind == THREE_MM_MOVIE_EDIT_TIME)
        request.shift = three_mm_fixed_sub(request.frame, s->frame(s->scenes->open));
    actor = s->selected_actor(s->scenes->open);
    if (kind == THREE_MM_MOVIE_EDIT_SQUASH)
    {
        if (request.factor == 0)
            return 1;
        request.pull[0] = request.pull[2] = request.factor;
        request.pull[1] = three_mm_fixed_div(65536, request.factor);
    }
    if (kind == THREE_MM_MOVIE_EDIT_REMOVE)
    {
        if (!s->remove_actor(s->scenes->open, s->rollcall.actor_id(actor)))
            return 0;
    }
    else if (!s->apply_actor(actor, kind, &request))
        return 0;
    if (kind == THREE_MM_MOVIE_EDIT_ACTOR_SOUND)
        return 1;
    if (kind == THREE_MM_MOVIE_EDIT_TIME && s->undo_count(s->context) > 0)
    {
        undo = s->last_undo(s->context);
        if (s->actor_undo(undo) && s->time_undo(undo))
            s->time_undo_frame(undo, request.frame);
    }
    changed(kind == THREE_MM_MOVIE_EDIT_SCALE || kind == THREE_MM_MOVIE_EDIT_REMOVE, s);
    if (kind == THREE_MM_MOVIE_EDIT_REMOVE)
        s->actor_nuked(s->context);
    return 1;
}
int three_mm_movie_edit_scene(int kind, const ThreeMMMovieEditRequest *input, const ThreeMMMovieEditServices *s)
{
    ThreeMMMovieEditRequest request = *input;
    void *sound;
    if (kind == THREE_MM_MOVIE_EDIT_BACKGROUND_SOUND && (request.volume == -1 || request.style == 0))
    {
        sound = s->fetch_sound(s->context, request.tag);
        if (!sound)
            return 0;
        if (request.volume == -1)
            request.volume = s->sound_volume(sound);
        if (request.style == 0)
            request.style = s->sound_style(sound);
        s->release_sound(sound);
    }
    if (!s->apply_scene(s->scenes->open, kind, &request))
        return 0;
    if (kind != THREE_MM_MOVIE_EDIT_BACKGROUND_SOUND)
    {
        s->dirty(s->context);
        if (kind == THREE_MM_MOVIE_EDIT_CAMERA)
            s->invalidate(s->context, 0);
    }
    return 1;
}
int three_mm_movie_paste_actor(void *actor, const ThreeMMMovieEditServices *s)
{
    void *view = s->first_view(s->context);
    if (!view || !s->paste_actor(s->scenes->open, actor))
        return 0;
    s->select_actor(s->scenes->open, actor);
    s->place_actor(view, 1);
    changed(1, s);
    return 1;
}
int three_mm_movie_paste_route(void *source, const ThreeMMMovieEditServices *s)
{
    void *actor = s->selected_actor(s->scenes->open), *snapshot;
    if (!actor)
    {
        s->diagnostic(s->context, THREE_MM_MOVIE_EDIT_NO_ACTOR);
        return 0;
    }
    snapshot = s->duplicate_actor(actor, 0);
    if (!snapshot)
        return 0;
    actor = s->selected_actor(s->scenes->open);
    if (!s->paste_route(actor, source))
    {
        s->release_actor(snapshot);
        return 0;
    }
    actor = s->selected_actor(s->scenes->open);
    if (!s->create_actor_undo(actor, snapshot))
    {
        actor = s->selected_actor(s->scenes->open);
        s->restore_actor(actor, snapshot);
        s->release_actor(snapshot);
        return 0;
    }
    changed(1, s);
    s->release_actor(snapshot);
    return 1;
}
int three_mm_movie_add_background(const ThreeMMTag *tag, const ThreeMMMovieEditServices *s)
{
    ThreeMMI32 index;
    void *undo;
    if (!cache(tag, 1, s))
        return 0;
    if (!s->scenes->open && s->data->title.count == 0)
        s->default_title(s->context);
    if (!s->scenes->open || !s->empty_scene(s->scenes->open))
    {
        index = three_mm_signed_word((ThreeMMU32)s->scenes->index + 1U);
        if (!s->new_scene(s->context, index))
            return 0;
        if (!s->background(s->context, tag, 0))
        {
            s->remove_scene(s->context, index);
            return 0;
        }
        if (s->sounds_enabled(s->context))
            s->play_sound(s->context);
        else
            s->flush_sound(s->context);
        undo = s->new_scene_undo(s->context);
        if (!undo)
        {
            s->remove_scene(s->context, index);
            return 0;
        }
        s->configure_scene_undo(s->context, undo, index, tag);
        if (!three_mm_movie_undo_add(undo, &s->undo))
        {
            s->release_undo(undo);
            s->remove_scene(s->context, index);
            return 0;
        }
        s->release_undo(undo);
    }
    else if (!s->background(s->context, tag, 1))
    {
        s->diagnostic(s->context, THREE_MM_MOVIE_EDIT_BACKGROUND_FAILED);
        return 0;
    }
    changed(1, s);
    return 1;
}

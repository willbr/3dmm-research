/* Movie editing and common undo control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_edit.h"
#include "mm/actor_motion.h"
#include "mm/actor_transform_edit.h"
#include "mm/actor_undo.h"
#include "mm/scene_selection.h"
#include "mm/wide.h"
static void changed(int bars, const MMMovieEditServices *s)
{
    s->dirty(s->context);
    s->invalidate(s->context, bars);
}
MMI32 mm_movie_selected_actor(const MMMovieEditServices *s)
{
    void *actor;
    if (!s->scenes->open || !(actor = s->selected_actor(s->scenes->open)))
        return -1;
    return s->rollcall.actor_id(actor);
}
int mm_movie_choose_actor(MMI32 id, const MMMovieEditServices *s)
{
    void *actor, *snapshot, *view = s->first_view(s->context);
    MMRollCallActor row;
    MMTag tag;
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
            if (!mm_movie_rollcall_add(actor, 0, 0, &s->rollcall))
                s->diagnostic(s->context, MM_MOVIE_EDIT_ROLLCALL_FAILED);
            s->place_actor(view, 0);
            s->placement_undo(view, snapshot);
        }
        s->select_actor(s->scenes->open, actor);
        s->invalidate(s->context, 0);
        return 1;
    }
    if (mm_movie_rollcall_find(&s->rollcall.data->actors, id, &row) == -1)
        return 0;
    tag = mm_movie_rollcall_tag(&row);
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
static int cache(const MMTag *tag, int background, const MMMovieEditServices *s)
{
    int result;
    s->long_op(s->context, 1);
    result = s->cache_tag(s->context, tag, background);
    s->long_op(s->context, 0);
    return result;
}
int mm_movie_insert_actor(const MMTag *tag, const MMMovieEditServices *s)
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
int mm_movie_onstage(MMI32 id, const MMMovieEditServices *s)
{
    void *actor;
    MMMovieEditRequest request = {0};
    if (id == -1)
        actor = s->selected_actor(s->scenes->open);
    else
    {
        actor = s->actor_from_id(s->scenes->open, id);
        s->select_actor(s->scenes->open, actor);
    }
    if (!actor)
    {
        s->diagnostic(s->context, MM_MOVIE_EDIT_NO_ACTOR);
        return 0;
    }
    /* This operation consumes no request fields. */
    request.tag = 0;
    if (!s->apply_actor(actor, MM_MOVIE_EDIT_ONSTAGE, &request))
        return 0;
    changed(0, s);
    return 1;
}
static int edit_time(MMMovieEditRequest request, int relative, const MMMovieEditServices *s)
{
    void *scene = s->scenes->open;
    void *actor;
    MMI32 current;
    MMActorUndoState *undo;
    if (!scene)
        return 0;
    current = s->frame(scene);
    actor = s->selected_actor(scene);
    if (!actor)
        return 0;
    if (relative)
        request.frame = mm_fixed_add(current, request.frame);
    request.shift = mm_fixed_sub(request.frame, current);
    if (!s->apply_actor(actor, MM_MOVIE_EDIT_TIME, &request))
        return 0;
    undo = s->last_actor_undo(s->context);
    if (undo && undo->sooner_later)
        undo->last_frame = request.frame;
    changed(0, s);
    return 1;
}

int mm_movie_step_actor_time(MMI32 frames, const MMMovieEditServices *s)
{
    MMMovieEditRequest request = {0};
    request.frame = frames;
    return edit_time(request, 1, s);
}

int mm_movie_edit_actor(int kind, const MMMovieEditRequest *input, const MMMovieEditServices *s)
{
    MMMovieEditRequest request = *input;
    void *actor;
    if (kind == MM_MOVIE_EDIT_TIME)
        return edit_time(request, 0, s);
    actor = s->selected_actor(s->scenes->open);
    if (kind == MM_MOVIE_EDIT_SQUASH)
    {
        if (request.factor == 0)
            return 1;
        request.pull[0] = request.pull[2] = request.factor;
        request.pull[1] = mm_fixed_div(65536, request.factor);
    }
    if (kind == MM_MOVIE_EDIT_REMOVE)
    {
        if (!s->remove_actor(s->scenes->open, s->rollcall.actor_id(actor)))
            return 0;
    }
    else if (!s->apply_actor(actor, kind, &request))
        return 0;
    if (kind == MM_MOVIE_EDIT_ACTOR_SOUND)
        return 1;
    changed(kind == MM_MOVIE_EDIT_SCALE || kind == MM_MOVIE_EDIT_REMOVE, s);
    if (kind == MM_MOVIE_EDIT_REMOVE)
        s->actor_nuked(s->context);
    return 1;
}

static void drag_compose(MMMovieActorDrag *drag, const MMMovieActorDragInput *input,
                         const MMMovieActorDragServices *s)
{
    MMActorGroupTransform edit = {0};
    int moved;
    edit.kind = input->command ? MM_ACTOR_GROUP_TWEAK : MM_ACTOR_GROUP_MOVE;
    edit.delta = input->world;
    if (input->ground)
        edit.move_flags |= MM_MOVE_GROUND;
    if (!input->command && input->shift)
        edit.move_flags |= MM_MOVE_SUBROUTE;
    if (input->command)
        s->adjust_cursor(s->view);
    moved = mm_actor_group_transform(&edit, s->selection, s->actors);
    if (moved)
    {
        mm_actor_drag_undo_commit(&drag->undo, 0, s->undo);
        if (!input->command)
        {
            s->adjust_cursor(s->view);
            s->world_dirty(s->view);
        }
    }
    if (input->command || moved)
        s->mark_views(s->view);
}

void mm_movie_actor_drag_sample(MMMovieActorDrag *drag, const MMMovieActorDragInput *input,
                                const MMMovieActorDragServices *s)
{
    MMActorGroupTransform edit = {0};
    MMMovieEditRequest request = {0};
    MMI32 sum, angle;
    int kind, axis, direction;
    if (input->tool == MM_MOVIE_DRAG_COMPOSE)
    {
        drag_compose(drag, input, s);
        return;
    }
    /* BRender's 0.001 multiplier is 65 in signed 16.16. The Y tool
     * reverses the converted angle; squash reverses the scalar delta. */
    sum = mm_fixed_add(input->mouse.dxr, input->mouse.dyr);
    edit.pivot = drag->undo.pivot;
    if (input->tool <= MM_MOVIE_DRAG_ROTATE_Z)
    {
        kind = MM_MOVIE_EDIT_ROTATE;
        edit.kind = MM_ACTOR_GROUP_ROTATE;
        axis = input->tool - MM_MOVIE_DRAG_ROTATE_X;
        angle = mm_fixed_mul(sum, -65);
        if (axis == 1)
            angle = mm_fixed_sub(0, angle);
        request.rotation[axis] = edit.angles[axis] = (unsigned short)angle;
        request.forward = edit.forward = input->command;
    }
    else
    {
        int squash = input->tool == MM_MOVIE_DRAG_SQUASH;
        kind = squash ? MM_MOVIE_EDIT_SQUASH : MM_MOVIE_EDIT_SCALE;
        edit.kind = squash ? MM_ACTOR_GROUP_SQUASH : MM_ACTOR_GROUP_RESIZE;
        if (squash)
            sum = mm_fixed_sub(0, sum);
        request.factor = edit.factor = mm_fixed_add(65536L, mm_fixed_mul(sum, 65));
        direction = (sum > 0) - (sum < 0);
        if (direction && direction != drag->size_direction)
        {
            drag->size_direction = direction;
            s->size_sound(s->view, squash ? direction > 0 : direction < 0);
        }
    }

    if (mm_scene_selection_count(s->selection) >= 2)
    {
        if (mm_actor_group_transform(&edit, s->selection, s->actors))
        {
            mm_actor_drag_undo_commit(&drag->undo, 0, s->undo);
            changed(kind == MM_MOVIE_EDIT_SCALE, s->movie);
        }
    }
    else if (mm_movie_edit_actor(kind, &request, s->movie))
        mm_actor_drag_undo_commit(&drag->undo, 1, s->undo);

    s->world_dirty(s->view);
    s->mark_views(s->view);
    s->adjust_cursor(s->view);
}

int mm_movie_edit_scene(int kind, const MMMovieEditRequest *input, const MMMovieEditServices *s)
{
    MMMovieEditRequest request = *input;
    void *sound;
    if (kind == MM_MOVIE_EDIT_BACKGROUND_SOUND && (request.volume == -1 || request.style == 0))
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
    if (kind != MM_MOVIE_EDIT_BACKGROUND_SOUND)
    {
        s->dirty(s->context);
        if (kind == MM_MOVIE_EDIT_CAMERA)
            s->invalidate(s->context, 0);
    }
    return 1;
}
int mm_movie_paste_actor(void *actor, const MMMovieEditServices *s)
{
    void *view = s->first_view(s->context);
    if (!view || !s->paste_actor(s->scenes->open, actor))
        return 0;
    s->select_actor(s->scenes->open, actor);
    s->place_actor(view, 1);
    changed(1, s);
    return 1;
}
int mm_movie_paste_route(void *source, const MMMovieEditServices *s)
{
    void *actor = s->selected_actor(s->scenes->open), *snapshot;
    if (!actor)
    {
        s->diagnostic(s->context, MM_MOVIE_EDIT_NO_ACTOR);
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
int mm_movie_add_background(const MMTag *tag, const MMMovieEditServices *s)
{
    MMI32 index;
    void *undo;
    if (!cache(tag, 1, s))
        return 0;
    if (!s->scenes->open && s->data->title.count == 0)
        s->default_title(s->context);
    if (!s->scenes->open || !s->empty_scene(s->scenes->open))
    {
        index = mm_signed_word((MMU32)s->scenes->index + 1U);
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
        if (!mm_movie_undo_add(undo, &s->undo))
        {
            s->release_undo(undo);
            s->remove_scene(s->context, index);
            return 0;
        }
        s->release_undo(undo);
    }
    else if (!s->background(s->context, tag, 1))
    {
        s->diagnostic(s->context, MM_MOVIE_EDIT_BACKGROUND_FAILED);
        return 0;
    }
    changed(1, s);
    return 1;
}

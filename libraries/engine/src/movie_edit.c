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
    void *actor, *view = s->first_view(s->context);
    MMRollCallActor row;
    MMTag tag;
    if (!view)
        return 0;
    actor = s->actor_from_id(s->scenes->open, id);
    if (actor)
    {
        if (!s->actor_visible(actor))
        {
            MMActorOwnerResult snapshot = {NULL, NULL, NULL};
            if (!mm_actor_owner_duplicate(actor, 1, &snapshot, &s->actors))
                return 0;
            if (!mm_actor_stage_apply(actor, 1, s->stage))
            {
                s->actors.release(s->actors.context, snapshot.actor);
                return 0;
            }
            s->select_actor(s->scenes->open, actor);
            if (!mm_movie_rollcall_add(actor, 0, 0, &s->rollcall))
                s->diagnostic(s->context, MM_MOVIE_EDIT_ROLLCALL_FAILED);
            s->place_actor(view, 0);
            s->placement_undo(view, snapshot.actor);
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
        s->actors.release(s->actors.context, actor);
        return 0;
    }
    s->place_actor(view, 0);
    s->actors.release(s->actors.context, actor);
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
        s->actors.release(s->actors.context, actor);
        return 0;
    }
    s->actors.release(s->actors.context, actor);
    changed(1, s);
    return 1;
}
int mm_movie_onstage(MMI32 id, const MMMovieEditServices *s)
{
    void *actor;
    MMActorStageEditServices stage;
    MMActorTransactionServices transaction;
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
    stage = s->stage(actor);
    transaction.actors = s->actors;
    transaction.undo = s->actor_undo(actor);
    if (!mm_actor_transaction_stage(actor, 1, &stage, &transaction))
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
    void *actor = s->selected_actor(s->scenes->open);
    MMActorOwnerResult snapshot = {NULL, NULL, NULL};
    MMActorUndoCreateServices undo;
    MMActorRoutePasteServices paste;
    MMActorOwner *owner;
    const MMActorOwner *clipboard;
    if (!actor)
    {
        s->diagnostic(s->context, MM_MOVIE_EDIT_NO_ACTOR);
        return 0;
    }
    if (!mm_actor_owner_duplicate(actor, 0, &snapshot, &s->actors))
        return 0;
    actor = s->selected_actor(s->scenes->open);
    owner = s->actors.owner(s->actors.context, actor);
    clipboard = s->actors.owner(s->actors.context, source);
    paste = s->route_paste(actor);
    if (!mm_actor_route_paste(owner, clipboard->track.route, &paste))
    {
        s->actors.release(s->actors.context, snapshot.actor);
        return 0;
    }
    actor = s->selected_actor(s->scenes->open);
    owner = s->actors.owner(s->actors.context, actor);
    undo = s->actor_undo(actor);
    if (!mm_actor_undo_create(snapshot.actor, owner->runtime.actor, 0, NULL, 0, &s->actors, &undo))
    {
        actor = s->selected_actor(s->scenes->open);
        mm_actor_owner_restore(actor, snapshot.actor, &s->actors);
        s->actors.release(s->actors.context, snapshot.actor);
        return 0;
    }
    changed(1, s);
    s->actors.release(s->actors.context, snapshot.actor);
    return 1;
}

static void clipboard_clear_selection(const MMMovieClipboardServices *s)
{
    void *scene = s->scene(s->context), *actor;
    int sounds, alive, deleted;

    if (!scene)
        return;
    actor = s->selected_actor(scene);
    if (!actor)
    {
        s->diagnostic(s->context, MM_MOVIE_CLIP_NO_ACTOR);
        return;
    }
    sounds = s->scene_sounds_enabled(scene);
    s->scene_sounds(scene, 0);
    deleted = s->delete_actor(actor, s->shift(s->context), &alive);
    if (sounds)
        s->scene_sounds(s->scene(s->context), 1);
    if (!deleted)
        return;
    if (alive)
        mm_actor_stage_apply(actor, 0, s->stage);
    else
        s->remove_actor(s->scene(s->context), actor);
    s->dirty(s->scene(s->context));
    s->invalidate(s->context);
}

static void clipboard_copy(int tool, const MMMovieClipboardServices *s)
{
    void *scene, *actor, *document;

    if (s->text_mode(s->context) || !(scene = s->scene(s->context)))
        return;
    actor = s->selected_actor(scene);
    if (!actor || !s->actor_visible(actor))
    {
        s->diagnostic(s->context, MM_MOVIE_CLIP_NO_ACTOR);
        return;
    }
    document = s->copy_actor(actor, tool == MM_MOVIE_CLIP_ROUTE, s->shift(s->context));
    if (!document)
        return;
    s->publish(s->context, document);
    s->release(document);
    if (tool == MM_MOVIE_CLIP_CUT)
        clipboard_clear_selection(s);
    s->cue(s->context, tool);
}

static void clipboard_paste(const MMMovieClipboardServices *s)
{
    void *document, *scene, *actor;
    int pasted, route;

    if (s->empty(s->context))
        return;
    if (s->format(s->context, MM_MOVIE_CLIP_TEXT_BOX, &document))
    {
        scene = s->scene(s->context);
        pasted = scene && s->paste_text(scene, document);
        s->release(document);
        if (!scene)
            return;
        if (pasted)
            s->enable_text(s->context);
    }
    else if (s->format(s->context, MM_MOVIE_CLIP_ACTOR, &document))
    {
        scene = s->scene(s->context);
        if (!scene)
            s->diagnostic(s->context, MM_MOVIE_CLIP_NO_SCENE);
        else
        {
            route = s->route_only(document);
            if (route && s->text_mode(s->context))
                s->diagnostic(s->context, MM_MOVIE_CLIP_UNSUPPORTED);
            else
            {
                actor = s->selected_actor(scene);
                if (route && (!actor || !s->actor_visible(actor)))
                    s->diagnostic(s->context, MM_MOVIE_CLIP_NO_ACTOR);
                else
                    s->paste_actor(s->context, document);
            }
        }
        s->release(document);
    }
    else if (s->format(s->context, MM_MOVIE_CLIP_TEXT_BOX, &document))
    {
        /* A format request can import the document from the native clipboard. */
        scene = s->scene(s->context);
        if (scene)
            s->paste_text(scene, document);
        else
            s->diagnostic(s->context, MM_MOVIE_CLIP_NO_SCENE);
        s->release(document);
    }
    else
        s->diagnostic(s->context, MM_MOVIE_CLIP_UNSUPPORTED);
    s->cue(s->context, MM_MOVIE_CLIP_PASTE);
}

void mm_movie_clipboard_command(int command, const MMMovieClipboardServices *s)
{
    void *scene = s->scene(s->context), *text = 0, *document;
    int immediate = 0, tool;

    if (scene)
    {
        if (s->text_mode(s->context))
        {
            text = s->selected_text(scene);
            if (text && !s->text_visible(text))
                text = 0;
        }
        if (command == MM_MOVIE_CLIP_PASTE_TOOL && s->format(s->context, MM_MOVIE_CLIP_ACTOR, &document))
        {
            immediate = !s->route_only(document);
            s->release(document);
        }
        else if ((text && s->text_selected(text)) ||
                 (command == MM_MOVIE_CLIP_PASTE_TOOL && s->format(s->context, MM_MOVIE_CLIP_TEXT_BOX, 0)))
        {
            if (text)
                s->queue_text(s->context, text);
            immediate = 1;
        }
    }

    switch (command)
    {
    case MM_MOVIE_CLIP_CUT_TOOL:
    case MM_MOVIE_CLIP_COPY_TOOL:
        tool = command == MM_MOVIE_CLIP_CUT_TOOL ? MM_MOVIE_CLIP_CUT : MM_MOVIE_CLIP_COPY;
        if (immediate)
            s->queue_generic(s->context, tool);
        else
            s->set_tool(s->context, tool);
        break;
    case MM_MOVIE_CLIP_PASTE_TOOL:
        if (!immediate)
            s->set_tool(s->context, MM_MOVIE_CLIP_PASTE);
        else if (s->format(s->context, MM_MOVIE_CLIP_TEXT_BOX, 0) || s->format(s->context, MM_MOVIE_CLIP_ACTOR, 0))
            clipboard_paste(s);
        else
            s->queue_generic(s->context, MM_MOVIE_CLIP_PASTE);
        break;
    case MM_MOVIE_CLIP_ROUTE:
        s->set_tool(s->context, MM_MOVIE_CLIP_ROUTE);
        break;
    case MM_MOVIE_CLIP_PASTE:
        clipboard_paste(s);
        break;
    case MM_MOVIE_CLIP_SHIFT_CUT:
    case MM_MOVIE_CLIP_SHIFT_COPY:
        s->set_shift(s->context, 1);
        /* Fall through to the same Edit menu operation. */
    case MM_MOVIE_CLIP_CUT:
    case MM_MOVIE_CLIP_COPY:
        tool = (command == MM_MOVIE_CLIP_CUT || command == MM_MOVIE_CLIP_SHIFT_CUT)
                   ? MM_MOVIE_CLIP_CUT : MM_MOVIE_CLIP_COPY;
        clipboard_copy(tool, s);
        s->set_shift(s->context, 0);
        break;
    default:
        s->diagnostic(s->context, MM_MOVIE_CLIP_BAD_COMMAND);
        break;
    }
}

void mm_movie_clipboard_click(int tool, int hit_actor, const MMMovieClipboardServices *s)
{
    void *document;

    if (s->text_mode(s->context))
        return;
    if (tool != MM_MOVIE_CLIP_PASTE)
        clipboard_copy(tool, s);
    else if (s->format(s->context, MM_MOVIE_CLIP_ACTOR, &document))
    {
        if (s->route_only(document) && hit_actor)
            clipboard_paste(s);
        s->release(document);
    }
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

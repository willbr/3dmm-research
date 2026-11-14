/* Scene pause/camera/background editing converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_edit.h"
#include "three_mm/wide.h"
#include <string.h>
static void *group(int start, const ThreeMMSceneEditServices *s)
{
    return s->sound.events.owner->owned[start ? THREE_MM_SCENE_STARTS : THREE_MM_SCENE_FRAMES];
}
static ThreeMMI32 count(int start, const ThreeMMSceneEditServices *s)
{
    const ThreeMMSceneOwnerServices *o = &s->sound.events.owners;
    return o->count(o->context, group(start, s));
}
static int event(int start, ThreeMMI32 i, ThreeMMI32 *frame, const ThreeMMSceneEditServices *s)
{
    int kind;
    s->sound.events.event(s->sound.events.context, group(start, s), i, frame, &kind);
    return kind;
}
static void *payload(int start, ThreeMMI32 i, const ThreeMMSceneEditServices *s)
{
    return s->sound.events.payload(s->sound.events.context, group(start, s), i);
}
static ThreeMMI32 camera_at(ThreeMMI32 i, const ThreeMMSceneEditServices *s)
{
    ThreeMMI32 camera;
    memcpy(&camera, payload(0, i, s), 4);
    return camera;
}
int three_mm_scene_pause_edit(ThreeMMI32 *reason, ThreeMMI32 *delay, const ThreeMMSceneEditServices *s)
{
    ThreeMMSceneRuntime *r = s->sound.events.runtime;
    ThreeMMI32 i, frame, old[2], value[2];
    for (i = r->next_event - 1; i >= 0; --i)
    {
        int kind = event(0, i, &frame, s);
        if (frame != s->sound.events.data->currentFrame)
            break;
        if (kind != THREE_MM_SCENE_PAUSE)
            continue;
        memcpy(old, payload(0, i, s), 8);
        if (!*reason)
        {
            s->erase(s->context, group(0, s), i);
            --r->next_event;
        }
        else
        {
            value[0] = *reason;
            value[1] = *delay;
            s->put(s->context, group(0, s), i, value);
        }
        *reason = old[0];
        *delay = old[1];
        return 1;
    }
    value[0] = *reason;
    value[1] = *delay;
    if (!three_mm_scene_edit_insert(s->sound.events.data->currentFrame, THREE_MM_SCENE_PAUSE, 8, value, &s->sound))
        return 0;
    *reason = 0;
    *delay = 0;
    return 1;
}
static int play_camera(ThreeMMI32 frame, ThreeMMI32 *camera, const ThreeMMSceneEditServices *s)
{
    return three_mm_scene_play_event(frame, THREE_MM_SCENE_CHANGE_CAMERA, camera, s->sound.events.runtime->disabled,
                                     &s->sound.events);
}
static void dirty(const ThreeMMSceneEditServices *s)
{
    s->sound.dirty(s->sound.context);
}
static void old_camera(ThreeMMI32 *out, ThreeMMI32 value, const ThreeMMSceneEditServices *s)
{
    *out = value;
    if (s->publish_camera)
        s->publish_camera(s->output_context, value);
}
int three_mm_scene_camera_edit(ThreeMMI32 camera, ThreeMMI32 *old, const ThreeMMSceneEditServices *s)
{
    ThreeMMSceneRuntime *r = s->sound.events.runtime;
    ThreeMMI32 i, j, frame, previous;
    old_camera(old, 0, s);
    for (i = r->next_event - 1; i >= 0; --i)
    {
        if (event(0, i, &frame, s) != THREE_MM_SCENE_CHANGE_CAMERA)
            continue;
        if (frame == s->sound.events.data->currentFrame)
        {
            for (j = i - 1; j >= 0; --j)
                if (event(0, j, &previous, s) == THREE_MM_SCENE_CHANGE_CAMERA)
                {
                    old_camera(old, camera_at(j, s), s);
                    break;
                }
            if (*old == camera)
            {
                old_camera(old, camera_at(i, s), s);
                if (!play_camera(frame, &camera, s))
                    return 0;
                s->erase(s->context, group(0, s), i);
                --r->next_event;
                dirty(s);
                goto success;
            }
            old_camera(old, camera_at(i, s), s);
            s->put(s->context, group(0, s), i, &camera);
            dirty(s);
            if (!play_camera(frame, &camera, s))
                return 0;
            goto success;
        }
        old_camera(old, camera_at(i, s), s);
        break;
    }
    if (*old == camera)
        goto success;
    frame = s->sound.events.data->currentFrame;
    if (!three_mm_scene_edit_insert(frame, THREE_MM_SCENE_CHANGE_CAMERA, 4, &camera, &s->sound))
        return 0;
    if (!play_camera(frame, &camera, s))
    {
        s->erase(s->context, group(0, s), --r->next_event);
        return 0;
    }
success:
    for (i = r->next_event; i < count(0, s); ++i)
        if (event(0, i, &frame, s) == THREE_MM_SCENE_CHANGE_CAMERA)
        {
            if (camera_at(i, s) == camera)
                s->erase(s->context, group(0, s), i);
            break;
        }
    return 1;
}
static void output_tag(ThreeMMTag *out, const ThreeMMTag *tag, int first, const ThreeMMSceneEditServices *s)
{
    *out = *tag;
    if (s->publish_tag)
        s->publish_tag(s->output_context, tag, first);
}
int three_mm_scene_background_edit(const ThreeMMTag *tag, ThreeMMTag *old, const ThreeMMSceneEditServices *s)
{
    const ThreeMMSceneEventServices *e = &s->sound.events;
    ThreeMMI32 i, j, frame = 0;
    ThreeMMTag value;
    ThreeMMU32 mask;
    if (e->owner->owned[THREE_MM_SCENE_BACKGROUND])
    {
        for (i = 0; i < count(1, s); ++i)
        {
            if (event(1, i, &frame, s) != THREE_MM_SCENE_SET_BACKGROUND)
                continue;
            e->read_tag(e->context, payload(1, i, s), &value);
            output_tag(old, &value, 0, s);
            if (!s->put_background(s->context, i, tag))
                return 0;
            e->owners.lights_off(e->owners.context, e->owner->owned[THREE_MM_SCENE_BACKGROUND]);
            three_mm_scene_owner_drop(e->owner, e->scene, THREE_MM_SCENE_BACKGROUND, &e->owners);
            dirty(s);
            mask = e->runtime->disabled;
            if (!s->play_background(s->context, frame, tag, mask))
            {
                s->put_background(s->context, i, old);
                s->play_background(s->context, frame, old, e->runtime->disabled);
                return 0;
            }
            s->background_name(s->context);
            goto success;
        }
        s->diagnostic(s->context, THREE_MM_SCENE_EDIT_NO_BACKGROUND_EVENT);
        return 0;
    }
    value.sid = -1;
    value.cache = NULL;
    value.type = value.number = 0;
    output_tag(old, &value, 1, s);
    if (!s->insert_background(s->context, 0, tag))
        return 0;
    if (!s->play_background(s->context, 0, tag, e->runtime->disabled))
    {
        s->erase(s->context, group(1, s), 0);
        return 0;
    }
    s->background_name(s->context);
    dirty(s);
success:
    while (count(0, s))
    {
        i = count(0, s) - 1;
        if (event(0, i, &frame, s) == THREE_MM_SCENE_PLAY_SOUND)
        {
            ThreeMMSceneSound *sound = (ThreeMMSceneSound *)payload(0, i, s);
            for (j = 0; j < sound->ctagc; ++j)
                e->owners.sound.close(e->owners.sound.context, payload(0, i, s), j);
        }
        s->erase(s->context, group(0, s), i);
    }
    e->runtime->next_event = 0;
    s->scene_change(s->context);
    if (s->can_notify(s->context))
        s->loaded(s->context);
    return 1;
}
int three_mm_scene_is_empty(const ThreeMMSceneEditServices *s)
{
    ThreeMMI32 i, frame;
    int kind;
    if (count(1, s) != 1 || three_mm_signed_word((ThreeMMU32)s->sound.events.data->lastFrame -
                                                 (ThreeMMU32)s->sound.events.data->firstFrame) > 0)
        return 0;
    for (i = 0; i < count(0, s); ++i)
    {
        kind = event(0, i, &frame, s);
        if (kind != THREE_MM_SCENE_CHANGE_CAMERA && kind != THREE_MM_SCENE_PLAY_SOUND)
            return 0;
    }
    return 1;
}
int three_mm_scene_pause_undo_edit(ThreeMMI32 reason, ThreeMMI32 delay, const ThreeMMSceneEditServices *s)
{
    void *u = s->new_undo(s->context, THREE_MM_SCENE_UNDO_PAUSE);
    ThreeMMScenePauseUndo *state;
    if (!u)
        return 0;
    if (!three_mm_scene_pause_edit(&reason, &delay, s))
    {
        s->release_undo(s->context, u);
        return 0;
    }
    state = s->pause_state(s->context, u);
    state->reason = reason;
    state->delay = delay;
    state->add = 1;
    if (!s->commit_undo(s->context, u))
    {
        three_mm_scene_pause_edit(&reason, &delay, s);
        s->release_undo(s->context, u);
        return 0;
    }
    s->release_undo(s->context, u);
    return 1;
}
static void not_undoable(const ThreeMMSceneEditServices *s)
{
    s->clear_undo(s->context);
    s->not_undoable(s->context);
}
int three_mm_scene_camera_undo_edit(ThreeMMI32 camera, const ThreeMMSceneEditServices *s)
{
    ThreeMMTag tag;
    ThreeMMI32 old;
    void *u;
    int cached;
    memset(&tag, 0, sizeof(tag));
    if (!s->camera_tag(s->context, camera, &tag))
        return 0;
    cached = s->cache_tag(s->context, &tag);
    s->close_tag(s->context, &tag);
    if (!cached)
        return 0;
    if (!three_mm_scene_camera_edit(camera, &old, s))
        return 0;
    u = s->new_undo(s->context, THREE_MM_SCENE_UNDO_BACKGROUND);
    if (u)
    {
        ThreeMMSceneBackgroundUndo *state = s->background_state(s->context, u);
        state->camera = old;
        state->background = 0;
        if (s->commit_undo(s->context, u))
        {
            s->release_undo(s->context, u);
            return 1;
        }
        s->release_undo(s->context, u);
    }
    /* Legacy rollback retries the requested camera rather than the saved one. */
    if (!three_mm_scene_camera_edit(camera, &old, s))
        not_undoable(s);
    return 0;
}
int three_mm_scene_background_undo_edit(ThreeMMTag *tag, const ThreeMMSceneEditServices *s)
{
    ThreeMMSceneEditServices local = *s;
    ThreeMMSceneBackgroundUndo *state;
    ThreeMMTag old;
    ThreeMMI32 camera;
    void *u;
    if (!s->sound.events.owner->owned[THREE_MM_SCENE_BACKGROUND])
    {
        s->diagnostic(s->context, THREE_MM_SCENE_EDIT_FIRST_BACKGROUND);
        return 0;
    }
    camera = s->current_camera(s->context);
    local.publish_tag = NULL;
    if (!three_mm_scene_background_edit(tag, &old, &local))
        return 0;
    u = s->new_undo(s->context, THREE_MM_SCENE_UNDO_BACKGROUND);
    if (u)
    {
        state = s->background_state(s->context, u);
        state->tag = old;
        state->camera = camera;
        state->background = 1;
        if (s->commit_undo(s->context, u))
        {
            s->release_undo(s->context, u);
            return 1;
        }
        s->release_undo(s->context, u);
    }
    if (!three_mm_scene_background_edit(&old, tag, s))
        not_undoable(s);
    return 0;
}
static int undo_failed(const ThreeMMSceneEditUndoServices *s)
{
    s->flush(s->context);
    s->clear(s->context);
    return 0;
}
int three_mm_scene_pause_undo(ThreeMMScenePauseUndo *p, const ThreeMMSceneEditUndoServices *s)
{
    ThreeMMI32 reason = p->reason;
    if (!s->switch_scene(s->context) || !s->seek_frame(s->context) || !s->pause(s->context, &p->reason, &p->delay))
        return undo_failed(s);
    s->pause_type(s->context, reason);
    s->flush(s->context);
    return 1;
}
int three_mm_scene_background_undo(ThreeMMSceneBackgroundUndo *p, const ThreeMMSceneEditUndoServices *s)
{
    ThreeMMTag old;
    ThreeMMI32 camera, previous;
    if (!s->switch_scene(s->context))
        return undo_failed(s);
    if (p->background)
    {
        if (!s->background_present(s->context))
            return undo_failed(s);
        camera = s->current_camera(s->context);
        if (!s->background(s->context, &p->tag, &old) || !s->camera(s->context, p->camera, &previous))
            return undo_failed(s);
        p->tag = old;
        p->camera = camera;
    }
    else
    {
        if (!s->seek_frame(s->context) || !s->camera(s->context, p->camera, &previous))
            return undo_failed(s);
        p->camera = previous;
    }
    s->flush(s->context);
    return 1;
}

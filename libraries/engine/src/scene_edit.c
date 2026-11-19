/* Scene pause/camera/background editing converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_edit.h"
#include "mm/wide.h"
#include <string.h>
static void *group(int start, const MMSceneEditServices *s)
{
    return s->sound.events.owner->owned[start ? MM_SCENE_STARTS : MM_SCENE_FRAMES];
}
static MMI32 count(int start, const MMSceneEditServices *s)
{
    const MMSceneOwnerServices *o = &s->sound.events.owners;
    return o->count(o->context, group(start, s));
}
static int event(int start, MMI32 i, MMI32 *frame, const MMSceneEditServices *s)
{
    int kind;
    s->sound.events.event(s->sound.events.context, group(start, s), i, frame, &kind);
    return kind;
}
static void *payload(int start, MMI32 i, const MMSceneEditServices *s)
{
    return s->sound.events.payload(s->sound.events.context, group(start, s), i);
}
static MMI32 camera_at(MMI32 i, const MMSceneEditServices *s)
{
    MMI32 camera;
    memcpy(&camera, payload(0, i, s), 4);
    return camera;
}
int mm_scene_pause_edit(MMI32 *reason, MMI32 *delay, const MMSceneEditServices *s)
{
    MMSceneRuntime *r = s->sound.events.runtime;
    MMI32 i, frame, old[2], value[2];
    for (i = r->next_event - 1; i >= 0; --i)
    {
        int kind = event(0, i, &frame, s);
        if (frame != s->sound.events.data->currentFrame)
            break;
        if (kind != MM_SCENE_PAUSE)
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
    if (!mm_scene_edit_insert(s->sound.events.data->currentFrame, MM_SCENE_PAUSE, 8, value, &s->sound))
        return 0;
    *reason = 0;
    *delay = 0;
    return 1;
}
static int play_camera(MMI32 frame, MMI32 *camera, const MMSceneEditServices *s)
{
    return mm_scene_play_event(frame, MM_SCENE_CHANGE_CAMERA, camera, s->sound.events.runtime->disabled,
                                     &s->sound.events);
}
static void dirty(const MMSceneEditServices *s)
{
    s->sound.dirty(s->sound.context);
}
static void old_camera(MMI32 *out, MMI32 value, const MMSceneEditServices *s)
{
    *out = value;
    if (s->publish_camera)
        s->publish_camera(s->output_context, value);
}
int mm_scene_camera_edit(MMI32 camera, MMI32 *old, const MMSceneEditServices *s)
{
    MMSceneRuntime *r = s->sound.events.runtime;
    MMI32 i, j, frame, previous;
    old_camera(old, 0, s);
    for (i = r->next_event - 1; i >= 0; --i)
    {
        if (event(0, i, &frame, s) != MM_SCENE_CHANGE_CAMERA)
            continue;
        if (frame == s->sound.events.data->currentFrame)
        {
            for (j = i - 1; j >= 0; --j)
                if (event(0, j, &previous, s) == MM_SCENE_CHANGE_CAMERA)
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
    if (!mm_scene_edit_insert(frame, MM_SCENE_CHANGE_CAMERA, 4, &camera, &s->sound))
        return 0;
    if (!play_camera(frame, &camera, s))
    {
        s->erase(s->context, group(0, s), --r->next_event);
        return 0;
    }
success:
    for (i = r->next_event; i < count(0, s); ++i)
        if (event(0, i, &frame, s) == MM_SCENE_CHANGE_CAMERA)
        {
            if (camera_at(i, s) == camera)
                s->erase(s->context, group(0, s), i);
            break;
        }
    return 1;
}
static void output_tag(MMTag *out, const MMTag *tag, int first, const MMSceneEditServices *s)
{
    *out = *tag;
    if (s->publish_tag)
        s->publish_tag(s->output_context, tag, first);
}
MMI32 mm_scene_camera_key(MMI32 first, MMI32 frame)
{
    return frame == first ? (MMI32)MM_SCENE_OPENING_CAMERA_FRAME : frame;
}

MMI32 mm_scene_camera_frame(MMI32 first, MMI32 key)
{
    return key == MM_SCENE_OPENING_CAMERA_FRAME ? first : key;
}

int mm_scene_camera_replace(MMI32 frame, MMU32 camera, const MMSceneCameraEvents *s)
{
    MMI32 index, at, count;
    int kind;
    for (index = s->count(s->context); index-- > 0;)
    {
        s->event(s->context, index, &at, &kind);
        if (at == frame && kind == MM_SCENE_CHANGE_CAMERA)
            s->erase(s->context, index);
    }
    count = s->count(s->context);
    for (index = 0; index < count; ++index)
    {
        s->event(s->context, index, &at, &kind);
        if (at > frame)
            break;
    }
    return s->insert(s->context, index, frame, camera);
}

int mm_scene_camera_store(MMI32 first, MMI32 key, MMU32 camera, const MMSceneCameraStoreServices *s)
{
    MMI32 frame = mm_scene_camera_frame(first, key);
    void *context = s->events.context;
    return s->open_events(context) && mm_scene_camera_replace(frame, camera, &s->events) &&
           s->replace_events(context) && (frame != first || s->drop_thumbnail(context));
}

int mm_scene_camera_journal(MMI32 scene, unsigned long camera, const MMSceneCameraJournalServices *s)
{
    MMI32 first, frame;
    MMU32 count;
    if (scene < 0)
        return MM_SCENE_CAMERA_INVALID;
    s->position(s->context, &first, &frame, &count);
    if (camera >= count)
        return MM_SCENE_CAMERA_INVALID;
    if (!s->prepare(s->context, mm_scene_camera_key(first, frame), (MMU32)camera) || !s->apply(s->context))
        return 0;
    s->publish(s->context);
    return 1;
}

int mm_scene_background_edit(const MMTag *tag, MMTag *old, const MMSceneEditServices *s)
{
    const MMSceneEventServices *e = &s->sound.events;
    MMI32 i, j, frame = 0;
    MMTag value;
    MMU32 mask;
    if (e->owner->owned[MM_SCENE_BACKGROUND])
    {
        for (i = 0; i < count(1, s); ++i)
        {
            if (event(1, i, &frame, s) != MM_SCENE_SET_BACKGROUND)
                continue;
            e->read_tag(e->context, payload(1, i, s), &value);
            output_tag(old, &value, 0, s);
            if (!s->put_background(s->context, i, tag))
                return 0;
            e->owners.lights_off(e->owners.context, e->owner->owned[MM_SCENE_BACKGROUND]);
            mm_scene_owner_drop(e->owner, e->scene, MM_SCENE_BACKGROUND, &e->owners);
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
        s->diagnostic(s->context, MM_SCENE_EDIT_NO_BACKGROUND_EVENT);
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
        if (event(0, i, &frame, s) == MM_SCENE_PLAY_SOUND)
        {
            MMSceneSound *sound = (MMSceneSound *)payload(0, i, s);
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
int mm_scene_is_empty(const MMSceneEditServices *s)
{
    MMI32 i, frame;
    int kind;
    if (count(1, s) != 1 || mm_signed_word((MMU32)s->sound.events.data->lastFrame -
                                                 (MMU32)s->sound.events.data->firstFrame) > 0)
        return 0;
    for (i = 0; i < count(0, s); ++i)
    {
        kind = event(0, i, &frame, s);
        if (kind != MM_SCENE_CHANGE_CAMERA && kind != MM_SCENE_PLAY_SOUND)
            return 0;
    }
    return 1;
}
int mm_scene_pause_undo_edit(MMI32 reason, MMI32 delay, const MMSceneEditServices *s)
{
    void *u = s->new_undo(s->context, MM_SCENE_UNDO_PAUSE);
    MMScenePauseUndo *state;
    if (!u)
        return 0;
    if (!mm_scene_pause_edit(&reason, &delay, s))
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
        mm_scene_pause_edit(&reason, &delay, s);
        s->release_undo(s->context, u);
        return 0;
    }
    s->release_undo(s->context, u);
    return 1;
}
static void not_undoable(const MMSceneEditServices *s)
{
    s->clear_undo(s->context);
    s->not_undoable(s->context);
}
int mm_scene_camera_undo_edit(MMI32 camera, const MMSceneEditServices *s)
{
    MMTag tag;
    MMI32 old;
    void *u;
    int cached;
    memset(&tag, 0, sizeof(tag));
    if (!s->camera_tag(s->context, camera, &tag))
        return 0;
    cached = s->cache_tag(s->context, &tag);
    s->close_tag(s->context, &tag);
    if (!cached)
        return 0;
    if (!mm_scene_camera_edit(camera, &old, s))
        return 0;
    u = s->new_undo(s->context, MM_SCENE_UNDO_BACKGROUND);
    if (u)
    {
        MMSceneBackgroundUndo *state = s->background_state(s->context, u);
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
    if (!mm_scene_camera_edit(camera, &old, s))
        not_undoable(s);
    return 0;
}
int mm_scene_background_undo_edit(MMTag *tag, const MMSceneEditServices *s)
{
    MMSceneEditServices local = *s;
    MMSceneBackgroundUndo *state;
    MMTag old;
    MMI32 camera;
    void *u;
    if (!s->sound.events.owner->owned[MM_SCENE_BACKGROUND])
    {
        s->diagnostic(s->context, MM_SCENE_EDIT_FIRST_BACKGROUND);
        return 0;
    }
    camera = s->current_camera(s->context);
    local.publish_tag = NULL;
    if (!mm_scene_background_edit(tag, &old, &local))
        return 0;
    u = s->new_undo(s->context, MM_SCENE_UNDO_BACKGROUND);
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
    if (!mm_scene_background_edit(&old, tag, s))
        not_undoable(s);
    return 0;
}
static int undo_failed(const MMSceneEditUndoServices *s)
{
    s->flush(s->context);
    s->clear(s->context);
    return 0;
}
int mm_scene_pause_undo(MMScenePauseUndo *p, const MMSceneEditUndoServices *s)
{
    MMI32 reason = p->reason;
    if (!s->switch_scene(s->context) || !s->seek_frame(s->context) || !s->pause(s->context, &p->reason, &p->delay))
        return undo_failed(s);
    s->pause_type(s->context, reason);
    s->flush(s->context);
    return 1;
}
int mm_scene_background_undo(MMSceneBackgroundUndo *p, const MMSceneEditUndoServices *s)
{
    MMTag old;
    MMI32 camera, previous;
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

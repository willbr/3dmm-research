/* Scene pause/camera/background editing converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_edit.h"
#include "mm/wide.h"
#include <string.h>
static MMI32 camera_at(const MMSceneTrack *frames, MMI32 i)
{
    MMI32 camera;
    memcpy(&camera, mm_scene_track_payload(frames, i, NULL), sizeof(camera));
    return camera;
}
int mm_scene_pause_edit(MMI32 *reason, MMI32 *delay, const MMSceneEditServices *s)
{
    MMSceneOwner *owner = s->sound.events.owner;
    MMSceneRuntime *r = &owner->runtime;
    MMSceneTrack *frames = &owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    MMI32 i, old[2], value[2];
    for (i = r->next_event - 1; i >= 0; --i)
    {
        if (!mm_scene_track_get(frames, i, &event) || event.frame != owner->data.currentFrame)
            break;
        if (event.kind != MM_SCENE_PAUSE)
            continue;
        memcpy(old, mm_scene_track_payload(frames, i, NULL), sizeof(old));
        if (!*reason)
        {
            if (mm_group_delete(&frames->rows, i) != MM_STORAGE_OK)
                return 0;
            --r->next_event;
        }
        else
        {
            value[0] = *reason;
            value[1] = *delay;
            if (!mm_scene_track_put(frames, i, sizeof(value), value))
                return 0;
        }
        *reason = old[0];
        *delay = old[1];
        return 1;
    }
    value[0] = *reason;
    value[1] = *delay;
    if (!mm_scene_edit_insert(owner->data.currentFrame, MM_SCENE_PAUSE, sizeof(value), value, &s->sound))
        return 0;
    *reason = 0;
    *delay = 0;
    return 1;
}
static int play_camera(MMI32 frame, MMI32 *camera, const MMSceneEditServices *s)
{
    const MMSceneEventServices *events = &s->sound.events;
    return mm_scene_play_event(frame, MM_SCENE_CHANGE_CAMERA, camera, events->owner->runtime.disabled, events);
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
    MMSceneOwner *owner = s->sound.events.owner;
    MMSceneRuntime *r = &owner->runtime;
    MMSceneTrack *frames = &owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    MMI32 i, j, frame;
    old_camera(old, 0, s);
    for (i = r->next_event - 1; i >= 0; --i)
    {
        if (!mm_scene_track_get(frames, i, &event) || event.kind != MM_SCENE_CHANGE_CAMERA)
            continue;
        frame = event.frame;
        if (frame == owner->data.currentFrame)
        {
            for (j = i - 1; j >= 0; --j)
                if (mm_scene_track_get(frames, j, &event) && event.kind == MM_SCENE_CHANGE_CAMERA)
                {
                    old_camera(old, camera_at(frames, j), s);
                    break;
                }
            if (*old == camera)
            {
                old_camera(old, camera_at(frames, i), s);
                if (!play_camera(frame, &camera, s))
                    return 0;
                if (mm_group_delete(&frames->rows, i) != MM_STORAGE_OK)
                    return 0;
                --r->next_event;
                dirty(s);
                goto success;
            }
            old_camera(old, camera_at(frames, i), s);
            if (!mm_scene_track_put(frames, i, sizeof(camera), &camera))
                return 0;
            dirty(s);
            if (!play_camera(frame, &camera, s))
                return 0;
            goto success;
        }
        old_camera(old, camera_at(frames, i), s);
        break;
    }
    if (*old == camera)
        goto success;
    frame = owner->data.currentFrame;
    if (!mm_scene_edit_insert(frame, MM_SCENE_CHANGE_CAMERA, sizeof(camera), &camera, &s->sound))
        return 0;
    if (!play_camera(frame, &camera, s))
    {
        mm_group_delete(&frames->rows, --r->next_event);
        return 0;
    }
success:
    for (i = r->next_event; (MMU32)i < frames->rows.count; ++i)
        if (mm_scene_track_get(frames, i, &event) && event.kind == MM_SCENE_CHANGE_CAMERA)
        {
            if (camera_at(frames, i) == camera && mm_group_delete(&frames->rows, i) != MM_STORAGE_OK)
                return 0;
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

static int play_background(const MMTag *tag, const MMSceneEventServices *s)
{
    MMTag value = *tag;
    return mm_scene_play_event(0, MM_SCENE_SET_BACKGROUND, &value, s->owner->runtime.disabled, s);
}
int mm_scene_background_edit(const MMTag *tag, MMTag *old, const MMSceneEditServices *s)
{
    const MMSceneEventServices *e = &s->sound.events;
    MMSceneTrack *starts = &e->owner->tracks[MM_SCENE_STARTS];
    MMSceneTrack *frames = &e->owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    MMI32 i, bytes;
    const void *payload;
    MMTag value;
    if (e->owner->owned[MM_SCENE_BACKGROUND])
    {
        for (i = 0; (MMU32)i < starts->rows.count; ++i)
        {
            if (!mm_scene_track_get(starts, i, &event) || event.kind != MM_SCENE_SET_BACKGROUND)
                continue;
            payload = mm_scene_track_payload(starts, i, &bytes);
            if (!payload || bytes < (MMI32)sizeof(value))
                return 0;
            memcpy(&value, payload, sizeof(value));
            output_tag(old, &value, 0, s);
            if (!mm_scene_track_put(starts, i, sizeof(*tag), tag))
                return 0;
            e->owners.lights_off(e->owners.context, e->owner->owned[MM_SCENE_BACKGROUND]);
            mm_scene_owner_drop(e->owner, MM_SCENE_BACKGROUND, &e->owners);
            dirty(s);
            if (!play_background(tag, e))
            {
                mm_scene_track_put(starts, i, sizeof(*old), old);
                play_background(old, e);
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
    event.frame = 0;
    event.kind = MM_SCENE_SET_BACKGROUND;
    if (!mm_scene_track_insert(starts, 0, sizeof(*tag), tag, &event))
        return 0;
    if (!play_background(tag, e))
    {
        mm_group_delete(&starts->rows, 0);
        return 0;
    }
    s->background_name(s->context);
    dirty(s);
success:
    while (frames->rows.count)
        if (!mm_scene_owner_erase_frame(e->owner, frames->rows.count - 1, &e->owners))
            return 0;
    e->owner->runtime.next_event = 0;
    s->scene_change(s->context);
    if (s->can_notify(s->context))
        s->loaded(s->context);
    return 1;
}
int mm_scene_is_empty(const MMSceneEditServices *s)
{
    const MMSceneOwner *owner = s->sound.events.owner;
    const MMSceneTrack *frames = &owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    MMI32 i;
    if (owner->tracks[MM_SCENE_STARTS].rows.count != 1 ||
        mm_signed_word((MMU32)owner->data.lastFrame - (MMU32)owner->data.firstFrame) > 0)
        return 0;
    for (i = 0; (MMU32)i < frames->rows.count; ++i)
        if (!mm_scene_track_get(frames, i, &event) ||
            (event.kind != MM_SCENE_CHANGE_CAMERA && event.kind != MM_SCENE_PLAY_SOUND))
            return 0;
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
    const MMI32 reason = p->reason;
    MMSceneEditServices edit;
    if (!s->switch_scene(s->context) || !s->seek_frame(s->context))
        return undo_failed(s);
    edit = s->scene_edit(s->context);
    if (!mm_scene_pause_edit(&p->reason, &p->delay, &edit))
        return undo_failed(s);
    edit.sound.events.pause_type(edit.sound.events.context, reason);
    s->flush(s->context);
    return 1;
}
int mm_scene_background_undo(MMSceneBackgroundUndo *p, const MMSceneEditUndoServices *s)
{
    MMSceneEditServices edit;
    MMTag old;
    MMI32 camera, previous;
    if (!s->switch_scene(s->context) || (!p->background && !s->seek_frame(s->context)))
        return undo_failed(s);
    edit = s->scene_edit(s->context);
    if (p->background)
    {
        if (!edit.sound.events.owner->owned[MM_SCENE_BACKGROUND])
            return undo_failed(s);
        camera = edit.current_camera(edit.context);
        if (!mm_scene_background_edit(&p->tag, &old, &edit) || !mm_scene_camera_edit(p->camera, &previous, &edit))
            return undo_failed(s);
        p->tag = old;
        p->camera = camera;
    }
    else
    {
        if (!mm_scene_camera_edit(p->camera, &previous, &edit))
            return undo_failed(s);
        p->camera = previous;
    }
    s->flush(s->context);
    return 1;
}

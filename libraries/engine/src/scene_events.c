/* Scene event dispatch converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_events.h"
#include "mm/movie_session.h"
#include "mm/scene_sound.h"
#include <stddef.h>
#include <string.h>
void *mm_scene_pick_actor(MMI32 x, MMI32 y, const MMScenePickServices *s)
{
    MMI32 i;
    void *actor, *body = s->pick(s->context, x, y);
    if (!body)
        return NULL;
    for (i = 0; i < s->count(s->context); ++i)
    {
        actor = s->actor(s->context, i);
        if (s->owns_body(s->context, actor, body))
            return actor;
    }
    s->orphan_body(s->context);
    return NULL;
}
int mm_scene_background_tag(MMTag *out, const MMSceneEventServices *s)
{
    const MMSceneTrack *track = &s->owner->tracks[MM_SCENE_STARTS];
    MMSceneTrackEvent event;
    const void *payload;
    MMI32 i, bytes;
    for (i = 0; (MMU32)i < track->rows.count; ++i)
    {
        if (mm_scene_track_get(track, i, &event) && event.kind == MM_SCENE_SET_BACKGROUND)
        {
            payload = mm_scene_track_payload(track, i, &bytes);
            if (!payload || bytes < (MMI32)sizeof(*out))
                return 0;
            memcpy(out, payload, sizeof(*out));
            return 1;
        }
    }
    return 0;
}
static void adopt(int slot, void *object, const MMSceneEventServices *s)
{
    if (s->owner->owned[slot])
        s->diagnostic(s->context, MM_SCENE_EVENT_OWNER_OCCUPIED);
    mm_scene_owner_set(s->owner, s->scene, slot, object, &s->owners);
}
static void require_background(const MMSceneEventServices *s)
{
    if (!s->owner->owned[MM_SCENE_BACKGROUND])
        s->diagnostic(s->context, MM_SCENE_EVENT_NO_BACKGROUND);
}
int mm_scene_play_event(MMI32 frame, int kind, void *payload, MMU32 disabled,
                              const MMSceneEventServices *s)
{
    void *object, *copy, *c = s->context;
    MMTag tag;
    MMI32 value, delay;
    int text, list;
    switch (kind)
    {
    case MM_SCENE_PLAY_SOUND:
        if (((const MMSceneSound *)payload)->sty == MM_SCENE_EVENT_MIDI)
        {
            copy = s->duplicate_sound(c, payload);
            if (!copy)
                return 0;
            mm_scene_owner_drop(s->owner, s->scene, MM_SCENE_BACKGROUND_SOUND, &s->owners);
            adopt(MM_SCENE_BACKGROUND_SOUND, copy, s);
            s->runtime->background_frame = frame;
        }
        if (!(disabled & MM_SCENE_EFFECT_SOUNDS))
            s->play_sound(c, payload);
        break;
    case MM_SCENE_SET_BACKGROUND:
        memcpy(&tag, payload, sizeof(tag));
        object = s->fetch_background(c, &tag);
        if (!object)
            return 0;
        if (!s->set_camera(c, object, 0))
        {
            s->owners.release(s->owners.context, object);
            return 0;
        }
        if (s->transition(c) == MM_SCENE_EVENT_NIL_TRANSITION)
            s->set_transition(c, MM_SCENE_EVENT_CUT_TRANSITION);
        adopt(MM_SCENE_BACKGROUND, object, s);
        s->publish_tag(c, &tag);
        break;
    case MM_SCENE_ADD_ACTOR:
    case MM_SCENE_ADD_TEXT:
        text = kind == MM_SCENE_ADD_TEXT;
        list = text ? MM_SCENE_TEXTS : MM_SCENE_ACTORS;
        memcpy(&object, payload, sizeof(object));
        if (!mm_scene_member_append(s->owner, list, object))
            return 0;
        if (!text)
            s->bind_actor(c, object);
        if (!s->seek(c, text, object, s->data->currentFrame))
        {
            mm_scene_member_remove(s->owner, list, mm_scene_member_count(s->owner, list) - 1);
            return 0;
        }
        break;
    case MM_SCENE_PAUSE:
        memcpy(&value, payload, sizeof(value));
        s->pause_type(c, value);
        if (!(disabled & MM_SCENE_EFFECT_PAUSES))
        {
            memcpy(&delay, (const unsigned char *)payload + sizeof(value), sizeof(delay));
            s->pause(c, value, delay);
        }
        break;
    case MM_SCENE_CHANGE_CAMERA:
        require_background(s);
        if (disabled & MM_SCENE_EFFECT_CAMERAS)
            return 1;
        memcpy(&value, payload, sizeof(value));
        if (!s->set_camera(c, s->owner->owned[MM_SCENE_BACKGROUND], value))
            return 0;
        break;
    default:
        s->diagnostic(c, MM_SCENE_EVENT_UNKNOWN);
        break;
    }
    return 1;
}
static int play_row(const MMSceneTrack *track, MMI32 index, const MMSceneTrackEvent *event,
                    MMU32 disabled, const MMSceneEventServices *s)
{
    return mm_scene_play_event(event->frame, event->kind,
                               mm_scene_track_payload(track, index, NULL), disabled, s);
}
int mm_scene_play_frame_event(MMI32 index, MMU32 disabled, const MMSceneEventServices *s)
{
    const MMSceneTrack *track = &s->owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    return mm_scene_track_get(track, index, &event) && play_row(track, index, &event, disabled, s);
}
static void read_frame_event(void *context, void *group, MMI32 index, MMI32 *frame, int *kind)
{
    MMSceneTrackEvent event;
    (void)context;
    *kind = -1;
    if (mm_scene_track_get((const MMSceneTrack *)group, index, &event))
    {
        *frame = event.frame;
        *kind = event.kind;
    }
}
int mm_scene_unplay_event(int kind, const MMSceneEventServices *s)
{
    MMMovieEventView view;
    MMSceneTrack *track = &s->owner->tracks[MM_SCENE_FRAMES];
    MMI32 i, frame, camera;
    int result;
    void *c = s->context;
    if (kind != MM_SCENE_PLAY_SOUND && kind != MM_SCENE_CHANGE_CAMERA)
        return 1;
    if (kind == MM_SCENE_CHANGE_CAMERA)
        require_background(s);
    view.context = NULL;
    view.group = track;
    view.count = s->runtime->next_event;
    view.read = read_frame_event;
    i = mm_movie_view_last_event(&view, kind, s->data->currentFrame, 0, &frame);
    if (i >= 0)
    {
        result = mm_scene_play_event(frame, kind, mm_scene_track_payload(track, i, NULL),
                    s->runtime->disabled | (kind == MM_SCENE_PLAY_SOUND ? MM_SCENE_EFFECT_SOUNDS : 0), s);
        if (kind == MM_SCENE_CHANGE_CAMERA && !result)
            s->diagnostic(c, MM_SCENE_EVENT_RESTORE_FAILED);
        return 1;
    }
    if (kind == MM_SCENE_PLAY_SOUND)
        mm_scene_owner_drop(s->owner, s->scene, MM_SCENE_BACKGROUND_SOUND, &s->owners);
    else
    {
        /* Host-order camera payload; frame is unused by camera dispatch. */
        camera = 0;
        if (!mm_scene_play_event(0, kind, &camera, s->runtime->disabled, s))
        {
            s->diagnostic(c, MM_SCENE_EVENT_FIRST_CAMERA_FAILED);
            return 0;
        }
    }
    return 1;
}
int mm_scene_start_events(int actors_only, const MMSceneEventServices *s)
{
    const MMSceneTrack *track = &s->owner->tracks[MM_SCENE_STARTS];
    MMSceneTrackEvent event;
    MMI32 i;
    for (i = 0; (MMU32)i < track->rows.count; ++i)
    {
        if (!mm_scene_track_get(track, i, &event))
            return 0;
        if (actors_only && event.kind != MM_SCENE_ADD_ACTOR)
            continue;
        if (!play_row(track, i, &event, s->runtime->disabled, s))
            return 0;
    }
    return 1;
}

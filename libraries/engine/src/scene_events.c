/* Scene event dispatch converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_events.h"
#include "mm/movie_session.h"
#include <stddef.h>
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
    void *group = s->owner->owned[MM_SCENE_STARTS];
    MMI32 i, frame;
    int kind;
    for (i = 0; i < s->owners.count(s->owners.context, group); ++i)
    {
        s->event(s->context, group, i, &frame, &kind);
        if (kind == MM_SCENE_SET_BACKGROUND)
        {
            s->read_tag(s->context, s->payload(s->context, group, i), out);
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
    int reason, text;
    switch (kind)
    {
    case MM_SCENE_PLAY_SOUND:
        if (s->sound_style(c, payload) == MM_SCENE_EVENT_MIDI)
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
        s->read_tag(c, payload, &tag);
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
        object = s->read_object(c, payload);
        if (!s->append(c, text, object))
            return 0;
        if (!text)
            s->bind_actor(c, object);
        if (!s->seek(c, text, object, s->data->currentFrame))
        {
            s->remove_last(c, text);
            return 0;
        }
        break;
    case MM_SCENE_PAUSE:
        reason = s->pause_reason(c, payload);
        s->pause_type(c, reason);
        if (!(disabled & MM_SCENE_EFFECT_PAUSES))
            s->pause(c, reason, s->pause_delay(c, payload));
        break;
    case MM_SCENE_CHANGE_CAMERA:
        require_background(s);
        if (disabled & MM_SCENE_EFFECT_CAMERAS)
            return 1;
        if (!s->set_camera(c, s->owner->owned[MM_SCENE_BACKGROUND], s->camera(c, payload)))
            return 0;
        break;
    default:
        s->diagnostic(c, MM_SCENE_EVENT_UNKNOWN);
        break;
    }
    return 1;
}
int mm_scene_unplay_event(int kind, const MMSceneEventServices *s)
{
    MMMovieEventView view;
    MMI32 i, frame, camera;
    int result;
    void *group = s->owner->owned[MM_SCENE_FRAMES], *c = s->context;
    if (kind != MM_SCENE_PLAY_SOUND && kind != MM_SCENE_CHANGE_CAMERA)
        return 1;
    if (kind == MM_SCENE_CHANGE_CAMERA)
        require_background(s);
    view.context = c;
    view.group = group;
    view.count = s->runtime->next_event;
    view.read = s->event;
    i = mm_movie_view_last_event(&view, kind, s->data->currentFrame, 0, &frame);
    if (i >= 0)
    {
        result = mm_scene_play_event(frame, kind, s->payload(c, group, i),
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
    MMI32 i, frame;
    int kind;
    void *group = s->owner->owned[MM_SCENE_STARTS];
    for (i = 0; i < s->owners.count(s->owners.context, group); ++i)
    {
        frame = 0; /* Non-sound start events do not consume a frame value. */
        s->event(s->context, group, i, &frame, &kind);
        if (actors_only && kind != MM_SCENE_ADD_ACTOR)
            continue;
        if (!mm_scene_play_event(frame, kind, s->payload(s->context, group, i), s->runtime->disabled, s))
            return 0;
    }
    return 1;
}

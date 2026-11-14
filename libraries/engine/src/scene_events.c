/* Scene event dispatch converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_events.h"
static void adopt(int slot, void *object, const ThreeMMSceneEventServices *s)
{
    if (s->owner->owned[slot])
        s->diagnostic(s->context, THREE_MM_SCENE_EVENT_OWNER_OCCUPIED);
    three_mm_scene_owner_set(s->owner, s->scene, slot, object, &s->owners);
}
static void require_background(const ThreeMMSceneEventServices *s)
{
    if (!s->owner->owned[THREE_MM_SCENE_BACKGROUND])
        s->diagnostic(s->context, THREE_MM_SCENE_EVENT_NO_BACKGROUND);
}
int three_mm_scene_play_event(ThreeMMI32 frame, int kind, void *payload, ThreeMMU32 disabled,
                              const ThreeMMSceneEventServices *s)
{
    void *object, *copy, *c = s->context;
    ThreeMMTag tag;
    int reason, text;
    switch (kind)
    {
    case THREE_MM_SCENE_PLAY_SOUND:
        if (s->sound_style(c, payload) == THREE_MM_SCENE_EVENT_MIDI)
        {
            copy = s->duplicate_sound(c, payload);
            if (!copy)
                return 0;
            three_mm_scene_owner_drop(s->owner, s->scene, THREE_MM_SCENE_BACKGROUND_SOUND, &s->owners);
            adopt(THREE_MM_SCENE_BACKGROUND_SOUND, copy, s);
            s->runtime->background_frame = frame;
        }
        if (!(disabled & THREE_MM_SCENE_EFFECT_SOUNDS))
            s->play_sound(c, payload);
        break;
    case THREE_MM_SCENE_SET_BACKGROUND:
        s->read_tag(c, payload, &tag);
        object = s->fetch_background(c, &tag);
        if (!object)
            return 0;
        if (!s->set_camera(c, object, 0))
        {
            s->owners.release(s->owners.context, object);
            return 0;
        }
        if (s->transition(c) == THREE_MM_SCENE_EVENT_NIL_TRANSITION)
            s->set_transition(c, THREE_MM_SCENE_EVENT_CUT_TRANSITION);
        adopt(THREE_MM_SCENE_BACKGROUND, object, s);
        s->publish_tag(c, &tag);
        break;
    case THREE_MM_SCENE_ADD_ACTOR:
    case THREE_MM_SCENE_ADD_TEXT:
        text = kind == THREE_MM_SCENE_ADD_TEXT;
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
    case THREE_MM_SCENE_PAUSE:
        reason = s->pause_reason(c, payload);
        s->pause_type(c, reason);
        if (!(disabled & THREE_MM_SCENE_EFFECT_PAUSES))
            s->pause(c, reason, s->pause_delay(c, payload));
        break;
    case THREE_MM_SCENE_CHANGE_CAMERA:
        require_background(s);
        if (disabled & THREE_MM_SCENE_EFFECT_CAMERAS)
            return 1;
        if (!s->set_camera(c, s->owner->owned[THREE_MM_SCENE_BACKGROUND], s->camera(c, payload)))
            return 0;
        break;
    default:
        s->diagnostic(c, THREE_MM_SCENE_EVENT_UNKNOWN);
        break;
    }
    return 1;
}
int three_mm_scene_unplay_event(int kind, const ThreeMMSceneEventServices *s)
{
    ThreeMMI32 i, frame, camera;
    int previous, result;
    void *group = s->owner->owned[THREE_MM_SCENE_FRAMES], *c = s->context;
    if (kind != THREE_MM_SCENE_PLAY_SOUND && kind != THREE_MM_SCENE_CHANGE_CAMERA)
        return 1;
    if (kind == THREE_MM_SCENE_CHANGE_CAMERA)
        require_background(s);
    for (i = s->runtime->next_event - 1; i >= 0; --i)
    {
        s->event(c, group, i, &frame, &previous);
        if (previous == kind && frame < s->data->currentFrame)
        {
            result = three_mm_scene_play_event(
                frame, previous, s->payload(c, group, i),
                s->runtime->disabled | (kind == THREE_MM_SCENE_PLAY_SOUND ? THREE_MM_SCENE_EFFECT_SOUNDS : 0), s);
            if (kind == THREE_MM_SCENE_CHANGE_CAMERA && !result)
                s->diagnostic(c, THREE_MM_SCENE_EVENT_RESTORE_FAILED);
            return 1;
        }
    }
    if (kind == THREE_MM_SCENE_PLAY_SOUND)
        three_mm_scene_owner_drop(s->owner, s->scene, THREE_MM_SCENE_BACKGROUND_SOUND, &s->owners);
    else
    {
        /* Host-order camera payload; frame is unused by camera dispatch. */
        camera = 0;
        if (!three_mm_scene_play_event(0, kind, &camera, s->runtime->disabled, s))
        {
            s->diagnostic(c, THREE_MM_SCENE_EVENT_FIRST_CAMERA_FAILED);
            return 0;
        }
    }
    return 1;
}
int three_mm_scene_start_events(int actors_only, const ThreeMMSceneEventServices *s)
{
    ThreeMMI32 i, frame;
    int kind;
    void *group = s->owner->owned[THREE_MM_SCENE_STARTS];
    for (i = 0; i < s->owners.count(s->owners.context, group); ++i)
    {
        frame = 0; /* Non-sound start events do not consume a frame value. */
        s->event(s->context, group, i, &frame, &kind);
        if (actors_only && kind != THREE_MM_SCENE_ADD_ACTOR)
            continue;
        if (!three_mm_scene_play_event(frame, kind, s->payload(s->context, group, i), s->runtime->disabled, s))
            return 0;
    }
    return 1;
}

/* Scene frame/replay and prerender policy converted from engine scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_runtime.h"
void three_mm_scene_runtime_init(ThreeMMSceneRuntime *r)
{
    r->next_event = 0;
    r->background_frame = 0;
    r->disabled = THREE_MM_SCENE_EFFECT_PAUSES;
}
int three_mm_scene_force(ThreeMMI32 frame, int text, int *sound, const ThreeMMSceneRuntimeServices *s)
{
    ThreeMMI32 i;
    for (i = 0; i < s->object_count(s->context, text); ++i)
        if (!s->seek(s->context, text, s->object(s->context, text, i), frame, sound))
            return 0;
    return 1;
}
void three_mm_scene_move_first(ThreeMMSceneState *d, ThreeMMI32 target, const ThreeMMSceneRuntimeServices *s)
{
    ThreeMMI32 i, frame;
    int kind;
    for (i = 0; i < s->event_count(s->context); ++i)
    {
        s->event(s->context, i, &frame, &kind);
        if (frame != d->firstFrame)
            break;
        if (kind == THREE_MM_SCENE_CHANGE_CAMERA)
            s->move_camera(s->context, i, target);
    }
    d->firstFrame = target;
}
void three_mm_scene_extend_range(ThreeMMSceneState *d, const ThreeMMSceneRuntimeServices *s)
{
    ThreeMMI32 i, first, last;
    int text;
    for (text = 0; text < 2; ++text)
        for (i = 0; i < s->object_count(s->context, text); ++i)
            if (s->lifetime(s->context, text, s->object(s->context, text, i), &first, &last))
            {
                if (first < d->firstFrame)
                    three_mm_scene_move_first(d, first, s);
                if (last > d->lastFrame)
                    d->lastFrame = last;
            }
}
int three_mm_scene_goto(ThreeMMSceneRuntime *r, ThreeMMSceneState *d, ThreeMMI32 target,
                        const ThreeMMSceneRuntimeServices *s)
{
    int sound, update, kind;
    ThreeMMI32 i, frame;
    ThreeMMU32 mask;
    void *c = s->context;
    sound = 0;
    update = target != d->currentFrame;
    if (target < d->currentFrame)
    {
        s->notice(c, THREE_MM_SCENE_RESET_PAUSE, 0);
        if (target < d->firstFrame)
        {
            three_mm_scene_move_first(d, target, s);
            s->notice(c, THREE_MM_SCENE_DIRTY, 0);
        }
        d->currentFrame = target;
        for (; r->next_event > 0; --r->next_event)
        {
            s->event(c, r->next_event - 1, &frame, &kind);
            if (frame <= d->currentFrame)
                break;
            if (!s->unplay(c, r->next_event - 1))
                goto failure;
        }
        for (i = r->next_event - 1; i >= 0; --i)
        {
            s->event(c, i, &frame, &kind);
            if (frame < d->currentFrame)
                break;
            if (!s->play(c, i, r->disabled))
                goto failure;
            if (kind == THREE_MM_SCENE_PLAY_SOUND && frame == d->currentFrame)
                sound = 1;
        }
    }
    else if (target > d->currentFrame)
    {
        if (s->recording(c))
            s->notice(c, THREE_MM_SCENE_RECORD_SOUND, 0);
        d->currentFrame = target;
        s->notice(c, THREE_MM_SCENE_RESET_PAUSE, 0);
        if (d->currentFrame > d->lastFrame)
        {
            d->lastFrame = d->currentFrame;
            s->notice(c, THREE_MM_SCENE_DIRTY, 0);
        }
        for (; r->next_event < s->event_count(c); ++r->next_event)
        {
            s->event(c, r->next_event, &frame, &kind);
            if (frame > d->currentFrame)
                break;
            mask = r->disabled;
            if (frame != d->currentFrame)
                mask |= THREE_MM_SCENE_EFFECT_SOUNDS | THREE_MM_SCENE_EFFECT_PAUSES;
            if (!s->play(c, r->next_event, mask))
                goto failure;
            if (kind == THREE_MM_SCENE_PLAY_SOUND && frame == d->currentFrame)
                sound = 1;
        }
    }
    if (!(r->disabled & THREE_MM_SCENE_EFFECT_ACTORS) && !three_mm_scene_force(target, 0, &sound, s))
        return 0;
    if (!(r->disabled & THREE_MM_SCENE_EFFECT_TEXTS) && !three_mm_scene_force(target, 1, 0, s))
        return 0;
    if (!(r->disabled & THREE_MM_SCENE_EFFECT_ACTORS))
        three_mm_scene_prerender(r, d, 0, s);
    s->notice(c, THREE_MM_SCENE_INVALIDATE, 0);
    if (update)
        s->notice(c, THREE_MM_SCENE_SOUND_INDICATOR, sound);
    return 1;
failure:
    s->notice(c, THREE_MM_SCENE_GOTO_FAILURE, 0);
    return 0;
}
int three_mm_scene_replay(ThreeMMSceneRuntime *r, const ThreeMMSceneState *d, ThreeMMU32 enabled,
                          const ThreeMMSceneRuntimeServices *s)
{
    ThreeMMI32 i, frame;
    int kind;
    void *c = s->context;
    for (i = r->next_event - 1; i >= 0; --i)
    {
        s->event(c, i, &frame, &kind);
        if (frame < d->currentFrame)
            break;
        if (!s->play(c, i, (~enabled) | THREE_MM_SCENE_EFFECT_CAMERAS))
        {
            s->notice(c, THREE_MM_SCENE_GOTO_FAILURE, 0);
            return 0;
        }
    }
    if (enabled & THREE_MM_SCENE_EFFECT_ACTORS)
        for (i = 0; i < s->object_count(c, 0); ++i)
            if (!s->replay_actor(c, s->object(c, 0, i), enabled))
                return 0;
    return 1;
}
void three_mm_scene_prerender(const ThreeMMSceneRuntime *r, const ThreeMMSceneState *d, int start_now,
                              const ThreeMMSceneRuntimeServices *s)
{
    ThreeMMI32 i, frame, until, count;
    int kind;
    void *actor, *c = s->context;
    if (!s->playing(c))
        return;
    if (d->currentFrame != d->firstFrame && !start_now)
    {
        for (i = r->next_event - 1; i >= 0; --i)
        {
            s->event(c, i, &frame, &kind);
            if (frame != d->currentFrame)
                return;
            if (kind == THREE_MM_SCENE_CHANGE_CAMERA)
                break;
        }
        if (i < 0)
            return;
    }
    until = d->lastFrame;
    for (i = r->next_event; i < s->event_count(c); ++i)
    {
        s->event(c, i, &frame, &kind);
        if (kind == THREE_MM_SCENE_CHANGE_CAMERA)
        {
            until = frame;
            break;
        }
    }
    count = 0;
    for (i = 0; i < s->object_count(c, 0); ++i)
    {
        actor = s->object(c, 0, i);
        if (s->must_render(c, actor, until))
            s->show(c, actor, 0);
        else
        {
            ++count;
            if (s->prerendered(c, actor))
            {
                s->show(c, actor, 1);
                s->set_prerendered(c, actor, 0);
            }
        }
    }
    if (count > 0)
        s->snapshot(c, 1);
    for (i = 0; i < s->object_count(c, 0); ++i)
    {
        actor = s->object(c, 0, i);
        if (s->must_render(c, actor, until))
        {
            s->show(c, actor, 1);
            if (s->prerendered(c, actor))
            {
                s->show(c, actor, 1);
                s->set_prerendered(c, actor, 0);
            }
        }
        else
        {
            if (s->prerendered(c, actor))
                s->notice(c, THREE_MM_SCENE_PRERENDER_ASSERT, 0);
            s->show(c, actor, 0);
            s->set_prerendered(c, actor, 1);
        }
    }
}
void three_mm_scene_end_prerender(const ThreeMMSceneRuntimeServices *s)
{
    ThreeMMI32 i;
    void *actor, *c = s->context;
    s->snapshot(c, 0);
    for (i = 0; i < s->object_count(c, 0); ++i)
    {
        actor = s->object(c, 0, i);
        if (s->prerendered(c, actor))
        {
            s->show(c, actor, 1);
            s->set_prerendered(c, actor, 0);
        }
    }
}

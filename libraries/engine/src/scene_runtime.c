/* Scene frame/replay and prerender policy converted from engine scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_runtime.h"
#include "mm/movie_session.h"
void mm_scene_runtime_init(MMSceneRuntime *r)
{
    r->next_event = 0;
    r->background_frame = 0;
    r->disabled = MM_SCENE_EFFECT_PAUSES;
}
int mm_scene_force(MMI32 frame, int text, int *sound, const MMSceneRuntimeServices *s)
{
    MMI32 i;
    for (i = 0; i < s->object_count(s->context, text); ++i)
        if (!s->seek(s->context, text, s->object(s->context, text, i), frame, sound))
            return 0;
    return 1;
}
void mm_scene_move_first(MMSceneState *d, MMI32 target, const MMSceneRuntimeServices *s)
{
    MMI32 i, frame;
    int kind;
    for (i = 0; i < s->event_count(s->context); ++i)
    {
        s->event(s->context, i, &frame, &kind);
        if (frame != d->firstFrame)
            break;
        if (kind == MM_SCENE_CHANGE_CAMERA)
            s->move_camera(s->context, i, target);
    }
    d->firstFrame = target;
}
void mm_scene_extend_range(MMSceneState *d, const MMSceneRuntimeServices *s)
{
    MMI32 i, first, last;
    int text;
    for (text = 0; text < 2; ++text)
        for (i = 0; i < s->object_count(s->context, text); ++i)
            if (s->lifetime(s->context, text, s->object(s->context, text, i), &first, &last))
            {
                if (first < d->firstFrame)
                    mm_scene_move_first(d, first, s);
                if (last > d->lastFrame)
                    d->lastFrame = last;
            }
}
typedef struct SceneSeek
{
    MMSceneRuntime *runtime;
    MMSceneState *data;
    const MMSceneRuntimeServices *services;
    int sound, update, actors_enabled;
} SceneSeek;

static int begin_seek(void *context, MMI32 target)
{
    SceneSeek *seek = (SceneSeek *)context;
    MMSceneRuntime *r = seek->runtime;
    MMSceneState *d = seek->data;
    const MMSceneRuntimeServices *s = seek->services;
    int kind;
    MMI32 i, frame;
    MMU32 mask;
    void *c = s->context;
    seek->sound = 0;
    seek->update = target != d->currentFrame;
    if (target < d->currentFrame)
    {
        s->notice(c, MM_SCENE_RESET_PAUSE, 0);
        if (target < d->firstFrame)
        {
            mm_scene_move_first(d, target, s);
            s->notice(c, MM_SCENE_DIRTY, 0);
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
            if (kind == MM_SCENE_PLAY_SOUND && frame == d->currentFrame)
                seek->sound = 1;
        }
    }
    else if (target > d->currentFrame)
    {
        if (s->recording(c))
            s->notice(c, MM_SCENE_RECORD_SOUND, 0);
        d->currentFrame = target;
        s->notice(c, MM_SCENE_RESET_PAUSE, 0);
        if (d->currentFrame > d->lastFrame)
        {
            d->lastFrame = d->currentFrame;
            s->notice(c, MM_SCENE_DIRTY, 0);
        }
        for (; r->next_event < s->event_count(c); ++r->next_event)
        {
            s->event(c, r->next_event, &frame, &kind);
            if (frame > d->currentFrame)
                break;
            mask = r->disabled;
            if (frame != d->currentFrame)
                mask |= MM_SCENE_EFFECT_SOUNDS | MM_SCENE_EFFECT_PAUSES;
            if (!s->play(c, r->next_event, mask))
                goto failure;
            if (kind == MM_SCENE_PLAY_SOUND && frame == d->currentFrame)
                seek->sound = 1;
        }
    }
    seek->actors_enabled = !(r->disabled & MM_SCENE_EFFECT_ACTORS);
    return MM_MOVIE_OK;
failure:
    s->notice(c, MM_SCENE_GOTO_FAILURE, 0);
    return MM_MOVIE_BAD_FILE;
}

static MMU32 seek_actor_count(void *context)
{
    const SceneSeek *seek = (const SceneSeek *)context;
    const MMSceneRuntimeServices *s = seek->services;
    MMI32 count = seek->actors_enabled ? s->object_count(s->context, 0) : 0;
    return count > 0 ? (MMU32)count : 0;
}

static int seek_actor(void *context, MMU32 ordinal, MMI32 target)
{
    SceneSeek *seek = (SceneSeek *)context;
    const MMSceneRuntimeServices *s = seek->services;
    void *actor = s->object(s->context, 0, (MMI32)ordinal);
    return s->seek(s->context, 0, actor, target, &seek->sound) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

static int finish_seek(void *context, MMI32 target)
{
    const SceneSeek *seek = (const SceneSeek *)context;
    const MMSceneRuntime *r = seek->runtime;
    const MMSceneRuntimeServices *s = seek->services;
    void *c = s->context;
    if (!(r->disabled & MM_SCENE_EFFECT_TEXTS) && !mm_scene_force(target, 1, 0, s))
        return MM_MOVIE_BAD_FILE;
    if (!(r->disabled & MM_SCENE_EFFECT_ACTORS))
        mm_scene_prerender(r, seek->data, 0, s);
    s->notice(c, MM_SCENE_INVALIDATE, 0);
    if (seek->update)
        s->notice(c, MM_SCENE_SOUND_INDICATOR, seek->sound);
    return MM_MOVIE_OK;
}

int mm_scene_goto(MMSceneRuntime *r, MMSceneState *d, MMI32 target,
                        const MMSceneRuntimeServices *s)
{
    SceneSeek seek;
    MMMovieFrameServices services;
    seek.runtime = r;
    seek.data = d;
    seek.services = s;
    services.context = &seek;
    services.begin = begin_seek;
    services.actor_count = seek_actor_count;
    services.seek_actor = seek_actor;
    services.finish = finish_seek;
    return mm_movie_seek_frame(target, &services) == MM_MOVIE_OK;
}
int mm_scene_replay(MMSceneRuntime *r, const MMSceneState *d, MMU32 enabled,
                          const MMSceneRuntimeServices *s)
{
    MMI32 i, frame;
    int kind;
    void *c = s->context;
    for (i = r->next_event - 1; i >= 0; --i)
    {
        s->event(c, i, &frame, &kind);
        if (frame < d->currentFrame)
            break;
        if (!s->play(c, i, (~enabled) | MM_SCENE_EFFECT_CAMERAS))
        {
            s->notice(c, MM_SCENE_GOTO_FAILURE, 0);
            return 0;
        }
    }
    if (enabled & MM_SCENE_EFFECT_ACTORS)
        for (i = 0; i < s->object_count(c, 0); ++i)
            if (!s->replay_actor(c, s->object(c, 0, i), enabled))
                return 0;
    return 1;
}
void mm_scene_prerender(const MMSceneRuntime *r, const MMSceneState *d, int start_now,
                              const MMSceneRuntimeServices *s)
{
    MMI32 i, frame, until, count;
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
            if (kind == MM_SCENE_CHANGE_CAMERA)
                break;
        }
        if (i < 0)
            return;
    }
    until = d->lastFrame;
    for (i = r->next_event; i < s->event_count(c); ++i)
    {
        s->event(c, i, &frame, &kind);
        if (kind == MM_SCENE_CHANGE_CAMERA)
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
                s->notice(c, MM_SCENE_PRERENDER_ASSERT, 0);
            s->show(c, actor, 0);
            s->set_prerendered(c, actor, 1);
        }
    }
}
void mm_scene_end_prerender(const MMSceneRuntimeServices *s)
{
    MMI32 i;
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

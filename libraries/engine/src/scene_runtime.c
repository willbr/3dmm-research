/* Scene frame/replay and prerender policy converted from engine scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_runtime.h"
void mm_scene_runtime_init(MMSceneRuntime *r)
{
    r->next_event = 0;
    r->background_frame = 0;
    r->disabled = MM_SCENE_EFFECT_PAUSES;
    r->publication.frame = 0;
    r->publication.phase = MM_MOVIE_FRAME_UNPUBLISHED;
}
int mm_scene_force(MMI32 frame, int text, int *sound, const MMSceneRuntimeServices *s)
{
    const MMSceneOwner *owner = s->events.owner;
    int list = text ? MM_SCENE_TEXTS : MM_SCENE_ACTORS;
    MMI32 i;
    for (i = 0; i < mm_scene_member_count(owner, list); ++i)
        if (!s->seek(s->context, text, mm_scene_member_at(owner, list, i), frame, sound))
            return 0;
    return 1;
}
void mm_scene_move_opening_cameras(MMI32 first, MMI32 target, const MMMovieEventView *view,
                                  MMSceneMoveCamera move)
{
    MMI32 i, frame;
    int kind;
    for (i = 0; i < view->count; ++i)
    {
        view->read(view->context, view->group, i, &frame, &kind);
        if (frame != first)
            break;
        if (kind == MM_SCENE_CHANGE_CAMERA)
            move(view->context, view->group, i, target);
    }
}
static void move_track_camera(void *context, void *group, MMI32 index, MMI32 frame)
{
    MMSceneTrack *track = (MMSceneTrack *)group;
    MMSceneTrackEvent event;
    (void)context;
    event.frame = frame;
    event.kind = MM_SCENE_CHANGE_CAMERA;
    if (mm_scene_track_set(track, index, &event))
        mm_group_move(&track->rows, (MMU32)index, 0);
}
void mm_scene_move_first(MMSceneState *d, MMI32 target, MMSceneOwner *owner)
{
    MMMovieEventView view = mm_scene_track_event_view(&owner->tracks[MM_SCENE_FRAMES]);
    mm_scene_move_opening_cameras(d->firstFrame, target, &view, move_track_camera);
    d->firstFrame = target;
}
void mm_scene_extend_range(MMSceneState *d, const MMSceneRuntimeServices *s)
{
    MMSceneOwner *owner = s->events.owner;
    MMI32 i, first, last;
    int list;
    for (list = MM_SCENE_ACTORS; list <= MM_SCENE_TEXTS; ++list)
        for (i = 0; i < mm_scene_member_count(owner, list); ++i)
            if (s->lifetime(s->context, list == MM_SCENE_TEXTS, mm_scene_member_at(owner, list, i), &first, &last))
            {
                if (first < d->firstFrame)
                    mm_scene_move_first(d, first, owner);
                if (last > d->lastFrame)
                    d->lastFrame = last;
            }
}
int mm_scene_range_contains(MMI32 authored_first, MMI32 authored_last, long first, long last)
{
    return first > (-2147483647L - 1L) && first <= authored_first &&
           last >= authored_last && last <= 2147483647L;
}

int mm_scene_range_store(MMSceneState *scene, long first, long last, const MMSceneRangeStoreServices *s)
{
    void *context = s->context;
    MMMovieEventView view;
    if (!mm_scene_range_contains(scene->firstFrame, scene->lastFrame, first, last))
        return MM_SCENE_RANGE_INVALID;
    if (first < scene->firstFrame)
    {
        if (!s->open_events(context, &view))
            return 0;
        mm_scene_move_opening_cameras(scene->firstFrame, (MMI32)first, &view, s->move_camera);
        scene->firstFrame = (MMI32)first;
        if (!s->replace_events(context) || !s->drop_thumbnail(context))
            return 0;
    }
    scene->lastFrame = (MMI32)last;
    return s->write_scene(context);
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
    const MMSceneTrack *track = &s->events.owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    MMI32 i;
    MMU32 mask;
    void *c = s->context;
    seek->sound = 0;
    seek->update = target != d->currentFrame;
    if (target < d->currentFrame)
    {
        s->notice(c, MM_SCENE_RESET_PAUSE, 0);
        if (target < d->firstFrame)
        {
            mm_scene_move_first(d, target, s->events.owner);
            s->notice(c, MM_SCENE_DIRTY, 0);
        }
        d->currentFrame = target;
        for (; r->next_event > 0; --r->next_event)
        {
            if (!mm_scene_track_get(track, r->next_event - 1, &event))
                goto failure;
            if (event.frame <= d->currentFrame)
                break;
            if (!mm_scene_unplay_event(event.kind, &s->events))
                goto failure;
        }
        for (i = r->next_event - 1; i >= 0; --i)
        {
            if (!mm_scene_track_get(track, i, &event))
                goto failure;
            if (event.frame < d->currentFrame)
                break;
            if (!mm_scene_play_frame_event(i, r->disabled, &s->events))
                goto failure;
            if (event.kind == MM_SCENE_PLAY_SOUND && event.frame == d->currentFrame)
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
        for (; (MMU32)r->next_event < track->rows.count; ++r->next_event)
        {
            if (!mm_scene_track_get(track, r->next_event, &event))
                goto failure;
            if (event.frame > d->currentFrame)
                break;
            mask = r->disabled;
            if (event.frame != d->currentFrame)
                mask |= MM_SCENE_EFFECT_SOUNDS | MM_SCENE_EFFECT_PAUSES;
            if (!mm_scene_play_frame_event(r->next_event, mask, &s->events))
                goto failure;
            if (event.kind == MM_SCENE_PLAY_SOUND && event.frame == d->currentFrame)
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
    return seek->actors_enabled ? (MMU32)mm_scene_member_count(seek->services->events.owner, MM_SCENE_ACTORS) : 0;
}

static int seek_actor(void *context, MMU32 ordinal, MMI32 target)
{
    SceneSeek *seek = (SceneSeek *)context;
    const MMSceneRuntimeServices *s = seek->services;
    void *actor = mm_scene_member_at(s->events.owner, MM_SCENE_ACTORS, (MMI32)ordinal);
    return s->seek(s->context, 0, actor, target, &seek->sound) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

static int finish_seek(void *context, MMI32 target)
{
    const SceneSeek *seek = (const SceneSeek *)context;
    const MMSceneRuntime *r = seek->runtime;
    const MMSceneRuntimeServices *s = seek->services;
    if (!(r->disabled & MM_SCENE_EFFECT_TEXTS) && !mm_scene_force(target, 1, 0, s))
        return MM_MOVIE_BAD_FILE;
    if (!(r->disabled & MM_SCENE_EFFECT_ACTORS))
        mm_scene_prerender(r, seek->data, 0, s);
    return MM_MOVIE_OK;
}

static void published_seek(void *context)
{
    const SceneSeek *seek = (const SceneSeek *)context;
    const MMSceneRuntimeServices *s = seek->services;
    void *c = s->context;
    s->notice(c, MM_SCENE_INVALIDATE, 0);
    if (seek->update)
        s->notice(c, MM_SCENE_SOUND_INDICATOR, seek->sound);
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
    services.position = &d->currentFrame;
    services.published = published_seek;
    return mm_movie_session_seek_live(s->movie, &r->publication, target, &services) == MM_MOVIE_OK;
}
int mm_scene_replay(MMSceneRuntime *r, const MMSceneState *d, MMU32 enabled,
                          const MMSceneRuntimeServices *s)
{
    const MMSceneOwner *owner = s->events.owner;
    const MMSceneTrack *track = &owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    MMI32 i;
    void *c = s->context;
    for (i = r->next_event - 1; i >= 0; --i)
    {
        if (mm_scene_track_get(track, i, &event) && event.frame < d->currentFrame)
            break;
        if (!mm_scene_play_frame_event(i, (~enabled) | MM_SCENE_EFFECT_CAMERAS, &s->events))
        {
            s->notice(c, MM_SCENE_GOTO_FAILURE, 0);
            return 0;
        }
    }
    if (enabled & MM_SCENE_EFFECT_ACTORS)
        for (i = 0; i < mm_scene_member_count(owner, MM_SCENE_ACTORS); ++i)
            if (!s->replay_actor(c, mm_scene_member_at(owner, MM_SCENE_ACTORS, i), enabled))
                return 0;
    return 1;
}
void mm_scene_prerender(const MMSceneRuntime *r, const MMSceneState *d, int start_now,
                              const MMSceneRuntimeServices *s)
{
    const MMSceneOwner *owner = s->events.owner;
    const MMSceneTrack *track = &owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    MMI32 i, until, count;
    void *actor, *c = s->context;
    if (!s->playing(c))
        return;
    if (d->currentFrame != d->firstFrame && !start_now)
    {
        for (i = r->next_event - 1; i >= 0; --i)
        {
            if (!mm_scene_track_get(track, i, &event) || event.frame != d->currentFrame)
                return;
            if (event.kind == MM_SCENE_CHANGE_CAMERA)
                break;
        }
        if (i < 0)
            return;
    }
    until = d->lastFrame;
    for (i = r->next_event; (MMU32)i < track->rows.count; ++i)
    {
        if (mm_scene_track_get(track, i, &event) && event.kind == MM_SCENE_CHANGE_CAMERA)
        {
            until = event.frame;
            break;
        }
    }
    count = 0;
    for (i = 0; i < mm_scene_member_count(owner, MM_SCENE_ACTORS); ++i)
    {
        actor = mm_scene_member_at(owner, MM_SCENE_ACTORS, i);
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
    for (i = 0; i < mm_scene_member_count(owner, MM_SCENE_ACTORS); ++i)
    {
        actor = mm_scene_member_at(owner, MM_SCENE_ACTORS, i);
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
    const MMSceneOwner *owner = s->events.owner;
    MMI32 i;
    void *actor, *c = s->context;
    s->snapshot(c, 0);
    for (i = 0; i < mm_scene_member_count(owner, MM_SCENE_ACTORS); ++i)
    {
        actor = mm_scene_member_at(owner, MM_SCENE_ACTORS, i);
        if (s->prerendered(c, actor))
        {
            s->show(c, actor, 1);
            s->set_prerendered(c, actor, 0);
        }
    }
}

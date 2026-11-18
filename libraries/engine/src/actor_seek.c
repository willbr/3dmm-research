/* Actor seek control converted from engine actor_seek.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_timeline.h"
enum
{
    INITIALIZE,
    REWIND,
    PREPARE,
    FRAMES,
    DONE
};
void mm_actor_seek_init(MMActorSeek *q, MMI32 first, MMI32 last, MMI32 target)
{
    q->firstFrame = first;
    q->lastFrame = last;
    q->targetFrame = target;
    q->phase = INITIALIZE;
    q->midPath = q->waiting = 0;
    q->success = 1;
    q->pending = MM_ACTOR_SEEK_FINISHED;
}
static int command(MMActorSeek *q, MMActorSeekCommand *out, int kind, int display)
{
    q->pending = kind;
    q->waiting = 1;
    out->kind = kind;
    out->displayFrame = display;
    return 1;
}
static void rewind_state(MMActorSeek *q, MMActorState *state, void *route, void *events,
                         const MMActorMotionServices *s)
{
    MMI32 index, entrance = -1;
    MMActorEvent event;
    if (q->midPath)
        for (index = state->nextEvent - 1; index >= 0; --index)
        {
            s->event(events, index, &event);
            if (event.aet == MM_ACTOR_ADD && event.nfrm <= q->targetFrame)
            {
                entrance = index;
                break;
            }
        }
    mm_actor_state_rewind(state, route, s);
    if (q->midPath)
    {
        state->firstEventInFrame = state->nextEvent = state->addEvent = entrance;
        s->event(events, entrance, &event);
        state->location = event.rtel;
        state->frame = event.nfrm;
        state->location.dnfrm = mm_fixed_sub(state->location.dnfrm, 1);
        mm_actor_route_position(route, &state->location, &state->position, s);
    }
    else
        state->frame = q->targetFrame < q->firstFrame ? q->targetFrame : q->firstFrame;
}
int mm_actor_seek_next(MMActorSeek *q, MMActorState *state, void *route, void *events,
                             MMActorSeekCommand *out, const MMActorTimelineServices *s)
{
    MMActorEvent event;
    MMActorQuickSeekResult result;
    if (q->waiting)
        return 0;
    while (q->phase != DONE)
        switch (q->phase)
        {
        case INITIALIZE:
            if (q->targetFrame == state->frame)
            {
                q->phase = DONE;
                break;
            }
            if (q->targetFrame < state->frame || state->frame == 2147483647L)
            {
                q->midPath =
                    s->motion.event_count(events) > 0 && state->nextEvent > 0 && q->targetFrame > q->firstFrame;
                q->phase = REWIND;
                if (q->targetFrame < q->firstFrame && state->onStage)
                    return command(q, out, MM_ACTOR_SEEK_HIDE, 0);
            }
            else
            {
                state->frame = mm_fixed_add(state->frame, 1);
                state->firstEventInFrame = state->nextEvent;
                q->phase = PREPARE;
            }
            break;
        case REWIND:
            if (q->targetFrame > q->firstFrame && state->frame != 2147483647L)
            {
                s->motion.event(events, state->nextEvent - 1, &event);
                if (event.nfrm < q->targetFrame)
                {
                    result = mm_actor_quick_backup(state, route, events, q->lastFrame, q->targetFrame, s);
                    if (!result.success)
                    {
                        q->success = 0;
                        q->phase = DONE;
                        break;
                    }
                    if (result.valid)
                    {
                        q->phase = DONE;
                        if (result.applyEffects)
                            return command(q, out, MM_ACTOR_SEEK_QUICK_EFFECTS, 0);
                        break;
                    }
                }
            }
            rewind_state(q, state, route, events, &s->motion);
            q->phase = PREPARE;
            return command(q, out, MM_ACTOR_SEEK_RESET_COSTUME, 0);
        case PREPARE:
            if (q->targetFrame < q->firstFrame)
                q->phase = DONE;
            else if (!s->motion.event_count(events))
            {
                state->frame = q->targetFrame;
                q->phase = DONE;
            }
            else
                q->phase = FRAMES;
            break;
        case FRAMES:
            return command(q, out, MM_ACTOR_SEEK_FRAME, state->frame == q->targetFrame);
        case DONE:
            break;
        }
    out->kind = MM_ACTOR_SEEK_FINISHED;
    out->displayFrame = 0;
    return 1;
}
int mm_actor_seek_complete(MMActorSeek *q, MMActorState *state, int success)
{
    if (!q->waiting)
        return 0;
    q->waiting = 0;
    if (q->pending == MM_ACTOR_SEEK_QUICK_EFFECTS)
        q->success = success;
    else if (q->pending == MM_ACTOR_SEEK_FRAME)
    {
        if (!success)
            q->success = 0;
        if (state->frame == q->targetFrame)
            q->phase = DONE;
        else
        {
            state->frame = mm_fixed_add(state->frame, 1);
            state->firstEventInFrame = state->nextEvent;
        }
    }
    return 1;
}

int mm_actor_seek_run(MMActorState *state, void *route, void *events, MMI32 first, MMI32 last, MMI32 target,
                      const MMActorTimelineServices *timeline, const MMActorSeekEffects *effects)
{
    MMActorSeek seek;
    MMActorSeekCommand command;
    int advanced, result;
    mm_actor_seek_init(&seek, first, last, target);
    for (;;)
    {
        advanced = mm_actor_seek_next(&seek, state, route, events, &command, timeline);
        if (effects->fault)
        {
            result = effects->fault(effects->context);
            if (result)
                return result;
        }
        if (!advanced)
            return 0;
        if (command.kind == MM_ACTOR_SEEK_FINISHED)
            return seek.success;
        result = effects->apply(effects->context, &command, state);
        if (result < 0)
            return result;
        mm_actor_seek_complete(&seek, state, result);
    }
}

typedef struct ActorPlayback
{
    void *route, *events;
    const MMActorTimelineServices *timeline;
    const MMActorPlaybackEffects *effects;
    MMI32 first, last;
    int profile, dirty, *sound;
} ActorPlayback;

static MMI32 playback_last(const ActorPlayback *p)
{
    return p->effects->last_frame ? p->effects->last_frame(p->effects->context) : p->last;
}

static int playback_fault(void *context)
{
    const MMActorPlaybackEffects *e = ((const ActorPlayback *)context)->effects;
    return e->fault ? e->fault(e->context) : 0;
}

static int playback_event(void *context, const MMActorFrameCommand *command, MMActorState *state)
{
    const ActorPlayback *p = (const ActorPlayback *)context;
    const MMActorPlaybackEffects *e = p->effects;
    MMActorEvent event;
    int prepared;
    if (command->kind != MM_ACTOR_CHECK_ENTRANCE)
        return e->apply_event(e->context, command->eventIndex);
    if (e->prepare_entrance)
    {
        prepared = e->prepare_entrance(e->context, command->eventIndex);
        if (prepared <= 0)
            return prepared;
    }
    p->timeline->motion.event(p->events, command->eventIndex, &event);
    return event.nfrm <= state->frame;
}

static int playback_frame(ActorPlayback *p, MMActorState *state, int display)
{
    const MMActorPlaybackEffects *e = p->effects;
    MMActorFrameEffects effects;
    MMActorFrameInputs inputs = {0};
    MMActorFrameResult frame;
    int result;
    inputs.firstFrame = p->first;
    inputs.displayFrame = display;
    if (e->prepare)
        e->prepare(e->context, &inputs);
    if (e->suppress_sounds)
        inputs.suppressSounds = e->suppress_sounds(e->context);
    inputs.stepSucceeded = mm_actor_playback_step(state, &inputs.step, p->timeline);
    result = playback_fault(p);
    if (result)
        return result;
    effects.context = p;
    effects.event = playback_event;
    effects.motion_sounds = e->motion_sounds;
    effects.position_body = e->position_body;
    effects.select_cel = e->select_cel;
    effects.update_pose = e->update_pose;
    result = mm_actor_frame_run(state, p->route, p->events, &inputs, p->dirty, &p->timeline->motion, &effects, &frame);
    if (result <= 0)
        return result;
    /* Event commands use the seek state. Display effects use the host directly. */
    effects.context = e->context;
    return mm_actor_frame_display(&frame, state, playback_last(p), p->profile,
                                   p->profile == MM_ACTOR_FRAME_STUDIO ? &p->dirty : 0, p->sound, &effects);
}

static int playback_quick(const ActorPlayback *p, const MMActorState *state)
{
    const MMActorPlaybackEffects *e = p->effects;
    if (p->profile == MM_ACTOR_FRAME_STUDIO && (!e->suppress_sounds || !e->suppress_sounds(e->context)) &&
        state->frame <= playback_last(p) && state->onStage)
        e->motion_sounds(e->context);
    e->position_body(e->context);
    return state->onStage ? e->select_cel(e->context) : 1;
}

static int playback_seek_effect(void *context, const MMActorSeekCommand *command, MMActorState *state)
{
    ActorPlayback *p = (ActorPlayback *)context;
    const MMActorPlaybackEffects *e = p->effects;
    switch (command->kind)
    {
    case MM_ACTOR_SEEK_HIDE:
        e->hide(e->context);
        return 1;
    case MM_ACTOR_SEEK_RESET_COSTUME:
        return e->reset_costume(e->context);
    case MM_ACTOR_SEEK_QUICK_EFFECTS:
        return playback_quick(p, state);
    case MM_ACTOR_SEEK_FRAME:
        return playback_frame(p, state, command->displayFrame);
    default:
        return 0;
    }
}

int mm_actor_playback_seek(MMActorState *state, void *route, void *events, MMI32 first, MMI32 last, MMI32 target,
                          int profile, int *sound, const MMActorTimelineServices *timeline,
                          const MMActorPlaybackEffects *effects)
{
    ActorPlayback playback;
    MMActorSeekEffects seek;
    playback.route = route;
    playback.events = events;
    playback.timeline = timeline;
    playback.effects = effects;
    playback.first = first;
    playback.last = last;
    playback.profile = profile;
    playback.dirty = profile == MM_ACTOR_FRAME_PREVIEW;
    playback.sound = sound;
    seek.context = &playback;
    seek.apply = playback_seek_effect;
    seek.fault = playback_fault;
    return mm_actor_seek_run(state, route, events, first, last, target, timeline, &seek);
}

int mm_actor_replay_frame_sounds(const MMActorState *state, void *events, int enabled,
                                 const MMActorMotionServices *motion, void *context,
                                 int (*apply)(void *, MMI32 index), int (*motion_sounds)(void *))
{
    MMActorEvent event;
    MMI32 i;
    int result;
    if (!enabled || !state->onStage)
        return 1;
    for (i = state->firstEventInFrame; i < state->nextEvent; ++i)
    {
        motion->event(events, i, &event);
        if (event.aet == MM_ACTOR_SOUND)
        {
            result = apply(context, i);
            if (result <= 0)
                return result;
        }
    }
    return motion_sounds(context);
}

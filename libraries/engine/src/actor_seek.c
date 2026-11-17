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

/* Actor seek control converted from engine actor_seek.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_timeline.h"
enum
{
    INITIALIZE,
    REWIND,
    PREPARE,
    FRAMES,
    DONE
};
void three_mm_actor_seek_init(ThreeMMActorSeek *q, ThreeMMI32 first, ThreeMMI32 last, ThreeMMI32 target)
{
    q->firstFrame = first;
    q->lastFrame = last;
    q->targetFrame = target;
    q->phase = INITIALIZE;
    q->midPath = q->waiting = 0;
    q->success = 1;
    q->pending = THREE_MM_ACTOR_SEEK_FINISHED;
}
static int command(ThreeMMActorSeek *q, ThreeMMActorSeekCommand *out, int kind, int display)
{
    q->pending = kind;
    q->waiting = 1;
    out->kind = kind;
    out->displayFrame = display;
    return 1;
}
static void rewind_state(ThreeMMActorSeek *q, ThreeMMActorState *state, void *route, void *events,
                         const ThreeMMActorMotionServices *s)
{
    ThreeMMI32 index, entrance = -1;
    ThreeMMActorEvent event;
    if (q->midPath)
        for (index = state->nextEvent - 1; index >= 0; --index)
        {
            s->event(events, index, &event);
            if (event.aet == THREE_MM_ACTOR_ADD && event.nfrm <= q->targetFrame)
            {
                entrance = index;
                break;
            }
        }
    three_mm_actor_state_rewind(state, route, s);
    if (q->midPath)
    {
        state->firstEventInFrame = state->nextEvent = state->addEvent = entrance;
        s->event(events, entrance, &event);
        state->location = event.rtel;
        state->frame = event.nfrm;
        state->location.dnfrm = three_mm_fixed_sub(state->location.dnfrm, 1);
        three_mm_actor_route_position(route, &state->location, &state->position, s);
    }
    else
        state->frame = q->targetFrame < q->firstFrame ? q->targetFrame : q->firstFrame;
}
int three_mm_actor_seek_next(ThreeMMActorSeek *q, ThreeMMActorState *state, void *route, void *events,
                             ThreeMMActorSeekCommand *out, const ThreeMMActorTimelineServices *s)
{
    ThreeMMActorEvent event;
    ThreeMMActorQuickSeekResult result;
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
                    return command(q, out, THREE_MM_ACTOR_SEEK_HIDE, 0);
            }
            else
            {
                state->frame = three_mm_fixed_add(state->frame, 1);
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
                    result = three_mm_actor_quick_backup(state, route, events, q->lastFrame, q->targetFrame, s);
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
                            return command(q, out, THREE_MM_ACTOR_SEEK_QUICK_EFFECTS, 0);
                        break;
                    }
                }
            }
            rewind_state(q, state, route, events, &s->motion);
            q->phase = PREPARE;
            return command(q, out, THREE_MM_ACTOR_SEEK_RESET_COSTUME, 0);
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
            return command(q, out, THREE_MM_ACTOR_SEEK_FRAME, state->frame == q->targetFrame);
        case DONE:
            break;
        }
    out->kind = THREE_MM_ACTOR_SEEK_FINISHED;
    out->displayFrame = 0;
    return 1;
}
int three_mm_actor_seek_complete(ThreeMMActorSeek *q, ThreeMMActorState *state, int success)
{
    if (!q->waiting)
        return 0;
    q->waiting = 0;
    if (q->pending == THREE_MM_ACTOR_SEEK_QUICK_EFFECTS)
        q->success = success;
    else if (q->pending == THREE_MM_ACTOR_SEEK_FRAME)
    {
        if (!success)
            q->success = 0;
        if (state->frame == q->targetFrame)
            q->phase = DONE;
        else
        {
            state->frame = three_mm_fixed_add(state->frame, 1);
            state->firstEventInFrame = state->nextEvent;
        }
    }
    return 1;
}

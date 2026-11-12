/* Actor frame sequencing converted from engine actor_frame.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_frame.h"
#include <string.h>
enum
{
    ENTRANCES,
    EVENTS,
    DONE
};
enum
{
    NONE,
    ENTRANCE_QUERY,
    ENTRANCE_EVENT,
    ORDERED_QUERY,
    ORDERED_EVENT,
    EVENT
};
static int after(const ThreeMMActorLocation *a, const ThreeMMActorLocation *b)
{
    return a->irpt > b->irpt || (a->irpt == b->irpt && (a->dwrOffset > b->dwrOffset ||
                                                        (a->dwrOffset == b->dwrOffset && a->dnfrm > b->dnfrm)));
}
int three_mm_actor_subroute_events_done(void *events, ThreeMMI32 i, const ThreeMMActorLocation *p,
                                        const ThreeMMActorMotionServices *s)
{
    ThreeMMActorEvent event;
    for (; i < s->event_count(events); ++i)
    {
        s->event(events, i, &event);
        if (event.aet == THREE_MM_ACTOR_ADD)
            return 1;
        if (after(&event.rtel, p))
            return 0;
    }
    return 1;
}
static void finish(ThreeMMActorFrame *q, ThreeMMActorState *state, void *events, const ThreeMMActorMotionServices *s)
{
    if (!q->freezeCel &&
        !(q->endRoute && three_mm_actor_subroute_events_done(events, state->nextEvent, &state->location, s)))
    {
        state->cel = three_mm_fixed_add(state->cel, 1);
        if (state->celCount > 1)
            q->result.positionDirty = 1;
    }
    if (q->oldPosition.dxr != state->position.dxr || q->oldPosition.dyr != state->position.dyr ||
        q->oldPosition.dzr != state->position.dzr)
        q->result.positionDirty = 1;
    q->result.considerMotionSounds = q->inputs.displayFrame && !q->inputs.suppressSounds && state->onStage;
    q->result.positionBody = q->inputs.displayFrame && q->result.positionDirty;
    q->result.updatePose = !q->inputs.displayFrame && q->result.positionDirty;
    q->result.selectCel = q->inputs.displayFrame && state->onStage;
    q->phase = DONE;
}
void three_mm_actor_frame_init(ThreeMMActorFrame *q, ThreeMMActorState *state, void *route, void *events,
                               const ThreeMMActorFrameInputs *inputs, int dirty, const ThreeMMActorMotionServices *s)
{
    memset(q, 0, sizeof(*q));
    q->inputs = *inputs;
    q->phase = inputs->recording ? EVENTS : ENTRANCES;
    q->entranceScan = state->nextEvent;
    q->entrance = -1;
    q->freezeCel = state->frozen;
    q->oldPosition = state->position;
    q->result.success = inputs->stepSucceeded;
    q->result.positionDirty = dirty;
    three_mm_actor_route_advance(route, events, inputs->step, &state->location, state->nextEvent, state->frame,
                                 inputs->firstFrame, !inputs->recording && !inputs->rejoining, &q->endRoute, s);
    three_mm_actor_route_position(route, &state->location, &state->position, s);
    state->useCurrentRotation =
        !inputs->step || (q->oldPosition.dxr == state->position.dxr && q->oldPosition.dzr == state->position.dzr);
    if (inputs->recording)
        finish(q, state, events, s);
}
int three_mm_actor_frame_next(ThreeMMActorFrame *q, ThreeMMActorState *state, void *events,
                              ThreeMMActorFrameCommand *command, const ThreeMMActorMotionServices *s)
{
    ThreeMMActorEvent event;
    ThreeMMActorSound sound;
    if (q->pending == ENTRANCE_QUERY || q->pending == ORDERED_QUERY)
        return 0;
    if (q->pending == ENTRANCE_EVENT || q->pending == ORDERED_EVENT || q->pending == EVENT)
        goto apply;
    while (q->phase != DONE)
    {
        if (q->phase == ENTRANCES)
        {
            for (; q->entranceScan < s->event_count(events); ++q->entranceScan)
            {
                s->event(events, q->entranceScan, &event);
                if (event.aet == THREE_MM_ACTOR_ADD)
                {
                    q->entrance = q->entranceScan++;
                    q->pending = ENTRANCE_QUERY;
                    command->kind = THREE_MM_ACTOR_CHECK_ENTRANCE;
                    command->eventIndex = q->entrance;
                    return 1;
                }
            }
            q->phase = EVENTS;
            q->scan = state->nextEvent;
        }
        if (q->scan == s->event_count(events))
        {
            finish(q, state, events, s);
            break;
        }
        s->event(events, q->scan, &event);
        if (event.aet == THREE_MM_ACTOR_ADD)
        {
            q->pending = ORDERED_QUERY;
            command->kind = THREE_MM_ACTOR_CHECK_ENTRANCE;
            command->eventIndex = q->scan;
            return 1;
        }
        if (after(&event.rtel, &state->location))
        {
            finish(q, state, events, s);
            break;
        }
        if (event.aet == THREE_MM_ACTOR_ROTATE_FORWARD || event.aet == THREE_MM_ACTOR_SIZE ||
            event.aet == THREE_MM_ACTOR_STRETCH || event.aet == THREE_MM_ACTOR_ROTATE_HERE ||
            event.aet == THREE_MM_ACTOR_MOVE)
            q->result.positionDirty = 1;
        else if (event.aet == THREE_MM_ACTOR_ACTION)
        {
            q->freezeCel = 1;
            q->result.positionDirty = 1;
        }
        if (event.aet == THREE_MM_ACTOR_SOUND)
        {
            if (q->inputs.displayFrame)
                q->result.soundInFrame = 1;
            if (!q->inputs.displayFrame || q->inputs.suppressSounds)
            {
                s->payload(events, q->scan, &sound);
                if (sound.cel == -1)
                {
                    state->nextEvent = three_mm_fixed_add(state->nextEvent, 1);
                    ++q->scan;
                    continue;
                }
            }
        }
        q->pending = EVENT;
    apply:
        command->kind = THREE_MM_ACTOR_APPLY_EVENT;
        command->eventIndex = state->nextEvent;
        return 1;
    }
    command->kind = THREE_MM_ACTOR_FRAME_FINISHED;
    command->eventIndex = -1;
    return 1;
}
int three_mm_actor_frame_complete(ThreeMMActorFrame *q, ThreeMMActorState *state, void *events, int succeeded,
                                  const ThreeMMActorMotionServices *s)
{
    int pending = q->pending;
    if (pending == NONE)
        return 0;
    q->pending = NONE;
    if (pending == ENTRANCE_QUERY)
    {
        if (succeeded)
        {
            state->nextEvent = q->entrance;
            q->pending = ENTRANCE_EVENT;
        }
        else
        {
            q->phase = EVENTS;
            q->scan = state->nextEvent;
        }
        return 1;
    }
    if (pending == ORDERED_QUERY)
    {
        if (succeeded)
            q->pending = ORDERED_EVENT;
        else
            finish(q, state, events, s);
        return 1;
    }
    if (succeeded)
        state->nextEvent = three_mm_fixed_add(state->nextEvent, 1);
    else if (pending == ENTRANCE_EVENT || pending == ORDERED_EVENT)
    {
        q->result.success = 0;
        q->result.aborted = 1;
        q->phase = DONE;
        q->pending = NONE;
        return 1;
    }
    else
        q->result.success = 0;
    if (pending == ENTRANCE_EVENT || pending == ORDERED_EVENT)
        q->result.positionDirty = 1;
    if (pending != ENTRANCE_EVENT)
        ++q->scan;
    return 1;
}

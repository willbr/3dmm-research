/* Actor frame sequencing converted from engine actor_frame.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_frame.h"
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
static int after(const MMActorLocation *a, const MMActorLocation *b)
{
    return a->irpt > b->irpt || (a->irpt == b->irpt && (a->dwrOffset > b->dwrOffset ||
                                                        (a->dwrOffset == b->dwrOffset && a->dnfrm > b->dnfrm)));
}
int mm_actor_subroute_events_done(void *events, MMI32 i, const MMActorLocation *p,
                                        const MMActorMotionServices *s)
{
    MMActorEvent event;
    for (; i < s->event_count(events); ++i)
    {
        s->event(events, i, &event);
        if (event.aet == MM_ACTOR_ADD)
            return 1;
        if (after(&event.rtel, p))
            return 0;
    }
    return 1;
}
static void finish(MMActorFrame *q, MMActorState *state, void *events, const MMActorMotionServices *s)
{
    if (!q->freezeCel &&
        !(q->endRoute && mm_actor_subroute_events_done(events, state->nextEvent, &state->location, s)))
    {
        state->cel = mm_fixed_add(state->cel, 1);
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
void mm_actor_frame_init(MMActorFrame *q, MMActorState *state, void *route, void *events,
                               const MMActorFrameInputs *inputs, int dirty, const MMActorMotionServices *s)
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
    mm_actor_route_advance(route, events, inputs->step, &state->location, state->nextEvent, state->frame,
                                 inputs->firstFrame, !inputs->recording && !inputs->rejoining, &q->endRoute, s);
    mm_actor_route_position(route, &state->location, &state->position, s);
    state->useCurrentRotation =
        !inputs->step || (q->oldPosition.dxr == state->position.dxr && q->oldPosition.dzr == state->position.dzr);
    if (inputs->recording)
        finish(q, state, events, s);
}
int mm_actor_frame_next(MMActorFrame *q, MMActorState *state, void *events,
                              MMActorFrameCommand *command, const MMActorMotionServices *s)
{
    MMActorEvent event;
    MMActorSound sound;
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
                if (event.aet == MM_ACTOR_ADD)
                {
                    q->entrance = q->entranceScan++;
                    q->pending = ENTRANCE_QUERY;
                    command->kind = MM_ACTOR_CHECK_ENTRANCE;
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
        if (event.aet == MM_ACTOR_ADD)
        {
            q->pending = ORDERED_QUERY;
            command->kind = MM_ACTOR_CHECK_ENTRANCE;
            command->eventIndex = q->scan;
            return 1;
        }
        if (after(&event.rtel, &state->location))
        {
            finish(q, state, events, s);
            break;
        }
        if (event.aet == MM_ACTOR_ROTATE_FORWARD || event.aet == MM_ACTOR_SIZE ||
            event.aet == MM_ACTOR_STRETCH || event.aet == MM_ACTOR_ROTATE_HERE ||
            event.aet == MM_ACTOR_MOVE)
            q->result.positionDirty = 1;
        else if (event.aet == MM_ACTOR_ACTION)
        {
            q->freezeCel = 1;
            q->result.positionDirty = 1;
        }
        if (event.aet == MM_ACTOR_SOUND)
        {
            if (q->inputs.displayFrame)
                q->result.soundInFrame = 1;
            if (!q->inputs.displayFrame || q->inputs.suppressSounds)
            {
                s->payload(events, q->scan, &sound);
                if (sound.cel == -1)
                {
                    state->nextEvent = mm_fixed_add(state->nextEvent, 1);
                    ++q->scan;
                    continue;
                }
            }
        }
        q->pending = EVENT;
    apply:
        command->kind = MM_ACTOR_APPLY_EVENT;
        command->eventIndex = state->nextEvent;
        return 1;
    }
    command->kind = MM_ACTOR_FRAME_FINISHED;
    command->eventIndex = -1;
    return 1;
}
int mm_actor_frame_complete(MMActorFrame *q, MMActorState *state, void *events, int succeeded,
                                  const MMActorMotionServices *s)
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
        state->nextEvent = mm_fixed_add(state->nextEvent, 1);
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

int mm_actor_frame_run(MMActorState *state, void *route, void *events, const MMActorFrameInputs *inputs,
                       int dirty, const MMActorMotionServices *motion, const MMActorFrameEffects *effects,
                       MMActorFrameResult *out)
{
    MMActorFrame frame;
    MMActorFrameCommand command;
    int status;
    mm_actor_frame_init(&frame, state, route, events, inputs, dirty, motion);
    while (mm_actor_frame_next(&frame, state, events, &command, motion))
    {
        if (command.kind == MM_ACTOR_FRAME_FINISHED)
        {
            *out = frame.result;
            return 1;
        }
        status = effects->event(effects->context, &command, state);
        if (status < 0)
            return status;
        mm_actor_frame_complete(&frame, state, events, status, motion);
    }
    return 0;
}

int mm_actor_frame_display(const MMActorFrameResult *result, const MMActorState *state, MMI32 last_frame,
                           int profile, int *dirty, int *sound, const MMActorFrameEffects *effects)
{
    int status = result->success;
    int preview = profile == MM_ACTOR_FRAME_PREVIEW;
    int effect;
    if (dirty)
        *dirty = result->positionDirty;
    if (sound && result->soundInFrame)
        *sound = 1;
    if (result->aborted || (preview && !status))
        return 0;
    if (result->considerMotionSounds && state->frame <= last_frame && (!preview || state->celCount > 0))
    {
        effect = effects->motion_sounds(effects->context);
        if (preview && effect <= 0)
            return effect;
    }
    if (result->positionBody)
    {
        effects->position_body(effects->context);
        if (dirty)
            *dirty = 0;
    }
    if (result->selectCel && (!preview || result->positionBody))
    {
        effect = effects->select_cel(effects->context);
        if (preview && effect < 0)
            return effect;
        if (!effect)
            status = 0;
    }
    if (result->updatePose)
        effects->update_pose(effects->context);
    return status;
}

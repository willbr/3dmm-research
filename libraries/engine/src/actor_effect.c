/* Actor event effects converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_effect.h"
typedef char EntranceWidth[(sizeof(ThreeMMActorEntrance) == 20) ? 1 : -1];
typedef struct Action
{
    ThreeMMI32 action, cel;
} Action;
static void publish(const ThreeMMActorEffectServices *s, const ThreeMMActorState *state)
{
    if (s->publish)
        s->publish(s->context, state);
}
static ThreeMMActorEffectReply run(const ThreeMMActorEffectServices *s, const ThreeMMActorState *state,
                                  int operation, ThreeMMI32 first, ThreeMMI32 second,
                                  const ThreeMMTag *tag, void *resource)
{
    ThreeMMActorEffectCommand command;
    ThreeMMActorEffectReply reply;
    command.operation = operation;
    command.first = first;
    command.second = second;
    command.tag = tag;
    command.resource = resource;
    reply.success = 1;
    reply.value = state->celCount;
    reply.flags = 0;
    reply.resource = 0;
    publish(s, state);
    s->run(s->context, &command, &reply);
    return reply;
}
void three_mm_actor_add_orientation(ThreeMMActorState *state, const ThreeMMActorEntrance *add, int no_reset)
{
    if (!no_reset)
        three_mm_matrix_identity(&state->transform.bmat34Cur);
    three_mm_matrix_rotate(&state->transform.bmat34Cur, THREE_MM_MATRIX_X, add->x, 0);
    three_mm_matrix_rotate(&state->transform.bmat34Cur, THREE_MM_MATRIX_Y, add->y, 0);
    three_mm_matrix_rotate(&state->transform.bmat34Cur, THREE_MM_MATRIX_Z, add->z, 0);
    state->transform.xaPath = add->x;
    state->transform.yaPath = add->y;
    state->transform.zaPath = add->z;
}
int three_mm_actor_effect_apply(ThreeMMActorState *state, void *route, void *events, ThreeMMI32 index,
                                const ThreeMMActorPoint *origin, int body_present, int metadata_only,
                                const ThreeMMActorMotionServices *m, const ThreeMMActorEffectServices *s)
{
    ThreeMMActorEvent event;
    ThreeMMActorEntrance add;
    ThreeMMActorCostume costume;
    ThreeMMActorRoutePoint point;
    ThreeMMActorEffectReply reply;
    Action action;
    int result, custom;
    void *resource;
    if (!events || m->event_width(events) != 20 || index < 0 || index >= m->event_count(events))
        return 0;
    publish(s, state);
    m->event(events, index, &event);
    if (event.nfrm != state->frame)
    {
        event.nfrm = state->frame;
        s->write_event(events, index, &event);
    }
    result = three_mm_actor_state_apply(state, route, events, index, origin, m);
    if (result != THREE_MM_ACTOR_NEEDS_RUNTIME)
        return result == THREE_MM_ACTOR_APPLIED;
    switch (event.aet)
    {
    case THREE_MM_ACTOR_ACTION:
        if (m->event_size(events, index) != (ThreeMMI32)sizeof(action))
            return 0;
        m->payload(events, index, &action);
        if (!metadata_only && action.action != state->action)
            run(s, state, THREE_MM_EFFECT_CLEAR_SOUNDS, 0, 0, 0, 0);
        if (!metadata_only && !run(s, state, THREE_MM_EFFECT_SET_ACTION_CEL, action.action, action.cel, 0, 0).success)
            return 0;
        state->cel = action.cel;
        state->action = action.action;
        reply = run(s, state, THREE_MM_EFFECT_CEL_COUNT, state->action, 0, 0, 0);
        state->celCount = reply.value;
        if (!reply.success)
            return 0;
        state->actionEvent = index;
        state->location = event.rtel;
        publish(s, state);
        three_mm_actor_route_position(route, &state->location, &state->position, m);
        reply = run(s, state, THREE_MM_EFFECT_ACTION_FLAGS, state->action, 0, 0, 0);
        if (!reply.success)
            return 0;
        state->actionFlags = reply.flags;
        break;
    case THREE_MM_ACTOR_ADD:
        if (m->event_size(events, index) != (ThreeMMI32)sizeof(add))
            return 0;
        if (!metadata_only && !run(s, state, THREE_MM_EFFECT_SAVE_COSTUME, 0, 0, 0, 0).success)
            return 0;
        if (!metadata_only && !run(s, state, THREE_MM_EFFECT_DEFAULT_COSTUME, 0, 0, 0, 0).success)
            return 0;
        if (!metadata_only && !run(s, state, THREE_MM_EFFECT_SET_ACTION_CEL, state->action, state->cel, 0, 0).success)
        {
            run(s, state, THREE_MM_EFFECT_RESTORE_COSTUME, 0, 0, 0, 0);
            return 0;
        }
        if (!metadata_only)
            run(s, state, THREE_MM_EFFECT_CLEAR_SOUNDS, 0, 0, 0, 0);
        m->payload(events, index, &add);
        state->subrouteTranslation = add.translation;
        three_mm_actor_state_origin(state, origin);
        three_mm_actor_state_init_transform(state);
        three_mm_actor_add_orientation(state, &add, 0);
        state->firstEventInFrame = state->addEvent = index;
        state->location = event.rtel;
        publish(s, state);
        three_mm_actor_route_position(route, &state->location, &state->position, m);
        publish(s, state);
        m->route_point(route, event.rtel.irpt, &point);
        if (!metadata_only && !state->onStage)
            run(s, state, THREE_MM_EFFECT_SHOW, 0, 0, 0, 0);
        state->onStage = 1;
        break;
    case THREE_MM_ACTOR_REMOVE:
        if (!metadata_only && state->onStage && body_present)
            run(s, state, THREE_MM_EFFECT_HIDE, 0, 0, 0, 0);
        state->onStage = 0;
        break;
    case THREE_MM_ACTOR_COSTUME:
        if (metadata_only)
            return 1;
        if (m->event_size(events, index) != (ThreeMMI32)sizeof(costume))
            return 0;
        m->payload(events, index, &costume);
        custom = costume.custom != 0;
        reply = run(s, state, custom ? THREE_MM_EFFECT_FETCH_CUSTOM : THREE_MM_EFFECT_FETCH_MATERIAL,
                    costume.costume, 0, &costume.tag, 0);
        resource = reply.resource;
        if (!resource)
            return 0;
        run(s, state, custom ? THREE_MM_EFFECT_APPLY_CUSTOM : THREE_MM_EFFECT_APPLY_MATERIAL,
            costume.body_set, 0, 0, resource);
        run(s, state, custom ? THREE_MM_EFFECT_RELEASE_CUSTOM : THREE_MM_EFFECT_RELEASE_MATERIAL,
            0, 0, 0, resource);
        break;
    case THREE_MM_ACTOR_SOUND:
        if (metadata_only)
            return 1;
        return run(s, state, THREE_MM_EFFECT_ENQUEUE_SOUND, index, 0, 0, 0).success;
    }
    return 1;
}

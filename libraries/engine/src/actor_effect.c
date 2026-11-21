/* Actor event effects converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_effect.h"
#include <string.h>
typedef char EntranceWidth[(sizeof(MMActorEntrance) == 20) ? 1 : -1];
void mm_actor_stationary_track(MMActorStationaryTrack *track, MMI32 frame, const unsigned short angles[3])
{
    memset(track, 0, sizeof(*track));
    track->entrance.x = angles[0];
    track->entrance.y = angles[1];
    track->entrance.z = angles[2];
    track->events[0].aet = MM_ACTOR_ADD;
    track->events[1].aet = MM_ACTOR_ACTION;
    track->events[2].aet = MM_ACTOR_STEP;
    track->events[0].nfrm = track->events[1].nfrm = track->events[2].nfrm = frame;
}
typedef struct Action
{
    MMI32 action, cel;
} Action;
static void publish(const MMActorEffectServices *s, const MMActorState *state)
{
    if (s->publish)
        s->publish(s->context, state);
}
static int action_valid(const MMActorEffectServices *s, MMI32 action)
{
    return !s->action_count || (action >= 0 && action < s->action_count(s->context));
}
static int select_cel(const MMActorEffectServices *s, const MMActorState *state, MMI32 action, MMI32 cel)
{
    publish(s, state);
    return action_valid(s, action) && s->select_cel(s->context, action, cel);
}
static void clear_sounds(const MMActorEffectServices *s, const MMActorState *state)
{
    if (s->clear_sounds)
    {
        publish(s, state);
        s->clear_sounds(s->context);
    }
}
static void visibility(const MMActorEffectServices *s, const MMActorState *state, int shown)
{
    if (s->visibility)
    {
        publish(s, state);
        s->visibility(s->context, shown);
    }
}
static int entrance_body(const MMActorEffectServices *s, const MMActorState *state)
{
    if (s->save_costume)
    {
        publish(s, state);
        if (!s->save_costume(s->context))
            return 0;
    }
    publish(s, state);
    if (!s->default_costume(s->context))
        return 0;
    if (!select_cel(s, state, state->action, state->cel))
    {
        if (s->restore_costume)
        {
            publish(s, state);
            s->restore_costume(s->context);
        }
        return 0;
    }
    clear_sounds(s, state);
    return 1;
}
void mm_actor_add_orientation(MMActorState *state, const MMActorEntrance *add, int no_reset)
{
    if (!no_reset)
        mm_matrix_identity(&state->transform.bmat34Cur);
    mm_matrix_rotate(&state->transform.bmat34Cur, MM_MATRIX_X, add->x, 0);
    mm_matrix_rotate(&state->transform.bmat34Cur, MM_MATRIX_Y, add->y, 0);
    mm_matrix_rotate(&state->transform.bmat34Cur, MM_MATRIX_Z, add->z, 0);
    state->transform.xaPath = add->x;
    state->transform.yaPath = add->y;
    state->transform.zaPath = add->z;
}
int mm_actor_effect_apply(MMActorState *state, void *route, void *events, MMI32 index,
                                const MMActorPoint *origin, int body_present, int metadata_only,
                                const MMActorMotionServices *m, const MMActorEffectServices *s)
{
    MMActorEvent event;
    MMActorEntrance add;
    MMActorCostume costume;
    MMActorRoutePoint point;
    Action action;
    int result, custom;
    MMI32 count;
    MMU32 flags;
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
    result = mm_actor_state_apply(state, route, events, index, origin, m);
    if (result != MM_ACTOR_NEEDS_RUNTIME)
        return result == MM_ACTOR_APPLIED;
    switch (event.aet)
    {
    case MM_ACTOR_ACTION:
        if (m->event_size(events, index) != (MMI32)sizeof(action))
            return 0;
        m->payload(events, index, &action);
        if (!metadata_only && action.action != state->action)
            clear_sounds(s, state);
        if (!metadata_only && !select_cel(s, state, action.action, action.cel))
            return 0;
        state->cel = action.cel;
        state->action = action.action;
        count = state->celCount;
        publish(s, state);
        result = action_valid(s, state->action) && s->cel_count(s->context, state->action, &count);
        state->celCount = count;
        if (!result)
            return 0;
        state->actionEvent = index;
        state->location = event.rtel;
        publish(s, state);
        mm_actor_route_position(route, &state->location, &state->position, m);
        flags = 0;
        publish(s, state);
        if (!action_valid(s, state->action) || !s->action_flags(s->context, state->action, &flags))
            return 0;
        state->actionFlags = flags;
        break;
    case MM_ACTOR_ADD:
        if (m->event_size(events, index) != (MMI32)sizeof(add))
            return 0;
        if (!metadata_only && !entrance_body(s, state))
            return 0;
        m->payload(events, index, &add);
        state->subrouteTranslation = add.translation;
        mm_actor_state_origin(state, origin);
        mm_actor_state_init_transform(state);
        mm_actor_add_orientation(state, &add, 0);
        state->firstEventInFrame = state->addEvent = index;
        state->location = event.rtel;
        publish(s, state);
        mm_actor_route_position(route, &state->location, &state->position, m);
        publish(s, state);
        m->route_point(route, event.rtel.irpt, &point);
        if (!metadata_only && !state->onStage)
            visibility(s, state, 1);
        state->onStage = 1;
        break;
    case MM_ACTOR_REMOVE:
        if (!metadata_only && state->onStage && body_present)
            visibility(s, state, 0);
        state->onStage = 0;
        break;
    case MM_ACTOR_COSTUME:
        if (metadata_only)
            return 1;
        if (m->event_size(events, index) != (MMI32)sizeof(costume))
            return 0;
        m->payload(events, index, &costume);
        custom = costume.custom != 0;
        publish(s, state);
        resource = custom ? s->fetch_custom(s->context, costume.costume) : s->fetch_material(s->context, &costume.tag);
        if (!resource)
            return 0;
        publish(s, state);
        if (custom)
            s->apply_custom(s->context, resource);
        else
            s->apply_material(s->context, costume.body_set, resource);
        if (s->release_resource)
        {
            publish(s, state);
            s->release_resource(s->context, resource, custom);
        }
        break;
    case MM_ACTOR_SOUND:
        if (metadata_only || !s->enqueue_sound)
            return 1;
        publish(s, state);
        return s->enqueue_sound(s->context, index);
    }
    return 1;
}

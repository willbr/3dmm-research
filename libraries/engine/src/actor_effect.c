/* Actor event effects converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_effect.h"
#include "mm/actor_sound.h"
#include "mm/actor_track.h"
#include "mm/body_runtime.h"
#include "mm/material_resource.h"
#include "mm/text_resource.h"
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

typedef struct RuntimeEffect
{
    MMTemplateOwner *tmpl;
    MMBodyOwner *body;
    MMActorSounds *sounds;
    const MMActorEffectRenderer *renderer;
    const MMActorEffectHooks *hooks;
    MMBodyCostume saved;
} RuntimeEffect;

static void runtime_write(void *events, MMI32 i, const MMActorEvent *event)
{
    mm_actor_events_set_fixed(events, i, event);
}
static MMI32 runtime_actions(void *p)
{
    MMTemplateOwner *tmpl = ((RuntimeEffect *)p)->tmpl;
    return tmpl ? mm_template_owner_state(tmpl)->action_count : 0;
}
static int runtime_pose(void *p, MMI32 action, MMI32 cel)
{
    RuntimeEffect *r = p;
    return r->renderer->pose(r->tmpl, r->body, action, cel, NULL);
}
static int runtime_cels(void *p, MMI32 action, MMI32 *count)
{
    RuntimeEffect *r = p;
    return r->renderer->cel_count(r->tmpl, action, count);
}
static int runtime_flags(void *p, MMI32 action, MMU32 *flags)
{
    RuntimeEffect *r = p;
    return r->renderer->action_flags(r->tmpl, action, flags);
}
static int runtime_save(void *p)
{
    RuntimeEffect *r = p;
    return mm_body_costume_capture(&r->saved, r->body, r->renderer->body);
}
static int runtime_defaults(void *p)
{
    RuntimeEffect *r = p;
    return r->renderer->default_costume(r->tmpl, r->body);
}
static void runtime_restore(void *p)
{
    RuntimeEffect *r = p;
    mm_body_costume_apply(&r->saved, r->body, 0, r->renderer->body);
}
static void runtime_visibility(void *p, int shown)
{
    const MMActorEffectHooks *h = ((RuntimeEffect *)p)->hooks;
    h->visibility(h->context, shown);
}
static void *runtime_fetch_custom(void *p, MMI32 id)
{
    RuntimeEffect *r = p;
    return r->renderer->fetch_custom(r->tmpl, id);
}
static void *runtime_fetch_material(void *p, const MMTag *tag)
{
    const MMActorEffectHooks *h = ((RuntimeEffect *)p)->hooks;
    return h && h->fetch_material ? h->fetch_material(h->context, tag) : NULL;
}
static void runtime_custom(void *p, void *costume)
{
    RuntimeEffect *r = p;
    mm_body_set_custom(r->body, costume, r->renderer->body);
}
static void runtime_material(void *p, MMI32 set, void *material)
{
    RuntimeEffect *r = p;
    mm_body_set_generic(r->body, set, material, r->renderer->body);
}
static void runtime_release(void *p, void *resource, int custom)
{
    (void)p; (void)custom;
    mm_material_owner_release(resource);
}
static void runtime_clear(void *p)
{
    mm_actor_sounds_clear(((RuntimeEffect *)p)->sounds);
}
static int runtime_sound(void *p, MMI32 index)
{
    const MMActorEffectHooks *h = ((RuntimeEffect *)p)->hooks;
    return h->enqueue_sound(h->context, index);
}

int mm_actor_runtime_effect(MMActorState *state, const MMActorTrack *track, MMI32 index, const MMActorPoint *origin,
                            MMTemplateOwner *tmpl, MMBodyOwner *body, MMActorSounds *sounds, int metadata_only,
                            const MMActorEffectRenderer *renderer, const MMActorEffectHooks *hooks)
{
    MMActorMotionServices motion = mm_actor_track_motion_services();
    MMActorEffectServices s = {0};
    RuntimeEffect runtime;
    int result;
    runtime.tmpl = tmpl;
    runtime.body = body;
    runtime.sounds = sounds;
    runtime.renderer = renderer;
    runtime.hooks = hooks;
    mm_body_costume_init(&runtime.saved);
    s.context = &runtime;
    s.write_event = runtime_write;
    s.action_count = metadata_only || (hooks && hooks->check_actions) ? runtime_actions : NULL;
    s.cel_count = runtime_cels;
    s.action_flags = runtime_flags;
    if (!metadata_only)
    {
        s.select_cel = runtime_pose;
        s.save_costume = runtime_save;
        s.default_costume = runtime_defaults;
        s.restore_costume = runtime_restore;
        s.visibility = hooks && hooks->visibility ? runtime_visibility : NULL;
        s.fetch_custom = runtime_fetch_custom;
        s.fetch_material = runtime_fetch_material;
        s.apply_custom = runtime_custom;
        s.apply_material = runtime_material;
        s.release_resource = runtime_release;
        s.clear_sounds = sounds ? runtime_clear : NULL;
        s.enqueue_sound = hooks && hooks->enqueue_sound ? runtime_sound : NULL;
    }
    result = mm_actor_effect_apply(state, track->route, track->events, index, origin, body != NULL,
                                  metadata_only, &motion, &s);
    if (!metadata_only)
        mm_body_costume_clear(&runtime.saved, renderer->body);
    return result;
}

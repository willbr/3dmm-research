/* Actor ownership/undo restoration converted from actor.cpp and actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_owner.h"
#include "mm/body_create.h"
#include "mm/text_resource.h"
#include <assert.h>
#include <string.h>

void mm_actor_runtime_init(MMActorRuntime *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    mm_actor_state_init(&runtime->state);
    runtime->template_tag.sid = runtime->sound_tag.sid = -1;
    runtime->first = MM_ACTOR_FRAME_UNPLACED;
    runtime->last = (-2147483647L - 1);
    runtime->actor = -1;
}

void mm_actor_owner_init(MMActorOwner *actor)
{
    mm_actor_runtime_init(&actor->runtime);
    mm_actor_track_init(&actor->track);
    mm_actor_sounds_init(&actor->sounds, NULL);
}

MMActorLifetime mm_actor_owner_lifetime(MMActorOwner *actor)
{
    MMActorRuntime *r = &actor->runtime;
    MMActorLifetime life;
    life.first = &r->first;
    life.last = &r->last;
    life.dirty = &r->life_dirty;
    life.recording = &r->recording.recording;
    life.rejoining = &r->recording.rejoining;
    life.publish_attempts = 1;
    return life;
}

int mm_actor_owner_frame_range(MMActorOwner *owner, MMI32 *first, MMI32 *last,
                               const MMActorTimelineServices *timeline)
{
    const MMActorRuntime *runtime = &owner->runtime;
    MMActorLifetime lifetime;
    if (runtime->first == MM_ACTOR_FRAME_UNPLACED)
        return MM_ACTOR_RANGE_UNPLACED;
    if (first)
        *first = runtime->first;
    if (!last)
        return MM_ACTOR_RANGE_READY;
    if (!runtime->life_dirty)
    {
        *last = runtime->last;
        if (runtime->last < runtime->first && timeline->diagnostic)
            timeline->diagnostic(timeline->context, MM_ACTOR_LIFETIME_BAD_RANGE);
        return MM_ACTOR_RANGE_READY;
    }
    lifetime = mm_actor_owner_lifetime(owner);
    if (mm_actor_refresh_lifetime(owner->track.route, owner->track.events, last, &lifetime, timeline))
        return MM_ACTOR_RANGE_READY;
    if (timeline->diagnostic)
        timeline->diagnostic(timeline->context, MM_ACTOR_LIFETIME_INCOMPLETE);
    return MM_ACTOR_RANGE_PARTIAL;
}

void mm_actor_owner_evaluate_pose(const MMActorOwner *owner, MMActorState *state, const MMActorPoint *position,
                                  MMActorBodyPose *pose, const MMActorMotionServices *motion,
                                  const MMActorEffectRenderer *renderer)
{
    MMActorPoseInputs inputs;
    MMTemplateOwner *tmpl = owner->runtime.template_object;
    renderer->rest(tmpl, &inputs.restX, &inputs.restY, &inputs.restZ);
    inputs.fixedPathOrientation = mm_template_owner_has_flag(tmpl, MM_TEMPLATE_TEXT);
    mm_actor_pose_evaluate(state, owner->track.route, owner->track.events, &inputs, position, pose, motion);
}

int mm_actor_owner_set_action(MMActorOwner *owner, MMI32 action, MMI32 cel, int frozen, MMU32 external_child,
                              const MMActorMotionServices *motion, const MMActorActionEditServices *services)
{
    MMActorState state = owner->runtime.state;
    int ok = mm_actor_edit_set_action(&state, &owner->sounds, owner->track.events, owner->runtime.actor,
                                      action, cel, frozen, external_child, motion, services);
    owner->runtime.state = state;
    return ok;
}

void mm_actor_owner_delete_forward(MMActorOwner *owner,int all,int *alive,MMI32 current,
                                  const MMActorRouteEditServices *services)
{
    MMActorState state=owner->runtime.state;
    const MMActorMotionServices motion=mm_actor_track_motion_services();
    mm_actor_route_edit_delete_forward(&state,&owner->sounds,owner->track.route,owner->track.events,
                                        owner->runtime.actor,all,alive,current,&motion,services);
    owner->runtime.state=state;
}

int mm_actor_replace_template(MMActorRuntime *r, MMActorEventTable *events, const MMTag *tag,
                              const MMActorTemplateServices *s, const MMActorEditServices *edits)
{
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    MMActorEvent event;
    MMActorCostume costume;
    MMI32 i, sets;
    int text;
    MMBodyOwner *body = r->body;
    MMTemplateOwner *next = s->fetch(s->context, tag);
    if (!next)
        return 0;
    text = mm_template_owner_has_flag(next, MM_TEMPLATE_TEXT);
    if (text && !s->adjust_body(next, body))
    {
        mm_template_owner_release(next);
        return 0;
    }
    if (!s->pose(next, body, r->state.action, r->state.cel, NULL))
        return 0;
    s->position(s->context);
    mm_template_owner_release(r->template_object);
    r->template_object = next;
    mm_tag_close(&r->template_tag, s->tags);
    r->template_tag = *tag;
    mm_tag_dup(&r->template_tag, s->tags);

    if (!text)
        return 1;
    sets = mm_body_owner_state(body)->shape.set_count;
    for (i = mm_actor_events_count(events) - 1; i >= 0; --i)
    {
        mm_actor_events_get_fixed(events, i, &event);
        if (event.aet != MM_ACTOR_COSTUME)
            continue;
        mm_actor_events_get_payload(events, i, &costume);
        if (costume.body_set >= sets)
            mm_actor_edit_remove(&r->state, events, i, 1, &motion, edits);
    }
    return 1;
}

static void publish_result(MMActorOwnerResult *out, void *actor)
{
    out->actor = actor;
    if (out->publish)
        out->publish(out->context, actor);
}

int mm_actor_owner_duplicate(void *source, int reset, MMActorOwnerResult *out, const MMActorOwnerServices *s)
{
    const MMActorOwner *original;
    MMActorOwner *copy;
    MMActorRuntime *runtime;
    void *actor = s->create(s->context);
    publish_result(out, actor);
    if (!actor)
        return 0;
    original = s->owner(s->context, source);
    copy = s->owner(s->context, actor);
    if (!mm_actor_track_clone(&copy->track, &original->track, s->tags) ||
        !mm_actor_sounds_clone(&copy->sounds, &original->sounds))
    {
        s->release(s->context, actor);
        publish_result(out, NULL);
        return 0;
    }
    runtime = &copy->runtime;
    *runtime = original->runtime;
    if (runtime->body)
        s->retain_body(s->context, runtime->body);
    s->retain_template(s->context, runtime->template_object);
    mm_tag_dup(&runtime->template_tag, s->tags);
    runtime->time_frozen = 0;
    if (reset)
        mm_actor_owner_reset(actor, s);
    return 1;
}

void mm_actor_owner_rewind(void *actor, const MMActorOwnerServices *s)
{
    MMActorOwner *owner = s->owner(s->context, actor);
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    MMActorState state = owner->runtime.state;
    MMActorRouteEditServices route;
    s->route_services(s->context, actor, &route);
    mm_actor_route_edit_rewind(&state, owner->track.route, &motion, &route);
    owner->runtime.state = state;
}

void mm_actor_owner_reset(void *actor, const MMActorOwnerServices *s)
{
    MMActorOwner *owner = s->owner(s->context, actor);
    MMActorRuntime *runtime = &owner->runtime;
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    MMActorState state;
    MMActorRouteEditServices route;
    runtime->scene = NULL;
    if (runtime->body)
        s->release_body(s->context, runtime->body);
    runtime->body = NULL;
    runtime->state.frame = 2147483647L;
    state = runtime->state;
    s->route_services(s->context, actor, &route);
    mm_actor_route_edit_reset(&state, owner->track.route, &motion, &route);
    runtime->state = state;
}

void mm_actor_owner_restore(void *actor, void *snapshot, const MMActorOwnerServices *s)
{
    MMActorOwner *live = s->owner(s->context, actor);
    MMActorOwner *stored = s->owner(s->context, snapshot);
    live->runtime = stored->runtime;
    mm_actor_track_swap(&live->track, &stored->track);
    mm_actor_sounds_swap(&live->sounds, &stored->sounds);
    s->invalidate_views(s->context, stored->runtime.scene);
}

void mm_actor_owner_restore_undo(void *actor, void *snapshot, const MMActorOwnerServices *s,
                                 const MMActorTimelineServices *timeline, const MMActorPlaybackEffects *playback)
{
    MMActorOwner *owner = s->owner(s->context, actor);
    MMActorRuntime *live = &owner->runtime;
    MMActorRuntime *stored = &s->owner(s->context, snapshot)->runtime;
    MMActorState state = live->state;
    MMActorRouteEditServices route;
    void *scene = stored->scene;
    MMI32 frame = state.frame;
    assert(!stored->body);
    stored->body = live->body;
    stored->scene = live->scene;
    s->route_services(s->context, actor, &route);
    mm_actor_route_edit_hide(&state, &route);
    mm_actor_owner_restore(actor, snapshot, s);
    stored->body = NULL;
    stored->scene = scene;
    mm_actor_owner_seek(owner, frame, NULL, timeline, playback);
    s->invalidate_views(s->context, live->scene);
}

void mm_actor_owner_dispose(void *actor, const MMActorOwnerServices *s)
{
    MMActorOwner *owner = s->owner(s->context, actor);
    MMActorRuntime *runtime = &owner->runtime;
    mm_tag_close(&runtime->template_tag, s->tags);
    mm_actor_track_close_tags(&owner->track, s->tags);
    if (runtime->body)
        s->release_body(s->context, runtime->body);
    runtime->body = NULL;
    mm_actor_track_dispose(&owner->track, s->tags);
    if (runtime->template_object)
        s->release_template(s->context, runtime->template_object);
    runtime->template_object = NULL;
    mm_actor_sounds_dispose(&owner->sounds);
}

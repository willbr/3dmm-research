/* Actor ownership/undo restoration converted from actor.cpp and actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_owner.h"
#include "mm/actor_track.h"
#include <string.h>

void mm_actor_runtime_init(MMActorRuntime *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    mm_actor_state_init(&runtime->state);
    runtime->template_tag.sid = runtime->sound_tag.sid = -1;
    runtime->first = 2147483647L;
    runtime->last = (-2147483647L - 1);
    runtime->actor = -1;
}

int mm_actor_replace_template(MMActorRuntime *r, MMActorEventTable *events, const MMTag *tag,
                              const MMActorTemplateServices *s, const MMActorEditServices *edits)
{
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    MMActorEvent event;
    MMActorCostume costume;
    MMI32 i, sets;
    void *next = s->fetch(s->context, tag);
    if (!next)
        return 0;
    if (s->text(s->context, next) && !s->adjust_body(s->context, next, r->body))
    {
        s->release(s->context, next);
        return 0;
    }
    if (!s->select_cel(s->context, next, r->body, r->state.action, r->state.cel))
        return 0;
    s->position(s->context);
    if (r->template_object)
        s->release(s->context, r->template_object);
    r->template_object = next;
    mm_tag_close(&r->template_tag, s->tags);
    r->template_tag = *tag;
    mm_tag_dup(&r->template_tag, s->tags);

    if (!s->text(s->context, next))
        return 1;
    sets = s->body_sets(s->context, r->body);
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
    MMActorRuntime *runtime;
    void *actor = s->create(s->context);
    publish_result(out, actor);
    if (!actor)
        return 0;
    if (!s->clone_track(s->context, actor, source) || !s->clone_sounds(s->context, actor, source))
    {
        s->release(s->context, actor);
        publish_result(out, NULL);
        return 0;
    }
    runtime = s->runtime(s->context, actor);
    *runtime = *s->runtime(s->context, source);
    if (runtime->body)
        s->retain(s->context, runtime->body);
    s->retain(s->context, runtime->template_object);
    s->duplicate_tag(s->context, &runtime->template_tag);
    runtime->time_frozen = 0;
    if (reset)
        mm_actor_owner_reset(actor, s);
    return 1;
}

void mm_actor_owner_reset(void *actor, const MMActorOwnerServices *s)
{
    MMActorRuntime *runtime = s->runtime(s->context, actor);
    MMActorState state;
    MMActorRouteEditServices route;
    runtime->scene = NULL;
    if (runtime->body)
        s->release(s->context, runtime->body);
    runtime->body = NULL;
    runtime->state.frame = 2147483647L;
    state = runtime->state;
    s->route_services(s->context, actor, &route);
    mm_actor_route_edit_reset(&state, s->route(s->context, actor), &s->motion, &route);
    runtime->state = state;
}

void mm_actor_owner_restore(void *actor, void *snapshot, const MMActorOwnerServices *s)
{
    const MMActorRuntime *stored = s->runtime(s->context, snapshot);
    *s->runtime(s->context, actor) = *stored;
    s->swap_track(s->context, actor, snapshot);
    s->swap_sounds(s->context, actor, snapshot);
    s->invalidate_views(s->context, stored->scene);
}

void mm_actor_owner_restore_undo(void *actor, void *snapshot, const MMActorOwnerServices *s)
{
    MMActorRuntime *live = s->runtime(s->context, actor);
    MMActorRuntime *stored = s->runtime(s->context, snapshot);
    MMActorState state = live->state;
    MMActorRouteEditServices route;
    void *scene = stored->scene;
    MMI32 frame = state.frame;
    stored->body = live->body;
    stored->scene = live->scene;
    s->route_services(s->context, actor, &route);
    mm_actor_route_edit_hide(&state, &route);
    mm_actor_owner_restore(actor, snapshot, s);
    stored->body = NULL;
    stored->scene = scene;
    s->seek(s->context, actor, frame);
    s->invalidate_views(s->context, live->scene);
}

void mm_actor_owner_dispose(void *actor, const MMActorOwnerServices *s)
{
    MMActorRuntime *runtime = s->runtime(s->context, actor);
    s->close_tag(s->context, &runtime->template_tag);
    s->close_track_tags(s->context, actor);
    if (runtime->body)
        s->release(s->context, runtime->body);
    runtime->body = NULL;
    s->clear_track(s->context, actor);
    if (runtime->template_object)
        s->release(s->context, runtime->template_object);
    runtime->template_object = NULL;
    s->dispose_sounds(s->context, actor);
}

/* Actor stage editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_stage_edit.h"
#include "mm/actor_effect.h"
#include <string.h>
#define FLAG(kind) ((MMU32)1 << (kind))
static void publish(const MMActorState *state, const MMActorEditServices *e)
{ if (e->publish) e->publish(e->context, state); }
int mm_actor_stage_add(MMActorState *state, MMActorSounds *sounds, void *route, void *events,
    MMI32 actor, MMU32 external_child, const MMActorMotionServices *m,
    const MMActorStageEditServices *s)
{
    const MMActorSubrouteEditServices *sub = &s->subroute;
    const MMActorEditServices *e = &sub->route.edits;
    MMActorEntrance add;
    MMActorPoint placement, origin;
    MMActorRoutePoint point;
    MMActorEditRange range;
    MMU32 count = 4, bytes = 36, mask;
    MMI32 frozen = 1;
    int update = 0;
    memset(&add, 0, sizeof(add));
    if (m->event_count(events))
    {
        count = 8;
        bytes += sizeof(MMActorCostume) + sizeof(MMActorMatrix) + 4 + sizeof(MMActorStretch);
    }
    if (!sub->route.reserve_events(e->context, count, bytes) || !s->reserve_points(route, 1)) return 0;
    publish(state, e);
    s->placement(e->context, &placement, &origin, &add.y);
    add.translation.dxr = mm_fixed_sub(placement.dxr, origin.dxr);
    add.translation.dyr = mm_fixed_sub(placement.dyr, origin.dyr);
    add.translation.dzr = mm_fixed_sub(placement.dzr, origin.dzr);
    e->get_range(e->context, &range);
    if (state->frame < range.first) { update = 1; range.first = state->frame; }
    if (state->frame > range.last) { update = 1; range.last = state->frame; }
    if (update) e->publish_range(e->context, &range);
    memset(&point, 0, sizeof(point));
    if (state->onStage)
        mm_actor_route_edit_delete_forward_core(state, route, events, 0, NULL, -1, m, &sub->route);
    state->location.dnfrm = state->location.dwrOffset = 0;
    state->location.irpt = state->addEvent < 0 ? 0 : state->location.irpt + 1;
    publish(state, e);
    if (!mm_actor_route_edit_insert(route, state->location.irpt, &point, 0, m, &sub->route))
        s->diagnostic(e->context, MM_STAGE_INSERT_FAILED);
    mm_actor_route_edit_adjust_insert(route, events, state->location.irpt, 0, m, &sub->route);
    mm_actor_route_position(route, &state->location, &state->position, m);
    if (!mm_actor_edit_add_apply(state, sounds, events, actor, MM_ACTOR_ADD, sizeof(add), &add, m, e)) return 0;
    if (state->nextEvent == 1)
    {
        if (!mm_actor_subroute_from_later(state, sounds, route, events, actor, external_child, m, sub)) return 0;
    }
    else
    {
        mask = FLAG(MM_ACTOR_ACTION) | FLAG(MM_ACTOR_COSTUME) | FLAG(MM_ACTOR_STRETCH) |
               FLAG(MM_ACTOR_SIZE) | FLAG(MM_ACTOR_ROTATE_FORWARD);
        if (!mm_actor_subroute_from_previous(state, sounds, route, events, actor, state->nextEvent - 1, mask, m, sub)) return 0;
    }
    mm_actor_subroute_position_body(state, route, events, &state->position, m, sub);
    if (!mm_actor_edit_set_step(state, sounds, events, actor, 0, m, &sub->action.sounds)) return 0;
    if (!mm_actor_edit_add_apply(state, sounds, events, actor, MM_ACTOR_FREEZE, 4, &frozen, m, e)) return 0;
    if (update) e->invalidate_range(e->context);
    return 1;
}
int mm_actor_stage_delete_subroute(MMActorState *state, void *route, void *events,
    const MMActorMotionServices *m, const MMActorSubrouteEditServices *s)
{
    const MMActorEditServices *e = &s->route.edits;
    MMI32 saved;
    MMActorEvent event;
    MMActorEditRange range;
    mm_actor_route_edit_delete_forward_core(state, route, events, 0, NULL, state->nextEvent, m, &s->route);
    mm_actor_route_edit_delete_add(state, route, events, state->addEvent, state->nextEvent, m, &s->route);
    mm_actor_route_edit_hide(state, &s->route);
    s->route.default_costume(e->context);
    mm_actor_route_edit_rewind(state, route, m, &s->route);
    saved = state->frame;
    state->frame = 2147483647L;
    publish(state, e);
    e->get_range(e->context, &range);
    if (m->event_count(events))
    {
        m->event(events, 0, &event);
        range.first = event.nfrm;
        e->publish_range(e->context, &range);
        return s->seek(e->context, state, saved);
    }
    range.first = 2147483647L;
    e->publish_range(e->context, &range);
    return 1;
}
int mm_actor_stage_remove(MMActorState *state, MMActorSounds *sounds, void *route, void *events,
    MMI32 actor, const MMActorMotionServices *m, const MMActorStageEditServices *s)
{
    const MMActorEditServices *e = &s->subroute.route.edits;
    MMActorEvent event;
    if (!state->onStage) return 1;
    if (!s->subroute.route.reserve_events(e->context, 1, 4)) return 0;
    m->event(events, state->addEvent, &event);
    if (state->frame == event.nfrm && !mm_actor_stage_delete_subroute(state, route, events, m, &s->subroute)) return 0;
    if (state->onStage)
        return mm_actor_edit_add_apply(state, sounds, events, actor, MM_ACTOR_REMOVE, 0, NULL, m, e);
    return 1;
}

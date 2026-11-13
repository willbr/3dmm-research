/* Actor stage editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_stage_edit.h"
#include "three_mm/actor_effect.h"
#include <string.h>
#define FLAG(kind) ((ThreeMMU32)1 << (kind))
static void publish(const ThreeMMActorState *state, const ThreeMMActorEditServices *e)
{ if (e->publish) e->publish(e->context, state); }
int three_mm_actor_stage_add(ThreeMMActorState *state, ThreeMMActorSounds *sounds, void *route, void *events,
    ThreeMMI32 actor, ThreeMMU32 external_child, const ThreeMMActorMotionServices *m,
    const ThreeMMActorStageEditServices *s)
{
    const ThreeMMActorSubrouteEditServices *sub = &s->subroute;
    const ThreeMMActorEditServices *e = &sub->route.edits;
    ThreeMMActorEntrance add;
    ThreeMMActorPoint placement, origin;
    ThreeMMActorRoutePoint point;
    ThreeMMActorEditRange range;
    ThreeMMU32 count = 4, bytes = 36, mask;
    ThreeMMI32 frozen = 1;
    int update = 0;
    memset(&add, 0, sizeof(add));
    if (m->event_count(events))
    {
        count = 8;
        bytes += sizeof(ThreeMMActorCostume) + sizeof(ThreeMMActorMatrix) + 4 + sizeof(ThreeMMActorStretch);
    }
    if (!sub->route.reserve_events(e->context, count, bytes) || !s->reserve_points(route, 1)) return 0;
    publish(state, e);
    s->placement(e->context, &placement, &origin, &add.y);
    add.translation.dxr = three_mm_fixed_sub(placement.dxr, origin.dxr);
    add.translation.dyr = three_mm_fixed_sub(placement.dyr, origin.dyr);
    add.translation.dzr = three_mm_fixed_sub(placement.dzr, origin.dzr);
    e->get_range(e->context, &range);
    if (state->frame < range.first) { update = 1; range.first = state->frame; }
    if (state->frame > range.last) { update = 1; range.last = state->frame; }
    if (update) e->publish_range(e->context, &range);
    memset(&point, 0, sizeof(point));
    if (state->onStage)
        three_mm_actor_route_edit_delete_forward_core(state, route, events, 0, NULL, -1, m, &sub->route);
    state->location.dnfrm = state->location.dwrOffset = 0;
    state->location.irpt = state->addEvent < 0 ? 0 : state->location.irpt + 1;
    publish(state, e);
    if (!three_mm_actor_route_edit_insert(route, state->location.irpt, &point, 0, m, &sub->route))
        s->diagnostic(e->context, THREE_MM_STAGE_INSERT_FAILED);
    three_mm_actor_route_edit_adjust_insert(route, events, state->location.irpt, 0, m, &sub->route);
    three_mm_actor_route_position(route, &state->location, &state->position, m);
    if (!three_mm_actor_edit_add_apply(state, sounds, events, actor, THREE_MM_ACTOR_ADD, sizeof(add), &add, m, e)) return 0;
    if (state->nextEvent == 1)
    {
        if (!three_mm_actor_subroute_from_later(state, sounds, route, events, actor, external_child, m, sub)) return 0;
    }
    else
    {
        mask = FLAG(THREE_MM_ACTOR_ACTION) | FLAG(THREE_MM_ACTOR_COSTUME) | FLAG(THREE_MM_ACTOR_STRETCH) |
               FLAG(THREE_MM_ACTOR_SIZE) | FLAG(THREE_MM_ACTOR_ROTATE_FORWARD);
        if (!three_mm_actor_subroute_from_previous(state, sounds, route, events, actor, state->nextEvent - 1, mask, m, sub)) return 0;
    }
    three_mm_actor_subroute_position_body(state, route, events, &state->position, m, sub);
    if (!three_mm_actor_edit_set_step(state, sounds, events, actor, 0, m, &sub->action.sounds)) return 0;
    if (!three_mm_actor_edit_add_apply(state, sounds, events, actor, THREE_MM_ACTOR_FREEZE, 4, &frozen, m, e)) return 0;
    if (update) e->invalidate_range(e->context);
    return 1;
}
int three_mm_actor_stage_delete_subroute(ThreeMMActorState *state, void *route, void *events,
    const ThreeMMActorMotionServices *m, const ThreeMMActorSubrouteEditServices *s)
{
    const ThreeMMActorEditServices *e = &s->route.edits;
    ThreeMMI32 saved;
    ThreeMMActorEvent event;
    ThreeMMActorEditRange range;
    three_mm_actor_route_edit_delete_forward_core(state, route, events, 0, NULL, state->nextEvent, m, &s->route);
    three_mm_actor_route_edit_delete_add(state, route, events, state->addEvent, state->nextEvent, m, &s->route);
    three_mm_actor_route_edit_hide(state, &s->route);
    s->route.default_costume(e->context);
    three_mm_actor_route_edit_rewind(state, route, m, &s->route);
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
int three_mm_actor_stage_remove(ThreeMMActorState *state, ThreeMMActorSounds *sounds, void *route, void *events,
    ThreeMMI32 actor, const ThreeMMActorMotionServices *m, const ThreeMMActorStageEditServices *s)
{
    const ThreeMMActorEditServices *e = &s->subroute.route.edits;
    ThreeMMActorEvent event;
    if (!state->onStage) return 1;
    if (!s->subroute.route.reserve_events(e->context, 1, 4)) return 0;
    m->event(events, state->addEvent, &event);
    if (state->frame == event.nfrm && !three_mm_actor_stage_delete_subroute(state, route, events, m, &s->subroute)) return 0;
    if (state->onStage)
        return three_mm_actor_edit_add_apply(state, sounds, events, actor, THREE_MM_ACTOR_REMOVE, 0, NULL, m, e);
    return 1;
}

#include "mm/actor_animation.h"
#include "mm/actor_transform_edit.h"
#include <math.h>

#define MAX_RECORD_FRAMES 4096

static MMActorAnimationPoint position(const MMActorState *state)
{
    MMActorAnimationPoint p;
    p.x = (float)mm_fixed_add(state->routeTranslation.dxr, state->position.dxr) / 65536.0f;
    p.y = (float)mm_fixed_add(state->routeTranslation.dyr, state->position.dyr) / 65536.0f;
    p.z = (float)mm_fixed_add(state->routeTranslation.dzr, state->position.dzr) / 65536.0f;
    return p;
}

static int in_range(double x, double y, double z)
{
    return fabs(x) <= 32700.0 && fabs(y) <= 32700.0 && fabs(z) <= 32700.0;
}

static MMActorPoint fixed_delta(float x, float y, float z)
{
    MMActorPoint p;
    p.dxr = (MMI32)(x * 65536.0f);
    p.dyr = (MMI32)(y * 65536.0f);
    p.dzr = (MMI32)(z * 65536.0f);
    return p;
}

static int set_action(MMPreviewActor *actor, MMActorSounds *sounds, const MMActorAnimation *edit,
                      MMI32 action_count, const MMActorSubrouteEditServices *services)
{
    const MMActorActionEditServices *action;
    const MMActorEditServices *edits;
    MMI32 count, disabled;
    action = &services->action;
    edits = &services->route.edits;
    if (edit->action < 0 || edit->action >= action_count ||
        !action->sounds.runtime.cel_count(action->sounds.runtime.context, edit->action, &count) ||
        edit->cel < 0 || edit->cel >= count ||
        !mm_actor_edit_set_action(&actor->state, sounds, actor->events, actor->header.actor_id,
                                  edit->action, edit->cel, edit->freeze, 0, &services->timeline.motion, action))
        return 0;
    if (edit->freeze || !actor->state.frozen)
        return 1;
    /* Let a standalone action play in place at the end of an existing route. */
    disabled = 0;
    return action->sounds.reserve_events(action->sounds.context, 1, sizeof(disabled)) &&
        mm_actor_edit_add_apply(&actor->state, sounds, actor->events, actor->header.actor_id,
                                MM_ACTOR_FREEZE, sizeof(disabled), &disabled, &services->timeline.motion, edits);
}

static int record_path(MMPreviewActor *actor, MMActorSounds *sounds, MMActorRecording *record,
                       const MMActorAnimation *edit, const MMActorAnimationPoint *start,
                       const MMActorRecordServices *services)
{
    const MMActorTimelineServices *timeline;
    MMActorState *state;
    MMI32 first, id;
    size_t i;
    timeline = &services->subroute.timeline;
    state = &actor->state;
    first = state->frame;
    id = actor->header.actor_id;
    if (!edit->point_count || first > 2147483647L - MAX_RECORD_FRAMES ||
        !mm_actor_record_begin(state, record, actor->route, actor->events, edit->replace,
                               NULL, &timeline->motion, services))
        return 0;
    if (edit->replace)
        mm_actor_route_edit_delete_forward(state, sounds, actor->route, actor->events, id, 0, NULL,
                                           -1, &timeline->motion, &services->subroute.route);
    for (i = 0; i < edit->point_count; ++i)
    {
        const MMActorAnimationPoint *point = &edit->points[i];
        for (;;)
        {
            MMActorAnimationPoint current;
            double x, y, z, length, fraction;
            MMI32 step;
            MMActorPoint delta;
            MMActorRecordResult result = {0};
            current = position(state);
            x = (double)start->x + point->x - current.x;
            y = (double)start->y + point->y - current.y;
            z = (double)start->z + point->z - current.z;
            length = sqrt(x*x + y*y + z*z);
            step = 0;
            if (!mm_actor_record_step(state, &step, timeline))
                return 0;
            if (!step)
                step = mm_fixed_mul(services->stationary_step, state->transform.rScaleStep);
            if (step <= 0 || length < (float)step / 65536.0f)
                break;
            if ((double)state->frame - first >= MAX_RECORD_FRAMES)
                return 0;
            fraction = length > 1024.0 ? 1024.0 / length : 1.0;
            delta = fixed_delta((float)(x * fraction), (float)(y * fraction), (float)(z * fraction));
            if (!mm_actor_record_move(state, record, sounds, actor->route, actor->events, id, &delta,
                                      edit->freeze ? MM_RECORD_FREEZE : 0, 0, &result, NULL,
                                      &timeline->motion, services))
                return 0;
            if (!result.frame)
                break;
        }
    }
    return state->frame != first &&
        mm_actor_record_end(state, record, sounds, actor->route, actor->events, id, edit->replace,
                            NULL, &timeline->motion, services);
}

int mm_actor_animation_edit(MMPreviewActor *actor, MMActorSounds *sounds, MMActorRecording *record,
                            const MMActorAnimation *edit, MMI32 action_count,
                            const MMActorRecordServices *services, MMActorAnimationRange *range)
{
    MMActorAnimationPoint start;
    size_t i;
    MMI32 last;
    if (!actor->state.onStage || actor->inactive || !actor->tmpl ||
        edit->point_count > MM_ACTOR_ANIMATION_MAX_POINTS || (edit->point_count && !edit->points))
        return 0;
    start = position(&actor->state);
    for (i = 0; i < edit->point_count; ++i)
    {
        const MMActorAnimationPoint *point = &edit->points[i];
        if (!in_range((double)start.x + point->x, (double)start.y + point->y, (double)start.z + point->z))
            return 0;
    }
    switch (edit->kind)
    {
    case MM_ACTOR_ANIMATION_ACTION:
        if (!set_action(actor, sounds, edit, action_count, &services->subroute))
            return 0;
        break;
    case MM_ACTOR_ANIMATION_TWEAK:
    {
        const MMActorAnimationPoint *point = edit->points;
        MMActorPoint delta;
        MMActorTransformEditServices transform = {0};
        if (edit->point_count != 1 || !in_range(point->x, point->y, point->z))
            return 0;
        transform.subroute = services->subroute;
        delta = fixed_delta(point->x, point->y, point->z);
        if (!mm_actor_transform_tweak(&actor->state, sounds, actor->route, actor->events,
                                      actor->header.actor_id, &delta, 0,
                                      &services->subroute.timeline.motion, &transform))
            return 0;
        break;
    }
    case MM_ACTOR_ANIMATION_RECORD:
        if (!record_path(actor, sounds, record, edit, &start, services))
            return 0;
        break;
    default:
        return 0;
    }
    last = actor->header.last_frame;
    if (!mm_actor_lifetime(actor->route, actor->events, actor->header.first_frame, 0, &last, 100000,
                           &services->subroute.timeline))
        return 0;
    range->edited_last = actor->state.frame;
    range->actor_last = last > range->edited_last ? last : range->edited_last;
    return 1;
}

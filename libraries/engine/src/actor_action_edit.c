/* Actor action and fill editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_action_edit.h"
#include "three_mm/actor_frame.h"
#include <string.h>
#define FLAG(kind) ((ThreeMMU32)1 << (kind))
static int before(const ThreeMMActorLocation *a, const ThreeMMActorLocation *b)
{
    return a->irpt<b->irpt || (a->irpt==b->irpt && (a->dwrOffset<b->dwrOffset ||
        (a->dwrOffset==b->dwrOffset && a->dnfrm<b->dnfrm)));
}
void three_mm_actor_edit_transform_fill(ThreeMMActorState *state, void *events, ThreeMMI32 kind,
    const void *payload, ThreeMMU32 bytes, ThreeMMI32 first, ThreeMMI32 compare, ThreeMMU32 flags,
    const ThreeMMActorMotionServices *m, const ThreeMMActorFillServices *s)
{
    ThreeMMActorEvent event;
    ThreeMMI32 i;
    const void *value = NULL;
    int replace = 1, remove;
    s->lock_events(events);
    if (compare!=-1) value=s->payload_view(events,compare);
    else for (i=0; i<first-1; ++i)
    {
        m->event(events,i,&event);
        if (event.aet==kind) value=s->payload_view(events,i);
    }
    if (!value) value=payload;
    for (i=first; i<m->event_count(events); ++i)
    {
        remove=0;
        m->event(events,i,&event);
        if (event.aet==THREE_MM_ACTOR_ADD) break;
        switch (event.aet)
        {
        case THREE_MM_ACTOR_SIZE: case THREE_MM_ACTOR_STRETCH: case THREE_MM_ACTOR_ROTATE_FORWARD:
            if (event.aet!=kind) continue;
            if (replace)
            {
                if (!memcmp(value,s->payload_view(events,i),bytes)) { remove=1; break; }
                replace=0; value=payload;
            }
            if (memcmp(value,s->payload_view(events,i),bytes)) goto done;
            remove=1;
            break;
        case THREE_MM_ACTOR_TWEAK: case THREE_MM_ACTOR_FREEZE: case THREE_MM_ACTOR_STEP:
            remove=(flags & FLAG(event.aet)) && !before(&event.rtel,&state->location);
            break;
        case THREE_MM_ACTOR_ROTATE_HERE:
            remove=(flags & FLAG(THREE_MM_ACTOR_ROTATE_FORWARD)) && !before(&event.rtel,&state->location) &&
                !before(&state->location,&event.rtel);
            break;
        default: break;
        }
        if (remove) { three_mm_actor_edit_remove(state,events,i,1,m,&s->edits); --i; }
    }
done:
    s->unlock_events(events);
}
void three_mm_actor_edit_action_fill(ThreeMMActorState *state, void *events, ThreeMMI32 first,
    ThreeMMI32 previous, ThreeMMI32 replacement, ThreeMMU32 flags,
    const ThreeMMActorMotionServices *m, const ThreeMMActorEditServices *s)
{
    ThreeMMActorEvent event;
    ThreeMMI32 i, action[2];
    for (i=first; i<m->event_count(events); ++i)
    {
        m->event(events,i,&event);
        if (event.aet==THREE_MM_ACTOR_ADD) return;
        if (event.aet==THREE_MM_ACTOR_ACTION)
        {
            m->payload(events,i,action);
            if (!(flags & FLAG(THREE_MM_ACTOR_ACTION)))
            {
                if (action[0]==replacement) continue;
                return;
            }
            if (action[0]==previous)
            { three_mm_actor_edit_remove(state,events,i,1,m,s); --i; }
            else if (action[0]==replacement)
            { previous=replacement; three_mm_actor_edit_remove(state,events,i,1,m,s); --i; }
            else return;
        }
        if (before(&event.rtel,&state->location)) continue;
        if ((event.aet==THREE_MM_ACTOR_TWEAK && (flags & FLAG(THREE_MM_ACTOR_TWEAK))) ||
            (event.aet==THREE_MM_ACTOR_FREEZE && (flags & FLAG(THREE_MM_ACTOR_FREEZE)) &&
             !three_mm_actor_subroute_events_done(events,i,&event.rtel,m)))
        { three_mm_actor_edit_remove(state,events,i,1,m,s); --i; }
    }
}
int three_mm_actor_edit_set_step(ThreeMMActorState *state, ThreeMMActorSounds *list, void *events, ThreeMMI32 actor,
    ThreeMMI32 step, const ThreeMMActorMotionServices *m, const ThreeMMActorSoundEditServices *s)
{
    if (!s->reserve_events(s->context,1,sizeof(step))) return 0;
    return three_mm_actor_edit_add_apply(state,list,events,actor,THREE_MM_ACTOR_STEP,sizeof(step),&step,m,&s->edits);
}
int three_mm_actor_edit_set_action(ThreeMMActorState *state, ThreeMMActorSounds *list, void *events, ThreeMMI32 actor,
    ThreeMMI32 action, ThreeMMI32 cel, int freeze, ThreeMMU32 external_child,
    const ThreeMMActorMotionServices *m, const ThreeMMActorActionEditServices *s)
{
    ThreeMMI32 value[2], first, frozen;
    ThreeMMU32 flags=0;
    ThreeMMActorEditRange range;
    const ThreeMMActorEditServices *edits=&s->sounds.edits;
    int is_static, changed=state->action!=action;
    if (!s->sounds.reserve_events(s->sounds.context,4,20)) return 0;
    if (!s->action_flags(s->context,action,&flags)) return 0;
    is_static=state->actionEvent==-1 || (flags & 8)!=0;
    if (changed) three_mm_actor_sound_remove_motion(state,events,action,m,&s->sounds);
    value[0]=action; value[1]=cel;
    first=state->firstEventInFrame==state->addEvent ? state->firstEventInFrame+1 : state->firstEventInFrame;
    three_mm_actor_edit_action_fill(state,events,first,state->action,action,
        FLAG(THREE_MM_ACTOR_TWEAK)|FLAG(THREE_MM_ACTOR_FREEZE)|FLAG(THREE_MM_ACTOR_ACTION),m,edits);
    if (!three_mm_actor_edit_add_apply(state,list,events,actor,THREE_MM_ACTOR_ACTION,sizeof(value),value,m,edits)) return 0;
    if (changed && !three_mm_actor_sound_defaults(state,list,events,actor,action,external_child,m,&s->sounds)) return 0;
    edits->get_range(edits->context,&range);
    if (state->frame!=range.last)
    { s->mark_life_dirty(s->context); edits->invalidate_range(edits->context); }
    frozen=freeze || three_mm_actor_subroute_events_done(events,state->nextEvent,&state->location,m);
    if (!three_mm_actor_edit_add_apply(state,list,events,actor,THREE_MM_ACTOR_FREEZE,sizeof(frozen),&frozen,m,edits)) return 0;
    if (!is_static && !s->text_template(s->context) &&
        !three_mm_actor_subroute_events_done(events,state->nextEvent,&state->location,m))
        return three_mm_actor_edit_set_step(state,list,events,actor,-65536L,m,&s->sounds);
    return 1;
}

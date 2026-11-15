/* Actor event editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_edit.h"
#include <string.h>
static void publish(const MMActorEditServices *s, const MMActorState *state)
{
    if (s->publish) s->publish(s->context, state);
}
void mm_actor_edit_remove(MMActorState *state, void *events, MMI32 index, int update,
                                const MMActorMotionServices *m, const MMActorEditServices *s)
{
    MMActorEvent event;
    MMI32 frame;
    int sound;
    if (s->publish) s->publish(s->context, state);
    m->event(events, index, &event);
    s->close_event_tag(s->context, index);
    sound = event.aet == MM_ACTOR_SOUND && s->scene_frame(s->context, &frame) && frame == event.nfrm;
    s->delete_event(s->context, index);
    if (update)
    {
        if (index < state->nextEvent) --state->nextEvent;
        if (index < state->actionEvent) --state->actionEvent;
        if (index < state->addEvent) --state->addEvent;
        if (index < state->firstEventInFrame) --state->firstEventInFrame;
    }
    if (s->publish) s->publish(s->context, state);
    if (sound) s->update_sound_frame(s->context);
}
int mm_actor_edit_insert(MMActorState *state, MMI32 index, MMU32 bytes, const void *payload,
                               const MMActorEvent *event, int update, const MMActorEditServices *s)
{
    publish(s,state);
    if (!s->insert_event(s->context,index,bytes,payload,event)) return 0;
    if (payload)
    {
        s->duplicate_event_tag(s->context,index);
        if (update)
        {
            if (index < state->nextEvent) ++state->nextEvent;
            if (index <= state->actionEvent) ++state->actionEvent;
            if (index <= state->addEvent) ++state->addEvent;
            if (index < state->firstEventInFrame) ++state->firstEventInFrame;
        }
    }
    publish(s,state);
    return 1;
}
static int same_location(const MMActorLocation *a, const MMActorLocation *b)
{
    return a->irpt==b->irpt && a->dwrOffset==b->dwrOffset && a->dnfrm==b->dnfrm;
}
void mm_actor_edit_merge(MMActorState *state, MMActorSounds *list, void *events, MMI32 actor,
                               MMI32 first, MMI32 added, MMI32 *result,
                               const MMActorMotionServices *m, const MMActorEditServices *s)
{
    MMActorEvent old, fresh;
    MMActorPoint move, delta;
    MMActorCostume costume, replacement;
    MMActorSound sound, new_sound;
    MMActorMotionSound match;
    MMI32 i;
    MMU32 j;
    if (m->event_count(events)<first || first==added) { if (result) *result=added; return; }
    m->event(events,added,&fresh);
    for (i=first; i<added; ++i)
    {
        m->event(events,i,&old);
        if (old.aet!=fresh.aet && old.aet!=MM_ACTOR_REMOVE && old.aet!=MM_ACTOR_ROTATE_HERE) continue;
        switch (old.aet)
        {
        case MM_ACTOR_REMOVE:
            if (fresh.aet==MM_ACTOR_ADD && fresh.nfrm!=old.nfrm)
            { mm_actor_edit_remove(state,events,i,1,m,s); --added; --i; }
            break;
        case MM_ACTOR_ADD: continue;
        case MM_ACTOR_MOVE:
            m->payload(events,i,&move); m->payload(events,added,&delta);
            move.dxr=mm_fixed_add(move.dxr,delta.dxr);
            move.dyr=mm_fixed_add(move.dyr,delta.dyr);
            move.dzr=mm_fixed_add(move.dzr,delta.dzr);
            s->write_payload(s->context,i,&move);
            goto delete_new;
        case MM_ACTOR_COSTUME:
            m->payload(events,i,&costume); m->payload(events,added,&replacement);
            if (costume.body_set!=replacement.body_set) continue;
            if (!same_location(&old.rtel,&fresh.rtel)) goto delete_old;
            /* Both rows own their tags: replacement must retain one reference
             * after the added row is closed. Avoid the old overwrite leak. */
            s->close_event_tag(s->context,i);
            s->duplicate_event_tag(s->context,added);
            s->write_payload(s->context,i,&replacement);
            goto delete_new;
        case MM_ACTOR_SOUND:
            m->payload(events,i,&sound); m->payload(events,added,&new_sound);
            if (mm_sound_queue_number(sound.style,actor,1)!=mm_sound_queue_number(new_sound.style,actor,1)) continue;
            if (new_sound.queue) continue;
            for (j=0; j<list->count; ++j)
            {
                mm_actor_sounds_get(list,j,&match);
                if (match.sound.style==sound.style && match.sound.cel==sound.cel)
                { mm_actor_sounds_delete(list,j); break; }
            }
            mm_actor_edit_remove(state,events,i,1,m,s); --added; --i;
            continue;
        case MM_ACTOR_ROTATE_HERE:
            if (fresh.aet==MM_ACTOR_ROTATE_FORWARD && fresh.nfrm==old.nfrm)
            { mm_actor_edit_remove(state,events,i,1,m,s); --added; --i; }
            if (fresh.aet!=MM_ACTOR_ROTATE_HERE) continue;
            if (!same_location(&old.rtel,&fresh.rtel)) goto delete_old;
            s->copy_payload(s->context,i,added);
            goto delete_new;
        case MM_ACTOR_ROTATE_FORWARD:
            mm_actor_edit_remove(state,events,i,1,m,s); --added; --i;
            continue;
        default:
            if (!same_location(&old.rtel,&fresh.rtel)) goto delete_old;
            s->copy_payload(s->context,i,added);
            goto delete_new;
        }
    }
    if (result) *result=added;
    return;
delete_new:
    if (result) *result=i;
    mm_actor_edit_remove(state,events,added,1,m,s);
    return;
delete_old:
    mm_actor_edit_remove(state,events,i,1,m,s);
    --added;
    if (result) *result=added;
}
int mm_actor_edit_add_apply(MMActorState *state, MMActorSounds *list, void *events, MMI32 actor,
                                  MMI32 kind, MMU32 bytes, const void *payload,
                                  const MMActorMotionServices *m, const MMActorEditServices *s)
{
    MMActorEvent event;
    MMI32 added;
    MMActorEditRange range;
    MMActorPoint delta;
    if (kind==MM_ACTOR_MOVE) memcpy(&delta,payload,sizeof(delta));
    event.aet=kind; event.rtel=state->location; event.nfrm=state->frame;
    if (!mm_actor_edit_insert(state,state->nextEvent,bytes,payload,&event,1,s)) return 0;
    mm_actor_edit_merge(state,list,events,actor,state->firstEventInFrame,state->nextEvent,&added,m,s);
    if (kind==MM_ACTOR_MOVE)
    {
        state->subrouteTranslation.dxr=mm_fixed_add(delta.dxr,state->subrouteTranslation.dxr);
        state->subrouteTranslation.dyr=mm_fixed_add(delta.dyr,state->subrouteTranslation.dyr);
        state->subrouteTranslation.dzr=mm_fixed_add(delta.dzr,state->subrouteTranslation.dzr);
    }
    else
    {
        publish(s,state);
        if (!s->apply_event(s->context,state,added)) { publish(s,state); return 0; }
    }
    if (state->nextEvent==added) ++state->nextEvent;
    publish(s,state);
    s->dirty(s->context);
    s->get_range(s->context,&range);
    if (state->frame<range.first)
    {
        s->invalidate_range(s->context);
        range.first=state->frame;
        if (s->publish_range) s->publish_range(s->context,&range);
    }
    if (state->frame>range.last)
    {
        s->invalidate_range(s->context);
        range.last=state->frame;
        if (s->publish_range) s->publish_range(s->context,&range);
    }
    return 1;
}

/* Actor event editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_edit.h"
void three_mm_actor_edit_remove(ThreeMMActorState *state, void *events, ThreeMMI32 index, int update,
                                const ThreeMMActorMotionServices *m, const ThreeMMActorEditServices *s)
{
    ThreeMMActorEvent event;
    ThreeMMI32 frame;
    int sound;
    if (s->publish) s->publish(s->context, state);
    m->event(events, index, &event);
    s->close_event_tag(s->context, index);
    sound = event.aet == THREE_MM_ACTOR_SOUND && s->scene_frame(s->context, &frame) && frame == event.nfrm;
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

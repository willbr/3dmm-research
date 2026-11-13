/* Actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_undo.h"
#include <stddef.h>
static void publish(const ThreeMMActorUndoState *state,const ThreeMMActorUndoServices *s)
{ s->publish(s->context,state); }
void three_mm_actor_undo_set_actor(ThreeMMActorUndoState *state,void *actor,const ThreeMMActorUndoServices *s)
{ state->actor=actor; publish(state,s); s->retain(s->context,actor); }
void three_mm_actor_undo_dispose(ThreeMMActorUndoState *state,const ThreeMMActorUndoServices *s)
{
    if (state->actor)
    { s->release(s->context,state->actor); state->actor=NULL; publish(state,s); }
}
int three_mm_actor_undo_toggle(ThreeMMActorUndoState *state,const ThreeMMActorUndoServices *s)
{
    void *scene,*old,*view; ThreeMMI32 id;
    if (!s->switch_scene(s->context,state->movie,state->scene)) return 0;
    scene=s->current_scene(s->context,state->movie);
    if (!s->seek(s->context,scene,state->frame))
    { s->clear_undo(s->context,state->movie); return 0; }
    s->flush(s->context,state->movie);
    scene=s->current_scene(s->context,state->movie);
    old=s->find_actor(s->context,scene,state->actor_id);
    if (old) s->retain(s->context,old);
    if (!state->actor)
    { scene=s->current_scene(s->context,state->movie); id=s->actor_id(s->context,old); s->remove_actor(s->context,scene,id); }
    else
    {
        if (s->has_name(s->context) && s->capture_name(s->context,state->movie,state->actor_id))
        { s->assign_name(s->context,state->movie,s->actor_id(s->context,state->actor)); s->store_name(s->context); }
        id=s->actor_id(s->context,state->actor);
        if (state->actor_id!=id)
        { scene=s->current_scene(s->context,state->movie); s->remove_actor(s->context,scene,state->actor_id); state->actor_id=id; publish(state,s); }
        scene=s->current_scene(s->context,state->movie);
        if (!s->add_actor(s->context,scene,state->actor))
        { if (old) s->release(s->context,old); return 0; }
        view=s->view(s->context,state->movie);
        if (view && !s->text_mode(s->context,view))
        { scene=s->current_scene(s->context,state->movie); s->select_actor(s->context,scene,state->actor); }
        s->release(s->context,state->actor); state->actor=NULL; publish(state,s);
    }
    scene=s->current_scene(s->context,state->movie); s->invalidate_range(s->context,scene);
    state->actor=old; publish(state,s);
    if (old) s->reset_actor(s->context,old);
    if (state->sound) s->play(s->context,state->movie); else s->flush(s->context,state->movie);
    return 1;
}
int three_mm_actor_undo_redo(ThreeMMActorUndoState *state,const ThreeMMActorUndoServices *s)
{
    ThreeMMI32 frame; int result;
    if (!state->sooner_later) return s->invoke_undo(s->context);
    frame=state->frame; state->frame=state->last_frame; publish(state,s);
    result=s->invoke_undo(s->context);
    s->read(s->context,state); state->frame=frame; publish(state,s); return result;
}
int three_mm_actor_undo_create(void *snapshot,ThreeMMI32 id,int sound,const void *name,
    const ThreeMMActorUndoCreateServices *s)
{
    void *undo=s->create(s->context); if (!undo) return 0;
    s->set_actor(s->context,undo,snapshot); s->set_id(s->context,undo,id); s->set_sound(s->context,undo,sound);
    if (name) s->set_name(s->context,undo,name);
    if (!s->add(s->context,undo))
    { s->clear(s->context); s->release(s->context,undo); return 0; }
    s->release(s->context,undo); s->reset_actor(s->context,snapshot); return 1;
}

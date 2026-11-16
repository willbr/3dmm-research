/* Actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_undo.h"
#include <stddef.h>
#include <string.h>
static void publish(const MMActorUndoState *state,const MMActorUndoServices *s)
{ if (s->publish) s->publish(s->context,state); }
int mm_actor_undo_init(MMActorUndoState *state, MMMovieUndoContext *base, unsigned unit)
{
    if (unit != 1 && unit != 2) return 0;
    memset(state, 0, sizeof(*state)); state->base = base;
    return mm_movie_title_init(&state->name, unit);
}
int mm_actor_undo_set_name(MMActorUndoState *state, const void *text, MMU32 count)
{
    return mm_movie_title_set(&state->name, text, count);
}
void mm_actor_undo_set_actor(MMActorUndoState *state,void *actor,const MMActorUndoServices *s)
{ state->actor=actor; publish(state,s); s->retain(s->context,actor); }
void mm_actor_undo_dispose(MMActorUndoState *state,const MMActorUndoServices *s)
{
    if (state->actor)
    { s->release(s->context,state->actor); state->actor=NULL; publish(state,s); }
}
int mm_actor_undo_toggle(MMActorUndoState *state,const MMActorUndoServices *s)
{
    MMMovieUndoContext base = *state->base;
    void *scene,*old,*view; MMI32 id;
    if (!s->switch_scene(s->context,base.movie,base.scene)) return 0;
    scene=s->current_scene(s->context,base.movie);
    if (!s->seek(s->context,scene,base.frame))
    { s->clear_undo(s->context,base.movie); return 0; }
    s->flush(s->context,base.movie);
    scene=s->current_scene(s->context,base.movie);
    old=s->find_actor(s->context,scene,state->actor_id);
    if (old) s->retain(s->context,old);
    if (!state->actor)
    { scene=s->current_scene(s->context,base.movie); id=s->actor_id(s->context,old); s->remove_actor(s->context,scene,id); }
    else
    {
        if ((s->has_name ? s->has_name(s->context) : state->name.count != 0) && s->capture_name(s->context,base.movie,state->actor_id))
        { s->assign_name(s->context,base.movie,s->actor_id(s->context,state->actor)); s->store_name(s->context); }
        id=s->actor_id(s->context,state->actor);
        if (state->actor_id!=id)
        { scene=s->current_scene(s->context,base.movie); s->remove_actor(s->context,scene,state->actor_id); state->actor_id=id; publish(state,s); }
        scene=s->current_scene(s->context,base.movie);
        if (!s->add_actor(s->context,scene,state->actor))
        { if (old) s->release(s->context,old); return 0; }
        view=s->view(s->context,base.movie);
        if (view && !s->text_mode(s->context,view))
        { scene=s->current_scene(s->context,base.movie); s->select_actor(s->context,scene,state->actor); }
        s->release(s->context,state->actor); state->actor=NULL; publish(state,s);
    }
    scene=s->current_scene(s->context,base.movie); s->invalidate_range(s->context,scene);
    state->actor=old; publish(state,s);
    if (old) s->reset_actor(s->context,old);
    if (state->sound) s->play(s->context,base.movie); else s->flush(s->context,base.movie);
    return 1;
}
int mm_actor_undo_redo(MMActorUndoState *state,const MMActorUndoServices *s)
{
    MMI32 frame; int result;
    if (!state->sooner_later) return s->invoke_undo(s->context);
    frame=state->base->frame; state->base->frame=state->last_frame; publish(state,s);
    result=s->invoke_undo(s->context);
    if (s->read) s->read(s->context,state);
    state->base->frame=frame; publish(state,s); return result;
}
int mm_actor_undo_create(void *snapshot,MMI32 id,int sound,const void *name,
    const MMActorUndoCreateServices *s)
{
    void *undo=s->create(s->context); if (!undo) return 0;
    s->set_actor(s->context,undo,snapshot); s->set_id(s->context,undo,id); s->set_sound(s->context,undo,sound);
    if (name) s->set_name(s->context,undo,name);
    if (!s->add(s->context,undo))
    { s->clear(s->context); s->release(s->context,undo); return 0; }
    s->release(s->context,undo); s->reset_actor(s->context,snapshot); return 1;
}

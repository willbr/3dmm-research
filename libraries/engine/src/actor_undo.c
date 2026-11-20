/* Actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_undo.h"
#include <stddef.h>
#include <string.h>
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
{ state->actor=actor; s->retain(s->context,actor); }
void mm_actor_undo_dispose(MMActorUndoState *state,const MMActorUndoServices *s)
{
    if (state->actor)
    { s->release(s->context,state->actor); state->actor=NULL; }
}
static void swap_name(MMActorUndoState *state,void *movie,const MMActorUndoServices *s)
{
    MMMovieTitle current;
    const void *text;
    MMU32 count;
    if (!state->name.count || !s->find_name(s->context,movie,state->actor_id,&text,&count))
        return;
    /* Renaming can invalidate the borrowed text even when the actor ID stays
     * the same. Copy the old name before writing to the roll call. */
    mm_movie_title_init(&current,state->name.unit);
    if (!mm_movie_title_set(&current,text,count))
        return;
    s->rename(s->context,movie,s->actor_id(s->context,state->actor),
              mm_movie_title_text(&state->name),state->name.count);
    state->name=current;
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
        swap_name(state,base.movie,s);
        id=s->actor_id(s->context,state->actor);
        if (state->actor_id!=id)
        { scene=s->current_scene(s->context,base.movie); s->remove_actor(s->context,scene,state->actor_id); state->actor_id=id; }
        scene=s->current_scene(s->context,base.movie);
        if (!s->add_actor(s->context,scene,state->actor))
        { if (old) s->release(s->context,old); return 0; }
        view=s->view(s->context,base.movie);
        if (view && !s->text_mode(s->context,view))
        { scene=s->current_scene(s->context,base.movie); s->select_actor(s->context,scene,state->actor); }
        s->release(s->context,state->actor); state->actor=NULL;
    }
    scene=s->current_scene(s->context,base.movie); s->invalidate_range(s->context,scene);
    state->actor=old;
    if (old) s->reset_actor(s->context,old);
    if (state->sound) s->play(s->context,base.movie); else s->flush(s->context,base.movie);
    return 1;
}
int mm_actor_undo_redo(MMActorUndoState *state,const MMActorUndoServices *s)
{
    MMI32 frame; int result;
    if (!state->sooner_later) return s->invoke_undo(s->context);
    frame=state->base->frame; state->base->frame=state->last_frame;
    result=s->invoke_undo(s->context);
    state->base->frame=frame; return result;
}
int mm_actor_undo_create(void *snapshot,MMI32 id,int sound,const void *name,MMU32 count,
    const MMActorOwnerServices *actors,const MMActorUndoCreateServices *s)
{
    void *undo;
    MMActorUndoState *state=s->create(s->context,&undo);
    if (!state) return 0;
    state->actor=snapshot;
    actors->retain(actors->context,snapshot);
    state->actor_id=id;
    state->sound=sound;
    if (name) mm_actor_undo_set_name(state,name,count);
    if (!s->add(s->context,undo))
    { s->clear(s->context); s->release(s->context,undo); return 0; }
    s->release(s->context,undo);
    mm_actor_owner_reset(snapshot,actors);
    return 1;
}

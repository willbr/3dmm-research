/* Movie sound integration converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_sound.h"
#include <stddef.h>
static MMU32 free_slot(void *file,MMU32 type,MMU32 number,const MMMovieSoundServices *s)
{
    MMI32 slot,count=s->count(file,type,number); MMU32 child;
    for (slot=0;slot<count;++slot)
        if (!s->find(file,type,number,(MMU32)slot,MM_SOUND_CHUNK,&child)) break;
    return (MMU32)slot;
}
static int current_scene(void *file,const MMMovieSoundState *state,MMU32 *scene,const MMMovieSoundServices *s)
{ return s->find(file,MM_SOUND_MOVIE,state->movie,state->scene,MM_SOUND_SCENE,scene); }
MMU32 mm_movie_sound_next_scene(const MMMovieSoundServices *s)
{
    MMMovieSoundState state; MMU32 scene; void *file;
    s->read(s->context,&state); file=s->file(state.cache);
    if (!current_scene(file,&state,&scene,s)) return 0;
    return free_slot(file,MM_SOUND_SCENE,scene,s);
}
MMU32 mm_movie_sound_next_movie(const MMMovieSoundServices *s)
{
    MMMovieSoundState state; s->read(s->context,&state);
    return free_slot(s->file(state.cache),MM_SOUND_MOVIE,state.movie,s);
}
int mm_movie_sound_is_child(void *file,MMU32 type,MMU32 number,const MMMovieSoundServices *s)
{
    MMMovieSoundState state; MMChunkChild child; MMI32 i,count;
    s->read(s->context,&state); count=s->count(file,MM_SOUND_MOVIE,state.movie);
    for (i=0;i<count;++i)
    {
        if (!s->child(file,MM_SOUND_MOVIE,state.movie,i,&child))
        { s->diagnostic(s->context,MM_SOUND_BAD_CHILD_ENUM); break; }
        if (child.type==type && child.number==number) return 1;
    }
    return 0;
}
int mm_movie_sound_adopt(void *file,MMU32 scene,const MMMovieSoundServices *s)
{
    MMMovieSoundState state; MMChunkChild child; MMI32 i,count;
    MMU32 slot;
    s->read(s->context,&state); count=s->count(file,MM_SOUND_SCENE,scene);
    for (i=0;i<count;++i)
    {
        if (!s->child(file,MM_SOUND_SCENE,scene,i,&child))
        { s->diagnostic(s->context,MM_SOUND_BAD_CHILD_ENUM); break; }
        if (child.type!=MM_SOUND_CHUNK) continue;
        if (mm_movie_sound_is_child(file,child.type,child.number,s)) continue;
        /* The slot comes from the working file, which may differ from file. */
        slot=mm_movie_sound_next_movie(s);
        if (!s->adopt(file,MM_SOUND_MOVIE,state.movie,child.type,child.number,slot)) return 0;
    }
    return 1;
}
int mm_movie_sound_resolve(MMTag *tag,MMU32 child,MMU32 scene,void *cache,const MMMovieSoundServices *s)
{
    MMTag updated=*tag; MMMovieSoundState state; MMU32 number; void *file;
    s->read(s->context,&state); if (!cache) cache=state.cache; file=s->file(cache);
    if (tag->sid!=0) return 1;
    if (scene==MM_SOUND_NO_SCENE && !current_scene(file,&state,&scene,s)) return 0;
    if (!s->find(file,MM_SOUND_SCENE,scene,child,MM_SOUND_CHUNK,&number)) return 0;
    if (tag->number!=number)
    {
        updated.number=number;
        if (!s->open_tag(s->context,&updated,cache)) return 0;
        s->close_tag(s->context,tag); *tag=updated;
    }
    return 1;
}
int mm_movie_sound_save(MMTag *tag,const MMMovieSoundServices *s)
{ MMMovieSoundState state; s->read(s->context,&state); return s->save_tag(s->context,tag,state.cache,1); }
int mm_movie_sound_child(MMU32 number,MMU32 *output,const MMMovieSoundServices *s)
{
    MMMovieSoundState state; MMU32 scene; MMChunkChild child;
    MMI32 i,count; void *file;
    s->read(s->context,&state); file=s->file(state.cache);
    if (!current_scene(file,&state,&scene,s)) return 0;
    count=s->count(file,MM_SOUND_SCENE,scene);
    for (i=0;i<count;++i)
    {
        if (!s->child(file,MM_SOUND_SCENE,scene,i,&child)) return 0;
        if (child.type==MM_SOUND_CHUNK && child.number==number) { *output=child.slot; return 1; }
    }
    *output=mm_movie_sound_next_scene(s);
    return s->adopt(file,MM_SOUND_SCENE,scene,MM_SOUND_CHUNK,number,*output);
}
static int finish_copy(void *file,MMU32 number,const MMMovieSoundServices *s)
{
    MMMovieSoundState state; MMU32 unused_scene,slot;
    s->read(s->context,&state);
    if (!current_scene(file,&state,&unused_scene,s)) s->diagnostic(s->context,MM_SOUND_NO_CURRENT_SCENE);
    slot=mm_movie_sound_next_movie(s);
    if (!s->adopt(file,MM_SOUND_MOVIE,state.movie,MM_SOUND_CHUNK,number,slot))
    { s->delete_chunk(file,MM_SOUND_CHUNK,number); return 0; }
    return s->save_file(file,MM_SOUND_CREATOR);
}
int mm_movie_sound_copy(void *source,MMU32 number,MMU32 *output,const MMMovieSoundServices *s)
{
    MMMovieSoundState state; void *file;
    if (!s->ensure_autosave(s->context)) return 0;
    s->read(s->context,&state); file=s->file(state.cache);
    if (s->inspect_source) s->inspect_source(source);
    if (!s->copy_chunk(source,MM_SOUND_CHUNK,number,file,output)) return 0;
    return finish_copy(file,*output,s);
}
int mm_movie_sound_import(void *source,MMI32 style,MMU32 *output,void *name,const MMMovieSoundServices *s)
{
    MMMovieSoundState state; void *file; int ok;
    s->read(s->context,&state); file=s->file(state.cache);
    if (!s->use_temp(s->context)) return 0;
    if (s->is_midi(source)) ok=s->copy_midi(source,file,output,name);
    else ok=s->copy_wave(source,file,style,output,name);
    if (!ok)
    {
        if (!s->has_bad_sound_error(s->context)) s->diagnostic(s->context,MM_SOUND_CANNOT_COPY);
        return 0;
    }
    if (!finish_copy(file,*output,s)) return 0;
    s->mark_gc(s->context); return 1;
}
int mm_movie_sound_unused(int *valid,const MMMovieSoundServices *s)
{
    MMMovieSoundState state; MMI32 i,count; MMU32 number; int unused=0;
    if (valid) *valid=0;
    s->read(s->context,&state); if (!state.cache) return 0;
    { void *file=s->file(state.cache);
      count=s->sound_count(file);
      for (i=0;i<count;++i)
      {
          number=0;
          if (!s->sound_at(file,i,&number)) s->diagnostic(s->context,MM_SOUND_BAD_CHUNK_ENUM);
          if (s->references(file,MM_SOUND_CHUNK,number)>=2) continue;
          unused=1; if (!valid) break;
          if (s->child_at_slot(file,MM_SOUND_CHUNK,number,0)) { *valid=1; break; }
      }
    }
    return unused;
}

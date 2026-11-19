/* Movie sound integration converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_sound.h"
#include <stddef.h>
static int find_parent(const MMChunkAccess *chunks, MMU32 type, MMU32 number, MMU32 *position,
                       MMChunkEntry *entry)
{
    int status = mm_chunk_find(chunks, type, number, position);
    return status == MM_CHUNK_OK ? mm_chunk_at(chunks, *position, entry) : status;
}

int mm_movie_sound_free_slot(const MMChunkAccess *chunks, MMU32 type, MMU32 parent, MMU32 *slot)
{
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 row, index, next = 0;
    int status = find_parent(chunks, type, parent, &index, &entry);
    if (status != MM_CHUNK_OK)
        return status;
    for (row = 0; row < entry.child_count; ++row)
    {
        status = mm_chunk_child_at(chunks, index, row, &child);
        if (status != MM_CHUNK_OK)
            return status;
        if (child.slot > next)
            break;
        if (child.slot == next && child.type == MM_SOUND_CHUNK)
            ++next;
    }
    *slot = next;
    return MM_CHUNK_OK;
}

static int current_scene(const MMChunkAccess *chunks, const MMMovieSoundState *state, MMU32 *scene)
{
    return mm_chunk_link_number(chunks, MM_SOUND_MOVIE, state->movie, state->scene, MM_SOUND_SCENE, scene) ==
           MM_CHUNK_OK;
}

MMU32 mm_movie_sound_next_scene(const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMChunkAccess chunks;
    MMU32 scene, slot = 0;
    s->read(s->context, &state);
    chunks = s->chunks(s->file(state.cache));
    if (current_scene(&chunks, &state, &scene))
        mm_movie_sound_free_slot(&chunks, MM_SOUND_SCENE, scene, &slot);
    return slot;
}

MMU32 mm_movie_sound_next_movie(const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMChunkAccess chunks;
    MMU32 slot = 0;
    s->read(s->context, &state);
    chunks = s->chunks(s->file(state.cache));
    mm_movie_sound_free_slot(&chunks, MM_SOUND_MOVIE, state.movie, &slot);
    return slot;
}

int mm_movie_sound_is_child(void *file, MMU32 type, MMU32 number, const MMMovieSoundServices *s)
{
    const MMChunkAccess chunks = s->chunks(file);
    MMMovieSoundState state;
    MMChunkEntry movie;
    MMChunkChild child;
    MMU32 i, parent;
    s->read(s->context, &state);
    if (find_parent(&chunks, MM_SOUND_MOVIE, state.movie, &parent, &movie) != MM_CHUNK_OK)
        return 0;
    for (i = 0; i < movie.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, parent, i, &child) != MM_CHUNK_OK)
        {
            s->diagnostic(s->context, MM_SOUND_BAD_CHILD_ENUM);
            break;
        }
        if (child.type == type && child.number == number)
            return 1;
    }
    return 0;
}

int mm_movie_sound_adopt(void *file, MMU32 scene, const MMMovieSoundServices *s)
{
    MMChunkAccess chunks = s->chunks(file);
    MMMovieSoundState state;
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 i, parent, slot;
    s->read(s->context, &state);
    if (find_parent(&chunks, MM_SOUND_SCENE, scene, &parent, &entry) != MM_CHUNK_OK)
        return 1;
    for (i = 0; i < entry.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, parent, i, &child) != MM_CHUNK_OK)
        {
            s->diagnostic(s->context, MM_SOUND_BAD_CHILD_ENUM);
            break;
        }
        if (child.type != MM_SOUND_CHUNK || mm_movie_sound_is_child(file, child.type, child.number, s))
            continue;
        /* The slot comes from the working file, which may differ from file. */
        slot = mm_movie_sound_next_movie(s);
        if (!s->adopt(file, MM_SOUND_MOVIE, state.movie, child.type, child.number, slot))
            return 0;
        chunks = s->chunks(file);
    }
    return 1;
}

int mm_movie_sound_resolve(MMTag *tag, MMU32 child, MMU32 scene, void *cache, const MMMovieSoundServices *s)
{
    MMTag updated = *tag;
    MMMovieSoundState state;
    MMChunkAccess chunks;
    MMU32 number;
    void *file;
    s->read(s->context, &state);
    if (!cache)
        cache = state.cache;
    file = s->file(cache);
    if (tag->sid != 0)
        return 1;
    chunks = s->chunks(file);
    if (scene == MM_SOUND_NO_SCENE && !current_scene(&chunks, &state, &scene))
        return 0;
    if (mm_chunk_link_number(&chunks, MM_SOUND_SCENE, scene, child, MM_SOUND_CHUNK, &number) != MM_CHUNK_OK)
        return 0;
    if (tag->number != number)
    {
        updated.number = number;
        if (!s->open_tag(s->context, &updated, cache))
            return 0;
        s->close_tag(s->context, tag);
        *tag = updated;
    }
    return 1;
}
int mm_movie_sound_save(MMTag *tag,const MMMovieSoundServices *s)
{ MMMovieSoundState state; s->read(s->context,&state); return s->save_tag(s->context,tag,state.cache,1); }
int mm_movie_sound_child(MMU32 number, MMU32 *output, const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 scene, i, parent;
    void *file;
    s->read(s->context, &state);
    file = s->file(state.cache);
    chunks = s->chunks(file);
    if (!current_scene(&chunks, &state, &scene))
        return 0;
    if (find_parent(&chunks, MM_SOUND_SCENE, scene, &parent, &entry) == MM_CHUNK_OK)
        for (i = 0; i < entry.child_count; ++i)
        {
            if (mm_chunk_child_at(&chunks, parent, i, &child) != MM_CHUNK_OK)
                return 0;
            if (child.type == MM_SOUND_CHUNK && child.number == number)
            {
                *output = child.slot;
                return 1;
            }
        }
    *output = mm_movie_sound_next_scene(s);
    return s->adopt(file, MM_SOUND_SCENE, scene, MM_SOUND_CHUNK, number, *output);
}
static int finish_copy(void *file,MMU32 number,const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMChunkAccess chunks;
    MMU32 unused_scene, slot;
    s->read(s->context, &state);
    chunks = s->chunks(file);
    if (!current_scene(&chunks, &state, &unused_scene))
        s->diagnostic(s->context, MM_SOUND_NO_CURRENT_SCENE);
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
int mm_movie_sound_scan_unused(const MMChunkAccess *chunks, int *unused, int *valid)
{
    MMChunkEntry sound;
    MMChunkChild child;
    MMU32 row;
    int status;
    *unused = 0;
    if (valid)
        *valid = 0;
    status = mm_chunk_find(chunks, MM_SOUND_CHUNK, 0, &row);
    if (status != MM_CHUNK_OK && status != MM_CHUNK_NOT_FOUND)
        return status;
    for (; row < chunks->count; ++row)
    {
        status = mm_chunk_at(chunks, row, &sound);
        if (status != MM_CHUNK_OK)
            return status;
        if (sound.type != MM_SOUND_CHUNK)
            break;
        if (sound.parent_count >= 2)
            continue;
        *unused = 1;
        if (!valid)
            break;
        if (mm_chunk_child_type(chunks, row, MM_SOUND_PAYLOAD_SLOT, 0, &child) == MM_CHUNK_OK)
        {
            *valid = 1;
            break;
        }
    }
    return MM_CHUNK_OK;
}
int mm_movie_sound_unused(int *valid, const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMChunkAccess chunks;
    int unused = 0;
    s->read(s->context, &state);
    if (state.cache)
    {
        chunks = s->chunks(s->file(state.cache));
        if (mm_movie_sound_scan_unused(&chunks, &unused, valid) != MM_CHUNK_OK)
            s->diagnostic(s->context, MM_SOUND_BAD_CHUNK_ENUM);
    }
    else if (valid)
        *valid = 0;
    return unused;
}

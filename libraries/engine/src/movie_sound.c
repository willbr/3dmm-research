/* Movie sound integration converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_sound.h"
#include "mm/chunk_copy.h"
#include "mm/movie_save.h"
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

static void read_sound_state(const MMMovieSoundServices *s, MMMovieSoundState *state)
{
    if (s->read)
    {
        s->read(s->context, state);
        return;
    }
    state->cache = s->data->resources;
    state->movie = s->data->movieNumber;
    state->scene = (MMU32)s->scenes->index;
}

static MMChunkEdit sound_edit(void *cache, const MMMovieSoundServices *s)
{
    return s->edit ? s->edit(cache) : mm_resource_cache_writer(cache).edit;
}

static MMChunkAccess sound_chunks(void *cache, const MMMovieSoundServices *s)
{
    const MMChunkEdit edit = sound_edit(cache, s);
    return mm_chunk_edit_access(&edit);
}

static int adopt_sound(const MMChunkEdit *edit, MMU32 type, MMU32 parent, const MMChunkChild *child,
                       void *context, void (*diagnostic)(void *, int))
{
    int status = mm_chunk_adopt(edit, type, parent, child, 1);
    if (status == MM_CHUNK_NOT_FOUND && diagnostic)
        diagnostic(context, MM_SOUND_MISSING_CHUNK);
    return status == MM_CHUNK_OK;
}

MMU32 mm_movie_sound_next_scene(const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMChunkAccess chunks;
    MMU32 scene, slot = 0;
    read_sound_state(s, &state);
    chunks = sound_chunks(state.cache, s);
    if (current_scene(&chunks, &state, &scene))
        mm_movie_sound_free_slot(&chunks, MM_SOUND_SCENE, scene, &slot);
    return slot;
}

MMU32 mm_movie_sound_next_movie(const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMChunkAccess chunks;
    MMU32 slot = 0;
    read_sound_state(s, &state);
    chunks = sound_chunks(state.cache, s);
    mm_movie_sound_free_slot(&chunks, MM_SOUND_MOVIE, state.movie, &slot);
    return slot;
}

static int is_sound_child(const MMChunkAccess *chunks, MMU32 movie, MMU32 type, MMU32 number,
                          void *context, void (*diagnostic)(void *, int))
{
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 i, parent;
    if (find_parent(chunks, MM_SOUND_MOVIE, movie, &parent, &entry) != MM_CHUNK_OK)
        return 0;
    for (i = 0; i < entry.child_count; ++i)
    {
        if (mm_chunk_child_at(chunks, parent, i, &child) != MM_CHUNK_OK)
        {
            if (diagnostic)
                diagnostic(context, MM_SOUND_BAD_CHILD_ENUM);
            break;
        }
        if (child.type == type && child.number == number)
            return 1;
    }
    return 0;
}

int mm_movie_sound_is_child(const MMChunkEdit *edit, MMU32 type, MMU32 number, const MMMovieSoundServices *s)
{
    const MMChunkAccess chunks = mm_chunk_edit_access(edit);
    MMMovieSoundState state;
    read_sound_state(s, &state);
    return is_sound_child(&chunks, state.movie, type, number, s->context, s->diagnostic);
}

static int adopt_scene(const MMChunkEdit *edit, MMU32 movie, MMU32 scene, void *context,
                        void (*diagnostic)(void *, int), const MMMovieSoundServices *working)
{
    const MMChunkAccess chunks = mm_chunk_edit_access(edit);
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 i, parent;
    if (find_parent(&chunks, MM_SOUND_SCENE, scene, &parent, &entry) != MM_CHUNK_OK)
        return 1;
    for (i = 0; i < entry.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, parent, i, &child) != MM_CHUNK_OK)
        {
            if (diagnostic)
                diagnostic(context, MM_SOUND_BAD_CHILD_ENUM);
            break;
        }
        if (child.type != MM_SOUND_CHUNK ||
            is_sound_child(&chunks, movie, child.type, child.number, context, diagnostic))
            continue;
        child.slot = 0;
        if (working)
            child.slot = mm_movie_sound_next_movie(working);
        else
            mm_movie_sound_free_slot(&chunks, MM_SOUND_MOVIE, movie, &child.slot);
        if (!adopt_sound(edit, MM_SOUND_MOVIE, movie, &child, context, diagnostic))
            return 0;
    }
    return 1;
}

int mm_movie_sound_adopt_scene(const MMChunkEdit *edit, MMU32 movie, MMU32 scene,
                               void *context, void (*diagnostic)(void *, int))
{
    return adopt_scene(edit, movie, scene, context, diagnostic, NULL);
}

int mm_movie_sound_adopt(const MMChunkEdit *edit, MMU32 scene, const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    read_sound_state(s, &state);
    /* Legacy callers can reserve slots in a different working file. */
    return adopt_scene(edit, state.movie, scene, s->context, s->diagnostic, s);
}

int mm_movie_sound_resolve(MMTag *tag, MMU32 child, MMU32 scene, void *cache, const MMMovieSoundServices *s)
{
    MMTag updated = *tag;
    MMMovieSoundState state;
    MMChunkAccess chunks;
    MMU32 number;
    read_sound_state(s, &state);
    if (!cache)
        cache = state.cache;
    if (tag->sid != 0)
        return 1;
    chunks = sound_chunks(cache, s);
    if (scene == MM_SOUND_NO_SCENE && !current_scene(&chunks, &state, &scene))
        return 0;
    if (mm_chunk_link_number(&chunks, MM_SOUND_SCENE, scene, child, MM_SOUND_CHUNK, &number) != MM_CHUNK_OK)
        return 0;
    if (tag->number != number)
    {
        updated.number = number;
        if (!mm_tag_open(&updated, cache, NULL, s->tags))
            return 0;
        mm_tag_close(tag, s->tags);
        *tag = updated;
    }
    return 1;
}
int mm_movie_sound_save(MMTag *tag, const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    read_sound_state(s, &state);
    return mm_tag_save(tag, state.cache, 1, s->tags);
}
int mm_movie_sound_child(MMU32 number, MMU32 *output, const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMChunkEdit edit;
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 scene, i, parent;
    read_sound_state(s, &state);
    edit = sound_edit(state.cache, s);
    chunks = mm_chunk_edit_access(&edit);
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
    child.type = MM_SOUND_CHUNK;
    child.number = number;
    child.slot = *output = mm_movie_sound_next_scene(s);
    return adopt_sound(&edit, MM_SOUND_SCENE, scene, &child, s->context, s->diagnostic);
}
static int finish_copy(void *cache, MMU32 number, const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMChunkEdit edit;
    MMChunkAccess chunks;
    MMChunkChild child = {MM_SOUND_CHUNK, 0, 0};
    MMU32 unused_scene;
    read_sound_state(s, &state);
    edit = sound_edit(cache, s);
    chunks = mm_chunk_edit_access(&edit);
    if (!current_scene(&chunks, &state, &unused_scene))
        s->diagnostic(s->context, MM_SOUND_NO_CURRENT_SCENE);
    child.number = number;
    child.slot = mm_movie_sound_next_movie(s);
    if (!adopt_sound(&edit, MM_SOUND_MOVIE, state.movie, &child, s->context, s->diagnostic))
    {
        if (mm_chunk_delete(&edit, child.type, number) != MM_CHUNK_OK)
            s->diagnostic(s->context, MM_SOUND_DELETE_FAILED);
        return 0;
    }
    return s->working_save ? s->working_save->save(cache, NULL) : s->save_cache(cache, MM_SOUND_CREATOR);
}
int mm_movie_sound_copy(void *source_cache, MMU32 number, MMU32 *output, const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    MMResourceChunkCopy source_binding;
    MMChunkFileCopy target_binding;
    MMChunkEndpoint from, to;
    int status;
    if (!(s->working_save ? mm_movie_save_ensure(NULL, s->working_save) : s->ensure_autosave(s->context)))
        return 0;
    read_sound_state(s, &state);
    from = s->tags->bind_cache(source_cache, &source_binding);
    to = s->tags->bind_target(state.cache, &target_binding);
    status = mm_chunk_copy(&from, &to, MM_SOUND_CHUNK, number, 0, output);
    if (status != MM_CHUNK_OK)
    {
        if (s->invalidate_number)
            s->invalidate_number(output);
        if (status == MM_CHUNK_NOT_FOUND && s->tags->missing_chunk)
            s->tags->missing_chunk(s->tags->context);
        return 0;
    }
    return finish_copy(state.cache, *output, s);
}
int mm_movie_sound_import(void *source,MMI32 style,MMU32 *output,void *name,const MMMovieSoundServices *s)
{
    MMMovieSoundState state;
    int ok;
    read_sound_state(s, &state);
    if (!(s->working_save ? mm_movie_save_temporary(s->working_save) : s->use_temp(s->context)))
        return 0;
    if (s->is_midi(source))
        ok = s->copy_midi(source, state.cache, output, name);
    else
        ok = s->copy_wave(source, state.cache, style, output, name);
    if (!ok)
    {
        if (!s->has_bad_sound_error(s->context)) s->diagnostic(s->context,MM_SOUND_CANNOT_COPY);
        return 0;
    }
    if (!finish_copy(state.cache,*output,s)) return 0;
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
    read_sound_state(s, &state);
    if (state.cache)
    {
        chunks = sound_chunks(state.cache, s);
        if (mm_movie_sound_scan_unused(&chunks, &unused, valid) != MM_CHUNK_OK)
            s->diagnostic(s->context, MM_SOUND_BAD_CHUNK_ENUM);
    }
    else if (valid)
        *valid = 0;
    return unused;
}

/* Movie save transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_save.h"
#include "mm/movie_sound.h"
#include <string.h>
#define NIL_NUMBER ((MMU32)0xffffffffUL)

void mm_movie_save_init(MMMovieSaveState *state)
{
    memset(state, 0, sizeof(*state));
}
void mm_movie_save_release(int clear_name, const MMMovieSaveServices *s)
{
    if (s->state->file)
        s->release_saved(s->state->file);
    s->state->file = 0;
    if (clear_name)
        s->state->nameValid = 0;
}
int mm_movie_save_bind(void *path, const MMMovieSaveServices *s)
{
    mm_movie_save_release(0, s);
    s->state->file = s->find_file(s->context, path);
    if (!s->state->file)
        return 0;
    s->retain_saved(s->state->file);
    s->state->nameValid = 1;
    s->state->readOnly = s->read_only(s->context, path);
    return 1;
}
void mm_movie_save_flush(const MMMovieSaveServices *s)
{
    if (s->state->nameValid)
        s->flush_saved(s->state->file);
}
int mm_movie_save_name(void *output, const MMMovieSaveServices *s)
{
    if (s->state->containerNamed)
        s->copy_path(s->container_name(s->context), output);
    else if (s->state->file)
        s->get_name(s->state->file, output);
    return s->state->nameValid;
}
MMI32 mm_movie_save_command(MMI32 command, MMI32 save, MMI32 save_as,
                                       const MMMovieSaveState *state)
{
    return command == save && state->readOnly ? save_as : command;
}
static int refresh_root(void *file, const MMMovieSaveServices *s)
{
    const MMChunkAccess chunks = s->chunks(file);
    MMChunkEntry root;
    MMU32 row;
    if (mm_chunk_type_at(&chunks, MM_SOUND_MOVIE, 0, &root, &row) != MM_CHUNK_OK)
    {
        s->missing_root(s->context);
        return 0;
    }
    s->data->movieNumber = root.number;
    return 1;
}
int mm_movie_save_temporary(const MMMovieSaveServices *s)
{
    void *path, *file = s->file(s->data->resources);
    if (!s->is_temp(file))
    {
        path = s->temporary_path(s->context);
        if (!path || !s->save(file, path))
            return 0;
        s->mark_temporary(file);
        return refresh_root(file, s);
    }
    return 1;
}
int mm_movie_save_resources(const MMMovieSaveServices *s)
{
    void *path, *file;
    int result;
    if (s->data->resources)
        return 1;
    path = s->temporary_path(s->context);
    if (!path)
        return 0;
    file = s->create_file(s->context, path);
    if (!file)
        return 0;
    s->data->resources = s->new_resources(s->context, file, 0);
    if (!s->data->resources)
    {
        s->release_archive(file);
        return 0;
    }
    s->state->autosaveDirty = 1;
    result = mm_movie_autosave(0, 0, s);
    s->release_archive(file);
    return result;
}
int mm_movie_save_ensure(void *output, void (*publish)(void *, void *resources),
                               const MMMovieSaveServices *s)
{
    if (!mm_movie_save_resources(s))
        return 0;
    if (publish)
        publish(output, s->data->resources);
    return mm_movie_save_temporary(s);
}
static void clean_actors(const MMMovieSaveServices *s)
{
    MMI32 i = 0;
    MMTag tag;
    while (i < s->actor_count(s->data->actors))
    {
        if (s->actor_refs(s->data->actors, i, &tag) == 0)
        {
            s->erase_actor(s->data->actors, i);
            s->close_tag(s->context, &tag);
        }
        else
            ++i;
    }
    s->rollcall_changed(s->context);
}
static MMU32 saved_child(void *file, MMU32 type, MMU32 slot, const MMMovieSaveServices *s)
{
    const MMChunkAccess chunks = s->chunks(file);
    MMU32 number;
    if (mm_chunk_link_number(&chunks, MM_SOUND_MOVIE, s->data->movieNumber, slot, type, &number) != MM_CHUNK_OK)
        return NIL_NUMBER;
    return number;
}
int mm_movie_autosave(void *path, int clean_rollcall, const MMMovieSaveServices *s)
{
    void *file, *saved, *retry_path;
    MMU32 scene = 0, old_scene = NIL_NUMBER, actors, old_actors, sources, old_sources;
    int success, retry = 1;
    if (s->has_container(s->context) && s->is_vmm(path))
        return mm_movie_save_to(path, 0, s);
    if (!s->data->resources)
        return 0;
    file = s->file(s->data->resources);
    if (!s->state->autosaveDirty && !path && !s->state->closing)
        return 1;
    s->long_op(s->context, 1);
    if (!path && !s->is_temp(file) && !mm_movie_save_temporary(s))
        goto fail_end;
retry_save:
    if (s->data->movieNumber == NIL_NUMBER && !s->create_root(s->context, file, &s->data->movieNumber))
        goto fail;
    if (s->scenes->open)
    {
        if (!s->write_scene(s->context, s->scenes->open, &scene))
            goto fail;
        old_scene = saved_child(file, MM_SAVE_SCENE, s->scenes->index, s);
        if (old_scene == NIL_NUMBER)
            s->missing_scene(s->context);
        if (!s->adopt(file, s->data->movieNumber, MM_SAVE_SCENE, scene, s->scenes->index))
        {
            s->delete_chunk(file, MM_SAVE_SCENE, scene);
            goto fail;
        }
    }
    if (clean_rollcall)
        clean_actors(s);
    old_actors = saved_child(file, MM_SAVE_TABLE, 0, s);
    if (!s->write_rollcall(s->context, file, s->data->movieNumber, s->data->actors, &actors))
        goto fail_scene;
    old_sources = saved_child(file, MM_SAVE_TABLE, 1, s);
    if (!s->add_sources(s->context, file, s->source_bytes(s->context), &sources))
        goto fail_actors;
    if (!s->write_sources(s->context) || !s->adopt(file, s->data->movieNumber, MM_SAVE_TABLE, sources, 1))
    {
        s->delete_chunk(file, MM_SAVE_TABLE, sources);
        goto fail_actors;
    }
    if (!s->write_title(s->context, file))
        goto fail_sources;
    if (old_scene != NIL_NUMBER)
        s->delete_child(file, s->data->movieNumber, MM_SAVE_SCENE, old_scene, s->scenes->index);
    if (old_actors != NIL_NUMBER)
        s->delete_child(file, s->data->movieNumber, MM_SAVE_TABLE, old_actors, 0);
    if (old_sources != NIL_NUMBER)
        s->delete_child(file, s->data->movieNumber, MM_SAVE_TABLE, old_sources, 1);
    if (path)
    {
        saved = s->find_file(s->context, path);
        if (saved == s->state->file)
            mm_movie_save_release(1, s);
        else
            saved = 0;
        mm_movie_save_gc(file, s);
        success = s->save(file, path);
        if (saved)
        {
            s->state->file = s->find_file(s->context, path);
            if (s->state->file)
            {
                s->retain_saved(s->state->file);
                s->state->nameValid = 1;
            }
        }
        if (!success)
            goto fail;
    }
    else if (!s->save(file, 0))
        goto fail;
    s->state->autosaveDirty = 0;
    s->document_dirty(s->context, 1);
    s->update_title(s->context, path);
    s->long_op(s->context, 0);
    s->release_sources(s->context);
    return 1;
fail_sources:
    s->delete_child(file, s->data->movieNumber, MM_SAVE_TABLE, sources, 1);
fail_actors:
    s->delete_child(file, s->data->movieNumber, MM_SAVE_TABLE, actors, 0);
fail_scene:
    if (s->scenes->open)
        s->delete_child(file, s->data->movieNumber, MM_SAVE_SCENE, scene, s->scenes->index);
fail:
    if (s->has_error(file))
    {
        s->reset_error(file);
        if (s->retry_enabled && retry && path)
        {
            retry_path = s->unique_path(s->context, path);
            if (retry_path && s->save(file, retry_path))
            {
                s->set_temp(file, 1);
                s->clear_errors(s->context);
                retry = 0;
                goto retry_save;
            }
        }
        s->save_error(s->context);
    }
fail_end:
    s->long_op(s->context, 0);
    s->release_sources(s->context);
    return 0;
}
int mm_movie_save_to(void *path, int remember, const MMMovieSaveServices *s)
{
    void *file, *working;
    if (!s->data->resources)
        return 0;
    file = s->file(s->data->resources);
    if (s->is_vmm(path) && !s->has_container(s->context))
    {
        s->bad_file(s->context);
        return 0;
    }
    if (s->has_container(s->context) && (s->is_vmm(path) || mm_movie_needs_container(s)))
    {
        if (!s->is_vmm(path))
            return s->package(s->context, 0, path);
        working = s->temporary_path(s->context);
        if (!working || !mm_movie_autosave(working, 1, s))
            return 0;
        if (!refresh_root(file, s))
            return 0;
        s->set_temp(file, 1);
        if (!s->package(s->context, working, path))
        {
            s->update_title(s->context, s->state->containerNamed ? s->container_name(s->context) : 0);
            return 0;
        }
        s->clear_undo(s->context);
        if (remember)
        {
            mm_movie_save_bind(working, s);
            s->set_container_name(s->context, path);
            s->state->containerNamed = 1;
            s->state->readOnly = s->read_only(s->context, path);
        }
        s->update_title(s->context, path);
        s->document_dirty(s->context, 0);
        return 1;
    }
    if (!mm_movie_autosave(path, 1, s))
        return 0;
    s->clear_undo(s->context);
    s->persistent(file);
    if (remember)
        mm_movie_save_bind(path, s);
    if (!refresh_root(file, s))
        return 0;
    s->document_dirty(s->context, 0);
    return 1;
}
int mm_movie_needs_container(const MMMovieSaveServices *s)
{
    void *tags;
    MMI32 i;
    MMTag tag;
    int portable = 0;
    if (!s->has_container(s->context))
        return 0;
    if (s->state->containerNamed || s->bundled(s->context))
        return 1;
    if (!s->source_archive(s->context, 6) || !s->data->resources)
        return 0;
    if (!mm_movie_autosave(0, 0, s))
        return 1;
    tags = mm_movie_save_tags(s);
    if (!tags)
        return 1;
    for (i = 0; i < s->tag_count(tags); ++i)
    {
        s->tag(tags, i, &tag);
        portable |= tag.sid == 6;
    }
    s->release_tags(tags);
    return portable;
}
void *mm_movie_save_tags(const MMMovieSaveServices *s)
{
    MMChunkAccess chunks;
    MMChunkChild child;
    void *file, *tags = s->new_tags(s->context);
    MMU32 root, scene = 0;
    if (!tags)
        return 0;
    file = s->file(s->data->resources);
    chunks = s->chunks(file);
    if (mm_chunk_find(&chunks, MM_SOUND_MOVIE, s->data->movieNumber, &root) != MM_CHUNK_OK)
        return tags;
    while (mm_chunk_child_exact(&chunks, root, scene++, MM_SAVE_SCENE, &child) == MM_CHUNK_OK)
        if (!s->scene_tags(file, child.number, tags))
        {
            s->release_tags(tags);
            return 0;
        }
    return tags;
}
int mm_movie_save_assets(void *file, const MMMovieSaveServices *s)
{
    static const MMU32 types[] = {MM_SAVE_MATERIAL, MM_SAVE_TEMPLATE};
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMCollectionStorage storage;
    MMArray doomed;
    MMU32 chunk[2], i, ordinal, row;
    size_t type;
    MMI32 tag_index;
    MMTag tag;
    int used, result = 1;
    void *tags = mm_movie_save_tags(s);
    if (!tags)
        return 1;
    if (mm_storage_init(&storage, s->allocator.resize || s->allocator.release ? &s->allocator : 0) !=
        MM_STORAGE_OK)
    {
        s->release_tags(tags);
        return 0;
    }
    memset(&doomed, 0, sizeof(doomed));
    doomed.storage = &storage;
    doomed.width = sizeof(chunk);
    doomed.free_count = -1;
    chunks = s->chunks(file);
    for (type = 0; type < sizeof(types) / sizeof(types[0]); ++type)
    {
        for (ordinal = 0; mm_chunk_type_at(&chunks, types[type], ordinal, &entry, &row) == MM_CHUNK_OK; ++ordinal)
        {
            used = 0;
            for (tag_index = 0; tag_index < s->tag_count(tags); ++tag_index)
            {
                s->tag(tags, tag_index, &tag);
                if (tag.sid != 0)
                    break;
                if (tag.type == entry.type && tag.number == entry.number)
                {
                    used = 1;
                    break;
                }
            }
            chunk[0] = entry.type;
            chunk[1] = entry.number;
            if (!used && mm_array_add(&doomed, chunk, 0) != MM_STORAGE_OK)
            {
                result = 0;
                goto done;
            }
        }
    }
    for (i = 0; i < doomed.count; ++i)
    {
        memcpy(chunk, storage.buffers[0].data + i * sizeof(chunk), sizeof(chunk));
        s->delete_chunk(file, chunk[0], chunk[1]);
        if (file == s->file(s->data->resources))
            s->toss_cache(s->context, chunk[0], chunk[1]);
    }
done:
    s->release_tags(tags);
    mm_storage_dispose(&storage);
    return result;
}
void mm_movie_save_sounds(int purge_all, const MMMovieSaveServices *s)
{
    MMChunkAccess chunks;
    MMChunkEntry root, sound;
    MMChunkChild child, payload;
    MMU32 root_row, sound_row, i, movie = s->data->movieNumber;
    void *file;
    if (!s->data->resources || !(file = s->file(s->data->resources)))
        return;
    chunks = s->chunks(file);
    if (mm_chunk_find(&chunks, MM_SOUND_MOVIE, movie, &root_row) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, root_row, &root) != MM_CHUNK_OK)
    {
        s->bad_child(s->context);
        return;
    }
    for (i = root.child_count; i > 0; --i)
    {
        if (mm_chunk_child_at(&chunks, root_row, i - 1, &child) != MM_CHUNK_OK)
        {
            s->bad_child(s->context);
            return;
        }
        if (child.type != MM_SOUND_CHUNK)
            continue;
        if (mm_chunk_find(&chunks, child.type, child.number, &sound_row) != MM_CHUNK_OK ||
            mm_chunk_at(&chunks, sound_row, &sound) != MM_CHUNK_OK)
        {
            s->bad_child(s->context);
            return;
        }
        if (sound.parent_count > 1 ||
            (!purge_all && mm_chunk_child_type(&chunks, sound_row, MM_SOUND_PAYLOAD_SLOT, 0, &payload) == MM_CHUNK_OK))
            continue;
        s->delete_child(file, movie, child.type, child.number, child.slot);
        /* Dropping a sound subtree can shift the movie row in the live index. */
        if (i > 1)
        {
            chunks = s->chunks(file);
            if (mm_chunk_find(&chunks, MM_SOUND_MOVIE, movie, &root_row) != MM_CHUNK_OK)
            {
                s->bad_child(s->context);
                return;
            }
        }
    }
}
int mm_movie_save_gc(void *file, const MMMovieSaveServices *s)
{
    MMChunkAccess chunks;
    void *working;
    int unused, valid, result = mm_movie_save_assets(file, s);
    if (s->state->closing && s->data->resources && (working = s->file(s->data->resources)))
    {
        chunks = s->chunks(working);
        if (mm_movie_sound_scan_unused(&chunks, &unused, &valid) != MM_CHUNK_OK)
            s->bad_sound_index(s->context);
        if (unused)
            mm_movie_save_sounds(!valid || s->purge_sounds(s->context), s);
    }
    return result;
}

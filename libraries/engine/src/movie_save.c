/* Movie save transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_save.h"
#include "mm/movie_rollcall.h"
#include "mm/movie_sound.h"
#include "mm/scene_store.h"
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
static int clean_actors(const MMMovieSaveServices *s)
{
    MMRollCallTable *actors = &s->data->actors;
    MMRollCallActor row;
    MMU32 i = 0;
    MMTag tag;
    while (mm_rollcall_table_extra(actors, i, &row))
    {
        if (row.data.reference_count == 0)
        {
            tag = mm_movie_rollcall_tag(&row);
            if (!mm_rollcall_table_delete(actors, i))
                return 0;
            s->close_tag(s->context, &tag);
        }
        else
            ++i;
    }
    s->rollcall_changed(s->context);
    return 1;
}
static MMU32 saved_child(void *file, MMU32 type, MMU32 slot, const MMMovieSaveServices *s)
{
    const MMChunkAccess chunks = s->chunks(file);
    MMU32 number;
    if (mm_chunk_link_number(&chunks, MM_SOUND_MOVIE, s->data->movieNumber, slot, type, &number) != MM_CHUNK_OK)
        return NIL_NUMBER;
    return number;
}
static int adopt_child(const MMChunkEdit *edit, const MMChunkChild *child, const MMMovieSaveServices *s)
{
    const int result = mm_chunk_adopt(edit, MM_MOVIE_CHUNK, s->data->movieNumber, child, 1);
    if (result == MM_CHUNK_NOT_FOUND && s->archive_issue)
        s->archive_issue(s->context, MM_SAVE_EDIT_MISSING_CHILD);
    return result == MM_CHUNK_OK;
}
static void unlink_child(const MMChunkEdit *edit, MMU32 movie, const MMChunkChild *child,
                         const MMMovieSaveServices *s)
{
    if (mm_chunk_unlink(edit, MM_MOVIE_CHUNK, movie, child) != MM_CHUNK_OK && s->archive_issue)
        s->archive_issue(s->context, MM_SAVE_EDIT_UNLINK);
}
static void discard_chunk(const MMChunkEdit *edit, MMU32 type, MMU32 number, const MMMovieSaveServices *s)
{
    if (mm_chunk_delete(edit, type, number) != MM_CHUNK_OK && s->archive_issue)
        s->archive_issue(s->context, MM_SAVE_EDIT_DELETE);
}
int mm_movie_autosave(void *path, int clean_rollcall, const MMMovieSaveServices *s)
{
    void *file, *saved, *retry_path;
    MMChunkEdit edit;
    MMChunkChild scene = {MM_SAVE_SCENE, 0, 0}, actors = {MM_SAVE_TABLE, 0, 0}, sources = {MM_SAVE_TABLE, 0, 1};
    MMChunkChild old_scene = {MM_SAVE_SCENE, NIL_NUMBER, 0}, old_actors = {MM_SAVE_TABLE, 0, 0},
                 old_sources = {MM_SAVE_TABLE, 0, 1};
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
    edit = s->edit(file);
retry_save:
    if (s->data->movieNumber == NIL_NUMBER && !s->create_root(s->context, file, &s->data->movieNumber))
        goto fail;
    if (s->scenes->open)
    {
        if (!s->write_scene(s->context, s->scenes->open, &scene.number))
            goto fail;
        scene.slot = old_scene.slot = (MMU32)s->scenes->index;
        old_scene.number = saved_child(file, scene.type, scene.slot, s);
        if (old_scene.number == NIL_NUMBER)
            s->missing_scene(s->context);
        if (!adopt_child(&edit, &scene, s))
        {
            discard_chunk(&edit, scene.type, scene.number, s);
            goto fail;
        }
    }
    if (clean_rollcall && !clean_actors(s))
        goto fail_scene;
    old_actors.number = saved_child(file, actors.type, actors.slot, s);
    if (!s->write_rollcall(s->context, file, s->data->movieNumber, &s->data->actors, &actors.number))
        goto fail_scene;
    old_sources.number = saved_child(file, sources.type, sources.slot, s);
    if (!s->add_sources(s->context, file, s->source_bytes(s->context), &sources.number))
        goto fail_actors;
    if (!s->write_sources(s->context) || !adopt_child(&edit, &sources, s))
    {
        discard_chunk(&edit, sources.type, sources.number, s);
        goto fail_actors;
    }
    if (old_scene.number != NIL_NUMBER && s->preserve_scene_assets && !s->preserve_scene_assets(file, old_scene.number))
        goto fail_sources;
    if (!mm_chunk_title_write(&s->data->title, file, MM_MOVIE_CHUNK, s->data->movieNumber, &s->titles))
        goto fail_sources;
    if (old_scene.number != NIL_NUMBER)
        unlink_child(&edit, s->data->movieNumber, &old_scene, s);
    if (old_actors.number != NIL_NUMBER)
        unlink_child(&edit, s->data->movieNumber, &old_actors, s);
    if (old_sources.number != NIL_NUMBER)
        unlink_child(&edit, s->data->movieNumber, &old_sources, s);
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
    unlink_child(&edit, s->data->movieNumber, &sources, s);
fail_actors:
    unlink_child(&edit, s->data->movieNumber, &actors, s);
fail_scene:
    if (s->scenes->open)
        unlink_child(&edit, s->data->movieNumber, &scene, s);
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
    MMTagList tags;
    MMU32 i;
    MMTagCached tag;
    int portable = 0;
    if (!s->has_container(s->context))
        return 0;
    if (s->state->containerNamed || s->bundled(s->context))
        return 1;
    if (!s->source_archive(s->context, 6) || !s->data->resources)
        return 0;
    if (!mm_movie_autosave(0, 0, s))
        return 1;
    if (!mm_movie_save_tags(&tags, s))
        return 1;
    for (i = 0; i < tags.count; ++i)
    {
        mm_tag_list_get(&tags, i, &tag);
        portable |= tag.tag.sid == 6;
    }
    mm_tag_list_dispose(&tags);
    return portable;
}
int mm_movie_save_tags(MMTagList *tags, const MMMovieSaveServices *s)
{
    MMMovieSceneSource source;
    MMSceneCollectServices collect;
    MMChunkAccess chunks;
    MMChunkChild child;
    MMU32 root, scene = 0;
    if (mm_tag_list_init(tags, s->allocator.resize || s->allocator.release ? &s->allocator : 0) != MM_STORAGE_OK)
        return 0;
    source.movie = NULL;
    source.graph = s->tag_graph(s->file(s->data->resources));
    source.graph.access(source.graph.context, &chunks);
    if (mm_chunk_find(&chunks, MM_SOUND_MOVIE, s->data->movieNumber, &root) != MM_CHUNK_OK)
        return 1;
    collect.context = s->context;
    collect.graph = source.graph;
    collect.read = mm_movie_scene_tag_reader(&source);
    collect.actor_diagnostic = s->actor_tag_issue;
    collect.diagnostic = s->scene_tag_issue;
    while (mm_chunk_child_exact(&chunks, root, scene++, MM_SAVE_SCENE, &child) == MM_CHUNK_OK)
        if (!mm_scene_collect_tags(child.number, tags, &collect))
        {
            mm_tag_list_dispose(tags);
            return 0;
        }
    return 1;
}
int mm_movie_save_assets(void *file, const MMMovieSaveServices *s)
{
    static const MMU32 types[] = {MM_SAVE_MATERIAL, MM_SAVE_TEMPLATE};
    MMChunkAccess chunks;
    MMChunkEdit edit;
    MMChunkEntry entry;
    MMCollectionStorage storage;
    MMArray doomed;
    MMU32 chunk[2], i, ordinal, row, tag_index;
    size_t type;
    MMTagList tags;
    MMTagCached tag;
    int used, result = 1;
    if (!mm_movie_save_tags(&tags, s))
        return 1;
    if (mm_storage_init(&storage, s->allocator.resize || s->allocator.release ? &s->allocator : 0) !=
        MM_STORAGE_OK)
    {
        mm_tag_list_dispose(&tags);
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
            /* The graph owns non-loner resources reachable through other chunks. */
            if (entry.parent_count && !(entry.flags & MM_CHUNK_LONER))
                continue;
            used = 0;
            for (tag_index = 0; tag_index < tags.count; ++tag_index)
            {
                mm_tag_list_get(&tags, tag_index, &tag);
                if (tag.tag.sid != 0)
                    break;
                if (tag.tag.type == entry.type && tag.tag.number == entry.number)
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
    edit = s->edit(file);
    for (i = 0; i < doomed.count; ++i)
    {
        memcpy(chunk, storage.buffers[0].data + i * sizeof(chunk), sizeof(chunk));
        discard_chunk(&edit, chunk[0], chunk[1], s);
        if (file == s->file(s->data->resources))
            s->toss_cache(s->context, chunk[0], chunk[1]);
    }
done:
    mm_tag_list_dispose(&tags);
    mm_storage_dispose(&storage);
    return result;
}
void mm_movie_save_sounds(int purge_all, const MMMovieSaveServices *s)
{
    MMChunkAccess chunks;
    MMChunkEdit edit;
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
    edit = s->edit(file);
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
        unlink_child(&edit, movie, &child, s);
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

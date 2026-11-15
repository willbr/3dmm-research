/* Movie save transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/movie_save.h"
#include <string.h>
#define NIL_NUMBER ((ThreeMMU32)0xffffffffUL)

void three_mm_movie_save_init(ThreeMMMovieSaveState *state)
{
    memset(state, 0, sizeof(*state));
}
void three_mm_movie_save_release(int clear_name, const ThreeMMMovieSaveServices *s)
{
    if (s->state->file)
        s->release_saved(s->state->file);
    s->state->file = 0;
    if (clear_name)
        s->state->nameValid = 0;
}
int three_mm_movie_save_bind(void *path, const ThreeMMMovieSaveServices *s)
{
    three_mm_movie_save_release(0, s);
    s->state->file = s->find_file(s->context, path);
    if (!s->state->file)
        return 0;
    s->retain_saved(s->state->file);
    s->state->nameValid = 1;
    s->state->readOnly = s->read_only(s->context, path);
    return 1;
}
void three_mm_movie_save_flush(const ThreeMMMovieSaveServices *s)
{
    if (s->state->nameValid)
        s->flush_saved(s->state->file);
}
int three_mm_movie_save_name(void *output, const ThreeMMMovieSaveServices *s)
{
    if (s->state->containerNamed)
        s->copy_path(s->container_name(s->context), output);
    else if (s->state->file)
        s->get_name(s->state->file, output);
    return s->state->nameValid;
}
ThreeMMI32 three_mm_movie_save_command(ThreeMMI32 command, ThreeMMI32 save, ThreeMMI32 save_as,
                                       const ThreeMMMovieSaveState *state)
{
    return command == save && state->readOnly ? save_as : command;
}
int three_mm_movie_save_temporary(const ThreeMMMovieSaveServices *s)
{
    void *path, *file = s->file(s->data->resources);
    if (!s->is_temp(file))
    {
        path = s->temporary_path(s->context);
        if (!path || !s->save(file, path))
            return 0;
        s->mark_temporary(file);
        s->data->movieNumber = s->first_root(file);
    }
    return 1;
}
int three_mm_movie_save_resources(const ThreeMMMovieSaveServices *s)
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
    result = three_mm_movie_autosave(0, 0, s);
    s->release_archive(file);
    return result;
}
int three_mm_movie_save_ensure(void *output, void (*publish)(void *, void *resources),
                               const ThreeMMMovieSaveServices *s)
{
    if (!three_mm_movie_save_resources(s))
        return 0;
    if (publish)
        publish(output, s->data->resources);
    return three_mm_movie_save_temporary(s);
}
static void clean_actors(const ThreeMMMovieSaveServices *s)
{
    ThreeMMI32 i = 0;
    ThreeMMTag tag;
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
int three_mm_movie_autosave(void *path, int clean_rollcall, const ThreeMMMovieSaveServices *s)
{
    void *file, *saved, *retry_path;
    ThreeMMU32 scene = 0, old_scene = 0, actors, old_actors, sources, old_sources;
    int success, retry = 1;
    if (s->has_container(s->context) && s->is_vmm(path))
        return three_mm_movie_save_to(path, 0, s);
    if (!s->data->resources)
        return 0;
    file = s->file(s->data->resources);
    if (!s->state->autosaveDirty && !path && !s->state->closing)
        return 1;
    s->long_op(s->context, 1);
    if (!path && !s->is_temp(file) && !three_mm_movie_save_temporary(s))
        goto fail_end;
retry_save:
    if (s->data->movieNumber == NIL_NUMBER && !s->create_root(s->context, file, &s->data->movieNumber))
        goto fail;
    if (s->scenes->open)
    {
        if (!s->write_scene(s->context, s->scenes->open, &scene))
            goto fail;
        if (!s->child(file, s->data->movieNumber, THREE_MM_SAVE_SCENE, s->scenes->index, &old_scene))
            s->missing_scene(s->context);
        if (!s->adopt(file, s->data->movieNumber, THREE_MM_SAVE_SCENE, scene, s->scenes->index))
        {
            s->delete_chunk(file, THREE_MM_SAVE_SCENE, scene);
            goto fail;
        }
    }
    if (clean_rollcall)
        clean_actors(s);
    if (!s->child(file, s->data->movieNumber, THREE_MM_SAVE_TABLE, 0, &old_actors))
        old_actors = NIL_NUMBER;
    if (!s->write_rollcall(s->context, file, s->data->movieNumber, s->data->actors, &actors))
        goto fail_scene;
    if (!s->child(file, s->data->movieNumber, THREE_MM_SAVE_TABLE, 1, &old_sources))
        old_sources = NIL_NUMBER;
    if (!s->add_sources(s->context, file, s->source_bytes(s->context), &sources))
        goto fail_actors;
    if (!s->write_sources(s->context) || !s->adopt(file, s->data->movieNumber, THREE_MM_SAVE_TABLE, sources, 1))
    {
        s->delete_chunk(file, THREE_MM_SAVE_TABLE, sources);
        goto fail_actors;
    }
    if (!s->write_title(s->context, file))
        goto fail_sources;
    if (s->scenes->open)
        s->delete_child(file, s->data->movieNumber, THREE_MM_SAVE_SCENE, old_scene, s->scenes->index);
    if (old_actors != NIL_NUMBER)
        s->delete_child(file, s->data->movieNumber, THREE_MM_SAVE_TABLE, old_actors, 0);
    if (old_sources != NIL_NUMBER)
        s->delete_child(file, s->data->movieNumber, THREE_MM_SAVE_TABLE, old_sources, 1);
    if (path)
    {
        saved = s->find_file(s->context, path);
        if (saved == s->state->file)
            three_mm_movie_save_release(1, s);
        else
            saved = 0;
        three_mm_movie_save_gc(file, s);
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
    s->delete_child(file, s->data->movieNumber, THREE_MM_SAVE_TABLE, sources, 1);
fail_actors:
    s->delete_child(file, s->data->movieNumber, THREE_MM_SAVE_TABLE, actors, 0);
fail_scene:
    if (s->scenes->open)
        s->delete_child(file, s->data->movieNumber, THREE_MM_SAVE_SCENE, scene, s->scenes->index);
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
int three_mm_movie_save_to(void *path, int remember, const ThreeMMMovieSaveServices *s)
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
    if (s->has_container(s->context) && (s->is_vmm(path) || three_mm_movie_needs_container(s)))
    {
        if (!s->is_vmm(path))
            return s->package(s->context, 0, path);
        working = s->temporary_path(s->context);
        if (!working || !three_mm_movie_autosave(working, 1, s))
            return 0;
        s->data->movieNumber = s->first_root(file);
        s->set_temp(file, 1);
        if (!s->package(s->context, working, path))
        {
            s->update_title(s->context, s->state->containerNamed ? s->container_name(s->context) : 0);
            return 0;
        }
        s->clear_undo(s->context);
        if (remember)
        {
            three_mm_movie_save_bind(working, s);
            s->set_container_name(s->context, path);
            s->state->containerNamed = 1;
            s->state->readOnly = s->read_only(s->context, path);
        }
        s->update_title(s->context, path);
        s->document_dirty(s->context, 0);
        return 1;
    }
    if (!three_mm_movie_autosave(path, 1, s))
        return 0;
    s->clear_undo(s->context);
    s->persistent(file);
    if (remember)
        three_mm_movie_save_bind(path, s);
    s->data->movieNumber = s->first_root(file);
    s->document_dirty(s->context, 0);
    return 1;
}
int three_mm_movie_needs_container(const ThreeMMMovieSaveServices *s)
{
    void *tags;
    ThreeMMI32 i;
    ThreeMMTag tag;
    int portable = 0;
    if (!s->has_container(s->context))
        return 0;
    if (s->state->containerNamed || s->bundled(s->context))
        return 1;
    if (!s->source_archive(s->context, 6) || !s->data->resources)
        return 0;
    if (!three_mm_movie_autosave(0, 0, s))
        return 1;
    tags = three_mm_movie_save_tags(s);
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
void *three_mm_movie_save_tags(const ThreeMMMovieSaveServices *s)
{
    void *file, *tags = s->new_tags(s->context);
    ThreeMMU32 child, scene = 0;
    if (!tags)
        return 0;
    file = s->file(s->data->resources);
    while (s->child(file, s->data->movieNumber, THREE_MM_SAVE_SCENE, scene++, &child))
        if (!s->scene_tags(file, child, tags))
        {
            s->release_tags(tags);
            return 0;
        }
    return tags;
}
int three_mm_movie_save_assets(void *file, const ThreeMMMovieSaveServices *s)
{
    ThreeMMCollectionStorage storage;
    ThreeMMArray doomed;
    ThreeMMU32 chunk[2], i;
    ThreeMMI32 material = 0, template_index = 0, tag_index;
    ThreeMMTag tag;
    int used, result = 1;
    void *tags = three_mm_movie_save_tags(s);
    if (!tags)
        return 1;
    if (three_mm_storage_init(&storage, s->allocator.resize || s->allocator.release ? &s->allocator : 0) !=
        THREE_MM_STORAGE_OK)
    {
        s->release_tags(tags);
        return 0;
    }
    memset(&doomed, 0, sizeof(doomed));
    doomed.storage = &storage;
    doomed.width = sizeof(chunk);
    doomed.free_count = -1;
    for (;;)
    {
        if (s->chunk(file, THREE_MM_SAVE_MATERIAL, material++, &chunk[1]))
            chunk[0] = THREE_MM_SAVE_MATERIAL;
        else if (s->chunk(file, THREE_MM_SAVE_TEMPLATE, template_index++, &chunk[1]))
            chunk[0] = THREE_MM_SAVE_TEMPLATE;
        else
            break;
        used = 0;
        for (tag_index = 0; tag_index < s->tag_count(tags); ++tag_index)
        {
            s->tag(tags, tag_index, &tag);
            if (tag.sid != 0)
                break;
            if (tag.type == chunk[0] && tag.number == chunk[1])
            {
                used = 1;
                break;
            }
        }
        if (!used && three_mm_array_add(&doomed, chunk, 0) != THREE_MM_STORAGE_OK)
        {
            result = 0;
            goto done;
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
    three_mm_storage_dispose(&storage);
    return result;
}
void three_mm_movie_save_sounds(int purge_all, const ThreeMMMovieSaveServices *s)
{
    ThreeMMI32 i;
    ThreeMMChunkChild child;
    void *file;
    if (!s->data->resources || !(file = s->file(s->data->resources)))
        return;
    i = s->child_count(file, s->data->movieNumber);
    while (i--)
    {
        if (!s->child_at(file, s->data->movieNumber, i, &child))
        {
            s->bad_child(s->context);
            break;
        }
        if (child.type != THREE_MM_SAVE_SOUND || s->references(file, THREE_MM_SAVE_SOUND, child.number) > 1)
            continue;
        if (purge_all || !s->sound_child(file, child.number))
            s->delete_child(file, s->data->movieNumber, THREE_MM_SAVE_SOUND, child.number, child.slot);
    }
}
int three_mm_movie_save_gc(void *file, const ThreeMMMovieSaveServices *s)
{
    int valid, result = three_mm_movie_save_assets(file, s);
    if (s->state->closing && s->unused_sounds(s->context, &valid))
        three_mm_movie_save_sounds(!valid || s->purge_sounds(s->context), s);
    return result;
}

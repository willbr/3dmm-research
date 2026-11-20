/* Movie composition converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_compose.h"
#include "mm/movie_rollcall.h"
#include "mm/actor_data.h"
#include "mm/scene_data.h"
#include "mm/wide.h"
#include <string.h>
int mm_composite_scene(const MMCompositeMovie *state, MMU32 index, MMComposeScene *out)
{
    if (index >= state->scene_count)
        return 0;
    memcpy(out, state->rows.buffers[0].data + index * sizeof(*out), sizeof(*out));
    return 1;
}

int mm_composite_movie(const MMCompositeMovie *state, MMU32 index, MMComposeMovie *out)
{
    if (index >= state->movie_count)
        return 0;
    memcpy(out, state->rows.buffers[1].data + index * sizeof(*out), sizeof(*out));
    return 1;
}

static MMArray scene_array(MMCompositeMovie *state)
{
    MMArray array = {NULL, sizeof(MMComposeScene), 0, -1, 0, 0};
    array.storage = &state->rows;
    array.count = state->scene_count;
    return array;
}

static void put_scene(MMCompositeMovie *state, MMU32 index, const MMComposeScene *row)
{
    memcpy(state->rows.buffers[0].data + index * sizeof(*row), row, sizeof(*row));
}

int mm_composite_move_scene(MMCompositeMovie *state, MMU32 from, MMU32 before)
{
    MMArray array = scene_array(state);
    return mm_array_move(&array, from, before) == MM_STORAGE_OK;
}

int mm_composite_remove_scene(MMCompositeMovie *state, MMU32 index)
{
    MMComposeScene scene;
    if (!mm_composite_scene(state, index, &scene) || scene.deleted ||
        !mm_composite_move_scene(state, index, state->scene_count))
        return 0;
    scene.deleted = 1;
    put_scene(state, state->scene_count - 1, &scene);
    return 1;
}

int mm_composite_set_transition(MMCompositeMovie *state, MMU32 index, MMI32 transition)
{
    MMComposeScene scene;
    if (!mm_composite_scene(state, index, &scene))
        return 0;
    scene.transition = transition;
    put_scene(state, index, &scene);
    return 1;
}

void mm_composite_empty(MMCompositeMovie *state, void (*release)(void *))
{
    MMComposeScene scene;
    MMComposeMovie movie;
    while (state->scene_count)
    {
        mm_composite_scene(state, state->scene_count - 1, &scene);
        --state->scene_count;
        release(scene.thumbnail);
    }
    while (state->movie_count)
    {
        mm_composite_movie(state, state->movie_count - 1, &movie);
        --state->movie_count;
        release(movie.resources);
    }
    if (state->rows.allocator.resize)
        mm_storage_dispose(&state->rows);
    memset(state, 0, sizeof(*state));
}

static const MMStorageAllocator *compose_allocator(const MMMovieComposeServices *s)
{
    return s->allocator.resize || s->allocator.release ? &s->allocator : NULL;
}
static int scene_link(void *file, MMU32 movie, MMU32 slot, MMU32 *number, const MMMovieSceneServices *s)
{
    const MMChunkAccess chunks = s->chunks(file);
    return mm_chunk_link_number(&chunks, MM_MOVIE_CHUNK, movie, slot, MM_SCENE_CHUNK, number) == MM_CHUNK_OK;
}

int mm_movie_compose_add(MMCompositeMovie *state, MMI32 *insertion,
                         const MMMovieComposeServices *s)
{
    MMArray array = scene_array(state);
    MMI32 inserted = 0, count = s->data->sceneCount;
    MMU32 position;
    MMComposeScene scene = {0};
    MMComposeMovie movie;
    void *file;
    if (count < 0 || *insertion < 0 || (MMU32)*insertion > state->scene_count ||
        (MMU32)count > (MM_STORAGE_LIMIT - 1) / sizeof(scene) - state->scene_count ||
        state->movie_count >= (MM_STORAGE_LIMIT - 1) / sizeof(movie))
        return 0;
    if ((s->temporary_before_save && !s->use_temporary(s->context)) || !s->autosave(s->context))
        return 0;
    if (!state->rows.allocator.resize && mm_storage_init(&state->rows, compose_allocator(s)) != MM_STORAGE_OK)
        return 0;
    if (mm_storage_ensure(&state->rows, (state->scene_count + (MMU32)count) * sizeof(scene),
                          (state->movie_count + 1) * sizeof(movie), 0) != MM_STORAGE_OK)
        goto fail;
    movie.resources = s->data->resources;
    movie.number = s->data->movieNumber;
    movie.actor_limit = s->data->actorIdLimit;
    file = s->scene_control.file(movie.resources);
    position = (MMU32)*insertion;
    scene.movie = (MMI32)state->movie_count;
    while (inserted < count)
    {
        if (!scene_link(file, movie.number, (MMU32)inserted, &scene.number, &s->scene_control))
        {
            s->diagnostic(s->context, MM_COMPOSE_MISSING_SCENE);
            goto fail;
        }
        scene.child = (MMU32)inserted;
        if (inserted != s->scenes->index)
        {
            if (!s->read_transition(movie.resources, scene.number, &scene.transition))
                goto fail;
            scene.thumbnail = s->read_thumbnail(file, scene.number);
            if (!scene.thumbnail)
                goto fail;
        }
        else
        {
            scene.transition = s->transition(s->scenes->open);
            scene.thumbnail = s->thumbnail(s->scenes->open);
            if (!scene.thumbnail)
                goto fail;
            s->retain(scene.thumbnail);
        }
        if (mm_array_insert(&array, position + (MMU32)inserted, &scene, 1) != MM_STORAGE_OK)
        {
            s->release(scene.thumbnail);
            goto fail;
        }
        ++inserted;
    }
    s->retain(movie.resources);
    memcpy(state->rows.buffers[1].data + state->movie_count * sizeof(movie), &movie, sizeof(movie));
    ++state->movie_count;
    state->scene_count = array.count;
    *insertion += inserted;
    return 1;
fail:
    while (inserted > 0)
    {
        --inserted;
        position = (MMU32)(*insertion + inserted);
        memcpy(&scene, state->rows.buffers[0].data + position * sizeof(scene), sizeof(scene));
        s->release(scene.thumbnail);
    }
    if (array.count > state->scene_count)
        mm_array_delete(&array, (MMU32)*insertion, array.count - state->scene_count);
    if (!state->movie_count)
        mm_composite_empty(state, s->release);
    return 0;
}
typedef struct ComposeReference
{
    MMU32 type, number;
} ComposeReference;

static int remap(MMU32 movie, MMI32 offset, void *source, const MMMovieComposeServices *s)
{
    MMChunkWalk walk;
    MMCollectionStorage visited;
    MMArray ids;
    MMChunkChild node, parent;
    ComposeReference reference;
    MMU32 flags, i;
    int actor, background;
    void *destination = s->scene_control.file(s->data->resources);
    int ok = 0;
    if (mm_storage_init(&visited, compose_allocator(s)) != MM_STORAGE_OK)
        return 0;
    memset(&ids, 0, sizeof(ids));
    ids.storage = &visited;
    ids.width = sizeof(reference);
    ids.free_count = -1;
    if (mm_chunk_walk_init(&walk, compose_allocator(s)) != MM_CHUNK_OK)
        goto storage;
    if (mm_chunk_walk_reset(&walk, &s->graph, 0x4d564945U, movie) != MM_CHUNK_OK)
        goto done;
    while (mm_chunk_walk_next(&walk, 0, &node, &parent, &flags))
    {
        if (source && (flags & MM_WALK_ERROR))
            goto done;
        actor = node.type == MM_ACTOR_CHUNK;
        background = node.type == MM_SCENE_CHUNK && source && s->rebind_scene_backgrounds;
        if (!(flags & MM_WALK_PRE) || (!actor && !background))
            continue;
        for (i = 0; i < ids.count; ++i)
        {
            memcpy(&reference, visited.buffers[0].data + (size_t)i * sizeof(reference), sizeof(reference));
            if (reference.type == node.type && reference.number == node.number)
                break;
        }
        if (i < ids.count)
            continue;
        reference.type = node.type;
        reference.number = node.number;
        if (mm_array_add(&ids, &reference, 0) != MM_STORAGE_OK ||
            (background && !s->rebind_scene_backgrounds(source, destination, node.number)) ||
            (actor && offset > 0 && !s->adjust_actor(destination, node.number, offset)) ||
            (actor && source && s->rebind_actor_materials &&
             !s->rebind_actor_materials(source, destination, node.number)))
            goto done;
    }
    ok = 1;
done:
    mm_chunk_walk_dispose(&walk);
storage:
    mm_storage_dispose(&visited);
    return ok;
}
static void close_source_row(MMRollCallTable *source, MMU32 index, const MMMovieComposeServices *s)
{
    MMRollCallActor row;
    MMTag tag;
    if (mm_rollcall_table_extra(source, index, &row))
    {
        tag = mm_movie_rollcall_tag(&row);
        s->close_tag(&tag);
    }
}
static int merge(MMU32 movie, MMI32 offset, void *source_file, const MMMovieComposeServices *s)
{
    MMRollCallTable source, *destination = &s->data->actors;
    MMRollCallActor row;
    const void *name;
    MMU32 count, appended = 0, i;
    int ok = 0;
    if (destination->stride != sizeof(row) + 4 ||
        mm_rollcall_table_init(&source, sizeof(row), destination->unit, destination->encoding,
                               destination->trash_unused, &destination->storage.allocator) != MM_STORAGE_OK)
        return 0;
    if (!s->read_rollcall(s->data->resources, movie, &source))
        goto done;
    for (; appended < source.count; ++appended)
    {
        if (!mm_rollcall_table_extra(&source, appended, &row) ||
            !mm_rollcall_table_name(&source, appended, &name, &count))
            goto fail;
        row.data.actor_id = mm_signed_word((MMU32)row.data.actor_id + (MMU32)offset);
        row.data.reference_count = 0;
        if (!mm_rollcall_table_add(destination, name, count, &row))
            goto fail;
    }
    if ((offset > 0 || source_file) && !remap(movie, offset, source_file, s))
        goto fail;
    ok = 1;
    goto done;
fail:
    for (i = appended; i > 0;)
    {
        close_source_row(&source, --i, s);
        mm_rollcall_table_delete(destination, destination->count - 1);
    }
    for (i = source.count; i > appended;)
        close_source_row(&source, --i, s);
done:
    mm_rollcall_table_dispose(&source);
    return ok;
}

int mm_movie_compose_merge(MMU32 movie, MMI32 offset, const MMMovieComposeServices *s)
{
    return merge(movie, offset, NULL, s);
}
int mm_movie_compose_apply(const MMCompositeMovie *state, const MMMovieComposeServices *s)
{
    const MMMovieSceneServices *scenes = &s->scene_control;
    MMCollectionStorage cloned;
    MMI32 old = s->scenes->index, offset = 0;
    MMU32 i, copied = 0, slot = 0, number, current_slot, *roots = NULL;
    void *resources = s->data->resources, *file, *scene;
    MMComposeMovie movie;
    MMComposeScene descriptor;
    int ok = 0;
    if (!state->movie_count || mm_storage_init(&cloned, compose_allocator(s)) != MM_STORAGE_OK)
        return 0;
    file = scenes->file(resources);
    if (mm_storage_ensure(&cloned, (state->movie_count - 1) * sizeof(*roots), 0, 0) != MM_STORAGE_OK)
        goto done;
    roots = (MMU32 *)cloned.buffers[0].data;
    for (i = 0; i < state->movie_count; ++i)
    {
        mm_composite_movie(state, i, &movie);
        if (i > 0)
        {
            void *source = scenes->file(movie.resources);
            if (!s->clone_movie(source, movie.number, file, &movie.number))
                goto done;
            if ((s->link_imported_sounds && !s->link_imported_sounds(file, movie.number)) ||
                !merge(movie.number, offset, s->rebind_actor_materials || s->rebind_scene_backgrounds ? source : NULL, s))
            {
                s->delete_movie(file, movie.number);
                goto done;
            }
            s->retain(resources);
            roots[copied++] = movie.number;
        }
        else if (movie.resources != resources || movie.number != s->data->movieNumber)
            s->diagnostic(s->context, MM_COMPOSE_BAD_BASE);
        offset = mm_signed_word((MMU32)offset + (MMU32)movie.actor_limit);
    }
    for (i = 0; i < state->scene_count; ++i)
    {
        MMChunkChild link;
        mm_composite_scene(state, i, &descriptor);
        link.type = MM_SCENE_CHUNK;
        current_slot = slot;
        number = 0xffffffffU;
        if (descriptor.movie > 0)
        {
            if (!scene_link(file, roots[descriptor.movie - 1], descriptor.child, &number, scenes))
                goto done;
            if (!descriptor.deleted)
            {
                link.number = number;
                link.slot = slot++;
                if (!scenes->adopt(file, MM_MOVIE_CHUNK, s->data->movieNumber, &link))
                    goto done;
                if (!s->adopt_sounds(s->context, file, number))
                    goto done;
                scene = scenes->read(scenes->context, resources, number, NULL);
                if (!scene || !s->start_scene(scene) || !s->add_actors(scene))
                    s->diagnostic(s->context, MM_COMPOSE_IMPORT_ACTORS);
                s->actor_tools(s->context);
                s->text_tools(s->context);
                if (scene)
                    s->release(scene);
            }
        }
        else if (descriptor.deleted)
        {
            if (scenes->preserve_scene_assets && !scenes->preserve_scene_assets(file, descriptor.number))
                goto done;
            if (descriptor.child != (MMU32)old)
            {
                scene = scenes->read(scenes->context, resources, descriptor.number, NULL);
                if (!scene || !s->start_scene(scene))
                    s->diagnostic(s->context, MM_COMPOSE_REMOVE_ACTORS);
            }
            else
            {
                scene = s->scenes->open;
                s->retain(scene);
            }
            if (scene)
            {
                s->remove_actors(scene);
                s->release(scene);
            }
            link.number = descriptor.number;
            link.slot = descriptor.child;
            scenes->delete_child(file, MM_MOVIE_CHUNK, s->data->movieNumber, &link);
        }
        else
        {
            number = descriptor.number;
            link.number = number;
            link.slot = descriptor.child;
            scenes->move_child(file, MM_MOVIE_CHUNK, s->data->movieNumber, &link, slot++);
        }
        if (descriptor.child != (MMU32)old || descriptor.movie != 0)
        {
            if (!descriptor.deleted && !s->write_transition(resources, number, descriptor.transition))
                goto done;
        }
        else if (descriptor.deleted)
            mm_movie_scene_close_open(&s->scene_control);
        else
        {
            s->set_transition(s->scenes->open, descriptor.transition);
            s->scenes->index = mm_signed_word(current_slot);
        }
    }
    s->data->sceneCount = mm_signed_word(slot);
    s->data->actorIdLimit = offset;
    s->dirty(s->context);
    if (!s->data->sceneCount)
        s->scene_nuked(s->context);
    s->invalidate(s->context);
    s->rollcall_changed(s->context);
    if (s->validate_children)
        s->validate_children(s->context);
    ok = 1;
done:
    while (copied)
    {
        s->delete_movie(file, roots[--copied]);
        s->release(resources);
    }
    mm_storage_dispose(&cloned);
    return ok;
}

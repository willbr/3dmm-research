/* Movie composition converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/movie_compose.h"
#include "three_mm/movie_rollcall.h"
#include "three_mm/wide.h"
#include <string.h>
int three_mm_composite_insert_scene(void *list, ThreeMMI32 index, const ThreeMMComposeScene *scene,
                                    const ThreeMMMovieComposeServices *s)
{
    if (!s->insert_scene(list, index, scene))
        return 0;
    s->retain(scene->thumbnail);
    return 1;
}
void three_mm_composite_delete_scene(void *list, ThreeMMI32 index, const ThreeMMMovieComposeServices *s)
{
    ThreeMMComposeScene scene;
    s->scene(list, index, &scene);
    s->erase(list, index);
    s->release(scene.thumbnail);
}
void three_mm_composite_empty(ThreeMMCompositeMovie *state, const ThreeMMMovieComposeServices *s)
{
    ThreeMMI32 i;
    ThreeMMComposeScene scene;
    ThreeMMComposeMovie movie;
    if (state->scenes)
    {
        i = s->count(state->scenes);
        while (i-- > 0)
        {
            s->scene(state->scenes, i, &scene);
            if (scene.thumbnail)
                s->release(scene.thumbnail);
        }
        s->release(state->scenes);
        state->scenes = 0;
    }
    if (state->movies)
    {
        i = s->count(state->movies);
        while (i-- > 0)
        {
            s->movie(state->movies, i, &movie);
            if (movie.resources)
                s->release(movie.resources);
        }
        s->release(state->movies);
        state->movies = 0;
    }
}
int three_mm_movie_compose_add(ThreeMMCompositeMovie *state, ThreeMMI32 *insertion,
                               const ThreeMMMovieComposeServices *s)
{
    ThreeMMI32 inserted = 0, index = -1, count = s->data->sceneCount;
    ThreeMMComposeScene scene;
    ThreeMMComposeMovie movie;
    void *file;
    if (s->temporary_before_save && !s->use_temporary(s->context))
        goto fail;
    if (!s->autosave(s->context))
        goto fail;
    if (!state->scenes && !(state->scenes = s->new_list(0)))
        goto fail;
    if (!state->movies && !(state->movies = s->new_list(1)))
        goto fail;
    movie.resources = s->data->resources;
    movie.number = s->data->movieNumber;
    movie.actor_limit = s->data->actorIdLimit;
    if (!s->append_movie(state->movies, &movie, &index))
        goto fail;
    s->retain(movie.resources);
    file = s->file(movie.resources);
    memset(&scene, 0, sizeof(scene));
    scene.movie = index;
    while (inserted < count)
    {
        if (!s->scene_child(file, s->data->movieNumber, (ThreeMMU32)inserted, &scene.number))
        {
            s->diagnostic(s->context, THREE_MM_COMPOSE_MISSING_SCENE);
            goto fail;
        }
        scene.child = (ThreeMMU32)inserted;
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
        if (!three_mm_composite_insert_scene(state->scenes, *insertion, &scene, s))
        {
            s->release(scene.thumbnail);
            goto fail;
        }
        s->release(scene.thumbnail);
        ++inserted;
        *insertion = three_mm_signed_word((ThreeMMU32)*insertion + 1U);
    }
    return 1;
fail:
    while (inserted-- > 0)
    {
        *insertion = three_mm_signed_word((ThreeMMU32)*insertion - 1U);
        three_mm_composite_delete_scene(state->scenes, *insertion, s);
    }
    if (state->scenes && !s->count(state->scenes))
    {
        s->release(state->scenes);
        state->scenes = 0;
    }
    if (index != -1)
    {
        s->release(movie.resources);
        s->erase(state->movies, index);
    }
    if (state->movies && !s->count(state->movies))
    {
        s->release(state->movies);
        state->movies = 0;
    }
    return 0;
}
static int remap(ThreeMMU32 movie, ThreeMMI32 offset, const ThreeMMMovieComposeServices *s)
{
    ThreeMMChunkWalk walk;
    ThreeMMCollectionStorage visited;
    ThreeMMArray ids;
    ThreeMMChunkChild node, parent;
    ThreeMMU32 flags, i, number;
    int ok = 0;
    if (three_mm_storage_init(&visited, &s->allocator) != THREE_MM_STORAGE_OK)
        return 0;
    memset(&ids, 0, sizeof(ids));
    ids.storage = &visited;
    ids.width = 4;
    ids.free_count = -1;
    if (three_mm_chunk_walk_init(&walk, &s->allocator) != THREE_MM_CHUNK_OK)
        goto storage;
    if (three_mm_chunk_walk_reset(&walk, &s->graph, 0x4d564945U, movie) != THREE_MM_CHUNK_OK)
        goto done;
    while (three_mm_chunk_walk_next(&walk, 0, &node, &parent, &flags))
    {
        if (!(flags & THREE_MM_WALK_PRE) || node.type != 0x41435452U)
            continue;
        for (i = 0; i < ids.count; ++i)
        {
            memcpy(&number, visited.buffers[0].data + (size_t)i * 4, 4);
            if (number == node.number)
                break;
        }
        if (i < ids.count)
            continue;
        if (three_mm_array_add(&ids, &node.number, 0) != THREE_MM_STORAGE_OK ||
            !s->adjust_actor(s->file(s->data->resources), node.number, offset))
            goto done;
    }
    ok = 1;
done:
    three_mm_chunk_walk_dispose(&walk);
storage:
    three_mm_storage_dispose(&visited);
    return ok;
}
int three_mm_movie_compose_merge(ThreeMMU32 movie, ThreeMMI32 offset, const ThreeMMMovieComposeServices *s)
{
    void *source = s->read_rollcall(s->data->resources, movie), *destination = s->data->actors;
    ThreeMMI32 i = 0, count, last;
    ThreeMMRollCallActor row;
    ThreeMMTag tag;
    if (!source)
        return 0;
    count = s->actor_count(source);
    for (i = 0; i < count; ++i)
    {
        s->actor(source, i, &row);
        row.data.actor_id = three_mm_signed_word((ThreeMMU32)row.data.actor_id + (ThreeMMU32)offset);
        row.data.reference_count = 0;
        if (!s->append_actor(destination, source, i, &row))
            goto fail;
    }
    if (offset > 0 && !remap(movie, offset, s))
        goto fail;
    s->release(source);
    return 1;
fail:
    last = s->actor_count(destination);
    while (i-- > 0)
    {
        s->actor(destination, --last, &row);
        tag = three_mm_movie_rollcall_tag(&row);
        s->close_tag(&tag);
        s->erase_actor(destination, last);
        s->erase_actor(source, i);
    }
    i = s->actor_count(source);
    while (i-- > 0)
    {
        s->actor(source, i, &row);
        tag = three_mm_movie_rollcall_tag(&row);
        s->close_tag(&tag);
    }
    s->release(source);
    return 0;
}
int three_mm_movie_compose_apply(const ThreeMMCompositeMovie *state, const ThreeMMMovieComposeServices *s)
{
    ThreeMMI32 i, copied = 1, movie_count = s->count(state->movies), scene_count = s->count(state->scenes),
                  old = s->scenes->index, offset = 0;
    ThreeMMU32 slot = 0, number, current_slot;
    void *resources = s->data->resources, *file = s->file(resources), *movies = s->duplicate_movies(state->movies),
         *scene;
    ThreeMMComposeMovie movie;
    ThreeMMComposeScene descriptor;
    int ok = 0;
    if (!movies)
        return 0;
    for (i = 0; i < movie_count; ++i)
    {
        s->movie(movies, i, &movie);
        if (i > 0)
        {
            if (!s->clone_movie(s->file(movie.resources), movie.number, file, &movie.number))
                goto done;
            if (!three_mm_movie_compose_merge(movie.number, offset, s))
            {
                s->delete_movie(file, movie.number);
                goto done;
            }
            movie.resources = resources;
            s->retain(resources);
            s->put_movie(movies, i, &movie);
            copied = i + 1;
        }
        else if (movie.resources != resources || movie.number != s->data->movieNumber)
            s->diagnostic(s->context, THREE_MM_COMPOSE_BAD_BASE);
        offset = three_mm_signed_word((ThreeMMU32)offset + (ThreeMMU32)movie.actor_limit);
    }
    for (i = 0; i < scene_count; ++i)
    {
        s->scene(state->scenes, i, &descriptor);
        s->movie(movies, descriptor.movie, &movie);
        current_slot = slot;
        number = 0xffffffffU;
        if (descriptor.movie > 0)
        {
            if (!s->scene_child(file, movie.number, descriptor.child, &number))
                goto done;
            if (!descriptor.deleted)
            {
                if (!s->adopt_scene(file, s->data->movieNumber, number, slot++))
                    goto done;
                if (!s->adopt_sounds(s->context, file, number))
                    goto done;
                scene = s->read_scene(s->context, resources, number);
                if (!scene || !s->start_scene(scene) || !s->add_actors(scene))
                    s->diagnostic(s->context, THREE_MM_COMPOSE_IMPORT_ACTORS);
                s->actor_tools(s->context);
                s->text_tools(s->context);
                if (scene)
                    s->release(scene);
            }
        }
        else if (descriptor.deleted)
        {
            if (descriptor.child != (ThreeMMU32)old)
            {
                scene = s->read_scene(s->context, resources, descriptor.number);
                if (!scene || !s->start_scene(scene))
                    s->diagnostic(s->context, THREE_MM_COMPOSE_REMOVE_ACTORS);
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
            s->delete_scene_child(file, s->data->movieNumber, descriptor.number, descriptor.child);
        }
        else
        {
            number = descriptor.number;
            s->change_child(file, s->data->movieNumber, number, descriptor.child, slot++);
        }
        if (descriptor.child != (ThreeMMU32)old || descriptor.movie != 0)
        {
            if (!descriptor.deleted && !s->write_transition(resources, number, descriptor.transition))
                goto done;
        }
        else if (descriptor.deleted)
            three_mm_movie_scene_close_open(&s->scene_control);
        else
        {
            s->set_transition(s->scenes->open, descriptor.transition);
            s->scenes->index = three_mm_signed_word(current_slot);
        }
    }
    s->data->sceneCount = three_mm_signed_word(slot);
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
    while (copied-- > 1)
    {
        s->movie(movies, copied, &movie);
        s->delete_movie(file, movie.number);
        s->release(movie.resources);
    }
    s->release(movies);
    return ok;
}

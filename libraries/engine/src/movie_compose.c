/* Movie composition converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_compose.h"
#include "mm/chunk_copy.h"
#include "mm/movie_archive_edit.h"
#include "mm/movie_rollcall.h"
#include "mm/movie_session.h"
#include "mm/movie_sound_archive.h"
#include "mm/movie_sound.h"
#include "mm/actor_archive.h"
#include "mm/actor_data.h"
#include "mm/scene_data.h"
#include "mm/scene_thumbnail.h"
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

void mm_composite_empty(MMCompositeMovie *state, void (*release)(void *), void (*release_thumbnail)(void *))
{
    MMComposeScene scene;
    MMComposeMovie movie;
    while (state->scene_count)
    {
        mm_composite_scene(state, state->scene_count - 1, &scene);
        --state->scene_count;
        release_thumbnail(scene.thumbnail);
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
static int scene_link(const MMChunkEdit *edit, MMU32 movie, MMU32 slot, MMU32 *number)
{
    const MMChunkAccess chunks = mm_chunk_edit_access(edit);
    return mm_chunk_link_number(&chunks, MM_MOVIE_CHUNK, movie, slot, MM_SCENE_CHUNK, number) == MM_CHUNK_OK;
}

typedef struct ComposeThumbnail
{
    const MMMovieComposeServices *services;
    void *bitmap;
} ComposeThumbnail;

static int receive_thumbnail(void *context, const MMReadStream *stream)
{
    ComposeThumbnail *thumbnail = context;
    const MMMovieComposeServices *s = thumbnail->services;
    thumbnail->bitmap = s->decode_thumbnail(s->context, stream);
    return thumbnail->bitmap != NULL;
}

static void *read_thumbnail(void *file, MMU32 scene, const MMArchiveWriter *archive, const MMMovieComposeServices *s)
{
    const MMChunkAccess chunks = mm_chunk_edit_access(&archive->edit);
    ComposeThumbnail thumbnail;
    MMU32 number;
    int decoded;
    if (mm_scene_thumbnail_link(&chunks, scene, &number) != MM_CHUNK_OK)
    {
        s->diagnostic(s->context, MM_COMPOSE_MISSING_THUMBNAIL);
        return NULL;
    }
    thumbnail.services = s;
    thumbnail.bitmap = NULL;
    mm_chunk_read_payload(archive->payloads, file, MM_SCENE_THUMBNAIL_TYPE, number,
                          receive_thumbnail, &thumbnail, &decoded);
    return thumbnail.bitmap;
}

int mm_movie_compose_add(MMCompositeMovie *state, MMI32 *insertion,
                         const MMMovieComposeServices *s)
{
    MMArray array = scene_array(state);
    MMI32 inserted = 0, count = s->data->sceneCount;
    MMU32 position;
    MMComposeScene scene = {0};
    MMComposeMovie movie;
    MMChunkEdit edit;
    MMArchiveWriter archive;
    MMSceneIO io;
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
    if (count)
    {
        file = s->archive_context(movie.resources);
        edit = s->scene_control.edit(movie.resources);
        archive = s->archive(movie.resources);
        io = mm_scene_file_io(file, &archive, &s->titles);
    }
    position = (MMU32)*insertion;
    scene.movie = (MMI32)state->movie_count;
    while (inserted < count)
    {
        if (!scene_link(&edit, movie.number, (MMU32)inserted, &scene.number))
        {
            s->diagnostic(s->context, MM_COMPOSE_MISSING_SCENE);
            goto fail;
        }
        scene.child = (MMU32)inserted;
        if (inserted != s->scenes->index)
        {
            if (!mm_scene_transition_read(scene.number, s->data->title.unit, &scene.transition, &io))
                goto fail;
            scene.thumbnail = read_thumbnail(file, scene.number, &archive, s);
            if (!scene.thumbnail)
                goto fail;
        }
        else
        {
            scene.transition = s->transition(s->scenes->open);
            scene.thumbnail = s->thumbnail(s->scenes->open);
            if (!scene.thumbnail)
                goto fail;
            s->retain_thumbnail(scene.thumbnail);
        }
        if (mm_array_insert(&array, position + (MMU32)inserted, &scene, 1) != MM_STORAGE_OK)
        {
            s->release_thumbnail(scene.thumbnail);
            goto fail;
        }
        ++inserted;
    }
    s->retain_cache(movie.resources);
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
        s->release_thumbnail(scene.thumbnail);
    }
    if (array.count > state->scene_count)
        mm_array_delete(&array, (MMU32)*insertion, array.count - state->scene_count);
    if (!state->movie_count)
        mm_composite_empty(state, s->release_cache, s->release_thumbnail);
    return 0;
}
typedef struct ComposeReference
{
    MMU32 type, number;
} ComposeReference;

typedef struct ComposeAssets
{
    MMChunkEndpoint source, destination;
} ComposeAssets;

static int copy_reference(void *context, const MMTag *tag, MMU32 *copied, int *created)
{
    const ComposeAssets *assets = context;
    return mm_chunk_copy_reference(&assets->source, &assets->destination, tag->type, tag->number,
                                   copied, created) == MM_CHUNK_OK;
}

static int rebind(const MMChunkChild *node, const MMArchiveWriter *archive,
                   const MMMovieEmbeddedArchive *assets, void *destination, const MMMovieComposeServices *s)
{
    MMMovieArchiveFile file;
    MMMovieArchiveEditor editor;
    int ok;
    if (!mm_movie_archive_file_init(&file, destination, archive, &s->titles))
        return 0;
    editor = mm_movie_archive_file_editor(&file, s->data->title.unit, s->rollcall.reader.convert);
    ok = node->type == MM_SCENE_CHUNK ? mm_movie_archive_rebind_backgrounds(&editor, assets, node->number)
                                     : mm_actor_archive_rebind_materials(&editor, node->number, assets);
    mm_movie_archive_file_clear(&file);
    return ok;
}

static int remap(MMU32 movie, MMI32 offset, ComposeAssets *imports, const MMMovieComposeServices *s)
{
    MMChunkWalk walk;
    MMCollectionStorage visited;
    MMArray ids;
    MMChunkChild node, parent;
    ComposeReference reference;
    MMU32 flags, i;
    int actor, background;
    void *resources = s->data->resources;
    void *destination = s->archive_context(resources);
    const MMArchiveWriter archive = s->archive(resources);
    const MMChunkGraph graph = mm_chunk_edit_graph(&archive.edit);
    MMMovieEmbeddedArchive assets = {0};
    MMMovieSessionGraph reader;
    int ok = 0;
    assets.context = imports;
    assets.copy_reference = copy_reference;
    assets.edit = archive.edit;
    if (offset > 0)
        mm_movie_bound_graph(resources, &reader, &s->scene_control.session);
    if (mm_storage_init(&visited, compose_allocator(s)) != MM_STORAGE_OK)
        return 0;
    memset(&ids, 0, sizeof(ids));
    ids.storage = &visited;
    ids.width = sizeof(reference);
    ids.free_count = -1;
    if (mm_chunk_walk_init(&walk, compose_allocator(s)) != MM_CHUNK_OK)
        goto storage;
    if (mm_chunk_walk_reset(&walk, &graph, MM_MOVIE_CHUNK, movie) != MM_CHUNK_OK)
        goto done;
    while (mm_chunk_walk_next(&walk, 0, &node, &parent, &flags))
    {
        if (imports && (flags & MM_WALK_ERROR))
            goto done;
        actor = node.type == MM_ACTOR_CHUNK;
        background = node.type == MM_SCENE_CHUNK && imports;
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
            (actor && offset > 0 && !mm_actor_adjust_id(node.number, offset, &reader, &archive)) ||
            (imports && !rebind(&node, &archive, &assets, destination, s)))
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
    if (mm_rollcall_table_extra(source, index, &row))
    {
        MMTag tag = mm_movie_rollcall_tag(&row);
        mm_tag_close(&tag, s->rollcall.reader.tags);
    }
}
static int merge(MMU32 movie, MMI32 offset, ComposeAssets *imports, const MMMovieComposeServices *s)
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
    if (!mm_movie_load_bound_rollcall(s->data->resources, movie, &source, NULL, &s->rollcall))
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
    if ((offset > 0 || imports) && !remap(movie, offset, imports, s))
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
static void delete_root(const MMChunkEdit *edit, MMU32 number, const MMMovieSceneServices *scenes)
{
    if (mm_chunk_delete(edit, MM_MOVIE_CHUNK, number) != MM_CHUNK_OK && scenes->archive_issue)
        scenes->archive_issue(scenes->context, MM_MOVIE_ARCHIVE_DELETE_FAILED);
}
static void validate_scenes(const MMChunkEdit *edit, const MMMovieComposeServices *s)
{
    const MMChunkAccess chunks = mm_chunk_edit_access(edit);
    MMChunkEntry root;
    MMChunkChild child;
    MMU32 row, i, previous = 0xffffffffUL;
    if (mm_chunk_find(&chunks, MM_MOVIE_CHUNK, s->data->movieNumber, &row) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, row, &root) != MM_CHUNK_OK)
        return;
    for (i = 0; i < root.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, row, i, &child) != MM_CHUNK_OK)
        {
            s->diagnostic(s->context, MM_COMPOSE_SCENE_ENUM);
            break;
        }
        if (child.type == MM_SCENE_CHUNK)
        {
            if (previous != 0xffffffffUL && child.slot <= previous)
                s->diagnostic(s->context, MM_COMPOSE_SCENE_ORDER);
            previous = child.slot;
        }
    }
}

int mm_movie_compose_apply(const MMCompositeMovie *state, const MMMovieComposeServices *s)
{
    const MMMovieSceneServices *scenes = &s->scene_control;
    MMChunkEdit edit;
    MMArchiveWriter archive;
    MMSceneIO io;
    MMCollectionStorage cloned;
    MMI32 old = s->scenes->index, offset = 0;
    MMU32 i, copied = 0, slot = 0, number, current_slot, *roots = NULL;
    void *resources = s->data->resources, *file, *scene;
    MMComposeMovie movie;
    MMComposeScene descriptor;
    int status, ok = 0;
    if (!state->movie_count || mm_storage_init(&cloned, compose_allocator(s)) != MM_STORAGE_OK)
        return 0;
    file = s->archive_context(resources);
    edit = scenes->edit(resources);
    archive = s->archive(resources);
    io = mm_scene_file_io(file, &archive, &s->titles);
    if (mm_storage_ensure(&cloned, (state->movie_count - 1) * sizeof(*roots), 0, 0) != MM_STORAGE_OK)
        goto done;
    roots = (MMU32 *)cloned.buffers[0].data;
    for (i = 0; i < state->movie_count; ++i)
    {
        mm_composite_movie(state, i, &movie);
        if (i > 0)
        {
            void *source = s->archive_context(movie.resources);
            const MMArchiveWriter source_archive = s->archive(movie.resources);
            MMChunkFileCopy source_binding, destination_binding;
            ComposeAssets imports;
            MMMovieSessionGraph graph;
            imports.source = mm_chunk_file_copy_endpoint(&source_binding, source, &source_archive,
                                                         &s->titles, s->data->title.unit, 1);
            imports.destination = mm_chunk_file_copy_endpoint(&destination_binding, file, &archive,
                                                              &s->titles, s->data->title.unit, 1);
            status = mm_chunk_copy(&imports.source, &imports.destination, MM_MOVIE_CHUNK, movie.number, 1, &movie.number);
            if (status != MM_CHUNK_OK)
            {
                if (status == MM_CHUNK_NOT_FOUND)
                    s->diagnostic(s->context, MM_COMPOSE_MISSING_COPY_ROOT);
                goto done;
            }
            mm_movie_bound_graph(resources, &graph, &scenes->session);
            if (!mm_movie_archive_link_imported_sounds(&graph, movie.number, &edit) ||
                !merge(movie.number, offset, &imports, s))
            {
                delete_root(&edit, movie.number, scenes);
                goto done;
            }
            s->retain_cache(resources);
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
            if (!scene_link(&edit, roots[descriptor.movie - 1], descriptor.child, &number))
                goto done;
            if (!descriptor.deleted)
            {
                link.number = number;
                link.slot = slot++;
                status = mm_chunk_adopt(&edit, MM_MOVIE_CHUNK, s->data->movieNumber, &link, 1);
                if (status != MM_CHUNK_OK)
                {
                    if (status == MM_CHUNK_NOT_FOUND && scenes->archive_issue)
                        scenes->archive_issue(scenes->context, MM_MOVIE_ARCHIVE_MISSING_CHILD);
                    goto done;
                }
                if (!mm_movie_sound_adopt_scene(&edit, s->data->movieNumber, number, s->context, s->sound_issue))
                    goto done;
                scene = scenes->read(scenes->context, resources, number, NULL);
                if (!scene || !s->start_scene(scene) || !s->add_actors(scene))
                    s->diagnostic(s->context, MM_COMPOSE_IMPORT_ACTORS);
                s->actor_tools(s->context);
                s->text_tools(s->context);
                if (scene)
                    s->release_scene(scene);
            }
        }
        else if (descriptor.deleted)
        {
            if (!mm_movie_archive_preserve_scene_assets(&edit, descriptor.number))
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
                s->retain_scene(scene);
            }
            if (scene)
            {
                s->remove_actors(scene);
                s->release_scene(scene);
            }
            link.number = descriptor.number;
            link.slot = descriptor.child;
            if (mm_chunk_unlink(&edit, MM_MOVIE_CHUNK, s->data->movieNumber, &link) != MM_CHUNK_OK &&
                scenes->archive_issue)
                scenes->archive_issue(scenes->context, MM_MOVIE_ARCHIVE_UNLINK_FAILED);
        }
        else
        {
            number = descriptor.number;
            link.number = number;
            link.slot = descriptor.child;
            if (mm_chunk_change_slot(&edit, MM_MOVIE_CHUNK, s->data->movieNumber, &link, slot++) != MM_CHUNK_OK &&
                scenes->archive_issue)
                scenes->archive_issue(scenes->context, MM_MOVIE_ARCHIVE_MOVE_FAILED);
        }
        if (descriptor.child != (MMU32)old || descriptor.movie != 0)
        {
            if (!descriptor.deleted && !mm_scene_transition_write(number, s->data->title.unit, descriptor.transition, &io))
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
        validate_scenes(&edit, s);
    ok = 1;
done:
    while (copied)
    {
        delete_root(&edit, roots[--copied], scenes);
        s->release_cache(resources);
    }
    mm_storage_dispose(&cloned);
    return ok;
}

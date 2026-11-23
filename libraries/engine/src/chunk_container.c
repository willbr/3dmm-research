/* Container construction converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_container.h"
#include "mm/chunk_context.h"
#include "mm/chunk_owner.h"
#include "mm/file_ops.h"
#include <string.h>
void *mm_chunk_construct_forest(const MMForestConstruction *s, void *source, MMU32 offset,
                                      MMU32 bytes, int copy)
{
    const MMChunkLink *link;
    MMChunkOwnerRead *reader;
    MMChunkOwnerStorage *storage;
    MMChunkOwner *owner;
    if (!s || !source || !s->allocate || !s->release || !s->read || offset > 0x7fffffffUL ||
        bytes > 0x7fffffffUL - offset)
        return NULL;
    link = s->allocate(s->context);
    if (!link)
        return NULL;
    reader = link->reader;
    storage = reader->storage;
    owner = storage->owner;
    if (mm_chunk_reader_create_index(reader) != MM_CHUNK_OK)
        goto fail;
    if (copy)
    {
        owner->files.main_file = storage->io.create_extra(storage->io.context);
        if (!owner->files.main_file || !storage->io.resize(owner->files.main_file, MM_CHUNK_PREFIX_BYTES))
            goto fail;
        owner->files.main_end = MM_CHUNK_PREFIX_BYTES;
    }
    else
    {
        const MMBlockFileServices *blocks = reader->payloads.files;
        blocks->retain(blocks->context, source);
        owner->files.main_file = source;
        owner->files.main_end = offset + bytes;
        owner->files.invalid_main = 1;
        owner->add_to_extra = 1;
    }
    if (!s->read(s->context, link->owner, source, offset, bytes, copy != 0))
        goto fail;
    if (s->finish)
        s->finish(s->context, link->owner);
    return link->owner;
fail:
    s->release(s->context, link->owner);
    return NULL;
}
static void issue(const MMChunkConstruction *services, int kind)
{
    if (services->issue)
        services->issue(services->context, kind);
}
void *mm_chunk_construct(const MMChunkConstruction *services, void *name, MMU32 flags, int create)
{
    const MMRegisteredFile *file;
    const MMChunkLink *link;
    MMChunkOwnerRead *reader;
    MMChunkOwnerStorage *storage;
    int error = create ? MM_CONTAINER_CREATE_FAILED : MM_CONTAINER_OPEN_FAILED;
    if (!services || !services->files || !services->archives || !services->allocate || !services->retain ||
        !services->release)
        return NULL;
    if (create)
        flags |= MM_CHUNK_WRITE;
    file = mm_file_ops_find(services->files, name);
    link = file ? mm_chunk_context_find(services->archives, file->object, services->extra_collision) : NULL;
    if (link)
    {
        if (create)
        {
            issue(services, MM_CONTAINER_ALREADY_OPEN);
            issue(services, error);
            return NULL;
        }
        if (mm_chunk_owner_registered_options(link->reader->storage, file, flags, 0xffffffffUL) != MM_CHUNK_OK)
            return NULL;
        services->retain(services->context, link->owner);
        return link->owner;
    }
    link = services->allocate(services->context);
    if (!link)
        goto fail;
    reader = link->reader;
    storage = reader->storage;
    if (create && mm_chunk_reader_create_index(reader) != MM_CHUNK_OK)
        goto fail;
    file = create ? mm_file_ops_create(services->files, name, mm_chunk_file_flags(flags)) :
                    mm_file_ops_open(services->files, name, mm_chunk_file_flags(flags));
    if (!file)
        goto fail;
    storage->owner->files.main_file = file->object;
    if (create)
    {
        if (!storage->io.resize(file->object, MM_CHUNK_PREFIX_BYTES))
            goto fail;
        storage->owner->files.main_end = MM_CHUNK_PREFIX_BYTES;
    }
    else if (mm_chunk_reader_read_index(reader) != MM_CHUNK_OK || mm_chunk_reader_validate_index(reader) != MM_CHUNK_OK)
        goto fail;
    if (mm_chunk_owner_registered_options(storage, file, flags, 0xffffffffUL) != MM_CHUNK_OK)
        issue(services, MM_CONTAINER_INITIAL_OPTIONS);
    if (services->finish)
        services->finish(services->context, link->owner, create != 0);
    return link->owner;
fail:
    if (link)
        services->release(services->context, link->owner);
    issue(services, error);
    return NULL;
}
void *mm_chunk_construct_temp(const MMChunkConstruction *services, void *basis, void *scratch)
{
    if (!services || !services->files || !scratch)
        return NULL;
    if (mm_file_ops_temp_name(services->files, basis, scratch))
        return mm_chunk_construct(services, scratch, MM_CHUNK_TEMP, 1);
    issue(services, MM_CONTAINER_CREATE_FAILED);
    return NULL;
}
int mm_chunk_dispose(MMChunkReopenState *state, MMChunkRuntime *runtime,
                           const MMChunkDisposal *services)
{
    MMChunkReopenState old;
    if (!state || !services || !services->release_file || !services->release_index || !services->publish)
        return MM_CHUNK_BAD_FILE;
    if (runtime && (runtime->storage.buffers[0].locks || runtime->storage.buffers[1].locks))
        return MM_CHUNK_BAD_FILE;
    if ((state->main_map && (state->main_map->storage.buffers[0].locks || state->main_map->storage.buffers[1].locks)) ||
        (state->extra_map &&
         (state->extra_map->storage.buffers[0].locks || state->extra_map->storage.buffers[1].locks)))
        return MM_CHUNK_BAD_FILE;
    old = *state;
    memset(state, 0, sizeof(*state));
    services->publish(services->context, state);
    if (old.main_file)
        services->release_file(services->context, old.main_file);
    mm_free_map_close(old.main_map);
    if (old.extra_file)
        services->release_file(services->context, old.extra_file);
    mm_free_map_close(old.extra_map);
    if (old.index)
        services->release_index(services->context, old.index);
    if (runtime && mm_chunk_runtime_dispose(runtime) != MM_CHUNK_OK)
        return MM_CHUNK_BAD_FILE;
    return MM_CHUNK_OK;
}

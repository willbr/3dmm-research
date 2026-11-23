/* Container construction converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_container.h"
#include <string.h>
void *mm_chunk_construct_forest(const MMForestConstruction *s, void *source, MMU32 offset,
                                      MMU32 bytes, int copy)
{
    void *owner, *index, *file;
    MMU32 end;
    if (!s || !source || !s->allocate || !s->release || !s->index || !s->attach_index || !s->temporary ||
        !s->retain_file || !s->attach_file || !s->resize || !s->destination || !s->read || offset > 0x7fffffffUL ||
        bytes > 0x7fffffffUL - offset)
        return NULL;
    owner = s->allocate(s->context);
    if (!owner)
        return NULL;
    index = s->index(s->context);
    s->attach_index(s->context, owner, index);
    if (!index)
        goto fail;
    if (copy)
    {
        file = s->temporary(s->context);
        s->attach_file(s->context, owner, file);
        if (!file || !s->resize(s->context, file, 128))
            goto fail;
        end = 128;
    }
    else
    {
        s->retain_file(s->context, source);
        s->attach_file(s->context, owner, source);
        end = offset + bytes;
    }
    s->destination(s->context, owner, end, !copy);
    if (!s->read(s->context, owner, source, offset, bytes, copy != 0))
        goto fail;
    if (s->finish)
        s->finish(s->context, owner);
    return owner;
fail:
    s->release(s->context, owner);
    return NULL;
}
static void issue(const MMChunkConstruction *services, int kind)
{
    if (services->issue)
        services->issue(services->context, kind);
}
void *mm_chunk_construct(const MMChunkConstruction *services, MMU32 flags, int create)
{
    void *owner, *file, *index;
    int error = create ? MM_CONTAINER_CREATE_FAILED : MM_CONTAINER_OPEN_FAILED;
    if (!services || !services->find || !services->allocate || !services->retain || !services->release ||
        !services->file || !services->index || !services->attach_file || !services->attach_index ||
        !services->set_end || !services->resize || !services->read_index || !services->validate || !services->options)
        return NULL;
    if (create)
        flags |= MM_CHUNK_WRITE;
    owner = services->find(services->context);
    if (owner)
    {
        if (create)
        {
            issue(services, MM_CONTAINER_ALREADY_OPEN);
            issue(services, error);
            return NULL;
        }
        if (!services->options(services->context, owner, flags))
            return NULL;
        services->retain(services->context, owner);
        return owner;
    }
    owner = services->allocate(services->context);
    if (!owner)
    {
        issue(services, error);
        return NULL;
    }
    if (create)
    {
        index = services->index(services->context);
        services->attach_index(services->context, owner, index);
        if (!index)
            goto fail;
    }
    file = services->file(services->context, create != 0, mm_chunk_file_flags(flags));
    services->attach_file(services->context, owner, file);
    if (!file)
        goto fail;
    if (create)
    {
        if (!services->resize(services->context, file, MM_CHUNK_PREFIX_BYTES))
            goto fail;
        services->set_end(services->context, owner, MM_CHUNK_PREFIX_BYTES);
    }
    else if (!services->read_index(services->context, owner) || !services->validate(services->context, owner))
        goto fail;
    if (!services->options(services->context, owner, flags))
        issue(services, MM_CONTAINER_INITIAL_OPTIONS);
    if (services->finish)
        services->finish(services->context, owner, create != 0);
    return owner;
fail:
    services->release(services->context, owner);
    issue(services, error);
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

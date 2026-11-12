/* Container construction converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_container.h"
#include <string.h>
static void issue(const ThreeMMChunkConstruction *services, int kind)
{
    if (services->issue)
        services->issue(services->context, kind);
}
void *three_mm_chunk_construct(const ThreeMMChunkConstruction *services, ThreeMMU32 flags, int create)
{
    void *owner, *file, *index;
    int error = create ? THREE_MM_CONTAINER_CREATE_FAILED : THREE_MM_CONTAINER_OPEN_FAILED;
    if (!services || !services->find || !services->allocate || !services->retain || !services->release ||
        !services->file || !services->index || !services->attach_file || !services->attach_index ||
        !services->set_end || !services->resize || !services->read_index || !services->validate || !services->options)
        return NULL;
    if (create)
        flags |= THREE_MM_CHUNK_WRITE;
    owner = services->find(services->context);
    if (owner)
    {
        if (create)
        {
            issue(services, THREE_MM_CONTAINER_ALREADY_OPEN);
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
    file = services->file(services->context, create != 0, three_mm_chunk_file_flags(flags));
    services->attach_file(services->context, owner, file);
    if (!file)
        goto fail;
    if (create)
    {
        if (!services->resize(services->context, file, THREE_MM_CHUNK_PREFIX_BYTES))
            goto fail;
        services->set_end(services->context, owner, THREE_MM_CHUNK_PREFIX_BYTES);
    }
    else if (!services->read_index(services->context, owner) || !services->validate(services->context, owner))
        goto fail;
    if (!services->options(services->context, owner, flags))
        issue(services, THREE_MM_CONTAINER_INITIAL_OPTIONS);
    if (services->finish)
        services->finish(services->context, owner, create != 0);
    return owner;
fail:
    services->release(services->context, owner);
    issue(services, error);
    return NULL;
}
void *three_mm_chunk_find_container(void *first, void *file, const ThreeMMChunkLookup *services)
{
    if (!file || !services || !services->next || !services->main_file || !services->extra_file ||
        !services->invalid_main)
        return NULL;
    while (first)
    {
        if (services->extra_file(first) == file && services->extra_collision)
            services->extra_collision(first);
        if (services->main_file(first) == file && !services->invalid_main(first))
            return first;
        first = services->next(first);
    }
    return NULL;
}
int three_mm_chunk_dispose(ThreeMMChunkReopenState *state, ThreeMMChunkRuntime *runtime,
                           const ThreeMMChunkDisposal *services)
{
    ThreeMMChunkReopenState old;
    if (!state || !services || !services->release_file || !services->release_index || !services->publish)
        return THREE_MM_CHUNK_BAD_FILE;
    if (runtime && (runtime->storage.buffers[0].locks || runtime->storage.buffers[1].locks))
        return THREE_MM_CHUNK_BAD_FILE;
    if ((state->main_map && (state->main_map->storage.buffers[0].locks || state->main_map->storage.buffers[1].locks)) ||
        (state->extra_map &&
         (state->extra_map->storage.buffers[0].locks || state->extra_map->storage.buffers[1].locks)))
        return THREE_MM_CHUNK_BAD_FILE;
    old = *state;
    memset(state, 0, sizeof(*state));
    services->publish(services->context, state);
    if (old.main_file)
        services->release_file(services->context, old.main_file);
    three_mm_free_map_close(old.main_map);
    if (old.extra_file)
        services->release_file(services->context, old.extra_file);
    three_mm_free_map_close(old.extra_map);
    if (old.index)
        services->release_index(services->context, old.index);
    if (runtime && three_mm_chunk_runtime_dispose(runtime) != THREE_MM_CHUNK_OK)
        return THREE_MM_CHUNK_BAD_FILE;
    return THREE_MM_CHUNK_OK;
}

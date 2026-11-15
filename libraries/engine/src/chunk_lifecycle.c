/* Chunk lifecycle converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_lifecycle.h"
#include <string.h>
MMU32 mm_chunk_file_flags(MMU32 options)
{
    return 2 | (options & MM_CHUNK_WRITE ? 1 | 4 : 0) | (options & MM_CHUNK_TEMP ? 16 : 0);
}
int mm_chunk_options_set(MMChunkOptions *state, const MMChunkOptionServices *services,
                               MMU32 options, MMU32 mask)
{
    MMU32 flags, file_mask;
    if (!state || !services || !services->file_flags || !services->set_file_flags || !services->read_map)
        return MM_CHUNK_BAD_FILE;
    options &= mask;
    flags = mm_chunk_file_flags(options);
    file_mask = mm_chunk_file_flags(mask);
    if ((flags ^ services->file_flags(services->context)) & file_mask)
        if (state->invalid_main || !services->set_file_flags(services->context, flags, file_mask))
            return MM_CHUNK_IO;
    if ((options & MM_CHUNK_WRITE) && state->free_map_unread)
    {
        services->read_map(services->context);
        state->free_map_unread = 0;
    }
    if (options & MM_CHUNK_MARK)
        state->marked = 1;
    if ((options & MM_CHUNK_ADD_EXTRA) || ((options ^ mask) & MM_CHUNK_WRITE))
        state->add_to_extra = 1;
    if (options & MM_CHUNK_READ_EXTRA)
        state->read_from_extra = 1;
    return MM_CHUNK_OK;
}
int mm_chunk_release(MMI32 references, int marked, MMI32 *remaining)
{
    if (references <= 0 || !remaining)
        return -1;
    *remaining = references - 1;
    return !*remaining && !marked;
}
int mm_chunk_clear_marks(void *first, const MMChunkRegistry *registry)
{
    if (!registry || !registry->next || !registry->clear_mark)
        return MM_CHUNK_BAD_FILE;
    while (first)
    {
        registry->clear_mark(first);
        first = registry->next(first);
    }
    return MM_CHUNK_OK;
}
int mm_chunk_close_unmarked(void *first, const MMChunkRegistry *registry)
{
    void *next;
    if (!registry || !registry->next || !registry->marked || !registry->references || !registry->destroy)
        return MM_CHUNK_BAD_FILE;
    while (first)
    {
        next = registry->next(first);
        if (!registry->marked(first) && !registry->references(first))
            registry->destroy(first);
        first = next;
    }
    return MM_CHUNK_OK;
}
int mm_chunk_reopen(MMChunkReopenState *state, MMChunkRuntime *runtime,
                          const MMChunkReopenServices *services)
{
    MMChunkReopenState old, failed;
    MMChunkRuntime saved_runtime;
    int ok;
    if (!state || !state->main_file || !services || !services->writable || !services->retain_file ||
        !services->release_file || !services->release_index || !services->read_index || !services->validate ||
        !services->publish)
        return MM_CHUNK_BAD_FILE;
    if (state->invalid_main || services->writable(services->context, state->main_file))
        return MM_CHUNK_BAD_FILE;
    if (runtime)
    {
        if (mm_chunk_runtime_init(&saved_runtime, &runtime->storage.allocator) != MM_CHUNK_OK)
            return MM_CHUNK_BAD_FILE;
        if (mm_chunk_runtime_swap(runtime, &saved_runtime) != MM_CHUNK_OK)
        {
            mm_chunk_runtime_dispose(&saved_runtime);
            return MM_CHUNK_BAD_FILE;
        }
    }
    old = *state;
    memset(state, 0, sizeof(*state));
    state->main_file = old.main_file;
    services->retain_file(services->context, state->main_file);
    services->publish(services->context, state);
    ok = services->read_index(services->context, state) && services->validate(services->context);
    if (!ok)
    {
        failed = *state;
        *state = old;
        old = failed;
        if (runtime)
            mm_chunk_runtime_swap(runtime, &saved_runtime);
    }
    services->publish(services->context, state);
    if (old.index)
        services->release_index(services->context, old.index);
    if (runtime)
        mm_chunk_runtime_dispose(&saved_runtime);
    if (old.main_file)
        services->release_file(services->context, old.main_file);
    mm_free_map_close(old.main_map);
    if (old.extra_file)
        services->release_file(services->context, old.extra_file);
    mm_free_map_close(old.extra_map);
    return ok ? MM_CHUNK_OK : MM_CHUNK_IO;
}

/* Container ownership converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_owner.h"
#include <string.h>
int mm_chunk_owner_init(MMChunkOwner *o, const MMStorageAllocator *allocator, int expanded)
{
    if (!o)
        return MM_CHUNK_BAD_FILE;
    memset(o, 0, sizeof(*o));
    o->expanded_runtime = expanded != 0;
    return expanded ? MM_CHUNK_OK : mm_chunk_runtime_init(&o->runtime, allocator);
}
int mm_chunk_owner_dispose(MMChunkOwner *o, const MMChunkDisposal *services)
{
    if (!o)
        return MM_CHUNK_BAD_FILE;
    return mm_chunk_dispose(&o->files, o->expanded_runtime ? NULL : &o->runtime, services);
}
int mm_chunk_owner_reopen(MMChunkOwner *o, const MMChunkReopenServices *services)
{
    if (!o)
        return MM_CHUNK_BAD_FILE;
    return mm_chunk_reopen(&o->files, o->expanded_runtime ? NULL : &o->runtime, services);
}
int mm_chunk_owner_options(MMChunkOwner *o, const MMChunkOptionServices *s, MMU32 flags,
                                 MMU32 mask)
{
    MMChunkOptions options;
    int result;
    if (!o)
        return MM_CHUNK_BAD_FILE;
    options.marked = o->marked;
    options.add_to_extra = o->add_to_extra;
    options.read_from_extra = o->read_from_extra;
    options.free_map_unread = o->files.free_map_unread;
    options.invalid_main = o->files.invalid_main;
    result = mm_chunk_options_set(&options, s, flags, mask);
    if (result == MM_CHUNK_OK)
    {
        o->marked = options.marked;
        o->add_to_extra = options.add_to_extra;
        o->read_from_extra = options.read_from_extra;
        o->files.free_map_unread = options.free_map_unread;
    }
    return result;
}
MMChunkSaveState mm_chunk_owner_save_state(const MMChunkOwner *o)
{
    MMChunkSaveState state;
    state.main_file = o->files.main_file;
    state.extra_file = o->files.extra_file;
    state.main_map = o->files.main_map;
    state.extra_map = o->files.extra_map;
    state.main_end = o->files.main_end;
    state.extra_end = o->files.extra_end;
    state.invalid_main = o->files.invalid_main;
    state.add_to_extra = o->add_to_extra;
    state.free_map_unread = o->files.free_map_unread;
    return state;
}
void mm_chunk_owner_publish_save(MMChunkOwner *o, const MMChunkSaveState *s)
{
    o->files.main_file = s->main_file;
    o->files.extra_file = s->extra_file;
    o->files.main_map = (MMChunkFreeMap *)s->main_map;
    o->files.extra_map = (MMChunkFreeMap *)s->extra_map;
    o->files.main_end = s->main_end;
    o->files.extra_end = s->extra_end;
    o->files.invalid_main = s->invalid_main != 0;
    o->files.free_map_unread = s->free_map_unread != 0;
    o->add_to_extra = s->add_to_extra != 0;
}
MMChunkStoragePolicy mm_chunk_owner_storage_policy(const MMChunkOwner *o, MMU32 max_bytes)
{
    MMChunkStoragePolicy policy;
    policy.max_bytes = max_bytes;
    policy.add_to_extra = o->add_to_extra;
    policy.read_from_extra = o->read_from_extra;
    policy.free_map_unread = o->files.free_map_unread;
    return policy;
}
MMI32 mm_chunk_owner_error(const MMChunkOwner *o, const MMChunkErrorServices *s)
{
    MMI32 level = s->get(o->files.main_file), extra;
    if (o->files.extra_file)
    {
        extra = s->get(o->files.extra_file);
        if (extra > level)
            level = extra;
    }
    return level;
}
void mm_chunk_owner_reset_error(MMChunkOwner *o, const MMChunkErrorServices *s, MMI32 level)
{
    if (s->get(o->files.main_file) > level)
        s->set(o->files.main_file, level);
    if (o->files.extra_file && s->get(o->files.extra_file) > level)
        s->set(o->files.extra_file, level);
}

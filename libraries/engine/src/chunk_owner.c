/* Container ownership converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_owner.h"
#include <string.h>
int three_mm_chunk_owner_init(ThreeMMChunkOwner *o, const ThreeMMStorageAllocator *allocator, int expanded)
{
    if (!o)
        return THREE_MM_CHUNK_BAD_FILE;
    memset(o, 0, sizeof(*o));
    o->expanded_runtime = expanded != 0;
    return expanded ? THREE_MM_CHUNK_OK : three_mm_chunk_runtime_init(&o->runtime, allocator);
}
int three_mm_chunk_owner_dispose(ThreeMMChunkOwner *o, const ThreeMMChunkDisposal *services)
{
    if (!o)
        return THREE_MM_CHUNK_BAD_FILE;
    return three_mm_chunk_dispose(&o->files, o->expanded_runtime ? NULL : &o->runtime, services);
}
int three_mm_chunk_owner_reopen(ThreeMMChunkOwner *o, const ThreeMMChunkReopenServices *services)
{
    if (!o)
        return THREE_MM_CHUNK_BAD_FILE;
    return three_mm_chunk_reopen(&o->files, o->expanded_runtime ? NULL : &o->runtime, services);
}
int three_mm_chunk_owner_options(ThreeMMChunkOwner *o, const ThreeMMChunkOptionServices *s, ThreeMMU32 flags,
                                 ThreeMMU32 mask)
{
    ThreeMMChunkOptions options;
    int result;
    if (!o)
        return THREE_MM_CHUNK_BAD_FILE;
    options.marked = o->marked;
    options.add_to_extra = o->add_to_extra;
    options.read_from_extra = o->read_from_extra;
    options.free_map_unread = o->files.free_map_unread;
    options.invalid_main = o->files.invalid_main;
    result = three_mm_chunk_options_set(&options, s, flags, mask);
    if (result == THREE_MM_CHUNK_OK)
    {
        o->marked = options.marked;
        o->add_to_extra = options.add_to_extra;
        o->read_from_extra = options.read_from_extra;
        o->files.free_map_unread = options.free_map_unread;
    }
    return result;
}
ThreeMMChunkSaveState three_mm_chunk_owner_save_state(const ThreeMMChunkOwner *o)
{
    ThreeMMChunkSaveState state;
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
void three_mm_chunk_owner_publish_save(ThreeMMChunkOwner *o, const ThreeMMChunkSaveState *s)
{
    o->files.main_file = s->main_file;
    o->files.extra_file = s->extra_file;
    o->files.main_map = (ThreeMMChunkFreeMap *)s->main_map;
    o->files.extra_map = (ThreeMMChunkFreeMap *)s->extra_map;
    o->files.main_end = s->main_end;
    o->files.extra_end = s->extra_end;
    o->files.invalid_main = s->invalid_main != 0;
    o->files.free_map_unread = s->free_map_unread != 0;
    o->add_to_extra = s->add_to_extra != 0;
}
ThreeMMChunkStoragePolicy three_mm_chunk_owner_storage_policy(const ThreeMMChunkOwner *o, ThreeMMU32 max_bytes)
{
    ThreeMMChunkStoragePolicy policy;
    policy.max_bytes = max_bytes;
    policy.add_to_extra = o->add_to_extra;
    policy.read_from_extra = o->read_from_extra;
    policy.free_map_unread = o->files.free_map_unread;
    return policy;
}
ThreeMMI32 three_mm_chunk_owner_error(const ThreeMMChunkOwner *o, const ThreeMMChunkErrorServices *s)
{
    ThreeMMI32 level = s->get(o->files.main_file), extra;
    if (o->files.extra_file)
    {
        extra = s->get(o->files.extra_file);
        if (extra > level)
            level = extra;
    }
    return level;
}
void three_mm_chunk_owner_reset_error(ThreeMMChunkOwner *o, const ThreeMMChunkErrorServices *s, ThreeMMI32 level)
{
    if (s->get(o->files.main_file) > level)
        s->set(o->files.main_file, level);
    if (o->files.extra_file && s->get(o->files.extra_file) > level)
        s->set(o->files.extra_file, level);
}

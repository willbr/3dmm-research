/* Whole-file save policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_save.h"
#define ON_EXTRA ((ThreeMMU32)1)
int three_mm_chunk_save(ThreeMMChunkSaveState *state, const ThreeMMChunkSaveIndex *index,
                        const ThreeMMChunkSaveServices *io, ThreeMMU32 creator, int named)
{
    ThreeMMChunkEntry entry;
    ThreeMMU32 i, end, offset;
    void *temporary, *old, *source, *old_map, *extra, *extra_map;
    int result;
    if (!state || !index || !index->entry || !index->place || !io || !io->write_index ||
        (state->invalid_main && !named) || (!state->invalid_main && !state->main_file))
        return THREE_MM_CHUNK_BAD_FILE;
    if (!state->add_to_extra && !state->extra_file && !named)
    {
        if (state->free_map_unread)
            return THREE_MM_CHUNK_BAD_FILE;
        return io->write_index(io->context, state, creator) ? THREE_MM_CHUNK_OK : THREE_MM_CHUNK_IO;
    }
    if (!io->create_temp || !io->resize || !io->copy || !io->release_file || !io->release_map || !io->is_temp ||
        !io->set_temp || (named ? !io->rename : !io->swap_names))
        return THREE_MM_CHUNK_BAD_FILE;
    /* Preflight without allocating a second index. Trusted index primitives are
     * stable throughout; every file range and the complete layout must fit. */
    end = THREE_MM_CHUNK_PREFIX_BYTES;
    for (i = 0; i < index->count; ++i)
    {
        index->entry(index->context, i, &entry);
        source = entry.flags & ON_EXTRA ? state->extra_file : state->main_file;
        if (entry.offset > 0x7fffffffUL || entry.bytes > 0x7fffffffUL - entry.offset ||
            entry.bytes > 0x7fffffffUL - end || (entry.bytes && !source))
            return THREE_MM_CHUNK_BAD_FILE;
        end += entry.bytes;
    }
    temporary = io->create_temp(io->context);
    if (!temporary)
        return THREE_MM_CHUNK_MEMORY;
    if (!io->resize(io->context, temporary, THREE_MM_CHUNK_PREFIX_BYTES))
        goto copy_failed;
    offset = THREE_MM_CHUNK_PREFIX_BYTES;
    for (i = 0; i < index->count; ++i)
    {
        index->entry(index->context, i, &entry);
        source = entry.flags & ON_EXTRA ? state->extra_file : state->main_file;
        if (entry.bytes && !io->copy(io->context, source, entry.offset, temporary, offset, entry.bytes))
            goto copy_failed;
        offset += entry.bytes;
    }
    offset = THREE_MM_CHUNK_PREFIX_BYTES;
    for (i = 0; i < index->count; ++i)
    {
        index->entry(index->context, i, &entry);
        index->place(index->context, i, entry.bytes ? offset : 0, entry.flags & ~ON_EXTRA);
        offset += entry.bytes;
    }
    old = state->main_file;
    old_map = state->main_map;
    extra = state->extra_file;
    extra_map = extra ? state->extra_map : 0;
    state->main_map = 0;
    state->free_map_unread = 0;
    state->main_file = temporary;
    state->main_end = end;
    if (extra)
    {
        state->extra_end = 0;
        state->extra_file = 0;
        state->extra_map = 0;
    }
    if (io->publish)
        io->publish(io->context, state);
    if (old_map)
        io->release_map(io->context, old_map);
    io->set_temp(io->context, temporary, state->invalid_main || io->is_temp(io->context, old));
    if (extra)
        io->release_file(io->context, extra);
    if (extra_map)
        io->release_map(io->context, extra_map);
    result = io->write_index(io->context, state, creator);
    if (result)
    {
        if (named)
        {
            result = io->rename(io->context, temporary);
            if (result)
                state->invalid_main = 0;
        }
        else
            result = io->swap_names(io->context, temporary, old);
    }
    if (!result)
    {
        if (state->invalid_main)
        {
            if (old)
                io->release_file(io->context, old);
        }
        else
        {
            state->main_file = old;
            state->main_end = THREE_MM_CHUNK_PREFIX_BYTES;
            for (i = 0; i < index->count; ++i)
            {
                index->entry(index->context, i, &entry);
                if (entry.bytes)
                    index->place(index->context, i, entry.offset, entry.flags | ON_EXTRA);
            }
            state->extra_file = temporary;
            state->extra_end = end;
            if (io->publish)
                io->publish(io->context, state);
            io->set_temp(io->context, temporary, 1);
        }
        return THREE_MM_CHUNK_IO;
    }
    if (io->publish)
        io->publish(io->context, state);
    if (!named && old)
        io->set_temp(io->context, old, 1);
    if (old)
        io->release_file(io->context, old);
    return THREE_MM_CHUNK_OK;
copy_failed:
    io->release_file(io->context, temporary);
    return THREE_MM_CHUNK_IO;
}
int three_mm_chunk_save_copy(const ThreeMMChunkSaveIndex *index, const ThreeMMChunkCopyServices *io, ThreeMMU32 creator)
{
    ThreeMMChunkEntry entry;
    ThreeMMU32 i, end;
    void *source;
    int result = THREE_MM_CHUNK_OK;
    if (!index || !index->entry || !io || !io->destination_file || !io->pin || !io->unpin || !io->copy || !io->insert ||
        !io->finish)
        return THREE_MM_CHUNK_BAD_FILE;
    io->pin(io->context);
    end = THREE_MM_CHUNK_PREFIX_BYTES;
    for (i = 0; i < index->count; ++i)
    {
        index->entry(index->context, i, &entry);
        source = entry.flags & ON_EXTRA ? io->extra_file : io->main_file;
        if (!source || entry.offset > 0x7fffffffUL || entry.bytes > 0x7fffffffUL - entry.offset ||
            entry.bytes > 0x7fffffffUL - end)
        {
            result = THREE_MM_CHUNK_BAD_FILE;
            goto done;
        }
        end += entry.bytes;
    }
    end = THREE_MM_CHUNK_PREFIX_BYTES;
    for (i = 0; i < index->count; ++i)
    {
        index->entry(index->context, i, &entry);
        source = entry.flags & ON_EXTRA ? io->extra_file : io->main_file;
        if (!io->copy(io->context, source, entry.offset, io->destination_file, end, entry.bytes))
        {
            result = THREE_MM_CHUNK_IO;
            goto done;
        }
        entry.offset = end;
        entry.flags &= ~ON_EXTRA;
        if (!io->insert(io->context, i, &entry))
        {
            result = THREE_MM_CHUNK_MEMORY;
            goto done;
        }
        end += entry.bytes;
    }
done:
    io->unpin(io->context);
    if (result == THREE_MM_CHUNK_OK && !io->finish(io->context, end, creator))
        result = THREE_MM_CHUNK_IO;
    return result;
}

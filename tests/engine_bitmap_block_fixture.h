#ifndef ENGINE_BITMAP_BLOCK_FIXTURE_H
#define ENGINE_BITMAP_BLOCK_FIXTURE_H

#include "engine_resource_block_fixture.h"

enum { BITMAP_FILE, BITMAP_PACKED_FILE, BITMAP_MEMORY };

static void bitmap_block_file(ResourceTestBlock *block, MMU32 offset, MMU32 length, int packed)
{
    MMBlockFile file;
    file.file = block->files.context;
    file.offset = offset;
    file.length = length;
    assert(mm_owned_block_set_file(&block->owner, &file, packed) == MM_DATA_OK);
}
static void bitmap_block_memory(ResourceTestBlock *block, const void *data, MMU32 length)
{
    void *handle;
    resource_handle_allocate(NULL, length, &handle);
    memcpy(resource_handle_lock(NULL, handle), data, (size_t)length);
    assert(mm_owned_block_set_handle(&block->owner, &handle, 0) == MM_DATA_OK && !handle);
}

static void bitmap_block_source(ResourceTestBlock *block, const void *data, MMU32 length, MMU32 stored, int storage)
{
    if (storage == BITMAP_MEMORY)
        bitmap_block_memory(block, data, length);
    else
        bitmap_block_file(block, 3, storage == BITMAP_PACKED_FILE ? stored : length, storage == BITMAP_PACKED_FILE);
}

#endif

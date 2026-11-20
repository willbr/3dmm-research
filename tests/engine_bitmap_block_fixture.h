#ifndef ENGINE_BITMAP_BLOCK_FIXTURE_H
#define ENGINE_BITMAP_BLOCK_FIXTURE_H

#include "mm/block_owner.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct BitmapTestBlock
{
    MMOwnedBlock owner;
    MMBlockFileServices files;
} BitmapTestBlock;
enum { BITMAP_FILE, BITMAP_PACKED_FILE, BITMAP_MEMORY };

static int bitmap_handle_allocate(void *context, MMU32 length, void **out)
{
    MMU32 *handle = (MMU32 *)malloc(sizeof(*handle) + (size_t)length);
    (void)context;
    assert(handle);
    *handle = length;
    *out = handle;
    return 1;
}
static int bitmap_handle_resize(void *context, void **handle, MMU32 length)
{
    MMU32 *resized = (MMU32 *)realloc(*handle, sizeof(*resized) + (size_t)length);
    (void)context;
    assert(resized);
    *resized = length;
    *handle = resized;
    return 1;
}
static MMU32 bitmap_handle_size(void *context, void *handle)
{
    (void)context;
    return *(MMU32 *)handle;
}
static void *bitmap_handle_lock(void *context, void *handle)
{
    (void)context;
    return (MMU32 *)handle + 1;
}
static void bitmap_handle_unlock(void *context, void *handle)
{
    (void)context;
    (void)handle;
}
static void bitmap_handle_release(void *context, void *handle)
{
    (void)context;
    free(handle);
}
static void bitmap_file_reference(void *context, void *file)
{
    assert(context == file);
}
static void *bitmap_temp_file(void *context)
{
    (void)context;
    return NULL;
}
static void bitmap_block_init(BitmapTestBlock *block, void *context,
                              int (*read)(void *, void *, MMU32, void *, size_t),
                              int (*write)(void *, void *, MMU32, const void *, size_t))
{
    static const MMHandleServices handles = {
        NULL, bitmap_handle_allocate, bitmap_handle_release, bitmap_handle_size,
        bitmap_handle_lock, bitmap_handle_unlock, bitmap_handle_resize
    };
    block->files.context = context;
    block->files.retain = bitmap_file_reference;
    block->files.release = bitmap_file_reference;
    block->files.create_temp = bitmap_temp_file;
    block->files.read = read;
    block->files.write = write;
    assert(mm_owned_block_init(&block->owner, &handles, &block->files) == MM_DATA_OK);
}
static void bitmap_block_file(BitmapTestBlock *block, MMU32 offset, MMU32 length, int packed)
{
    MMBlockFile file;
    file.file = block->files.context;
    file.offset = offset;
    file.length = length;
    assert(mm_owned_block_set_file(&block->owner, &file, packed) == MM_DATA_OK);
}
static void bitmap_block_memory(BitmapTestBlock *block, const void *data, MMU32 length)
{
    void *handle;
    bitmap_handle_allocate(NULL, length, &handle);
    memcpy(bitmap_handle_lock(NULL, handle), data, (size_t)length);
    assert(mm_owned_block_set_handle(&block->owner, &handle, 0) == MM_DATA_OK && !handle);
}
static void bitmap_block_decode(void **handle, const void *data, MMU32 length)
{
    bitmap_handle_resize(NULL, handle, length);
    memcpy(bitmap_handle_lock(NULL, *handle), data, (size_t)length);
}
static void bitmap_block_source(BitmapTestBlock *block, const void *data, MMU32 length, MMU32 stored, int storage)
{
    if (storage == BITMAP_MEMORY)
        bitmap_block_memory(block, data, length);
    else
        bitmap_block_file(block, 3, storage == BITMAP_PACKED_FILE ? stored : length, storage == BITMAP_PACKED_FILE);
}

#endif

#ifndef ENGINE_RESOURCE_BLOCK_FIXTURE_H
#define ENGINE_RESOURCE_BLOCK_FIXTURE_H

#include "mm/block_owner.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct ResourceTestBlock
{
    MMOwnedBlock owner;
    MMBlockFileServices files;
} ResourceTestBlock;

static int resource_handle_allocate(void *context, MMU32 length, void **out)
{
    MMU32 *handle = (MMU32 *)malloc(sizeof(*handle) + (size_t)length);
    (void)context;
    assert(handle);
    *handle = length;
    *out = handle;
    return 1;
}
static int resource_handle_resize(void *context, void **handle, MMU32 length)
{
    MMU32 *resized = (MMU32 *)realloc(*handle, sizeof(*resized) + (size_t)length);
    (void)context;
    assert(resized);
    *resized = length;
    *handle = resized;
    return 1;
}
static MMU32 resource_handle_size(void *context, void *handle)
{
    (void)context;
    return *(MMU32 *)handle;
}
static void *resource_handle_lock(void *context, void *handle)
{
    (void)context;
    return (MMU32 *)handle + 1;
}
static void resource_handle_unlock(void *context, void *handle)
{
    (void)context;
    (void)handle;
}
static void resource_handle_release(void *context, void *handle)
{
    (void)context;
    free(handle);
}
static void resource_file_reference(void *context, void *file)
{
    assert(context == file);
}
static void *resource_temp_file(void *context)
{
    (void)context;
    return NULL;
}
static void resource_block_init(ResourceTestBlock *block, void *context,
                              int (*read)(void *, void *, MMU32, void *, size_t),
                              int (*write)(void *, void *, MMU32, const void *, size_t))
{
    static const MMHandleServices handles = {
        NULL, resource_handle_allocate, resource_handle_release, resource_handle_size,
        resource_handle_lock, resource_handle_unlock, resource_handle_resize
    };
    block->files.context = context;
    block->files.retain = resource_file_reference;
    block->files.release = resource_file_reference;
    block->files.create_temp = resource_temp_file;
    block->files.read = read;
    block->files.write = write;
    assert(mm_owned_block_init(&block->owner, &handles, &block->files) == MM_DATA_OK);
}
static void resource_block_decode(void **handle, const void *data, MMU32 length)
{
    resource_handle_resize(NULL, handle, length);
    memcpy(resource_handle_lock(NULL, *handle), data, (size_t)length);
}

#endif

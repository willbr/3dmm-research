#ifndef ENGINE_WORLD_BACKGROUND_FIXTURE_H
#define ENGINE_WORLD_BACKGROUND_FIXTURE_H

#include "engine_resource_block_fixture.h"
#include "mm/world_buffers.h"

enum
{
    WORLD_TEST_COLOUR_TYPE = 11,
    WORLD_TEST_COLOUR_NUMBER = 22,
    WORLD_TEST_DEPTH_TYPE = 33,
    WORLD_TEST_DEPTH_NUMBER = 44
};

typedef struct WorldTestAssets
{
    ResourceTestBlock block;
    MMResourceCache *cache;
    int *phase, *fail;
    MMI32 width, colour_height, depth_height;
    unsigned char colour[24], depth[48];
} WorldTestAssets;

static int world_test_contains(void *file, MMU32 type, MMU32 number)
{
    (void)file;
    return (type == WORLD_TEST_COLOUR_TYPE && number == WORLD_TEST_COLOUR_NUMBER) ||
           (type == WORLD_TEST_DEPTH_TYPE && number == WORLD_TEST_DEPTH_NUMBER);
}

static int world_test_locate(void *context, MMU32 type, MMU32 number, MMBlockFile *file, int *packed)
{
    if (!world_test_contains(context, type, number))
        return 0;
    file->file = context;
    file->offset = 0;
    file->length = 1;
    *packed = 0;
    return 1;
}

static void world_test_reference_file(void *file)
{
    assert(file);
}

/* The readers below generate typed objects in memory. The located extent
 * only gives the real cache a file-backed block to pass through to them. */
static int world_test_read_file(void *context, void *file, MMU32 offset, void *data, size_t size)
{
    (void)context;
    (void)file;
    (void)offset;
    (void)data;
    (void)size;
    assert(0);
    return 0;
}

static int world_test_write_file(void *context, void *file, MMU32 offset, const void *data, size_t size)
{
    (void)context;
    (void)file;
    (void)offset;
    (void)data;
    (void)size;
    assert(0);
    return 0;
}

static void world_test_cache_init(WorldTestAssets *assets, int budget, int *phase, int *fail)
{
    MMResourceSource source = {0};
    MMResourceServices services = {0};
    assets->phase = phase;
    assets->fail = fail;
    resource_block_init(&assets->block, assets, world_test_read_file, world_test_write_file);
    source.live = assets;
    services.handles = assets->block.owner.handles;
    services.block_files = assets->block.owner.files;
    services.contains = world_test_contains;
    services.locate = world_test_locate;
    services.retain_file = world_test_reference_file;
    services.release_file = world_test_reference_file;
    assets->cache = mm_resource_cache_create(&source, budget, &services, NULL, NULL);
    assert(assets->cache);
}

static int world_test_read_colour(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                                  void **out, MMI32 *bytes)
{
    WorldTestAssets *assets = (WorldTestAssets *)cache->file;
    MMMaskedRect rect = {0, 0, 0, 0};
    assert(type == WORLD_TEST_COLOUR_TYPE && number == WORLD_TEST_COLOUR_NUMBER && block->file == assets && out);
    if (++*assets->phase == *assets->fail || !assets->colour_height)
        return 0;
    rect.right = assets->width;
    rect.bottom = assets->colour_height;
    assert((size_t)(rect.right * rect.bottom) <= sizeof(assets->colour));
    *out = mm_masked_owner_new(assets->colour, rect.right, rect.bottom, &rect, 2, -3, 0, 0, 0, NULL);
    assert(*out);
    *bytes = mm_masked_owner_bitmap((MMMaskedOwner *)*out)->bytes;
    return 1;
}

static int world_test_read_depth(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                                 void **out, MMI32 *bytes)
{
    WorldTestAssets *assets = (WorldTestAssets *)cache->file;
    assert(type == WORLD_TEST_DEPTH_TYPE && number == WORLD_TEST_DEPTH_NUMBER && block->file == assets && out);
    if (++*assets->phase == *assets->fail || !assets->depth_height)
        return 0;
    assert((size_t)(assets->width * assets->depth_height * 2) <= sizeof(assets->depth));
    *out = mm_depth_owner_new(assets->width, assets->depth_height, assets->depth, NULL);
    assert(*out);
    *bytes = mm_depth_owner_bitmap((MMDepthOwner *)*out)->bytes;
    return 1;
}
#endif

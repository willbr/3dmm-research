/***************************************************************************

    zbmp.cpp: Z-buffer bitmap class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "bren_core.h"
#include "handle_services.h"
#include <cstring>
ASSERTNAME
namespace BRender
{
namespace
{
struct DepthWriteContext
{
    DataBlock block;
    MMBlockIO io;
};

const MMDepthOwnerHost &DepthHost()
{
    static const MMDepthOwnerHost host = {
        KauaiResourceObjectAllocator(), KauaiCopyAllocator(), nullptr, nullptr,
        [](void *) { Warn("Couldn't load ZBMP"); },
#ifdef DEBUG
        [](MMDepthOwner *owner) {
            AssertDepthBitmap(owner);
            MarkPv(owner);
            MarkPv(mm_depth_owner_bitmap(owner)->pixels);
            const auto binding = mm_depth_owner_binding(owner);
            if (binding->cache && !binding->attached)
                mm_resource_cache_mark(binding->cache);
        },
        [](MMDepthOwner *, int) { Assert(false, "invalid ZBMP cache ownership"); }
#else
        nullptr, nullptr
#endif
    };
    return host;
}

MMI32 DepthCacheOverhead()
{
    // Preserve the historical ZBMP cache charge, independent of owner layout.
#ifdef DEBUG
    const MMI32 prefix = sizeof(void *) == 8 ? 40 : 32;
#else
    const MMI32 prefix = sizeof(void *) == 8 ? 40 : 28;
#endif
    return prefix + sizeof(MMDepthBitmap);
}
} // namespace

const MMDepthOwnerHost *DepthBitmapHost()
{
    return &DepthHost();
}

MMDepthOwner *NewDepthBitmap(MMI32 width, MMI32 height, const void *initial)
{
    return mm_depth_owner_new(width, height, initial, DepthBitmapHost());
}

MMDepthOwner *ReadDepthBitmap(PDataBlock block)
{
    return mm_depth_owner_read_block(block->CoreBlock(), mm_depth_native_big_endian(), &DepthHost());
}

int ReadCachedDepthBitmap(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_depth_owner_fetch(block, out, bytes, DepthCacheOverhead(), mm_depth_native_big_endian(), &DepthHost());
}

bool WriteDepthBitmap(const MMDepthOwner *owner, const MMArchiveWriter &archive, ChunkTagOrType type,
                      ChunkNumber *number)
{
    DepthWriteContext context;
    MMDepthWriteServices services = {};
    services.context = &context;
    services.host_big_endian = mm_depth_native_big_endian();
    services.system = koskCur;
    services.add = [](void *p, void *file, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *writer) -> int {
        auto &target = *static_cast<DepthWriteContext *>(p);
        auto archive = static_cast<const MMArchiveWriter *>(file);
        return mm_archive_add_block(archive, type, bytes, number, target.block.CoreBlock()) == MM_CHUNK_OK &&
               mm_owned_block_io(target.block.CoreBlock(), &target.io) == MM_DATA_OK &&
               mm_block_io_writer(&target.io, writer) == MM_DATA_OK;
    };
    auto writer = archive;
    MMU32 n;
    std::memcpy(&n, number, sizeof(n));
    bool ok = mm_depth_write(mm_depth_owner_bitmap(owner), &writer, type, &n, &services) != 0;
    std::memcpy(number, &n, sizeof(n));
    return ok;
}

#ifdef DEBUG
void AssertDepthBitmap(const MMDepthOwner *owner)
{
    const auto bitmap = mm_depth_owner_bitmap(owner);
    AssertPvCb(bitmap->pixels, bitmap->bytes);
    Assert(bitmap->row_bytes == LwMul(bitmap->bounds.right - bitmap->bounds.left, 2), "bad ZBMP row");
    Assert(bitmap->bytes == LwMul(bitmap->bounds.bottom - bitmap->bounds.top, bitmap->row_bytes), "bad ZBMP size");
}
#endif
} // namespace BRender

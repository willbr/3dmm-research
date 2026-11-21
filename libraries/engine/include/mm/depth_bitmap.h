/* Depth bitmap policy converted from bren/zbmp.cpp. */
#ifndef MM_ENGINE_DEPTH_BITMAP_H
#define MM_ENGINE_DEPTH_BITMAP_H
#include "resource_cache.h"
#include "region.h"
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef MMPixelRect MMDepthRect;
    typedef struct MMDepthBitmap
    {
        MMDepthRect bounds;
        MMI32 row_bytes, bytes;
        void *pixels;
    } MMDepthBitmap;
    typedef struct MMDepthOwner MMDepthOwner;
    typedef struct MMDepthOwnerHost
    {
        const MMStorageAllocator *objects, *pixels;
        void *context;
        MMBlockTransform decode;
        void (*warning)(void *);
        void (*mark)(MMDepthOwner *);
        void (*issue)(MMDepthOwner *, int kind);
    } MMDepthOwnerHost;
    /* The optional host and allocators outlive their owners. Without a host,
     * use the C heap. New fills with 0xff unless given contiguous pixels. */
    MMDepthOwner *mm_depth_owner_new(MMI32 width, MMI32 height, const void *initial, const MMDepthOwnerHost *);
    const MMDepthBitmap *mm_depth_owner_bitmap(const MMDepthOwner *);
    const MMResourceObject *mm_depth_owner_binding(const MMDepthOwner *);
    const MMResourceObjectServices *mm_depth_owner_services(void);
    void mm_depth_owner_release(MMDepthOwner *);
    void mm_depth_owner_detach(MMDepthOwner *);
    void mm_depth_owner_priority(MMDepthOwner *, MMI32);
    int mm_depth_native_big_endian(void);
    int mm_depth_header(const unsigned char[12], int host_big, MMDepthBitmap *);
    /* Decode shorts in host order and ignore bo/osk, matching the native reader.
     * Accept zero-sized serialized layouts and reject negative layouts. */
    MMDepthOwner *mm_depth_owner_read_stream(const MMReadStream *, int host_big, const MMDepthOwnerHost *);
    MMDepthOwner *mm_depth_owner_read_block(MMOwnedBlock *, int host_big, const MMDepthOwnerHost *);
    /* The size-only probe returns the stored size without decoding. A successful
     * load uses the caller's cache overhead plus decoded pixel bytes. */
    int mm_depth_owner_fetch(MMOwnedBlock *, void **, MMI32 *, MMI32 overhead, int host_big,
                             const MMDepthOwnerHost *);
    typedef struct MMDepthWriteServices
    {
        void *context;
        int host_big_endian;
        MMU32 system;
        int (*add)(void *, void *file, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *);
    } MMDepthWriteServices;
    int mm_depth_write(const MMDepthBitmap *, void *file, MMU32 type, MMU32 *number, const MMDepthWriteServices *);
    /* No clip/region means no draw. Half mode doubles only the bottom bound and
     * samples every other clip row, starting at its actual (possibly odd) top.
     * Source/destination addressing uses absolute coordinates, without subtracting
     * bounds or reference offsets. Invalid spans fail before that span is copied;
     * preceding valid spans remain copied. */
    int mm_depth_draw(const MMDepthBitmap *, void *pixels, MMI32 row_bytes, MMI32 height,
                            MMI32 x, MMI32 y, const MMDepthRect *clip, const MMRegion *, int half);
#ifdef __cplusplus
}
#endif
#endif

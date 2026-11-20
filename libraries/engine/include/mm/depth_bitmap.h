/* Depth bitmap policy converted from bren/zbmp.cpp. */
#ifndef MM_ENGINE_DEPTH_BITMAP_H
#define MM_ENGINE_DEPTH_BITMAP_H
#include "block_owner.h"
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
    typedef struct MMDepthServices
    {
        void *context;
        int host_big_endian;
        MMU32 system;
        MMI32 object_bytes;
        void *(*create)(void *);
        MMDepthBitmap *(*state)(void *, void *);
        void (*release)(void *, void *);
        int (*allocate)(void *, MMI32, void **);
        void (*free_memory)(void *, void **);
        void (*warning)(void *);
        MMBlockTransform decode;
        int (*add)(void *, void *file, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *);
        void (*publish_fetch)(void *, int valid, void *object, MMI32 bytes);
    } MMDepthServices;
    /* Fresh host objects provide zeroed state. C owns pixel storage. New fills with
     * 0xff unless initial is supplied (a contiguous copy, ignoring renderer stride).
     * Read ignores bo/osk and decodes shorts in host order, as the native reader did.
     * Zero-sized serialized layouts are accepted; negative layouts are rejected. */
    void *mm_depth_new(MMI32 width, MMI32 height, const void *initial, const MMDepthServices *);
    void mm_depth_dispose(MMDepthBitmap *, const MMDepthServices *);
    int mm_depth_header(const unsigned char[12], int host_big, MMDepthBitmap *);
    /* Read a decoded ZBMP into an empty owner. Failure leaves it unchanged. */
    int mm_depth_read_stream(const MMReadStream *, MMDepthBitmap *, const MMDepthServices *);
    void *mm_depth_read(MMOwnedBlock *, const MMDepthServices *);
    int mm_depth_fetch(MMOwnedBlock *, int want_object, const MMDepthServices *);
    int mm_depth_write(const MMDepthBitmap *, void *file, MMU32 type, MMU32 *number,
                             const MMDepthServices *);
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

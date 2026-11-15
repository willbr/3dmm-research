/* Depth bitmap policy converted from bren/zbmp.cpp. */
#ifndef MM_ENGINE_DEPTH_BITMAP_H
#define MM_ENGINE_DEPTH_BITMAP_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMDepthRect
    {
        MMI32 left, top, right, bottom;
    } MMDepthRect;
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
        int (*unpack)(void *, void *);
        MMI32 (*size)(void *, void *, int packed);
        int (*read)(void *, void *, void *, MMI32 bytes, MMI32 offset);
        int (*write)(void *, void *, const void *, MMI32 bytes, MMI32 offset);
        void *(*add)(void *, void *file, MMU32 type, MMI32 bytes, MMU32 *number);
        void (*publish_fetch)(void *, int valid, void *object, MMI32 bytes);
        int (*region_bounds)(void *, void *region, MMDepthRect *);
        void (*scan_init)(void *, void *region, const MMDepthRect *clip, const MMDepthRect *bounds);
        int (*scan_pair)(void *, MMI32 *left, MMI32 *right);
        void (*scan_next)(void *, MMI32 rows);
    } MMDepthServices;
    /* Fresh host objects provide zeroed state. C owns pixel storage. New fills with
     * 0xff unless initial is supplied (a contiguous copy, ignoring renderer stride).
     * Read ignores bo/osk and decodes shorts in host order, as the native reader did.
     * Zero-sized serialized layouts are accepted; negative layouts are rejected. */
    void *mm_depth_new(MMI32 width, MMI32 height, const void *initial, const MMDepthServices *);
    void mm_depth_dispose(MMDepthBitmap *, const MMDepthServices *);
    int mm_depth_header(const unsigned char[12], int host_big, MMDepthBitmap *);
    void *mm_depth_read(void *block, const MMDepthServices *);
    int mm_depth_fetch(void *block, int want_object, const MMDepthServices *);
    int mm_depth_write(const MMDepthBitmap *, void *file, MMU32 type, MMU32 *number,
                             const MMDepthServices *);
    /* No clip/region means no draw. Half mode doubles only the bottom bound and
     * samples every other clip row, starting at its actual (possibly odd) top.
     * Source/destination addressing uses absolute coordinates, without subtracting
     * bounds or reference offsets. Invalid spans fail before that span is copied;
     * preceding valid spans remain copied. Callbacks must not reenter or mutate state. */
    int mm_depth_draw(const MMDepthBitmap *, void *pixels, MMI32 row_bytes, MMI32 height,
                            MMI32 x, MMI32 y, const MMDepthRect *clip, void *region, int half,
                            const MMDepthServices *);
#ifdef __cplusplus
}
#endif
#endif

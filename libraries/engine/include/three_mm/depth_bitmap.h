/* Depth bitmap policy converted from bren/zbmp.cpp. */
#ifndef THREE_MM_ENGINE_DEPTH_BITMAP_H
#define THREE_MM_ENGINE_DEPTH_BITMAP_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMDepthRect
    {
        ThreeMMI32 left, top, right, bottom;
    } ThreeMMDepthRect;
    typedef struct ThreeMMDepthBitmap
    {
        ThreeMMDepthRect bounds;
        ThreeMMI32 row_bytes, bytes;
        void *pixels;
    } ThreeMMDepthBitmap;
    typedef struct ThreeMMDepthServices
    {
        void *context;
        int host_big_endian;
        ThreeMMU32 system;
        ThreeMMI32 object_bytes;
        void *(*create)(void *);
        ThreeMMDepthBitmap *(*state)(void *, void *);
        void (*release)(void *, void *);
        int (*allocate)(void *, ThreeMMI32, void **);
        void (*free_memory)(void *, void **);
        void (*warning)(void *);
        int (*unpack)(void *, void *);
        ThreeMMI32 (*size)(void *, void *, int packed);
        int (*read)(void *, void *, void *, ThreeMMI32 bytes, ThreeMMI32 offset);
        int (*write)(void *, void *, const void *, ThreeMMI32 bytes, ThreeMMI32 offset);
        void *(*add)(void *, void *file, ThreeMMU32 type, ThreeMMI32 bytes, ThreeMMU32 *number);
        void (*publish_fetch)(void *, int valid, void *object, ThreeMMI32 bytes);
        int (*region_bounds)(void *, void *region, ThreeMMDepthRect *);
        void (*scan_init)(void *, void *region, const ThreeMMDepthRect *clip, const ThreeMMDepthRect *bounds);
        int (*scan_pair)(void *, ThreeMMI32 *left, ThreeMMI32 *right);
        void (*scan_next)(void *, ThreeMMI32 rows);
    } ThreeMMDepthServices;
    /* Fresh host objects provide zeroed state. C owns pixel storage. New fills with
     * 0xff unless initial is supplied (a contiguous copy, ignoring renderer stride).
     * Read ignores bo/osk and decodes shorts in host order, as the native reader did.
     * Zero-sized serialized layouts are accepted; negative layouts are rejected. */
    void *three_mm_depth_new(ThreeMMI32 width, ThreeMMI32 height, const void *initial, const ThreeMMDepthServices *);
    void three_mm_depth_dispose(ThreeMMDepthBitmap *, const ThreeMMDepthServices *);
    int three_mm_depth_header(const unsigned char[12], int host_big, ThreeMMDepthBitmap *);
    void *three_mm_depth_read(void *block, const ThreeMMDepthServices *);
    int three_mm_depth_fetch(void *block, int want_object, const ThreeMMDepthServices *);
    int three_mm_depth_write(const ThreeMMDepthBitmap *, void *file, ThreeMMU32 type, ThreeMMU32 *number,
                             const ThreeMMDepthServices *);
    /* No clip/region means no draw. Half mode doubles only the bottom bound and
     * samples every other clip row, starting at its actual (possibly odd) top.
     * Source/destination addressing uses absolute coordinates, without subtracting
     * bounds or reference offsets. Invalid spans fail before that span is copied;
     * preceding valid spans remain copied. Callbacks must not reenter or mutate state. */
    int three_mm_depth_draw(const ThreeMMDepthBitmap *, void *pixels, ThreeMMI32 row_bytes, ThreeMMI32 height,
                            ThreeMMI32 x, ThreeMMI32 y, const ThreeMMDepthRect *clip, void *region, int half,
                            const ThreeMMDepthServices *);
#ifdef __cplusplus
}
#endif
#endif

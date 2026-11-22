/* World buffer ownership/control converted from bren/bwld.cpp. */
#ifndef MM_ENGINE_WORLD_BUFFERS_H
#define MM_ENGINE_WORLD_BUFFERS_H
#include "depth_bitmap.h"
#include "masked_bitmap.h"
#include "pixel_storage.h"
#include "region.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMWorldPixelView
    {
        MMI32 row, width, height, x, y;
        unsigned type;
        void *pixels;
    } MMWorldPixelView;
    typedef struct MMWorldPixels
    {
        unsigned char *pixels;
        MMI32 row;
        size_t bytes;
    } MMWorldPixels;
    typedef struct MMWorldBuffers
    {
        MMI32 width, height, buffer_width, buffer_height, scale;
        int half_x, half_y;
        MMDepthOwner *depth_background, *depth_working;
        MMResourceCache *cache;
        MMRegion dirty_working, dirty_screen;
        MMPixelBuffer background, working;
        MMWorldPixelView rgb, z;
        MMU32 rgb_type, rgb_number, z_type, z_number;
    } MMWorldBuffers;
    typedef struct MMWorldBufferServices
    {
        void *context;
        unsigned rgb_type, depth_type;
        const MMDepthOwnerHost *depth_host;
        MMResourceBlockReader read_colour, read_depth;
        void (*setup_scene)(void *);
        void (*changed)(void *);
    } MMWorldBufferServices;
    /* Initialize a zeroed, unique owning record. Native SDK pixelmaps borrow
     * the current buffer views. Viewports are capped at 4096 per axis.
     * Scene setup occurs after buffers and before dirty-region construction,
     * matching the native order.
     * Callbacks must not reenter/mutate the same world. */
    int mm_world_buffers_init(MMWorldBuffers *, MMI32 width, MMI32 height, MMI32 scale,
                                    int half_x, int half_y, const MMWorldBufferServices *);
    void mm_world_buffers_dispose(MMWorldBuffers *);
    /* Mark the full working buffer dirty and optionally the screen copy. */
    void mm_world_buffers_dirty(MMWorldBuffers *, int screen);
    int mm_world_pixels_valid(const MMWorldPixels *, MMI32 width, MMI32 height);
    /* A view borrows the given buffer's current storage. */
    MMWorldPixels mm_world_buffer_pixels(const MMPixelBuffer *);
    /* Copy indices between disjoint spans with COLORONCOLOR sampling.
     * Padding stays untouched. */
    int mm_world_copy_rgb(const MMWorldPixels *source, MMI32 source_width, MMI32 source_height,
                          const MMWorldPixels *target, MMI32 target_width, MMI32 target_height);
    /* Draw masked indices at the viewport's top left. Resizing samples a
     * zero-filled native-size buffer. Equal sizes preserve transparent pixels
     * in the supplied target. Used by live backgrounds and bitmap export. */
    int mm_world_fill_background_rgb(const MMMaskedBitmap *, MMI32 width, MMI32 height,
                                     const MMWorldPixels *target, MMI32 target_width, MMI32 target_height);
    int mm_world_depth_valid(const MMDepthBitmap *, MMI32 width, MMI32 height);
    /* Draw with the stored depth coordinates and retain any completed spans
     * if the source ends early. Returns zero for an invalid destination. */
    int mm_world_draw_depth(const MMDepthBitmap *source, const MMDepthBitmap *target, MMI32 width, MMI32 height);
    /* Shared live/export nearest-neighbor copying of opaque two-byte samples.
     * Scale is 1..16; half-axis sampling applies only at scale 1. Failure may
     * retain completed prefix copies, but never reads/writes outside spans. */
    int mm_world_depth_resample(const MMDepthBitmap *, const MMDepthBitmap *, MMI32 width,
                                      MMI32 height, MMI32 scale, int half_x, int half_y);
    /* Mode changes hold old buffers until allocation/background reload succeeds;
     * failure restores owners, descriptors and mode. Dirty/cache owners survive. */
    int mm_world_buffers_mode(MMWorldBuffers *, int half_x, int half_y, MMI32 scale,
                                    const MMWorldBufferServices *);
    int mm_world_buffers_resolution(MMWorldBuffers *, MMI32 scale, int reduced,
                                          const MMWorldBufferServices *);
    int mm_world_buffers_background(MMWorldBuffers *, MMResourceCache *, MMU32 rgb_type,
                                          MMU32 rgb_number, MMU32 z_type, MMU32 z_number,
                                          const MMWorldBufferServices *);
    /* Reload the retained background. Returns zero if it is absent or fails to load. */
    int mm_world_buffers_restore_background(MMWorldBuffers *, const MMWorldBufferServices *);
/* Copies opaque two-byte depth samples without interpolation. Reduced mode
 * samples every other column/row; scaled mode first draws into a full viewport
 * depth buffer, preserving source origins. Partial RGB work is retained if a
 * later depth allocation/validation fails, as in the original controller. */
#ifdef __cplusplus
}
#endif
#endif

/* World buffer ownership/control converted from bren/bwld.cpp. */
#ifndef MM_ENGINE_WORLD_BUFFERS_H
#define MM_ENGINE_WORLD_BUFFERS_H
#include "depth_bitmap.h"
#include "masked_bitmap.h"
#include "region.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_WORLD_DEPTH_BACKGROUND,
        MM_WORLD_DEPTH_WORKING,
        MM_WORLD_RGB_BACKGROUND,
        MM_WORLD_RGB_WORKING,
        MM_WORLD_RGB_STRETCH,
        MM_WORLD_CACHE,
        MM_WORLD_OWNERS
    };
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
        void *owners[MM_WORLD_OWNERS];
        MMRegion dirty_working, dirty_screen;
        MMWorldPixelView rgb, z;
        MMU32 rgb_type, rgb_number, z_type, z_number;
    } MMWorldBuffers;
    typedef struct MMWorldBufferServices
    {
        void *context;
        unsigned rgb_type, depth_type;
        void *(*create)(void *, int kind, MMI32 width, MMI32 height);
        void (*retain_cache)(void *, void *), (*release_cache)(void *, void *);
        void (*release)(void *, void *);
        void (*release_depth)(void *, void *), (*release_bitmap)(void *, void *);
        /* A successful lock borrows the full indexed span until unlock. Failed
         * locks acquire nothing. Distinct RGB owners have disjoint spans. */
        int (*lock)(void *, void *, MMWorldPixels *);
        void (*unlock)(void *, void *);
        const MMDepthBitmap *(*depth_bitmap)(void *, void *);
        const MMMaskedBitmap *(*masked_bitmap)(void *, void *);
        void (*setup_scene)(void *);
        void (*changed)(void *);
        void *(*fetch)(void *, void *cache, int kind, MMU32 type, MMU32 number);
        void (*priority)(void *, void *);
        void (*priority_depth)(void *, void *);
    } MMWorldBufferServices;
    /* Initialize a zeroed, unique owning record. Native SDK pixelmaps borrow
     * the current buffer views. Viewports are capped at 4096 per axis.
     * Working RGB retains one lock until replacement/disposal. Scene setup occurs
     * after buffers and before dirty-region construction, matching the native order.
     * Callbacks must not reenter/mutate the same world. */
    int mm_world_buffers_init(MMWorldBuffers *, MMI32 width, MMI32 height, MMI32 scale,
                                    int half_x, int half_y, const MMWorldBufferServices *);
    void mm_world_buffers_dispose(MMWorldBuffers *, const MMWorldBufferServices *);
    /* Mark the full working buffer dirty and optionally the screen copy. */
    void mm_world_buffers_dirty(MMWorldBuffers *, int screen);
    int mm_world_pixels_valid(const MMWorldPixels *, MMI32 width, MMI32 height);
    /* Copy raw indices between distinct ports using the shared palette and
     * COLORONCOLOR sampling. Padding stays untouched. */
    int mm_world_copy_rgb(const MMWorldBufferServices *, void *source, MMI32 source_width, MMI32 source_height,
                          void *target, MMI32 target_width, MMI32 target_height);
    /* Draw masked indices at the viewport's top left, then stretch through a
     * temporary native-size port. Used by live backgrounds and bitmap export. */
    int mm_world_scale_background_rgb(const MMWorldBufferServices *, void *bitmap, MMI32 width, MMI32 height,
                                      void *target, MMI32 target_width, MMI32 target_height);
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
    int mm_world_buffers_background(MMWorldBuffers *, void *cache, MMU32 rgb_type,
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

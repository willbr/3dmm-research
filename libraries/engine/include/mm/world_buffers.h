/* World buffer ownership/control converted from bren/bwld.cpp. */
#ifndef MM_ENGINE_WORLD_BUFFERS_H
#define MM_ENGINE_WORLD_BUFFERS_H
#include "types.h"
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
        MM_WORLD_DIRTY_WORKING,
        MM_WORLD_DIRTY_SCREEN,
        MM_WORLD_CACHE,
        MM_WORLD_OWNERS
    };
    typedef struct MMWorldPixelView
    {
        MMI32 row, width, height, x, y;
        unsigned type;
        void *pixels;
    } MMWorldPixelView;
    typedef struct MMWorldDepthSpan
    {
        unsigned char *pixels;
        MMI32 row, bytes;
    } MMWorldDepthSpan;
    typedef struct MMWorldBuffers
    {
        MMI32 width, height, buffer_width, buffer_height, scale;
        int half_x, half_y;
        void *owners[MM_WORLD_OWNERS];
        MMWorldPixelView rgb, z;
        MMU32 rgb_type, rgb_number, z_type, z_number;
    } MMWorldBuffers;
    typedef struct MMWorldBufferServices
    {
        void *context;
        unsigned rgb_type, depth_type;
        void *(*create)(void *, int kind, MMI32 width, MMI32 height);
        void (*retain)(void *, void *), (*release)(void *, void *);
        void *(*lock)(void *, void *);
        void (*unlock)(void *, void *);
        void (*depth_span)(void *, void *, MMWorldDepthSpan *);
        void (*publish)(void *, const MMWorldBuffers *);
        void (*setup_scene)(void *);
        void (*dirty)(void *, int working, int screen, int changed);
        void *(*fetch)(void *, void *cache, int kind, MMU32 type, MMU32 number);
        void (*priority)(void *, void *);
        void (*draw_bitmap)(void *, void *port, void *bitmap);
        void (*copy_rgb)(void *, void *source, void *destination, MMI32 source_width, MMI32 source_height,
                         MMI32 destination_width, MMI32 destination_height);
        void (*draw_depth)(void *, void *source, void *destination, MMI32 width, MMI32 height);
    } MMWorldBufferServices;
    /* Initialize a zeroed, unique owning record. Native objects/SDK pixelmaps are
     * borrowed views published by the host. Viewports are capped at 4096 per axis.
     * Working RGB retains one lock until replacement/disposal. Scene setup occurs
     * after buffers and before dirty-region construction, matching the native order.
     * Callbacks must not reenter/mutate the same world. */
    int mm_world_buffers_init(MMWorldBuffers *, MMI32 width, MMI32 height, MMI32 scale,
                                    int half_x, int half_y, const MMWorldBufferServices *);
    void mm_world_buffers_dispose(MMWorldBuffers *, const MMWorldBufferServices *);
    /* Shared live/export nearest-neighbor copying of opaque two-byte samples.
     * Scale is 1..16; half-axis sampling applies only at scale 1. Failure may
     * retain completed prefix copies, but never reads/writes outside spans. */
    int mm_world_depth_resample(const MMWorldDepthSpan *, const MMWorldDepthSpan *, MMI32 width,
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
/* Copies opaque two-byte depth samples without interpolation. Reduced mode
 * samples every other column/row; scaled mode first draws into a full viewport
 * depth buffer, preserving source origins. Partial RGB work is retained if a
 * later depth allocation/validation fails, as in the original controller. */
#ifdef __cplusplus
}
#endif
#endif

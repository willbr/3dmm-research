/* World buffer ownership/control converted from bren/bwld.cpp. */
#ifndef THREE_MM_ENGINE_WORLD_BUFFERS_H
#define THREE_MM_ENGINE_WORLD_BUFFERS_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_WORLD_DEPTH_BACKGROUND,
        THREE_MM_WORLD_DEPTH_WORKING,
        THREE_MM_WORLD_RGB_BACKGROUND,
        THREE_MM_WORLD_RGB_WORKING,
        THREE_MM_WORLD_RGB_STRETCH,
        THREE_MM_WORLD_DIRTY_WORKING,
        THREE_MM_WORLD_DIRTY_SCREEN,
        THREE_MM_WORLD_CACHE,
        THREE_MM_WORLD_OWNERS
    };
    typedef struct ThreeMMWorldPixelView
    {
        ThreeMMI32 row, width, height, x, y;
        unsigned type;
        void *pixels;
    } ThreeMMWorldPixelView;
    typedef struct ThreeMMWorldDepthSpan
    {
        unsigned char *pixels;
        ThreeMMI32 row, bytes;
    } ThreeMMWorldDepthSpan;
    typedef struct ThreeMMWorldBuffers
    {
        ThreeMMI32 width, height, buffer_width, buffer_height, scale;
        int half_x, half_y;
        void *owners[THREE_MM_WORLD_OWNERS];
        ThreeMMWorldPixelView rgb, z;
        ThreeMMU32 rgb_type, rgb_number, z_type, z_number;
    } ThreeMMWorldBuffers;
    typedef struct ThreeMMWorldBufferServices
    {
        void *context;
        unsigned rgb_type, depth_type;
        void *(*create)(void *, int kind, ThreeMMI32 width, ThreeMMI32 height);
        void (*retain)(void *, void *), (*release)(void *, void *);
        void *(*lock)(void *, void *);
        void (*unlock)(void *, void *);
        void (*depth_span)(void *, void *, ThreeMMWorldDepthSpan *);
        void (*publish)(void *, const ThreeMMWorldBuffers *);
        void (*setup_scene)(void *);
        void (*dirty)(void *, int working, int screen, int changed);
        void *(*fetch)(void *, void *cache, int kind, ThreeMMU32 type, ThreeMMU32 number);
        void (*priority)(void *, void *);
        void (*draw_bitmap)(void *, void *port, void *bitmap);
        void (*copy_rgb)(void *, void *source, void *destination, ThreeMMI32 source_width, ThreeMMI32 source_height,
                         ThreeMMI32 destination_width, ThreeMMI32 destination_height);
        void (*draw_depth)(void *, void *source, void *destination, ThreeMMI32 width, ThreeMMI32 height);
    } ThreeMMWorldBufferServices;
    /* Initialize a zeroed, unique owning record. Native objects/SDK pixelmaps are
     * borrowed views published by the host. Viewports are capped at 4096 per axis.
     * Working RGB retains one lock until replacement/disposal. Scene setup occurs
     * after buffers and before dirty-region construction, matching the native order.
     * Callbacks must not reenter/mutate the same world. */
    int three_mm_world_buffers_init(ThreeMMWorldBuffers *, ThreeMMI32 width, ThreeMMI32 height, ThreeMMI32 scale,
                                    int half_x, int half_y, const ThreeMMWorldBufferServices *);
    void three_mm_world_buffers_dispose(ThreeMMWorldBuffers *, const ThreeMMWorldBufferServices *);
    /* Mode changes hold old buffers until allocation/background reload succeeds;
     * failure restores owners, descriptors and mode. Dirty/cache owners survive. */
    int three_mm_world_buffers_mode(ThreeMMWorldBuffers *, int half_x, int half_y, ThreeMMI32 scale,
                                    const ThreeMMWorldBufferServices *);
    int three_mm_world_buffers_resolution(ThreeMMWorldBuffers *, ThreeMMI32 scale, int reduced,
                                          const ThreeMMWorldBufferServices *);
    int three_mm_world_buffers_background(ThreeMMWorldBuffers *, void *cache, ThreeMMU32 rgb_type,
                                          ThreeMMU32 rgb_number, ThreeMMU32 z_type, ThreeMMU32 z_number,
                                          const ThreeMMWorldBufferServices *);
/* Copies opaque two-byte depth samples without interpolation. Reduced mode
 * samples every other column/row; scaled mode first draws into a full viewport
 * depth buffer, preserving source origins. Partial RGB work is retained if a
 * later depth allocation/validation fails, as in the original controller. */
#ifdef __cplusplus
}
#endif
#endif

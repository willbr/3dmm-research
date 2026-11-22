/* World bitmap export control, independent of native debug/front-end APIs. */
#ifndef MM_ENGINE_WORLD_EXPORT_H
#define MM_ENGINE_WORLD_EXPORT_H
#include "world_render.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMWorldExportServices
    {
        void *context;
        MMWorldBufferServices buffers;
        MMWorldRenderServices render;
        void *(*palette)(void *);
        void (*bounds_callback)(void *, int enabled);
        void (*raster)(void *, const MMRenderFrame *, const MMWorldPixelView *rgb, const MMWorldPixelView *depth);
        int (*write)(void *, const MMWorldPixels *, void *palette, MMI32 width, MMI32 height);
    } MMWorldExportServices;
    /* Services provide retained cache fetches and owned palette copies. Release
     * palettes through buffers.release. The current-frame export borrows the
     * working port through buffers.lock/unlock. Callbacks must not reenter
     * rendering or replace/dispose the world, cache, actors or material registry. */
    int mm_world_export_size(const MMWorldBuffers *, MMI32 requested_scale, MMI32 *scale,
                                   MMI32 *width, MMI32 *height);
    /* Current exports the existing working buffer without rendering. Scaled
     * exports use native view dimensions, ignoring live half/scale preferences.
     * C owns its zeroed RGB and FF-filled depth buffers. The legacy tight
     * renderer row is retained and the writer receives actual pitch. Both paths
     * release their palette copy. Scaled exports restore material state and the
     * bounds callback before flushing and writing. */
    int mm_world_export_current(const MMWorldBuffers *, const MMWorldExportServices *);
    int mm_world_export_scaled(const MMWorldBuffers *, MMI32 requested_scale,
                                     const MMWorldExportServices *);
#ifdef __cplusplus
}
#endif
#endif

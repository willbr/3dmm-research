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
        int (*lock)(void *, void *port, MMWorldPixels *);
        void (*bounds_callback)(void *, int enabled);
        void (*raster)(void *, const MMWorldPixelView *rgb, const MMWorldPixelView *depth);
        int (*write)(void *, const MMWorldPixels *, void *palette, MMI32 width, MMI32 height);
    } MMWorldExportServices;
    /* Services provide uniquely owned zero-filled RGB ports and FF-filled depth
     * buffers, retained cache fetches, and owned palette copies. Release handles
     * through buffers.release; successful locks through buffers.unlock. Failed
     * lock callbacks acquire no lock. Callbacks must not reenter rendering or
     * replace/dispose the world, cache, actors or material registry. */
    int mm_world_export_size(const MMWorldBuffers *, MMI32 requested_scale, MMI32 *scale,
                                   MMI32 *width, MMI32 *height);
    /* Background fill ignores cached origins, matching the original exporter;
     * source rows are sampled directly. No-cache leaves default fills intact. */
    int mm_world_export_background(const MMWorldBuffers *, void *rgb, void *depth, MMI32 scale,
                                         const MMWorldBufferServices *);
    /* Current exports the existing working buffer without rendering. Scaled
     * exports use native view dimensions, ignoring live half/scale preferences.
     * The legacy tight renderer row is retained; write receives actual pitch.
     * Both paths release their palette copy on every path. Scaled exports restore
     * material state and bounds callback, then flush/unlock before writing. */
    int mm_world_export_current(const MMWorldBuffers *, const MMWorldExportServices *);
    int mm_world_export_scaled(const MMWorldBuffers *, MMI32 requested_scale,
                                     const MMWorldExportServices *);
#ifdef __cplusplus
}
#endif
#endif

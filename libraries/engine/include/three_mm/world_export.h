/* World bitmap export control, independent of native debug/front-end APIs. */
#ifndef THREE_MM_ENGINE_WORLD_EXPORT_H
#define THREE_MM_ENGINE_WORLD_EXPORT_H
#include "world_render.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMWorldExportServices
    {
        void *context;
        ThreeMMWorldBufferServices buffers;
        ThreeMMWorldRenderServices render;
        void *(*palette)(void *);
        int (*lock)(void *, void *port, ThreeMMWorldPixels *);
        void (*bounds_callback)(void *, int enabled);
        void (*raster)(void *, const ThreeMMWorldPixelView *rgb, const ThreeMMWorldPixelView *depth);
        int (*write)(void *, const ThreeMMWorldPixels *, void *palette, ThreeMMI32 width, ThreeMMI32 height);
    } ThreeMMWorldExportServices;
    /* Services provide uniquely owned zero-filled RGB ports and FF-filled depth
     * buffers, retained cache fetches, and owned palette copies. Release handles
     * through buffers.release; successful locks through buffers.unlock. Failed
     * lock callbacks acquire no lock. Callbacks must not reenter rendering or
     * replace/dispose the world, cache, actors or material registry. */
    int three_mm_world_export_size(const ThreeMMWorldBuffers *, ThreeMMI32 requested_scale, ThreeMMI32 *scale,
                                   ThreeMMI32 *width, ThreeMMI32 *height);
    /* Background fill ignores cached origins, matching the original exporter;
     * source rows are sampled directly. No-cache leaves default fills intact. */
    int three_mm_world_export_background(const ThreeMMWorldBuffers *, void *rgb, void *depth, ThreeMMI32 scale,
                                         const ThreeMMWorldBufferServices *);
    /* Current exports the existing working buffer without rendering. Scaled
     * exports use native view dimensions, ignoring live half/scale preferences.
     * The legacy tight renderer row is retained; write receives actual pitch.
     * Both paths release their palette copy on every path. Scaled exports restore
     * material state and bounds callback, then flush/unlock before writing. */
    int three_mm_world_export_current(const ThreeMMWorldBuffers *, const ThreeMMWorldExportServices *);
    int three_mm_world_export_scaled(const ThreeMMWorldBuffers *, ThreeMMI32 requested_scale,
                                     const ThreeMMWorldExportServices *);
#ifdef __cplusplus
}
#endif
#endif

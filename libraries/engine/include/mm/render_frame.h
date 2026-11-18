#ifndef MM_ENGINE_RENDER_FRAME_H
#define MM_ENGINE_RENDER_FRAME_H
#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct MMRenderFrame
{
    void *world, *camera, *colour, *depth;
    void *context;
    void (*release_world)(void *, void *), (*release_pixel)(void *, void *);
} MMRenderFrame;

typedef struct MMRenderFrameFactory
{
    void *(*colour)(void *, MMU32 width, MMU32 height);
    void *(*depth)(void *, void *colour);
    void *(*world)(void *);
    void (*free_world)(void *, void *), (*free_pixel)(void *, void *);
} MMRenderFrameFactory;

/* A frame either borrows all four handles or owns the world and pixelmaps
 * it allocates. Its camera is embedded in or attached to the world. Zero
 * initializes an empty owner. Borrow and allocate require an empty frame.
 * The synchronous provider and borrowed handles must remain valid while
 * the frame is in use. Allocation failure leaves any acquired prefix for
 * disposal. The world is released before its depth and colour targets. */
void mm_render_frame_borrow(MMRenderFrame *, void *world, void *camera, void *colour, void *depth);
int mm_render_frame_allocate_world(MMRenderFrame *, void *context, const MMRenderFrameFactory *);
int mm_render_frame_allocate(MMRenderFrame *, MMU32 width, MMU32 height, void *context,
                             const MMRenderFrameFactory *);
void mm_render_frame_dispose(MMRenderFrame *);

#ifdef __cplusplus
}
#endif
#endif

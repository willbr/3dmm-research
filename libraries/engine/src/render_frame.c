#include "mm/render_frame.h"
#include <string.h>

void mm_render_frame_borrow(MMRenderFrame *frame, void *world, void *camera, void *colour, void *depth)
{
    memset(frame, 0, sizeof(*frame));
    frame->world = world;
    frame->camera = camera;
    frame->colour = colour;
    frame->depth = depth;
}

int mm_render_frame_allocate_world(MMRenderFrame *frame, void *context, const MMRenderFrameFactory *factory)
{
    frame->context = context;
    frame->release_world = factory->free_world;
    frame->world = factory->world(context);
    return frame->world != NULL;
}

int mm_render_frame_allocate(MMRenderFrame *frame, MMU32 width, MMU32 height, void *context,
                             const MMRenderFrameFactory *factory)
{
    frame->release_pixel = factory->free_pixel;
    frame->colour = factory->colour(context, width, height);
    if (frame->colour)
        frame->depth = factory->depth(context, frame->colour);
    return mm_render_frame_allocate_world(frame, context, factory) && frame->colour && frame->depth;
}

void mm_render_frame_dispose(MMRenderFrame *frame)
{
    if (frame->world && frame->release_world)
        frame->release_world(frame->context, frame->world);
    if (frame->release_pixel)
    {
        if (frame->depth)
            frame->release_pixel(frame->context, frame->depth);
        if (frame->colour)
            frame->release_pixel(frame->context, frame->colour);
    }
    memset(frame, 0, sizeof(*frame));
}

#include "mm/render_frame.h"
#include "movie_session_internal.h"
#include <string.h>

void mm_movie_render_scene_init(MMMovieRenderScene *scene, MMMovieSession *session,
                               const MMStorageAllocator *allocator, void *context,
                               void (*retain)(void *, void *), void (*release)(void *, void *))
{
    memset(scene, 0, sizeof(*scene));
    mm_storage_init(&scene->storage, allocator);
    scene->session = session;
    scene->reuse_bodies = session && !allocator;
    if (scene->reuse_bodies)
    {
        scene->storage.buffers[0] = session->render_bodies;
        memset(&session->render_bodies, 0, sizeof(session->render_bodies));
    }
    scene->bodies.storage = &scene->storage;
    scene->bodies.width = sizeof(MMRenderBody);
    scene->bodies.free_count = -1;
    scene->context = context;
    if (retain && release)
    {
        scene->retain = retain;
        scene->release = release;
    }
}

int mm_movie_render_scene_add(MMMovieRenderScene *scene, const MMRenderBody *body)
{
    if (mm_array_add(&scene->bodies, body, NULL) != MM_STORAGE_OK)
        return 0;
    if (body->resource && scene->retain)
        scene->retain(scene->context, body->resource);
    return 1;
}

void mm_movie_render_scene_publish(MMMovieRenderScene *scene, const MMRenderCamera *camera,
                                    const MMMovieLiveFrame *position, MMRenderFrame *frame)
{
    scene->camera = *camera;
    if (position && (position->phase != MM_MOVIE_FRAME_PREPARED || !scene->session ||
                     scene->session->seeking_frame == position))
        scene->position = *position;
    else
        memset(&scene->position, 0, sizeof(scene->position));
    frame->scene = scene;
}

void mm_movie_render_scene_dispose(MMMovieRenderScene *scene)
{
    const MMRenderBody *bodies = (const MMRenderBody *)scene->storage.buffers[0].data;
    MMU32 i;
    if (scene->release)
        for (i = 0; i < scene->bodies.count; ++i)
            if (bodies[i].resource)
                scene->release(scene->context, bodies[i].resource);
    if (scene->reuse_bodies && scene->storage.buffers[0].capacity > scene->session->render_bodies.capacity)
    {
        MMStorageBuffer previous = scene->session->render_bodies;
        scene->session->render_bodies = scene->storage.buffers[0];
        scene->storage.buffers[0] = previous;
    }
    mm_storage_dispose(&scene->storage);
    scene->bodies.count = 0;
    scene->reuse_bodies = 0;
    scene->session = NULL;
}

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

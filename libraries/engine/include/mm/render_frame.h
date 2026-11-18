#ifndef MM_ENGINE_RENDER_FRAME_H
#define MM_ENGINE_RENDER_FRAME_H
#include "body_create.h"
#include "collections.h"
#include "movie_frame.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct MMRenderCamera
{
    MMI32 matrix[12], near_z, far_z, aspect, width, height;
    unsigned short fov;
    int parallel;
} MMRenderCamera;

typedef struct MMRenderBody
{
    MMBodyDrawView draw;
    void *resource, *target;
    float low[3], high[3];
    MMU32 order_low, order_high;
    MMI32 owner;
} MMRenderBody;

typedef struct MMMovieRenderScene
{
    MMRenderCamera camera;
    MMMovieLiveFrame position;
    MMCollectionStorage storage;
    MMArray bodies;
    struct MMMovieSession *session;
    int reuse_bodies;
    void *context;
    void (*retain)(void *, void *), (*release)(void *, void *);
} MMMovieRenderScene;

typedef struct MMRenderFrame
{
    void *world, *camera, *colour, *depth;
    const MMMovieRenderScene *scene;
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
/* Live frames collect body bindings in submission order. Their owners are
 * retained through submission when both callbacks are supplied. The bindings
 * stay stable throughout the synchronous render or pick. Publish after all
 * bodies and the actual projection camera have been selected. The raster
 * frame borrows the published scene until rendering finishes. Optionally borrow
 * default-heap body storage from a movie session that outlives the submission.
 * The session keeps capacity after disposal. Bodies, camera and host callbacks
 * remain local to each submission, including nested draws. Pass the live
 * frame record for the scene being drawn when it matches the scene cursor.
 * A prepared pose is valid only during its own session's finish callback. */
void mm_movie_render_scene_init(MMMovieRenderScene *, struct MMMovieSession *, const MMStorageAllocator *, void *context,
                               void (*retain)(void *, void *), void (*release)(void *, void *));
int mm_movie_render_scene_add(MMMovieRenderScene *, const MMRenderBody *);
void mm_movie_render_scene_publish(MMMovieRenderScene *, const MMRenderCamera *, const MMMovieLiveFrame *, MMRenderFrame *);
void mm_movie_render_scene_dispose(MMMovieRenderScene *);

#ifdef __cplusplus
}
#endif
#endif

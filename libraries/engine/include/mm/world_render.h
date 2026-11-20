/* World rendering policy; SDK and graphics primitives are host services. */
#ifndef MM_ENGINE_WORLD_RENDER_H
#define MM_ENGINE_WORLD_RENDER_H
#include "world_buffers.h"
#include "render_frame.h"
#include "depth_bitmap.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMWorldMaterial
    {
        void *object, *texture;
        MMU32 flags;
        unsigned char base, range;
    } MMWorldMaterial;
    typedef struct MMWorldMaterialServices
    {
        void *context;
        MMU32 gouraud;
        size_t (*count)(void *);
        void (*enumerate)(void *, int (*visit)(void *, void *material), void *argument);
        void (*read)(void *, void *, MMWorldMaterial *);
        void (*write)(void *, const MMWorldMaterial *);
        void *(*allocate)(void *, size_t);
        void (*free)(void *, void *);
    } MMWorldMaterialServices;
    typedef struct MMWorldMaterials
    {
        MMWorldMaterial *saved;
        size_t count, capacity;
        int flat, no_texture, failed;
    } MMWorldMaterials;
    /* Begin initializes a fresh scope. Failure restores the completed prefix.
     * End restores forward enumeration order. Borrowed material/texture handles
     * must stay valid and the registry must not mutate during the scope. */
    int mm_world_materials_begin(MMWorldMaterials *, int flat, int no_texture,
                                       const MMWorldMaterialServices *);
    void mm_world_materials_end(MMWorldMaterials *, const MMWorldMaterialServices *);
    typedef struct MMWorldRender
    {
        int changed;
    } MMWorldRender;
    typedef struct MMWorldPixels
    {
        unsigned char *pixels;
        MMI32 row;
        size_t bytes;
    } MMWorldPixels;
    enum
    {
        MM_WORLD_BEGIN,
        MM_WORLD_RECT
    };
    typedef struct MMWorldRenderServices
    {
        void *context;
        struct MMMovieSession *movie;
        const MMMovieLiveFrame *position;
        MMRenderFrame frame;
        MMRenderCamera camera;
        int (*body)(void *, void *actor, MMRenderBody *);
        void (*prepare_body)(void *, const MMRenderBody *);
        void (*retain_body)(void *, void *), (*release_body)(void *, void *);
        int actor_none, actor_model, bounding_points, bounding_edges, bounding_faces, faces, edges;
        void *(*child)(void *, void *), *(*next)(void *, void *);
        int (*type)(void *, void *), (*style)(void *, void *);
        void (*set_style)(void *, void *, int);
        int (*has)(void *, int callback);
        void (*begin)(void *, void *);
        void (*rect)(void *, void *, MMI32 rect[4]);
        void (*options)(void *, int *wire, int *flat, int *no_texture);
        const MMStorageAllocator *region_allocator;
        const MMDepthBitmap *depth_background;
        /* A successful lock must be paired with unlock, including scan failure. */
        int (*lock)(void *, int buffer, MMWorldPixels *);
        void (*unlock)(void *, int);
        void (*render)(void *, const MMRenderFrame *);
        void (*detach_depth)(void *, void *depth), (*flush)(void *);
        void (*screen_mark)(void *, const MMRegion *);
        void (*draw)(void *, const MMI32 source[4], const MMI32 destination[4]);
        MMWorldMaterialServices materials;
    } MMWorldRenderServices;
    /* Unique state; begin registration must stay callable during its pass.
     * Rectangle registration may change and callbacks may mark dirty. Callbacks
     * must not reenter rendering or replace/dispose the world resources. */
    void mm_world_render_init(MMWorldRender *);
    void mm_world_render_dirty(MMWorldRender *);
    void mm_world_render_styles(void *root, int style, const MMWorldRenderServices *);
    int mm_world_render_clean(const MMWorldBuffers *, const MMWorldRenderServices *);
    int mm_world_render(MMWorldRender *, MMWorldBuffers *, const MMWorldRenderServices *);
    int mm_world_prerender(MMWorldRender *, MMWorldBuffers *, const MMWorldRenderServices *,
                           const MMWorldBufferServices *);
    void mm_world_render_mark(MMWorldBuffers *, MMI32 x, MMI32 y,
                                    const MMWorldRenderServices *);
    int mm_world_render_draw(const MMWorldBuffers *, MMI32 x, MMI32 y,
                                   const MMWorldRenderServices *);
#ifdef __cplusplus
}
#endif
#endif

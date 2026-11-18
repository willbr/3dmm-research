/* World rendering policy; SDK and graphics primitives are host services. */
#ifndef MM_ENGINE_WORLD_RENDER_H
#define MM_ENGINE_WORLD_RENDER_H
#include "world_buffers.h"
#include "render_frame.h"
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
        int (*region_bounds)(void *, int region, MMI32 rect[4]);
        void (*region_clear)(void *, int), (*region_union)(void *, int destination, int source);
        void (*region_add)(void *, int, const MMI32 rect[4]);
        void (*depth_clean)(void *, const MMI32 rect[4]);
        /* A successful lock must be paired with unlock, including scan failure. */
        int (*lock)(void *, int buffer, MMWorldPixels *);
        void (*unlock)(void *, int);
        void (*scan_init)(void *, const MMI32 rect[4]);
        /* Spans are relative to the clipped scan rectangle. End releases any
         * borrowed region storage before the controller mutates regions. */
        int (*scan_span)(void *, MMI32 *left, MMI32 *right);
        void (*scan_next)(void *), (*scan_end)(void *);
        void (*render)(void *, const MMRenderFrame *), (*publish)(void *, const MMWorldRender *);
        void (*snapshot_depth)(void *), (*detach_depth)(void *), (*snapshot_rgb)(void *), (*flush)(void *);
        void (*restore_background)(void *);
        void (*screen_scale)(void *, MMI32 nx, MMI32 dx, MMI32 ny, MMI32 dy);
        void (*screen_offset)(void *, MMI32 x, MMI32 y), (*screen_mark)(void *);
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
    int mm_world_render(MMWorldRender *, const MMWorldBuffers *, const MMWorldRenderServices *);
    int mm_world_prerender(MMWorldRender *, const MMWorldBuffers *, const MMWorldRenderServices *);
    void mm_world_unprerender(const MMWorldRenderServices *);
    void mm_world_render_mark(const MMWorldBuffers *, MMI32 x, MMI32 y,
                                    const MMWorldRenderServices *);
    int mm_world_render_draw(const MMWorldBuffers *, MMI32 x, MMI32 y,
                                   const MMWorldRenderServices *);
#ifdef __cplusplus
}
#endif
#endif

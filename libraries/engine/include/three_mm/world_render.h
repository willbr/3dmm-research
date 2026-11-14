/* World rendering policy; SDK and graphics primitives are host services. */
#ifndef THREE_MM_ENGINE_WORLD_RENDER_H
#define THREE_MM_ENGINE_WORLD_RENDER_H
#include "world_buffers.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMWorldMaterial
    {
        void *object, *texture;
        ThreeMMU32 flags;
        unsigned char base, range;
    } ThreeMMWorldMaterial;
    typedef struct ThreeMMWorldMaterialServices
    {
        void *context;
        ThreeMMU32 gouraud;
        size_t (*count)(void *);
        void (*enumerate)(void *, int (*visit)(void *, void *material), void *argument);
        void (*read)(void *, void *, ThreeMMWorldMaterial *);
        void (*write)(void *, const ThreeMMWorldMaterial *);
        void *(*allocate)(void *, size_t);
        void (*free)(void *, void *);
    } ThreeMMWorldMaterialServices;
    typedef struct ThreeMMWorldMaterials
    {
        ThreeMMWorldMaterial *saved;
        size_t count, capacity;
        int flat, no_texture, failed;
    } ThreeMMWorldMaterials;
    /* Begin initializes a fresh scope. Failure restores the completed prefix.
     * End restores forward enumeration order. Borrowed material/texture handles
     * must stay valid and the registry must not mutate during the scope. */
    int three_mm_world_materials_begin(ThreeMMWorldMaterials *, int flat, int no_texture,
                                       const ThreeMMWorldMaterialServices *);
    void three_mm_world_materials_end(ThreeMMWorldMaterials *, const ThreeMMWorldMaterialServices *);
    typedef struct ThreeMMWorldRender
    {
        int changed;
    } ThreeMMWorldRender;
    typedef struct ThreeMMWorldPixels
    {
        unsigned char *pixels;
        ThreeMMI32 row;
        size_t bytes;
    } ThreeMMWorldPixels;
    enum
    {
        THREE_MM_WORLD_BEGIN,
        THREE_MM_WORLD_RECT
    };
    typedef struct ThreeMMWorldRenderServices
    {
        void *context, *root;
        int actor_none, actor_model, bounding_points, bounding_edges, bounding_faces, faces, edges;
        void *(*child)(void *, void *), *(*next)(void *, void *);
        int (*type)(void *, void *), (*style)(void *, void *);
        void (*set_style)(void *, void *, int);
        int (*has)(void *, int callback);
        void (*begin)(void *, void *);
        void (*rect)(void *, void *, ThreeMMI32 rect[4]);
        void (*options)(void *, int *wire, int *flat, int *no_texture);
        int (*region_bounds)(void *, int region, ThreeMMI32 rect[4]);
        void (*region_clear)(void *, int), (*region_union)(void *, int destination, int source);
        void (*region_add)(void *, int, const ThreeMMI32 rect[4]);
        void (*depth_clean)(void *, const ThreeMMI32 rect[4]);
        /* A successful lock must be paired with unlock, including scan failure. */
        int (*lock)(void *, int buffer, ThreeMMWorldPixels *);
        void (*unlock)(void *, int);
        void (*scan_init)(void *, const ThreeMMI32 rect[4]);
        /* Spans are relative to the clipped scan rectangle. End releases any
         * borrowed region storage before the controller mutates regions. */
        int (*scan_span)(void *, ThreeMMI32 *left, ThreeMMI32 *right);
        void (*scan_next)(void *), (*scan_end)(void *);
        void (*render)(void *), (*publish)(void *, const ThreeMMWorldRender *);
        void (*snapshot_depth)(void *), (*detach_depth)(void *), (*snapshot_rgb)(void *), (*flush)(void *);
        void (*restore_background)(void *);
        void (*screen_scale)(void *, ThreeMMI32 nx, ThreeMMI32 dx, ThreeMMI32 ny, ThreeMMI32 dy);
        void (*screen_offset)(void *, ThreeMMI32 x, ThreeMMI32 y), (*screen_mark)(void *);
        void (*draw)(void *, const ThreeMMI32 source[4], const ThreeMMI32 destination[4]);
        ThreeMMWorldMaterialServices materials;
    } ThreeMMWorldRenderServices;
    /* Unique state; begin registration must stay callable during its pass.
     * Rectangle registration may change and callbacks may mark dirty. Callbacks
     * must not reenter rendering or replace/dispose the world resources. */
    void three_mm_world_render_init(ThreeMMWorldRender *);
    void three_mm_world_render_dirty(ThreeMMWorldRender *);
    void three_mm_world_render_styles(void *root, int style, const ThreeMMWorldRenderServices *);
    int three_mm_world_render_clean(const ThreeMMWorldBuffers *, const ThreeMMWorldRenderServices *);
    int three_mm_world_render(ThreeMMWorldRender *, const ThreeMMWorldBuffers *, const ThreeMMWorldRenderServices *);
    int three_mm_world_prerender(ThreeMMWorldRender *, const ThreeMMWorldBuffers *, const ThreeMMWorldRenderServices *);
    void three_mm_world_unprerender(const ThreeMMWorldRenderServices *);
    void three_mm_world_render_mark(const ThreeMMWorldBuffers *, ThreeMMI32 x, ThreeMMI32 y,
                                    const ThreeMMWorldRenderServices *);
    int three_mm_world_render_draw(const ThreeMMWorldBuffers *, ThreeMMI32 x, ThreeMMI32 y,
                                   const ThreeMMWorldRenderServices *);
#ifdef __cplusplus
}
#endif
#endif

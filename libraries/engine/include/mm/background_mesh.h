/* Background depth reconstruction and renderer ownership. */
#ifndef MM_ENGINE_BACKGROUND_MESH_H
#define MM_ENGINE_BACKGROUND_MESH_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMBackgroundVertex
    {
        double x, y, z, u, v;
    } MMBackgroundVertex;
    typedef struct MMBackgroundTriangle
    {
        MMU32 vertices[3];
    } MMBackgroundTriangle;
    typedef struct MMBackgroundPrepared
    {
        void *map, *material;
        MMCollectionStorage models;
        MMU32 count;
        int map_added, material_added;
    } MMBackgroundPrepared;
    typedef struct MMBackgroundMesh
    {
        MMCollectionStorage geometry, pixels;
        MMU32 vertex_count, triangle_count, width, height, infinite_count;
        float low[3], high[3];
        MMBackgroundPrepared prepared;
    } MMBackgroundMesh;
    typedef struct MMBackgroundMeshServices
    {
        void *context;
        void *(*map_new)(void *, MMU32 width, MMU32 height);
        unsigned char *(*map_row)(void *, void *, MMU32 y);
        void (*map_add)(void *, void *), (*map_remove)(void *, void *), (*map_free)(void *, void *);
        void *(*material_new)(void *);
        void (*material_configure)(void *, void *material, void *map);
        void (*material_add)(void *, void *), (*material_remove)(void *, void *), (*material_free)(void *, void *);
        void *(*model_new)(void *, MMU32 vertices, MMU32 faces);
        void (*model_configure)(void *, void *);
        void (*vertex)(void *, void *model, MMU32, const MMBackgroundVertex *normalized);
        void (*face)(void *, void *model, MMU32, const MMU32 indices[3], void *material);
        void (*model_add)(void *, void *), (*model_remove)(void *, void *), (*model_free)(void *, void *);
        void *(*actor_new)(void *);
        void (*actor_configure)(void *, void *actor, void *model, void *material);
        void (*actor_add)(void *, void *world, void *actor);
    } MMBackgroundMeshServices;
    /* Initialize a unique owner; allocator context and SDK services outlive it.
     * Inputs borrow separate arrays and never alias owned buffers. Callbacks are
     * synchronous, non-reentrant typed SDK primitives and must not throw.
     * Build clears previous SDK registrations first. Return 1 for valid geometry
     * (possibly empty), 0 for malformed input, -1 for allocation failure. Allocation
     * failure retains completed logical prefixes for disposal/rebuilding. */
    int mm_background_mesh_init(MMBackgroundMesh *, const MMStorageAllocator *);
    void mm_background_mesh_dispose(MMBackgroundMesh *, const MMBackgroundMeshServices *);
    int mm_background_mesh_build(MMBackgroundMesh *, MMU32 width, MMU32 height,
                                       const unsigned char *colors, MMU32 color_count,
                                       const unsigned short *depths, MMU32 depth_count,
                                       const double camera_to_world[16], const double projection[16],
                                       const MMBackgroundMeshServices *);
    /* Models contain at most 4096 faces; indices remain safe for SDK signed-16-bit
     * intermediates. Preparation publishes only after all models succeed. Actor
     * allocation failure retains the cached preparation and already attached actors,
     * which belong to the caller's world/frame. */
    int mm_background_mesh_add(MMBackgroundMesh *, void *world, const MMBackgroundMeshServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Background depth reconstruction and renderer ownership. */
#ifndef THREE_MM_ENGINE_BACKGROUND_MESH_H
#define THREE_MM_ENGINE_BACKGROUND_MESH_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMBackgroundVertex
    {
        double x, y, z, u, v;
    } ThreeMMBackgroundVertex;
    typedef struct ThreeMMBackgroundTriangle
    {
        ThreeMMU32 vertices[3];
    } ThreeMMBackgroundTriangle;
    typedef struct ThreeMMBackgroundPrepared
    {
        void *map, *material;
        ThreeMMCollectionStorage models;
        ThreeMMU32 count;
        int map_added, material_added;
    } ThreeMMBackgroundPrepared;
    typedef struct ThreeMMBackgroundMesh
    {
        ThreeMMCollectionStorage geometry, pixels;
        ThreeMMU32 vertex_count, triangle_count, width, height, infinite_count;
        float low[3], high[3];
        ThreeMMBackgroundPrepared prepared;
    } ThreeMMBackgroundMesh;
    typedef struct ThreeMMBackgroundMeshServices
    {
        void *context;
        void *(*map_new)(void *, ThreeMMU32 width, ThreeMMU32 height);
        unsigned char *(*map_row)(void *, void *, ThreeMMU32 y);
        void (*map_add)(void *, void *), (*map_remove)(void *, void *), (*map_free)(void *, void *);
        void *(*material_new)(void *);
        void (*material_configure)(void *, void *material, void *map);
        void (*material_add)(void *, void *), (*material_remove)(void *, void *), (*material_free)(void *, void *);
        void *(*model_new)(void *, ThreeMMU32 vertices, ThreeMMU32 faces);
        void (*model_configure)(void *, void *);
        void (*vertex)(void *, void *model, ThreeMMU32, const ThreeMMBackgroundVertex *normalized);
        void (*face)(void *, void *model, ThreeMMU32, const ThreeMMU32 indices[3], void *material);
        void (*model_add)(void *, void *), (*model_remove)(void *, void *), (*model_free)(void *, void *);
        void *(*actor_new)(void *);
        void (*actor_configure)(void *, void *actor, void *model, void *material);
        void (*actor_add)(void *, void *world, void *actor);
    } ThreeMMBackgroundMeshServices;
    /* Initialize a unique owner; allocator context and SDK services outlive it.
     * Inputs borrow separate arrays and never alias owned buffers. Callbacks are
     * synchronous, non-reentrant typed SDK primitives and must not throw.
     * Build clears previous SDK registrations first. Return 1 for valid geometry
     * (possibly empty), 0 for malformed input, -1 for allocation failure. Allocation
     * failure retains completed logical prefixes for disposal/rebuilding. */
    int three_mm_background_mesh_init(ThreeMMBackgroundMesh *, const ThreeMMStorageAllocator *);
    void three_mm_background_mesh_dispose(ThreeMMBackgroundMesh *, const ThreeMMBackgroundMeshServices *);
    int three_mm_background_mesh_build(ThreeMMBackgroundMesh *, ThreeMMU32 width, ThreeMMU32 height,
                                       const unsigned char *colors, ThreeMMU32 color_count,
                                       const unsigned short *depths, ThreeMMU32 depth_count,
                                       const double camera_to_world[16], const double projection[16],
                                       const ThreeMMBackgroundMeshServices *);
    /* Models contain at most 4096 faces; indices remain safe for SDK signed-16-bit
     * intermediates. Preparation publishes only after all models succeed. Actor
     * allocation failure retains the cached preparation and already attached actors,
     * which belong to the caller's world/frame. */
    int three_mm_background_mesh_add(ThreeMMBackgroundMesh *, void *world, const ThreeMMBackgroundMeshServices *);
#ifdef __cplusplus
}
#endif
#endif

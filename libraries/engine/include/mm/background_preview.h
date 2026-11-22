/* Preview background image ownership and two-entry mesh cache. */
#ifndef MM_ENGINE_BACKGROUND_PREVIEW_H
#define MM_ENGINE_BACKGROUND_PREVIEW_H
#include "background_resource.h"
#include "background_mesh.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMBackgroundPreview
    {
        MMCollectionStorage metadata, images;
        MMU32 camera_count, light_count, width, height, rgb_count, depth_count, active;
        MMBackgroundCameraBinding camera;
        MMBackgroundCameraState *placement;
        MMBackgroundMesh *meshes[2];
        MMU32 keys[2], mesh_count;
    } MMBackgroundPreview;
    typedef struct MMBackgroundPreviewServices
    {
        void *context;
        MMBackgroundPayloadRead payload;
        MMBackgroundCameraImageRead child;
        void *(*fetch)(void *, MMU32 number, int depth);
        void (*release)(void *, void *, int depth);
        void (*bounds)(void *, void *, MMI32 rect[4]);
        void (*draw_rgb)(void *, void *, unsigned char *, MMU32, MMU32, MMI32 x, MMI32 y);
        void (*draw_depth)(void *, void *, unsigned short *, MMU32, MMU32);
        void (*publish)(void *, const MMBackgroundPreview *);
        void (*selected)(void *, MMU32 slot);
        /* Use the same renderer for the lifetime of each cached mesh.
         * Geometry-only consumers can leave this unset. */
        const MMBackgroundMeshServices *mesh_renderer;
        float (*scalar_float)(void *, MMI32);
        MMI32 (*float_scalar)(void *, float);
        void (*perspective)(void *, unsigned short, MMI32 aspect, MMI32 near_z, MMI32 far_z,
                            MMI32 out[16]);
    } MMBackgroundPreviewServices;
    struct MMMovieSessionGraph;
    struct MMPreviewProjection;
    typedef struct MMBackgroundPreviewOwner MMBackgroundPreviewOwner;
    /* Open a graph-backed background with camera zero selected. Success
     * publishes one owned reference and failure leaves output untouched.
     * Return 1 for success, 0 for unusable content, -1 for owner allocation
     * failure. The graph's provider, renderer and allocator contexts outlive
     * the owner. Returned views borrow storage until the next mutation. */
    int mm_background_preview_open(MMBackgroundPreviewOwner **, const struct MMMovieSessionGraph *, MMU32 number,
                                   const unsigned char base_palette[1024], const MMBackgroundPreviewServices *renderer,
                                   const MMStorageAllocator *);
    void mm_background_preview_close(MMBackgroundPreviewOwner *);
    const MMBackgroundPreview *mm_background_preview_state(const MMBackgroundPreviewOwner *);
    const unsigned char *mm_background_preview_palette(const MMBackgroundPreviewOwner *);
    void mm_background_preview_overlay_palette(const MMBackgroundPreviewOwner *, unsigned char colours[1024]);
    int mm_background_preview_set_camera(MMBackgroundPreviewOwner *, MMU32 slot, int update_placement);
    int mm_background_preview_projection(MMBackgroundPreviewOwner *, MMU32 slot, struct MMPreviewProjection *);
    int mm_background_preview_next_place(MMBackgroundPreviewOwner *, float point[3]);
    void mm_background_preview_reuse_placement(MMBackgroundPreviewOwner *);
    MMBackgroundMesh *mm_background_preview_owner_mesh(MMBackgroundPreviewOwner *);
    /* Attach a borrowed cached mesh from this owner to the caller's world.
     * Requires the renderer supplied when opening the owner. */
    int mm_background_preview_add_mesh(MMBackgroundPreviewOwner *, MMBackgroundMesh *, void *world);
    /* The initialized placement state is borrowed for the lifetime of the view.
     * Unique C owners. Metadata append operations copy their inputs and do not alias
     * storage. Fresh image selection publishes decoded camera before clip validation,
     * dimensions before RGB allocation, and active camera only after both draws.
     * Completed image/metadata prefixes survive failures. Fetched references release
     * on every path, including buffer allocation failure. SDK services are synchronous,
     * non-reentrant, typed primitives; no exceptions cross the C boundary. */
    int mm_background_preview_init(MMBackgroundPreview *, MMBackgroundCameraState *, const MMStorageAllocator *);
    void mm_background_preview_dispose(MMBackgroundPreview *, const MMBackgroundPreviewServices *);
    int mm_background_preview_camera(MMBackgroundPreview *, MMU32 number);
    int mm_background_preview_light(MMBackgroundPreview *, const MMBackgroundLight *);
    int mm_background_preview_select(MMBackgroundPreview *, MMU32 slot,
                                           const MMBackgroundPreviewServices *);
    /* A normal selection also reads the placement points from its CAM.
     * Prepare the points before drawing and replace the active cursor only
     * after both images succeed. Temporary selections leave it untouched. */
    enum { MM_BACKGROUND_PREVIEW_PLACEMENT_MEMORY = -1 };
    int mm_background_preview_select_placement(MMBackgroundPreview *, MMU32 slot,
                                               const MMBackgroundPreviewServices *);
    /* Apply a prepared binding from the owning graph. Placement borrows the
     * matching decoded payload until this call returns. With child unset,
     * image lookup uses only the keys already present in the binding. */
    int mm_background_preview_apply(MMBackgroundPreview *, MMU32 slot, const MMBackgroundCameraBinding *,
                                    const MMReadStream *, int update_placement, const MMBackgroundPreviewServices *);
    /* Finish reading CAM and stage placement before fetching colour and depth
     * chunks. Image fetch may reuse the graph provider's decoded buffer. */
    int mm_background_preview_select_graph(MMBackgroundPreview *, const struct MMMovieSessionGraph *,
                                           MMU32 background, MMU32 slot, int update_placement,
                                           const MMBackgroundPreviewServices *);
    int mm_background_preview_place(MMBackgroundPreview *, MMActorPoint *);
    void mm_background_preview_reuse_place(MMBackgroundPreview *);
    /* Hits move to the most recent end. Misses build before evicting the oldest;
     * failed construction leaves the existing cache untouched. */
    MMBackgroundMesh *mm_background_preview_mesh(MMBackgroundPreview *, const MMBackgroundPreviewServices *);
#ifdef __cplusplus
}
#endif
#endif

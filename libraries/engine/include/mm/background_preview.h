/* Preview background image ownership and two-entry mesh cache. */
#ifndef MM_ENGINE_BACKGROUND_PREVIEW_H
#define MM_ENGINE_BACKGROUND_PREVIEW_H
#include "background_resource.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMBackgroundPreview
    {
        MMCollectionStorage metadata, images;
        MMU32 camera_count, light_count, width, height, rgb_count, depth_count, active;
        MMBackgroundCameraBinding camera;
        MMBackgroundCameraState placement;
        void *meshes[2];
        MMU32 keys[2], mesh_count;
    } MMBackgroundPreview;
    typedef struct MMBackgroundPreviewServices
    {
        void *context;
        MMBackgroundPayloadRead payload;
        MMBackgroundCameraImageRead child;
        void *(*fetch)(void *, MMU32 number, int depth);
        void (*release)(void *, void *);
        void (*bounds)(void *, void *, MMI32 rect[4]);
        void (*draw_rgb)(void *, void *, unsigned char *, MMU32, MMU32, MMI32 x, MMI32 y);
        void (*draw_depth)(void *, void *, unsigned short *, MMU32, MMU32);
        void (*publish)(void *, const MMBackgroundPreview *);
        void (*selected)(void *, MMU32 slot);
        void *(*mesh_new)(void *);
        void (*mesh_release)(void *, void *);
        int (*mesh_build)(void *, void *, const MMBackgroundPreview *, const double transform[16],
                          const double projection[16]);
        float (*scalar_float)(void *, MMI32);
        MMI32 (*float_scalar)(void *, float);
        void (*perspective)(void *, unsigned short, MMI32 aspect, MMI32 near_z, MMI32 far_z,
                            MMI32 out[16]);
    } MMBackgroundPreviewServices;
    struct MMMovieSessionGraph;
    typedef struct MMBackgroundPreviewGraph
    {
        const struct MMMovieSessionGraph *graph;
        void *owner; /* Borrowed by the host publication and mesh services. */
    } MMBackgroundPreviewGraph;
    /* Bind decoded graph payloads and C bitmap services. The graph and context
     * must stay live through the call. Image allocations belong to the reader
     * until the matching release service. */
    void mm_background_preview_bind_graph(MMBackgroundPreviewServices *, MMBackgroundPreviewGraph *);
    /* Unique C owners. Metadata append operations copy their inputs and do not alias
     * storage. Fresh image selection publishes decoded camera before clip validation,
     * dimensions before RGB allocation, and active camera only after both draws.
     * Completed image/metadata prefixes survive failures. Fetched references release
     * on every path, including buffer allocation failure. SDK services are synchronous,
     * non-reentrant, typed primitives; no exceptions cross the C boundary. */
    int mm_background_preview_init(MMBackgroundPreview *, const MMStorageAllocator *);
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
    void *mm_background_preview_mesh(MMBackgroundPreview *, const MMBackgroundPreviewServices *);
#ifdef __cplusplus
}
#endif
#endif

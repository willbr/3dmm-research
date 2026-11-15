/* Preview background image ownership and two-entry mesh cache. */
#ifndef THREE_MM_ENGINE_BACKGROUND_PREVIEW_H
#define THREE_MM_ENGINE_BACKGROUND_PREVIEW_H
#include "background_resource.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMBackgroundPreview
    {
        ThreeMMCollectionStorage metadata, images;
        ThreeMMU32 camera_count, light_count, width, height, rgb_count, depth_count, active;
        ThreeMMBackgroundCameraData camera;
        void *meshes[2];
        ThreeMMU32 keys[2], mesh_count;
    } ThreeMMBackgroundPreview;
    typedef struct ThreeMMBackgroundPreviewServices
    {
        void *context;
        int (*camera_read)(void *, ThreeMMU32 number, ThreeMMReadStream *);
        int (*child)(void *, ThreeMMU32 camera, int depth, ThreeMMU32 *number);
        void *(*fetch)(void *, ThreeMMU32 number, int depth);
        void (*release)(void *, void *);
        void (*bounds)(void *, void *, ThreeMMI32 rect[4]);
        void (*draw_rgb)(void *, void *, unsigned char *, ThreeMMU32, ThreeMMU32, ThreeMMI32 x, ThreeMMI32 y);
        void (*draw_depth)(void *, void *, unsigned short *, ThreeMMU32, ThreeMMU32);
        void (*publish)(void *, const ThreeMMBackgroundPreview *);
        void (*selected)(void *, ThreeMMU32 slot);
        void *(*mesh_new)(void *);
        void (*mesh_release)(void *, void *);
        int (*mesh_build)(void *, void *, const ThreeMMBackgroundPreview *, const double transform[16],
                          const double projection[16]);
        float (*scalar_float)(void *, ThreeMMI32);
        ThreeMMI32 (*float_scalar)(void *, float);
        void (*perspective)(void *, unsigned short, ThreeMMI32 aspect, ThreeMMI32 near_z, ThreeMMI32 far_z,
                            ThreeMMI32 out[16]);
    } ThreeMMBackgroundPreviewServices;
    /* Unique C owners. Metadata append operations copy their inputs and do not alias
     * storage. Fresh image selection publishes decoded camera before clip validation,
     * dimensions before RGB allocation, and active camera only after both draws.
     * Completed image/metadata prefixes survive failures. Fetched references release
     * on every path, including buffer allocation failure. SDK services are synchronous,
     * non-reentrant, typed primitives; no exceptions cross the C boundary. */
    int three_mm_background_preview_init(ThreeMMBackgroundPreview *, const ThreeMMStorageAllocator *);
    void three_mm_background_preview_dispose(ThreeMMBackgroundPreview *, const ThreeMMBackgroundPreviewServices *);
    int three_mm_background_preview_camera(ThreeMMBackgroundPreview *, ThreeMMU32 number);
    int three_mm_background_preview_light(ThreeMMBackgroundPreview *, const ThreeMMBackgroundLight *);
    int three_mm_background_preview_select(ThreeMMBackgroundPreview *, ThreeMMU32 slot,
                                           const ThreeMMBackgroundPreviewServices *);
    /* Hits move to the most recent end. Misses build before evicting the oldest;
     * failed construction leaves the existing cache untouched. */
    void *three_mm_background_preview_mesh(ThreeMMBackgroundPreview *, const ThreeMMBackgroundPreviewServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Preview camera interaction, bounds and guide metadata ownership. */
#ifndef MM_ENGINE_PREVIEW_CAMERA_H
#define MM_ENGINE_PREVIEW_CAMERA_H
#include "preview_render.h"
#include "background_preview.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMPreviewCameraPose
    {
        float position[3], yaw, pitch, roll, move_speed, vertical_fov;
    } MMPreviewCameraPose;
    typedef struct MMPreviewProjectionCache
    {
        MMCollectionStorage storage;
        MMU32 count;
    } MMPreviewProjectionCache;
    enum
    {
        MM_PREVIEW_CAMERA_OK,
        MM_PREVIEW_CAMERA_INVALID,
        MM_PREVIEW_CAMERA_PAN_RANGE,
        MM_PREVIEW_CAMERA_MEMORY,
        MM_PREVIEW_CAMERA_READ
    };
    /* Services are synchronous and never throw. Math callbacks retain the host's
     * float rounding. Matching writes default pose values before validation;
     * failed matching/panning leaves the orbit unchanged. Reset clears only framed.
     * Body descriptors supply world bounds, as used by the frame renderer. */
    int mm_preview_camera_match(float aspect, MMPreviewOrbit *, MMPreviewCameraPose *, const MMPreviewRenderServices *,
                                float (*atan_value)(float));
    int mm_preview_orbit_pan(const MMPreviewView *, float right, float up, MMPreviewOrbit *, int ready,
                             const MMPreviewMath *);
    void mm_preview_orbit_reset(MMPreviewOrbit *);
    int mm_preview_orbit_follows(const MMPreviewOrbit *, MMI32 object, int visible);
    void mm_preview_orbit_move(MMPreviewOrbit *, MMI32 object, const float before[3], const float after[3]);
    void mm_preview_orbit_resize(MMPreviewOrbit *, const MMI32 before[6], const MMI32 after[6], const MMPreviewMath *);
    /* Invalid local bounds publish zero corners and return false. Double precision
     * is retained through corner accumulation, then rounded once to float. */
    int mm_preview_camera_bounds(const MMI32 low[3], const MMI32 high[3], const MMI32 matrix[12], float corners[24],
                                 const MMPreviewMath *);
    int mm_preview_camera_admit(int ready, int background, MMU32 count, MMU32 index);
    /* select returns 1 on success, 0 on read/SDK failure, -1 on allocation failure.
     * Read failure clears ready; allocation failure preserves it. Same-camera
     * selection does no I/O. The current index publishes only after success. */
    int mm_preview_camera_select(int *ready, MMU32 *current, int background, MMU32 count, MMU32 index, void *context,
                                 int (*select)(void *, MMU32));
    int mm_preview_camera_placement(int ready, const MMI32 place[3], float point[3], const MMPreviewMath *);
    /* Unique C cache owner. Reset invalidates reads but retains capacity. Allocation
     * failure publishes no count/entry and can be retried. Read failures are cached.
     * Camera numbers and bitmap references are borrowed; fetched bitmap references
     * release before validating bounds. No image selection/depth/mesh callbacks run.
     * Failed reads return the cached descriptor (zero for failures before decoding
     * a complete descriptor); allocation failure publishes a zero descriptor. */
    int mm_preview_projection_cache_init(MMPreviewProjectionCache *, const MMStorageAllocator *);
    void mm_preview_projection_cache_reset(MMPreviewProjectionCache *);
    void mm_preview_projection_cache_dispose(MMPreviewProjectionCache *);
    int mm_preview_projection_cache_get(MMPreviewProjectionCache *, const MMU32 *numbers, MMU32 count, MMU32 index,
                                        MMPreviewProjection *, const MMBackgroundPreviewServices *);
#ifdef __cplusplus
}
#endif
#endif

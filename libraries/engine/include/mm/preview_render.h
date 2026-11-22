/* Preview frame control converted from archive/preview.cpp. */
#ifndef MM_ENGINE_PREVIEW_RENDER_H
#define MM_ENGINE_PREVIEW_RENDER_H
#include "background_resource.h"
#include "body_create.h"
#include "collections.h"
#include "render_frame.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMPreviewView
    {
        MMU32 width, height;
        float yaw, pitch, roll, zoom, vertical_fov, near_clip, position[3];
        int authored, output_resolution, free_camera, frame_camera, stable_orbit, object, depth, fit_depth, display,
            orthographic, axis, rasterizer;
    } MMPreviewView;
    typedef struct MMPreviewOrbit
    {
        int framed, includes_camera, object;
        float center[3], radius, anchor[3];
    } MMPreviewOrbit;
    typedef struct MMPreviewProjection
    {
        int valid, orthographic;
        float transform[12], vertical_fov, aspect, near_clip, far_clip, display_depth, ortho_height;
    } MMPreviewProjection;
    typedef MMRenderCamera MMPreviewCamera;
    typedef struct MMPreviewMath
    {
        float (*sin)(float), (*cos)(float), (*tan)(float), (*sqrt)(float), (*asin)(float), (*atan2)(float, float),
            (*fmod)(float, float);
        float (*scalar_float)(MMI32);
        MMI32 (*scalar)(float);
        MMI32 (*scalar_double)(double);
        unsigned short (*angle)(float);
    } MMPreviewMath;
    typedef MMRenderBody MMPreviewBody;
    typedef struct MMPreviewData
    {
        int scene, background;
        MMU32 width, height, light_count;
        const unsigned char *colours;
        const unsigned short *depths;
        const MMBackgroundLight *lights;
        unsigned char palette[1024];
        MMPreviewCamera camera;
        void *model, *model_resource;
        float model_low[3], model_high[3];
    } MMPreviewData;
    typedef struct MMPreviewMesh
    {
        int empty;
        float low[3], high[3];
        const unsigned char *infinite;
    } MMPreviewMesh;
    typedef struct MMPreviewBuffer
    {
        unsigned char *colour, *depth;
        MMI32 colour_stride, depth_stride;
    } MMPreviewBuffer;
    typedef struct MMPreviewImage
    {
        MMU32 width, height, covered;
        unsigned char *rgba;
        float *depth;
        float position[3], yaw, pitch, roll, move_speed, vertical_fov;
        MMPreviewProjection projection, scene_camera;
    } MMPreviewImage;
    typedef struct MMPreviewMaterial
    {
        void *material, *map;
        MMU32 flags;
        unsigned char base, range;
    } MMPreviewMaterial;
    typedef struct MMPreviewTreeServices
    {
        void *context;
        void *(*first)(void *, const void *), *(*next)(void *, const void *);
        void *(*clone_attach)(void *, void *parent, const void *source);
        void *(*model)(void *, const void *actor);
        int (*owner)(void *, void *copy, MMI32 owner);
    } MMPreviewTreeServices;
    typedef struct MMPreviewDepthRange
    {
        double x_slope, y_slope, near_clip, far_clip, nearest, farthest;
        int parallel;
    } MMPreviewDepthRange;
    enum
    {
        MM_PREVIEW_OK,
        MM_PREVIEW_NOT_READY,
        MM_PREVIEW_INVALID,
        MM_PREVIEW_BUFFERS,
        MM_PREVIEW_MEMORY,
        MM_PREVIEW_ASSET_RANGE,
        MM_PREVIEW_CAMERA_RANGE,
        MM_PREVIEW_CAMERA,
        MM_PREVIEW_LIGHTS,
        MM_PREVIEW_MODELS,
        MM_PREVIEW_MESH,
        MM_PREVIEW_RENDERER,
        MM_PREVIEW_HOST,
        MM_PREVIEW_ISOLATED_INVALID,
        MM_PREVIEW_ISOLATED_FIT,
        MM_PREVIEW_LIGHT_RIG_INVALID,
        MM_PREVIEW_LIGHT_VALUE_INVALID
    };
    struct MMBackgroundPreviewOwner;
    struct MMModelOwner;
    struct MMPreviewActor;
    struct MMPreviewLive;
    typedef struct MMPreviewRenderServices
    {
        void *context;
        struct MMMovieSession *movie;
        const MMMovieLiveFrame *position;
        /* An optional live source supplies scene status and typed draw owners.
         * The host unwraps its actor handle to the C89 actor owner.
         * Without this source, ready/describe/body_count/body supply the view. */
        const struct MMPreviewLive *live;
        MMBodyOwner *body_asset;
        struct MMModelOwner *model_asset;
        const MMBodyGeometryServices *body_geometry;
        const struct MMPreviewActor *(*actor)(const void *handle);
        MMI32 only_object;
        /* With a base BGRX palette, the C frame reads its environment directly.
         * Otherwise describe/mesh/add_mesh supply an external environment.
         * The palette and background are borrowed during the call.
         * Isolated views omit the scenery and still use its palette. */
        const unsigned char *base_palette;
        struct MMBackgroundPreviewOwner *background;
        /* A borrowed override with count zero suppresses the authored lights. */
        const MMBackgroundLightRig *light_override;
        MMStorageAllocator allocator;
        MMPreviewMath math;
        MMPreviewTreeServices tree;
        int (*ready)(void *);
        void (*describe)(void *, MMPreviewData *);
        int (*mesh)(void *, MMPreviewMesh *);
        MMU32 (*body_count)(void *);
        int (*body)(void *, MMU32, MMPreviewBody *);
        void *(*attach_body_part)(void *, void *parent, MMI32 index, const MMBodyPartBinding *);
        void (*retain_pose_resource)(void *, void *), (*release_pose_resource)(void *, void *);
        void (*retain_model)(void *, void *), (*release_model)(void *, void *);
        int (*object_bounds)(void *, MMI32, float corners[24], int *valid);
        MMRenderFrameFactory frame_factory;
        void (*fill)(void *, void *pixelmap, MMU32);
        void (*buffers)(void *, void *colour, void *depth, MMPreviewBuffer *);
        void *(*camera)(void *, void *world, const MMPreviewCamera *);
        void (*update_camera)(void *, void *camera, const MMPreviewCamera *);
        MMU32 light_bytes;
        MMBackgroundLightRotation light_rotation;
        void *(*light)(void *, void *world, void *data, const MMBackgroundLight *);
        void (*enable_light)(void *, void *), (*disable_light)(void *, void *);
        int (*add_model)(void *, void *world, void *model), (*add_mesh)(void *, void *world);
        void *(*material)(void *, void *actor), *(*face_material)(void *, void *model, MMU32);
        MMU32 (*material_count)(void *, void *model);
        void (*read_material)(void *, void *material, MMPreviewMaterial *),
            (*write_material)(void *, const MMPreviewMaterial *);
        MMU32 light_flag, prelit_flag, smooth_flag;
        void (*edges)(void *, void *actor);
        MMU32 (*vertices)(void *, void *model), (*faces)(void *, void *model);
        void (*vertex)(void *, void *model, MMU32, MMI32 xyz[3]);
        void (*face)(void *, void *model, MMU32, unsigned short vertices[3]);
        void (*relative)(void *, void *actor, void *camera, MMI32 matrix[12]);
        int (*raster_begin)(void *, const MMPreviewProjection *, const unsigned char *palette, int opaque,
                            MMU32, MMU32);
        void (*sink_off)(void *), (*render)(void *, const MMRenderFrame *);
        int (*renderer_ok)(void *), (*raster_end)(void *, const MMPreviewBuffer *);
        const unsigned char *(*raster_rgba)(void *);
        void (*depth_projection)(void *, void *camera, MMI32 matrix[16]);
        int (*output)(void *, MMU32, MMU32, int depth, unsigned char **rgba, float **distances);
        /* Optional caller-owned width * height index plane for indexed output. */
        unsigned char *(*palette_indices)(void *, MMU32 width, MMU32 height);
    } MMPreviewRenderServices;
    /* All services are initialized, synchronous, non-reentrant and do not throw.
     * A zero allocator selects the C heap. Nonzero allocators provide both callbacks.
     * Body bindings are submitted to a temporary world. Services can supply
     * resource owners for bodies and standalone models with paired retain/release
     * callbacks. Otherwise these handles stay valid through the synchronous call.
     * Submission preserves sibling order with SDK head insertion. Failure
     * retains the attached prefix, which the caller's world owns. Frame cleanup
     * restores materials, disables lights, frees the world and pixelmaps, then
     * releases pose resources and scratch storage. Scene image pointers are borrowed.
     * Prepared SDK geometry and source images must satisfy their decoded bounds.
     * Input/output dimensions are at most 4096, making pixel products 32-bit bounded.
     * output allocates caller-owned zeroed RGBA and +infinity depth buffers; they
     * remain stable until render returns. scalar_double preserves fitted depth
     * precision until fixed-point conversion, avoiding an intermediate float.
     * Failed render publishes no image metadata. Orbit changes retain completed
     * framing prefixes, as before. Math services retain the host's float rounding.
     * Raster and SDK failures still unwind C owners; foreign exceptions must be
     * transported by the host and rethrown only after the C call has returned. */
    int mm_preview_copy_tree(void *parent, const void *source, MMI32 owner, const MMStorageAllocator *,
                                   const MMPreviewTreeServices *);
    void mm_preview_depth_init(MMPreviewDepthRange *, double, double, double, double, int);
    void mm_preview_depth_include(MMPreviewDepthRange *, const double triangle[3][3]);
    void mm_preview_depth_fit(const MMPreviewDepthRange *, double *near_clip, double *far_clip);
    int mm_preview_render(const MMPreviewView *, MMPreviewOrbit *, MMPreviewImage *,
                                const MMPreviewRenderServices *);
    /* Stage a temporary rig. Disabled lights are skipped after validation.
     * Failure leaves the destination untouched. Rendering with a rig uses a
     * private copy of the current orbit. */
    int mm_preview_prepare_lights(const MMPreviewLightValues *, size_t count, int has_background,
                                  MMBackgroundLightRig *, const MMPreviewRenderServices *);
    /* Fit and render a view through services already bound to the one object
     * and its bounds. Use a private orbit so the live camera keeps its framing. */
    int mm_preview_render_isolated(const MMPreviewView *, const MMPreviewOrbit *, MMI32 object,
                                    const float bounds[24], int crop, int rect[4], MMPreviewImage *,
                                    const MMPreviewRenderServices *);
    /* Submit described bodies in render order. The caller owns the attached trees
     * and keeps their source models and materials alive while using them. */
    int mm_preview_copy_bodies(void *world, const MMPreviewRenderServices *);
#ifdef __cplusplus
}
#endif
#endif

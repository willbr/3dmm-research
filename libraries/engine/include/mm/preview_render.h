/* Preview frame control converted from archive/preview.cpp. */
#ifndef MM_ENGINE_PREVIEW_RENDER_H
#define MM_ENGINE_PREVIEW_RENDER_H
#include "background_resource.h"
#include "collections.h"
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
    typedef struct MMPreviewCamera
    {
        MMI32 matrix[12], near_z, far_z, aspect, width, height;
        unsigned short fov;
        int parallel;
    } MMPreviewCamera;
    typedef struct MMPreviewMath
    {
        float (*sin)(float), (*cos)(float), (*tan)(float), (*sqrt)(float), (*asin)(float), (*atan2)(float, float),
            (*fmod)(float, float);
        float (*scalar_float)(MMI32);
        MMI32 (*scalar)(float);
        MMI32 (*scalar_double)(double);
        unsigned short (*angle)(float);
    } MMPreviewMath;
    typedef struct MMPreviewBody
    {
        void *tree;
        float low[3], high[3];
        MMU32 order_low, order_high;
        MMI32 owner;
    } MMPreviewBody;
    typedef struct MMPreviewData
    {
        int scene, background;
        MMU32 width, height, light_count;
        const unsigned char *colours;
        const unsigned short *depths;
        const MMBackgroundLight *lights;
        unsigned char palette[1024];
        MMPreviewCamera camera;
        void *model;
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
        MM_PREVIEW_HOST
    };
    typedef struct MMPreviewRenderServices
    {
        void *context;
        MMStorageAllocator allocator;
        MMPreviewMath math;
        MMPreviewTreeServices tree;
        int (*ready)(void *);
        void (*describe)(void *, MMPreviewData *);
        int (*mesh)(void *, MMPreviewMesh *);
        MMU32 (*body_count)(void *);
        int (*body)(void *, MMU32, MMPreviewBody *);
        int (*object_bounds)(void *, MMI32, float corners[24], int *valid);
        void *(*colour)(void *, MMU32, MMU32), *(*depth)(void *, void *colour), *(*world)(void *);
        void (*free_pixel)(void *, void *), (*free_world)(void *, void *);
        void (*fill)(void *, void *pixelmap, MMU32);
        void (*buffers)(void *, void *colour, void *depth, MMPreviewBuffer *);
        void *(*camera)(void *, void *world, const MMPreviewCamera *);
        void (*update_camera)(void *, void *camera, const MMPreviewCamera *);
        MMU32 light_bytes;
        MMI32 direct_light_type;
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
        void (*sink_off)(void *), (*render)(void *, void *world, void *camera, void *colour, void *depth);
        int (*renderer_ok)(void *), (*raster_end)(void *, const MMPreviewBuffer *);
        const unsigned char *(*raster_rgba)(void *);
        void (*depth_projection)(void *, void *camera, MMI32 matrix[16]);
        int (*output)(void *, MMU32, MMU32, int depth, unsigned char **rgba, float **distances);
    } MMPreviewRenderServices;
    /* All services are initialized, synchronous, non-reentrant and do not throw.
     * A zero allocator selects the C heap. Nonzero allocators provide both callbacks.
     * Borrowed input trees/models/materials are never reparented or freed. Copy creates
     * nodes in reverse sibling order for SDK head insertion; failure retains the
     * attached prefix, which the caller's world owns. Frame cleanup restores shared
     * materials, disables lights, frees the world and pixelmaps, then scratch storage.
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
#ifdef __cplusplus
}
#endif
#endif

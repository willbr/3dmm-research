/* Preview frame control converted from archive/preview.cpp. */
#ifndef THREE_MM_ENGINE_PREVIEW_RENDER_H
#define THREE_MM_ENGINE_PREVIEW_RENDER_H
#include "background_resource.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMPreviewView
    {
        ThreeMMU32 width, height;
        float yaw, pitch, roll, zoom, vertical_fov, near_clip, position[3];
        int authored, output_resolution, free_camera, frame_camera, stable_orbit, object, depth, fit_depth, display,
            orthographic, axis, rasterizer;
    } ThreeMMPreviewView;
    typedef struct ThreeMMPreviewOrbit
    {
        int framed, includes_camera, object;
        float center[3], radius, anchor[3];
    } ThreeMMPreviewOrbit;
    typedef struct ThreeMMPreviewProjection
    {
        int valid, orthographic;
        float transform[12], vertical_fov, aspect, near_clip, far_clip, display_depth, ortho_height;
    } ThreeMMPreviewProjection;
    typedef struct ThreeMMPreviewCamera
    {
        ThreeMMI32 matrix[12], near_z, far_z, aspect, width, height;
        unsigned short fov;
        int parallel;
    } ThreeMMPreviewCamera;
    typedef struct ThreeMMPreviewMath
    {
        float (*sin)(float), (*cos)(float), (*tan)(float), (*sqrt)(float), (*asin)(float), (*atan2)(float, float),
            (*fmod)(float, float);
        float (*scalar_float)(ThreeMMI32);
        ThreeMMI32 (*scalar)(float);
        ThreeMMI32 (*scalar_double)(double);
        unsigned short (*angle)(float);
    } ThreeMMPreviewMath;
    typedef struct ThreeMMPreviewBody
    {
        void *tree;
        float low[3], high[3];
        ThreeMMU32 order_low, order_high;
        ThreeMMI32 owner;
    } ThreeMMPreviewBody;
    typedef struct ThreeMMPreviewData
    {
        int scene, background;
        ThreeMMU32 width, height, light_count;
        const unsigned char *colours;
        const unsigned short *depths;
        const ThreeMMBackgroundLight *lights;
        unsigned char palette[1024];
        ThreeMMPreviewCamera camera;
        void *model;
        float model_low[3], model_high[3];
    } ThreeMMPreviewData;
    typedef struct ThreeMMPreviewMesh
    {
        int empty;
        float low[3], high[3];
        const unsigned char *infinite;
    } ThreeMMPreviewMesh;
    typedef struct ThreeMMPreviewBuffer
    {
        unsigned char *colour, *depth;
        ThreeMMI32 colour_stride, depth_stride;
    } ThreeMMPreviewBuffer;
    typedef struct ThreeMMPreviewImage
    {
        ThreeMMU32 width, height, covered;
        unsigned char *rgba;
        float *depth;
        float position[3], yaw, pitch, roll, move_speed, vertical_fov;
        ThreeMMPreviewProjection projection, scene_camera;
    } ThreeMMPreviewImage;
    typedef struct ThreeMMPreviewMaterial
    {
        void *material, *map;
        ThreeMMU32 flags;
        unsigned char base, range;
    } ThreeMMPreviewMaterial;
    typedef struct ThreeMMPreviewTreeServices
    {
        void *context;
        void *(*first)(void *, const void *), *(*next)(void *, const void *);
        void *(*clone_attach)(void *, void *parent, const void *source);
        void *(*model)(void *, const void *actor);
        int (*owner)(void *, void *copy, ThreeMMI32 owner);
    } ThreeMMPreviewTreeServices;
    typedef struct ThreeMMPreviewDepthRange
    {
        double x_slope, y_slope, near_clip, far_clip, nearest, farthest;
        int parallel;
    } ThreeMMPreviewDepthRange;
    enum
    {
        THREE_MM_PREVIEW_OK,
        THREE_MM_PREVIEW_NOT_READY,
        THREE_MM_PREVIEW_INVALID,
        THREE_MM_PREVIEW_BUFFERS,
        THREE_MM_PREVIEW_MEMORY,
        THREE_MM_PREVIEW_ASSET_RANGE,
        THREE_MM_PREVIEW_CAMERA_RANGE,
        THREE_MM_PREVIEW_CAMERA,
        THREE_MM_PREVIEW_LIGHTS,
        THREE_MM_PREVIEW_MODELS,
        THREE_MM_PREVIEW_MESH,
        THREE_MM_PREVIEW_RENDERER,
        THREE_MM_PREVIEW_HOST
    };
    typedef struct ThreeMMPreviewRenderServices
    {
        void *context;
        ThreeMMStorageAllocator allocator;
        ThreeMMPreviewMath math;
        ThreeMMPreviewTreeServices tree;
        int (*ready)(void *);
        void (*describe)(void *, ThreeMMPreviewData *);
        int (*mesh)(void *, ThreeMMPreviewMesh *);
        ThreeMMU32 (*body_count)(void *);
        int (*body)(void *, ThreeMMU32, ThreeMMPreviewBody *);
        int (*object_bounds)(void *, ThreeMMI32, float corners[24], int *valid);
        void *(*colour)(void *, ThreeMMU32, ThreeMMU32), *(*depth)(void *, void *colour), *(*world)(void *);
        void (*free_pixel)(void *, void *), (*free_world)(void *, void *);
        void (*fill)(void *, void *pixelmap, ThreeMMU32);
        void (*buffers)(void *, void *colour, void *depth, ThreeMMPreviewBuffer *);
        void *(*camera)(void *, void *world, const ThreeMMPreviewCamera *);
        void (*update_camera)(void *, void *camera, const ThreeMMPreviewCamera *);
        ThreeMMU32 light_bytes;
        ThreeMMI32 direct_light_type;
        void *(*light)(void *, void *world, void *data, const ThreeMMBackgroundLight *);
        void (*enable_light)(void *, void *), (*disable_light)(void *, void *);
        int (*add_model)(void *, void *world, void *model), (*add_mesh)(void *, void *world);
        void *(*material)(void *, void *actor), *(*face_material)(void *, void *model, ThreeMMU32);
        ThreeMMU32 (*material_count)(void *, void *model);
        void (*read_material)(void *, void *material, ThreeMMPreviewMaterial *),
            (*write_material)(void *, const ThreeMMPreviewMaterial *);
        ThreeMMU32 light_flag, prelit_flag, smooth_flag;
        void (*edges)(void *, void *actor);
        ThreeMMU32 (*vertices)(void *, void *model), (*faces)(void *, void *model);
        void (*vertex)(void *, void *model, ThreeMMU32, ThreeMMI32 xyz[3]);
        void (*face)(void *, void *model, ThreeMMU32, unsigned short vertices[3]);
        void (*relative)(void *, void *actor, void *camera, ThreeMMI32 matrix[12]);
        int (*raster_begin)(void *, const ThreeMMPreviewProjection *, const unsigned char *palette, int opaque,
                            ThreeMMU32, ThreeMMU32);
        void (*sink_off)(void *), (*render)(void *, void *world, void *camera, void *colour, void *depth);
        int (*renderer_ok)(void *), (*raster_end)(void *, const ThreeMMPreviewBuffer *);
        const unsigned char *(*raster_rgba)(void *);
        void (*depth_projection)(void *, void *camera, ThreeMMI32 matrix[16]);
        int (*output)(void *, ThreeMMU32, ThreeMMU32, int depth, unsigned char **rgba, float **distances);
    } ThreeMMPreviewRenderServices;
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
    int three_mm_preview_copy_tree(void *parent, const void *source, ThreeMMI32 owner, const ThreeMMStorageAllocator *,
                                   const ThreeMMPreviewTreeServices *);
    void three_mm_preview_depth_init(ThreeMMPreviewDepthRange *, double, double, double, double, int);
    void three_mm_preview_depth_include(ThreeMMPreviewDepthRange *, const double triangle[3][3]);
    void three_mm_preview_depth_fit(const ThreeMMPreviewDepthRange *, double *near_clip, double *far_clip);
    int three_mm_preview_render(const ThreeMMPreviewView *, ThreeMMPreviewOrbit *, ThreeMMPreviewImage *,
                                const ThreeMMPreviewRenderServices *);
#ifdef __cplusplus
}
#endif
#endif

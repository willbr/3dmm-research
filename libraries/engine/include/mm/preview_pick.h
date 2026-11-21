/* Preview picking policy and temporary resource ownership. */
#ifndef MM_ENGINE_PREVIEW_PICK_H
#define MM_ENGINE_PREVIEW_PICK_H
#include "preview_render.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMPreviewPickState MMPreviewPickState;
    typedef struct MMPreviewPickTexture
    {
        const unsigned char *pixels;
        MMI32 width, height, stride, base_x, base_y;
        int opaque, indexed;
    } MMPreviewPickTexture;
    typedef struct MMPreviewPickServices
    {
        void *context;
        const MMPreviewRenderServices *render;
        int (*invertible)(void *, void *camera);
        void (*perspective)(void *, const MMPreviewCamera *, MMI32 matrix[16]);
        void (*scene_pick)(void *, void *world, void *camera, MMU32 width, MMU32 height, MMI32 x, MMI32 y,
                           MMPreviewPickState *);
        MMU32 face_bytes;
        MMU32 (*face_count)(void *, void *model);
        void (*prepare_face)(void *, void *model, MMU32 index, void *face);
        void (*model_pick)(void *, void *model, void *faces, void *material, void *position, void *direction,
                           MMI32 near_clip, MMI32 far_clip, MMPreviewPickState *);
        int (*texture)(void *, void *material, MMPreviewPickTexture *);
        void (*map)(void *, void *material, void *map, float uv[2]);
    } MMPreviewPickServices;
    enum
    {
        MM_PREVIEW_PICK_OK,
        MM_PREVIEW_PICK_INVALID,
        MM_PREVIEW_PICK_RANGE,
        MM_PREVIEW_PICK_SPAN,
        MM_PREVIEW_PICK_CAMERA,
        MM_PREVIEW_PICK_SINGULAR,
        MM_PREVIEW_PICK_MODELS,
        MM_PREVIEW_PICK_RENDERER,
        MM_PREVIEW_PICK_MEMORY
    };
    /* Synchronous services never throw. render supplies scene/body descriptors,
     * camera/world primitives, tree copying, mesh services, math, allocator and
     * renderer_ok, which checks diagnostics after the scene scan.
     * The C owner releases the world before its scratch buffers on every exit.
     * SDK camera data must remain alive through world disposal. Prepared face copies
     * are private; prepare_face copies one SDK face and rebuilds its pivot-adjusted
     * plane using SDK fixed math. All source geometry/textures remain borrowed.
     * A scene callback invokes model; a model callback invokes face. Nested calls
     * are synchronous. SDK callbacks must not retain the state or scratch faces.
     * Dimensions/descriptors have the same bounds as Preview rendering.
     * No hit returns OK. No hit or failure publishes object -1 and +infinity
     * distance; successful hits publish the selected owner's camera depth. */
    int mm_preview_pick(const MMPreviewProjection *, int authored, int output_resolution, MMU32 width, MMU32 height,
                        MMI32 x, MMI32 y, MMI32 *object, float *distance, const MMPreviewPickServices *);
    void mm_preview_pick_model(MMPreviewPickState *, void *actor, void *model, void *material, void *position,
                               void *direction, MMI32 near_clip, MMI32 far_clip);
    void mm_preview_pick_face(MMPreviewPickState *, float distance, void *material, void *map);
#ifdef __cplusplus
}
#endif
#endif

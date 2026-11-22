#ifndef MM_BRENDER_SCENE_RENDERER_H
#define MM_BRENDER_SCENE_RENDERER_H

#include "mm/preview_render.h"
#include "mm/world_camera.h"
#include "mm/world_render.h"

#ifdef __cplusplus
extern "C" {
#endif

struct br_actor;
struct br_camera;
struct br_light;
struct br_pixelmap;
struct MMBackgroundMeshServices;
struct MMBackgroundPreviewServices;

/* Studio embeds its actors and camera data. Preview adds allocated actors
 * to a temporary tree and keeps their camera and light data alive separately. */
void mm_brender_world_init(struct br_actor *world, struct br_actor *camera, struct br_camera *data,
                           void *owner, MMI32 width, MMI32 height);
void mm_brender_world_camera_pose(struct br_actor *, const MMWorldCamera *);
void mm_brender_world_camera_view(const struct br_actor *, const MMWorldCamera *, MMRenderCamera *);
void *mm_brender_world_owner(void *actor);
void mm_brender_pixelmap_bind(struct br_pixelmap *, const MMWorldPixelView *);
void mm_brender_world_raster(void *, const MMRenderFrame *, const MMWorldPixelView *, const MMWorldPixelView *);
void mm_brender_world_services(MMWorldRenderServices *);

int mm_brender_background_light(void *, MMBackgroundResource *, MMI32, const MMBackgroundLight *);
void mm_brender_background_light_enable(void *, MMBackgroundResource *, MMI32);
void mm_brender_background_light_disable(void *, MMBackgroundResource *, MMI32);
void mm_brender_background_light_remove(void *, MMBackgroundResource *, MMI32);
void mm_brender_background_light_rotation(unsigned short pitch, unsigned short yaw, MMActorMatrix *);
void mm_brender_camera_matrix(MMActorMatrix *, float x, float y, float z, unsigned short yaw,
                              unsigned short pitch, unsigned short roll);
int mm_brender_matrix_inverse(MMActorMatrix *, const MMActorMatrix *);
void mm_brender_matrix_apply_point(MMActorPoint *, const MMActorPoint *, const MMActorMatrix *);

struct br_actor *mm_brender_preview_camera(void *world, struct br_camera *, const MMRenderCamera *);
void mm_brender_preview_tree(MMPreviewTreeServices *);
void mm_brender_preview_services(MMPreviewRenderServices *);
const struct MMBackgroundMeshServices *mm_brender_background_mesh_services(void);
void mm_brender_preview_background_services(struct MMBackgroundPreviewServices *);

#ifdef __cplusplus
}
#endif
#endif

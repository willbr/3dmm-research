#ifndef MM_ENGINE_PREVIEW_POSE_H
#define MM_ENGINE_PREVIEW_POSE_H
#include "mm/preview_render.h"

/* Collect once per operation. The host keeps background buffers alive and the
   pose keeps model and body resources alive until the temporary world is freed. */
typedef struct MMPreviewPose
{
    MMPreviewData scene;
    MMMovieRenderScene render;
    struct MMBackgroundMesh *mesh;
} MMPreviewPose;

void mm_preview_describe(MMPreviewData *, const MMPreviewRenderServices *);
int mm_preview_ready(const MMPreviewRenderServices *);
const MMMovieLiveFrame *mm_preview_position(const MMPreviewRenderServices *);
MMU32 mm_preview_body_count(const MMPreviewRenderServices *);
int mm_preview_read_body(MMPreviewBody *, MMU32 index, int bounds, const MMPreviewRenderServices *);
void mm_preview_pose_init(MMPreviewPose *, const MMPreviewRenderServices *);
void mm_preview_pose_describe(MMPreviewPose *, const MMPreviewRenderServices *);
int mm_preview_pose_mesh(MMPreviewPose *, MMPreviewMesh *, const MMPreviewRenderServices *);
int mm_preview_pose_add_mesh(const MMPreviewPose *, void *world, const MMPreviewRenderServices *);
int mm_preview_pose_bodies(MMPreviewPose *, int bounds, const MMPreviewRenderServices *);
int mm_preview_pose_submit(MMPreviewPose *, void *world, MMCollectionStorage *scratch,
                           const MMPreviewRenderServices *);
void mm_preview_pose_dispose(MMPreviewPose *, const MMPreviewRenderServices *);

#endif

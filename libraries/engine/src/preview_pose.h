#ifndef MM_ENGINE_PREVIEW_POSE_H
#define MM_ENGINE_PREVIEW_POSE_H
#include "mm/preview_render.h"

/* Collect once per operation. The host keeps background buffers alive and the
   pose keeps model and body resources alive until the temporary world is freed. */
typedef struct MMPreviewPose
{
    MMPreviewData scene;
    MMCollectionStorage storage;
    MMArray bodies;
} MMPreviewPose;

void mm_preview_pose_init(MMPreviewPose *, const MMPreviewRenderServices *);
void mm_preview_pose_describe(MMPreviewPose *, const MMPreviewRenderServices *);
int mm_preview_pose_bodies(MMPreviewPose *, const MMPreviewRenderServices *);
int mm_preview_pose_submit(MMPreviewPose *, void *world, MMCollectionStorage *scratch,
                           const MMPreviewRenderServices *);
void mm_preview_pose_dispose(MMPreviewPose *, const MMPreviewRenderServices *);

#endif

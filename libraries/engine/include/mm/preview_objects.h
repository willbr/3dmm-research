#ifndef MM_ENGINE_PREVIEW_OBJECTS_H
#define MM_ENGINE_PREVIEW_OBJECTS_H

#include "preview_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MMPreviewLive;
typedef struct MMPreviewObjectRows
{
    MMCollectionStorage storage;
    MMU32 count;
} MMPreviewObjectRows;

MMU32 mm_preview_object_count(const struct MMPreviewLive *);
int mm_preview_object_at(const struct MMPreviewLive *, MMI32 index, MMPreviewActorInfo *);
void mm_preview_object_position(const MMPreviewActorInfo *, float position[3]);

int mm_preview_object_rows_init(MMPreviewObjectRows *, const MMStorageAllocator *);
void mm_preview_object_rows_dispose(MMPreviewObjectRows *);
/* Capture in Preview object order. A failed capture preserves the rows. */
int mm_preview_object_rows_capture(MMPreviewObjectRows *, const struct MMPreviewLive *);
int mm_preview_object_rows_replace(MMPreviewObjectRows *, MMI32 index, const MMPreviewActorInfo *);
const MMPreviewActorInfo *mm_preview_object_rows_data(const MMPreviewObjectRows *);
MMI32 mm_preview_object_rows_find(const MMPreviewObjectRows *, MMI32 actor, int prefer_last);

#ifdef __cplusplus
}
#endif
#endif

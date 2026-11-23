#ifndef MM_PREVIEW_DRAFT_H
#define MM_PREVIEW_DRAFT_H
#include "mm/actor_persistence.h"
#include "mm/material_resource.h"
#include "mm/native_archive_file.h"
#include "mm/preview_live.h"

#ifdef __cplusplus
extern "C" {
#endif

int mm_preview_store_actor_tracks(const MMNativePathChar *, const MMPrivateArchiveProfile *, MMU32 creator,
                                  const MMActorTrack *, int with_route);

typedef struct MMPreviewMaterialSource
{
    MMI32 object, source;
    MMU32 number;
    int preset;
} MMPreviewMaterialSource;

typedef struct MMPreviewDraftServices
{
    void *context;
    const MMPrivateArchiveProfile *profile;
    MMU32 creator;
    /* Synchronous providers borrow the converted path and resource cache.
     * A null result reports failure. Providers must not unwind into C. */
    const MMNativePathChar *(*path)(void *);
    MMResourceCache *(*resolve)(void *, const MMTag *);
} MMPreviewDraftServices;

enum
{
    MM_PREVIEW_DRAFT_OK, MM_PREVIEW_DRAFT_MATERIAL_READ, MM_PREVIEW_DRAFT_MATERIAL_INVALID,
    MM_PREVIEW_DRAFT_MATERIAL_FETCH, MM_PREVIEW_DRAFT_MATERIAL_PATH, MM_PREVIEW_DRAFT_MATERIAL_WRITE,
    MM_PREVIEW_DRAFT_MATERIAL_SAVE, MM_PREVIEW_DRAFT_FONT_SOURCE, MM_PREVIEW_DRAFT_FONT_WRITE,
    MM_PREVIEW_DRAFT_FONT_MISSING, MM_PREVIEW_DRAFT_FONT_SAVE, MM_PREVIEW_DRAFT_NOT_TEXT,
    MM_PREVIEW_DRAFT_MATERIAL_LOAD
};

int mm_preview_read_text_material(const MMPreviewLive *, const MMPreviewMaterialSource *, MMMaterialAppearance *,
                                  const MMPreviewDraftServices *);
int mm_preview_store_text_material(const MMPreviewLive *, const MMPreviewMaterialSource *, const MMMaterialAppearance *,
                                   const MMPreviewDraftServices *);
int mm_preview_bind_text_font(MMI32 source, MMU32 font, const MMPreviewDraftServices *);
/* Complete a staged text preview. A null appearance copies the live or
 * default appearance. Publish the private file only if both saves succeed. */
int mm_preview_store_text_preview(const MMPreviewLive *, const MMPreviewMaterialSource *, const MMMaterialAppearance *,
                                  MMI32 font_source, MMU32 font, const MMPreviewDraftServices *);
int mm_preview_use_text_material(const MMPreviewAssets *, MMResourceCache *);

#ifdef __cplusplus
}
#endif
#endif

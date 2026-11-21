#ifndef MM_PREVIEW_DRAFT_H
#define MM_PREVIEW_DRAFT_H
#include "mm/actor_persistence.h"
#include "mm/native_files.h"
#include "mm/private_archive.h"

#ifdef __cplusplus
extern "C" {
#endif

MMPrivateArchive *mm_preview_open_draft(const MMNativePathChar *, int mode, const MMPrivateArchiveProfile *);
int mm_preview_store_actor_tracks(const MMNativePathChar *, const MMPrivateArchiveProfile *, MMU32 creator,
                                  const MMActorTrack *, int with_route);

#ifdef __cplusplus
}
#endif
#endif

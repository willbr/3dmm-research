#ifndef MM_PREVIEW_DRAFT_H
#define MM_PREVIEW_DRAFT_H
#include "mm/actor_persistence.h"
#include "mm/native_archive_file.h"

#ifdef __cplusplus
extern "C" {
#endif

int mm_preview_store_actor_tracks(const MMNativePathChar *, const MMPrivateArchiveProfile *, MMU32 creator,
                                  const MMActorTrack *, int with_route);

#ifdef __cplusplus
}
#endif
#endif

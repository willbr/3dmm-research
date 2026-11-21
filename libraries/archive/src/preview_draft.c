#include "preview_draft.h"

typedef struct ActorDraft
{
    const MMNativePathChar *path;
    const MMPrivateArchiveProfile *profile;
    MMU32 creator;
    MMPrivateArchive *archive;
} ActorDraft;

static int create_track_archive(void *p, MMArchiveWriter *writer)
{
    ActorDraft *draft = p;
    draft->archive = mm_native_private_archive_open(draft->path, MM_PRIVATE_ARCHIVE_CREATE, draft->profile);
    if (!draft->archive)
        return 0;
    *writer = mm_private_archive_writer(draft->archive);
    return 1;
}

static int save_track_archive(void *p)
{
    ActorDraft *draft = p;
    return mm_private_archive_save(draft->archive, draft->creator);
}

int mm_preview_store_actor_tracks(const MMNativePathChar *path, const MMPrivateArchiveProfile *profile, MMU32 creator,
                                  const MMActorTrack *track, int with_route)
{
    ActorDraft draft = {NULL, NULL, 0, NULL};
    MMActorTrackArchive archive = {NULL, create_track_archive, save_track_archive};
    int ok;
    draft.path = path;
    draft.profile = profile;
    draft.creator = creator;
    archive.context = &draft;
    ok = mm_actor_store_edit_tracks(track, with_route, &archive);
    mm_private_archive_close(draft.archive);
    return ok;
}

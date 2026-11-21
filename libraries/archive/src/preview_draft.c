#include "preview_draft.h"
#include <stdlib.h>

typedef struct DraftFile
{
    void *handle;
    size_t references;
} DraftFile;

static int seek_extent(DraftFile *file, MMU32 offset, size_t bytes)
{
    return offset <= 0x7fffffffUL && bytes <= 0x7fffffffUL - offset && mm_native_file_seek(file->handle, offset);
}

static int read_at(void *context, void *p, MMU32 offset, void *data, size_t bytes)
{
    DraftFile *file = p;
    (void)context;
    return !bytes || (seek_extent(file, offset, bytes) && mm_native_file_read(file->handle, data, (MMU32)bytes));
}

static int write_at(void *p, MMU32 offset, const void *data, size_t bytes)
{
    DraftFile *file = p;
    return !bytes || (seek_extent(file, offset, bytes) && mm_native_file_write(file->handle, data, (MMU32)bytes) &&
                      (offset || mm_native_file_flush(file->handle)));
}

static int write_block(void *context, void *p, MMU32 offset, const void *data, size_t bytes)
{
    (void)context;
    return write_at(p, offset, data, bytes);
}

static MMReadStream read_stream(void *p)
{
    MMReadStream stream = {0};
    mm_native_file_read_stream(((DraftFile *)p)->handle, &stream);
    return stream;
}

static MMU32 size(void *p)
{
    MMU32 bytes;
    return mm_native_file_size(((DraftFile *)p)->handle, &bytes) ? bytes : 0xffffffffUL;
}

static int resize(void *p, MMU32 bytes)
{
    return mm_native_file_resize(((DraftFile *)p)->handle, bytes);
}

static void retain(void *context, void *p)
{
    (void)context;
    ++((DraftFile *)p)->references;
}

static void release(void *p)
{
    DraftFile *file = p;
    if (--file->references == 0)
    {
        mm_native_file_close(file->handle);
        free(file);
    }
}

static void release_block(void *context, void *p)
{
    (void)context;
    release(p);
}

static void *create_extra(void *context)
{
    (void)context;
    return NULL;
}

MMPrivateArchive *mm_preview_open_draft(const MMNativePathChar *path, int mode, const MMPrivateArchiveProfile *profile)
{
    static const MMChunkOwnerFileIO owner = {NULL, create_extra, read_stream, size, resize, NULL,
                                             write_at, release, NULL, NULL, NULL, NULL};
    static const MMBlockFileServices blocks = {NULL, retain, release_block, create_extra, read_at, write_block};
    MMNativeOpenOptions options;
    DraftFile *file;
    if (!path || (mode != MM_PRIVATE_ARCHIVE_CREATE && mode != MM_PRIVATE_ARCHIVE_EDIT))
        return NULL;
    file = malloc(sizeof(*file));
    if (!file)
        return NULL;
    mm_native_open_options_init(&options);
    options.write = 1;
    if (mode == MM_PRIVATE_ARCHIVE_CREATE)
        options.creation = MM_NATIVE_CREATE_ALWAYS;
    file->handle = mm_native_file_open(path, &options);
    if (!file->handle)
    {
        free(file);
        return NULL;
    }
    file->references = 1;
    return mm_private_archive_open(file, mode, &owner, &blocks, profile);
}

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
    draft->archive = mm_preview_open_draft(draft->path, MM_PRIVATE_ARCHIVE_CREATE, draft->profile);
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

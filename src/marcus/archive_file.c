#include "archive_file.h"
#include "mm/actor_archive.h"
#include "mm/scene_data.h"
#include <SDL3/SDL.h>

typedef struct ArchiveFile
{
    SDL_IOStream *stream;
    size_t references;
} ArchiveFile;

static void retain(void *context, void *file)
{
    (void)context;
    ++((ArchiveFile *)file)->references;
}

static void release(void *p)
{
    ArchiveFile *file = p;
    if (--file->references == 0)
    {
        SDL_CloseIO(file->stream);
        SDL_free(file);
    }
}

static void release_block(void *context, void *file)
{
    (void)context;
    release(file);
}

static void *create_temp(void *context)
{
    (void)context;
    return NULL;
}

static int read_at(void *p, MMU32 offset, void *data, size_t bytes)
{
    SDL_IOStream *stream = ((ArchiveFile *)p)->stream;
    return !bytes || (SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) == offset &&
                      SDL_ReadIO(stream, data, bytes) == bytes);
}

static int write_at(void *p, MMU32 offset, const void *data, size_t bytes)
{
    SDL_IOStream *stream = ((ArchiveFile *)p)->stream;
    return !bytes || (SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) == offset &&
                      SDL_WriteIO(stream, data, bytes) == bytes &&
                      (offset != 0 || SDL_FlushIO(stream)));
}

static MMU32 size(void *p)
{
    const Sint64 bytes = SDL_GetIOSize(((ArchiveFile *)p)->stream);
    return bytes >= 0 && bytes <= 0x7fffffffL ? (MMU32)bytes : 0xffffffffUL;
}

static int extend(void *file, MMU32 bytes)
{
    const MMU32 end = size(file);
    const unsigned char zero = 0;
    return bytes == end || (bytes > end && write_at(file, bytes - 1, &zero, 1));
}

static MMReadStream read_stream(void *file)
{
    MMReadStream stream = {NULL, 0, read_at};
    const MMU32 bytes = size(file);
    stream.context = file;
    if (bytes <= 0x7fffffffUL)
        stream.length = bytes;
    return stream;
}

static int read_block(void *context, void *file, MMU32 offset, void *data, size_t bytes)
{
    (void)context;
    return read_at(file, offset, data, bytes);
}

static int write_block(void *context, void *file, MMU32 offset, const void *data, size_t bytes)
{
    (void)context;
    return write_at(file, offset, data, bytes);
}

MMPrivateArchive *marcus_archive_open(const char *path, int mode, const MMPrivateArchiveProfile *profile)
{
    static const MMChunkOwnerFileIO owner_io = {NULL, create_temp, read_stream, size, extend, NULL,
                                                write_at, release, NULL, NULL, NULL, NULL, NULL};
    static const MMBlockFileServices blocks = {NULL, retain, release_block, create_temp, read_block, write_block};
    const char *open_mode = mode == MM_PRIVATE_ARCHIVE_READ ? "rb" : mode == MM_PRIVATE_ARCHIVE_EDIT ? "r+b" : "w+b";
    ArchiveFile *file = SDL_malloc(sizeof(*file));
    if (!file)
        return NULL;
    file->stream = SDL_IOFromFile(path, open_mode);
    if (!file->stream)
    {
        SDL_free(file);
        return NULL;
    }
    file->references = 1;
    return mm_private_archive_open(file, mode, &owner_io, &blocks, profile);
}

int marcus_archive_install_events(MMPrivateArchive *candidate, MMU32 actor, const char *track,
                                  const MMPrivateArchiveProfile *profile)
{
    MMPrivateArchive *source = marcus_archive_open(track, MM_PRIVATE_ARCHIVE_READ, profile);
    int result = source ? mm_actor_archive_import_events(candidate, actor, source) : MM_ACTOR_EDIT_ARCHIVE_COPY;
    mm_private_archive_close(source);
    return result;
}

int marcus_archive_animate(const char *path, const char *track, const MMPrivateArchiveProfile *profile,
                           const MMActorArchiveAnimation *animation, MMU32 creator, int *edit, int *scene)
{
    MMPrivateArchive *candidate = marcus_archive_open(path, MM_PRIVATE_ARCHIVE_EDIT, profile);
    MMPrivateArchive *source = marcus_archive_open(track, MM_PRIVATE_ARCHIVE_READ, profile);
    int result = MARCUS_ARCHIVE_ANIMATION_OPEN;
    if (candidate && source)
    {
        *edit = mm_actor_archive_animate(candidate, source, animation, scene);
        if (*edit != MM_ACTOR_EDIT_ARCHIVE_OK || *scene != MM_SCENE_ARCHIVE_EDIT_OK)
            result = MARCUS_ARCHIVE_ANIMATION_EDIT;
        else
            result = mm_private_archive_save(candidate, creator)
                ? MARCUS_ARCHIVE_ANIMATION_OK : MARCUS_ARCHIVE_ANIMATION_SAVE;
    }
    mm_private_archive_close(source);
    mm_private_archive_close(candidate);
    return result;
}

#include "mm/movie_content_resolver.h"
#include "movie_session_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct Archive
{
    const char *location;
    void *handle;
    MMReadStream stream;
    MMChunkIndex *index;
    struct Archive *next;
} Archive;

struct MMMovieContent
{
    const MMMovieSession *movie;
    const MMVfs *vfs;
    MMMovieArchiveProvider provider;
    Archive *archives;
};

MMMovieContent *mm_movie_content_open(const MMMovieSession *movie, const MMVfs *vfs,
                                     const MMMovieArchiveProvider *provider)
{
    MMMovieContent *content;
    if (!movie || !vfs || !provider || !provider->open || !provider->close)
        return NULL;
    content = (MMMovieContent *)malloc(sizeof(*content));
    if (content)
    {
        content->movie = movie;
        content->vfs = vfs;
        content->provider = *provider;
        content->archives = NULL;
    }
    return content;
}

void mm_movie_content_close(MMMovieContent *content)
{
    Archive *archive;
    if (!content)
        return;
    while (content->archives)
    {
        archive = content->archives;
        content->archives = archive->next;
        mm_chunk_index_close(archive->index);
        content->provider.close(content->provider.context, archive->handle);
        free(archive);
    }
    free(content);
}

static int open_archive(MMMovieContent *content, const char *location, Archive **out)
{
    Archive *archive;
    int status;
    for (archive = content->archives; archive; archive = archive->next)
        if (!strcmp(archive->location, location))
        {
            *out = archive;
            return MM_MOVIE_OK;
        }
    archive = (Archive *)malloc(sizeof(*archive));
    if (!archive)
        return MM_MOVIE_MEMORY;
    status = content->provider.open(content->provider.context, location, &archive->handle, &archive->stream);
    if (status == MM_MOVIE_OK)
    {
        status = mm_session_index(&archive->stream, &content->movie->policy, &archive->index);
        if (status != MM_MOVIE_OK)
            content->provider.close(content->provider.context, archive->handle);
    }
    if (status != MM_MOVIE_OK)
    {
        free(archive);
        return status;
    }
    archive->location = location;
    archive->next = content->archives;
    content->archives = archive;
    *out = archive;
    return MM_MOVIE_OK;
}

static int content_archive(const char *name)
{
    const char *extension = strrchr(name, '.');
    return extension && strlen(extension) == 4 && extension[1] == '3' &&
           (extension[2] == 'c' || extension[2] == 'C') && (extension[3] == 'n' || extension[3] == 'N');
}

int mm_movie_content_source(void *ctx, const MMMovieAsset *asset, MMReadStream *out)
{
    MMMovieContent *content = (MMMovieContent *)ctx;
    char source[MM_MOVIE_SOURCE_NAME_BYTES];
    MMVfsMount mount;
    Archive *archive;
    MMChunkAccess access;
    MMU32 position;
    size_t cursor = 0;
    int status;
    if (!content || !asset || !out)
        return MM_MOVIE_BAD_FILE;
    status = mm_movie_session_source_name(content->movie, asset->source, source);
    if (status != MM_MOVIE_OK)
        return status;
    while (mm_vfs_next(content->vfs, source, &cursor, &mount))
    {
        if (!content_archive(mount.name))
            continue;
        status = open_archive(content, mount.location, &archive);
        if (status != MM_MOVIE_OK)
            return status;
        access = mm_chunk_index_access(archive->index);
        status = mm_chunk_find(&access, asset->type, asset->number, &position);
        if (status == MM_CHUNK_OK)
        {
            *out = archive->stream;
            return MM_MOVIE_OK;
        }
        if (status != MM_CHUNK_NOT_FOUND)
            return MM_MOVIE_BAD_FILE;
    }
    return MM_MOVIE_ASSET_NOT_FOUND;
}

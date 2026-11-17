#include "mm/movie_content_resolver.h"
#include "mm/resource_lookup.h"
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

typedef struct ContentLookup
{
    MMMovieContent *content;
    const MMMovieAsset *asset;
    const char *source;
} ContentLookup;

static int next_archive(void *context, size_t *cursor, void **out)
{
    ContentLookup *search = (ContentLookup *)context;
    MMVfsMount mount;
    Archive *archive;
    int status;
    if (!mm_vfs_next_extension(search->content->vfs, search->source, ".3cn", cursor, &mount))
        return MM_RESOURCE_LOOKUP_MISSING;
    status = open_archive(search->content, mount.location, &archive);
    if (status != MM_MOVIE_OK)
        return status;
    *out = archive;
    return MM_RESOURCE_LOOKUP_OK;
}

static int probe_archive(void *context, void *candidate, void **out)
{
    const ContentLookup *search = (const ContentLookup *)context;
    Archive *archive = (Archive *)candidate;
    MMChunkAccess access = mm_chunk_index_access(archive->index);
    MMU32 position;
    int status = mm_chunk_find(&access, search->asset->type, search->asset->number, &position);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_RESOURCE_LOOKUP_MISSING : MM_MOVIE_BAD_FILE;
    *out = archive;
    return MM_RESOURCE_LOOKUP_OK;
}

int mm_movie_content_source(void *ctx, const MMMovieAsset *asset, MMReadStream *out)
{
    MMMovieContent *content = (MMMovieContent *)ctx;
    char source[MM_MOVIE_SOURCE_NAME_BYTES];
    ContentLookup search;
    MMResourceLookup lookup;
    void *result;
    int status;
    if (!content || !asset || !out)
        return MM_MOVIE_BAD_FILE;
    status = mm_movie_session_source_name(content->movie, asset->source, source);
    if (status != MM_MOVIE_OK)
        return status;
    search.content = content;
    search.asset = asset;
    search.source = source;
    lookup.context = &search;
    lookup.next = next_archive;
    lookup.probe = probe_archive;
    status = mm_resource_lookup(&lookup, &result);
    if (status == MM_RESOURCE_LOOKUP_OK)
        *out = ((Archive *)result)->stream;
    return status == MM_RESOURCE_LOOKUP_MISSING ? MM_MOVIE_ASSET_NOT_FOUND : status;
}

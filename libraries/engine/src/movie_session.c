#include "mm/movie_session.h"
#include "mm/codec.h"
#include "mm/data_stream.h"
#include "mm/scene_data.h"
#include <stdlib.h>

#define MOVIE_TYPE 0x4d564945UL
#define SCENE_TYPE 0x5343454eUL

struct MMMovieSession
{
    MMReadStream stream;
    MMChunkIndex *index;
    MMU32 root_position;
    MMMovieSessionRoot root;
};

static void *find_codec(void *context, MMI32 format)
{
    return format == MM_CODEC_KCDC || format == MM_CODEC_KCD2 ? context : NULL;
}

static int decode(void *codec, MMI32 format, const void *input, MMU32 bytes, void *output,
                  MMU32 capacity, MMI32 *written)
{
    (void)codec;
    return mm_codec_decode(format, input, bytes, output, capacity, written) == MM_DATA_OK;
}

static int read_record(const MMMovieSession *movie, const MMChunkEntry *entry, unsigned char *out, MMU32 bytes)
{
    MMU32 index_offset = mm_chunk_index_header(movie->index)->index_offset;
    unsigned char *packed;
    MMI32 written;
    MMPackedDecoder decoder;
    int status;
    if (entry->offset < MM_CHUNK_PREFIX_BYTES || entry->offset > index_offset ||
        entry->bytes > index_offset - entry->offset)
        return MM_MOVIE_BAD_FILE;
    if (!(entry->flags & MM_CHUNK_PACKED))
    {
        if (entry->bytes != bytes)
            return MM_MOVIE_BAD_FILE;
        return mm_read_exact(&movie->stream, entry->offset, out, bytes) ? MM_MOVIE_OK : MM_MOVIE_IO;
    }
    if (entry->bytes <= 8 || entry->bytes >= MM_STORAGE_LIMIT || (size_t)entry->bytes != entry->bytes)
        return MM_MOVIE_BAD_FILE;
    packed = (unsigned char *)malloc((size_t)entry->bytes);
    if (!packed)
        return MM_MOVIE_MEMORY;
    status = MM_MOVIE_IO;
    if (mm_read_exact(&movie->stream, entry->offset, packed, (size_t)entry->bytes))
    {
        decoder.context = packed;
        decoder.find = find_codec;
        decoder.convert = decode;
        status = mm_packed_decode(&decoder, packed, entry->bytes, out, bytes, &written) == MM_DATA_OK &&
                 written == (MMI32)bytes ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
    }
    free(packed);
    return status;
}

static int load_root(MMMovieSession *movie)
{
    MMChunkAccess access = mm_chunk_index_access(movie->index);
    MMChunkEntry entry;
    unsigned char bytes[MM_MOVIE_PREFIX_BYTES];
    MMMemoryStream memory;
    int status;
    if (mm_chunk_type_at(&access, MOVIE_TYPE, 0, &entry, &movie->root_position) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = read_record(movie, &entry, bytes, sizeof(bytes));
    if (status != MM_MOVIE_OK)
        return status;
    movie->root.number = entry.number;
    mm_memory_stream_init(&memory, bytes, sizeof(bytes));
    return mm_movie_read_header(&memory.stream, &movie->root.header);
}

int mm_movie_session_open(const MMReadStream *stream, const MMChunkPolicy *policy, MMMovieSession **out)
{
    MMMovieSession *movie;
    MMChunkIndex *index;
    int status;
    if (!stream || !policy || !out)
        return MM_MOVIE_BAD_FILE;
    status = mm_chunk_index_open(stream, policy, &index);
    if (status == MM_CHUNK_IO)
        return MM_MOVIE_IO;
    if (status == MM_CHUNK_MEMORY)
        return MM_MOVIE_MEMORY;
    if (status != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    movie = (MMMovieSession *)malloc(sizeof(*movie));
    if (!movie)
    {
        mm_chunk_index_close(index);
        return MM_MOVIE_MEMORY;
    }
    movie->stream = *stream;
    movie->index = index;
    status = load_root(movie);
    if (status != MM_MOVIE_OK)
    {
        mm_movie_session_close(movie);
        return status;
    }
    *out = movie;
    return MM_MOVIE_OK;
}

void mm_movie_session_close(MMMovieSession *movie)
{
    if (movie)
    {
        mm_chunk_index_close(movie->index);
        free(movie);
    }
}

const MMMovieSessionRoot *mm_movie_session_root(const MMMovieSession *movie)
{
    return movie ? &movie->root : NULL;
}

int mm_movie_session_scene(const MMMovieSession *movie, MMU32 slot, MMMovieSessionScene *out)
{
    MMChunkAccess access;
    MMChunkChild child;
    MMChunkEntry entry;
    MMU32 position;
    unsigned char bytes[MM_SCENE_HEADER_BYTES];
    MMMemoryStream memory;
    MMSceneState scene;
    int status;
    if (!movie || !out)
        return MM_MOVIE_BAD_FILE;
    access = mm_chunk_index_access(movie->index);
    status = mm_chunk_child_type(&access, movie->root_position, slot, SCENE_TYPE, &child);
    if (status == MM_CHUNK_NOT_FOUND || (status == MM_CHUNK_OK && child.type != SCENE_TYPE))
        return MM_MOVIE_SCENE_NOT_FOUND;
    if (status != MM_CHUNK_OK || mm_chunk_find(&access, SCENE_TYPE, child.number, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&access, position, &entry) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = read_record(movie, &entry, bytes, sizeof(bytes));
    if (status != MM_MOVIE_OK)
        return status;
    mm_memory_stream_init(&memory, bytes, sizeof(bytes));
    mm_scene_init(&scene, 1);
    if (!mm_scene_decode_header(&memory.stream, &scene))
        return MM_MOVIE_BAD_FILE;
    out->number = child.number;
    out->first_frame = scene.firstFrame;
    out->last_frame = scene.lastFrame;
    out->transition = scene.transition;
    return MM_MOVIE_OK;
}

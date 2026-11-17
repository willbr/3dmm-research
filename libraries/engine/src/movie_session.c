#include "mm/movie_session.h"
#include "mm/codec.h"
#include "mm/data_stream.h"
#include "mm/scene_data.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

#define MOVIE_TYPE 0x4d564945UL
#define SCENE_TYPE 0x5343454eUL
#define SCENE_START_TYPE 0x47475354UL
#define SCENE_FRAME_TYPE 0x47474652UL
#define BACKGROUND_TYPE 0x424b4744UL
#define CAMERA_TYPE 0x43414d20UL

struct MMMovieSession
{
    MMReadStream stream;
    MMChunkIndex *index;
    MMChunkPolicy policy;
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

typedef struct ChunkPayload
{
    MMReadSlice slice;
    MMMemoryStream memory;
    unsigned char *decoded;
    const MMReadStream *stream;
} ChunkPayload;

static int open_payload(const MMReadStream *stream, const MMChunkIndex *index, const MMChunkEntry *entry,
                        MMU32 exact_size, ChunkPayload *payload)
{
    MMU32 index_offset = mm_chunk_index_header(index)->index_offset;
    unsigned char *packed;
    MMI32 expected, written;
    MMPackedDecoder decoder;
    int status;
    payload->decoded = NULL;
    if (entry->offset < MM_CHUNK_PREFIX_BYTES || entry->offset > index_offset ||
        entry->bytes > index_offset - entry->offset ||
        !mm_read_slice_init(&payload->slice, stream, entry->offset, entry->bytes))
        return MM_MOVIE_BAD_FILE;
    payload->stream = &payload->slice.stream;
    if (!(entry->flags & MM_CHUNK_PACKED))
        return !exact_size || entry->bytes == exact_size ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
    if (entry->bytes <= 8 || entry->bytes >= MM_STORAGE_LIMIT || (size_t)entry->bytes != entry->bytes)
        return MM_MOVIE_BAD_FILE;
    packed = (unsigned char *)malloc((size_t)entry->bytes);
    if (!packed)
        return MM_MOVIE_MEMORY;
    decoder.context = packed;
    decoder.find = find_codec;
    decoder.convert = decode;
    if (!mm_read_exact(payload->stream, 0, packed, (size_t)entry->bytes))
        status = MM_MOVIE_IO;
    else if (mm_packed_decode(&decoder, packed, entry->bytes, NULL, 0, &expected) != MM_DATA_OK ||
             (MMU32)(size_t)expected != (MMU32)expected || (exact_size && (MMU32)expected != exact_size))
        status = MM_MOVIE_BAD_FILE;
    else if (!(payload->decoded = (unsigned char *)malloc((size_t)expected)))
        status = MM_MOVIE_MEMORY;
    else if (mm_packed_decode(&decoder, packed, entry->bytes, payload->decoded, (MMU32)expected,
                              &written) != MM_DATA_OK)
    {
        free(payload->decoded);
        payload->decoded = NULL;
        status = MM_MOVIE_BAD_FILE;
    }
    else
    {
        mm_memory_stream_init(&payload->memory, payload->decoded, (size_t)written);
        payload->stream = &payload->memory.stream;
        status = MM_MOVIE_OK;
    }
    free(packed);
    return status;
}

static int load_root(MMMovieSession *movie)
{
    MMChunkAccess access = mm_chunk_index_access(movie->index);
    MMChunkEntry entry;
    ChunkPayload payload;
    int status;
    if (mm_chunk_type_at(&access, MOVIE_TYPE, 0, &entry, &movie->root_position) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = open_payload(&movie->stream, movie->index, &entry, MM_MOVIE_PREFIX_BYTES, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    movie->root.number = entry.number;
    status = mm_movie_read_header(payload.stream, &movie->root.header);
    free(payload.decoded);
    return status;
}

static int open_index(const MMReadStream *stream, const MMChunkPolicy *policy, MMChunkIndex **index)
{
    switch (mm_chunk_index_open(stream, policy, index))
    {
    case MM_CHUNK_OK:
        return MM_MOVIE_OK;
    case MM_CHUNK_IO:
        return MM_MOVIE_IO;
    case MM_CHUNK_MEMORY:
        return MM_MOVIE_MEMORY;
    default:
        return MM_MOVIE_BAD_FILE;
    }
}

int mm_movie_session_open(const MMReadStream *stream, const MMChunkPolicy *policy, MMMovieSession **out)
{
    MMMovieSession *movie;
    MMChunkIndex *index;
    int status;
    if (!stream || !policy || !out)
        return MM_MOVIE_BAD_FILE;
    status = open_index(stream, policy, &index);
    if (status != MM_MOVIE_OK)
        return status;
    movie = (MMMovieSession *)malloc(sizeof(*movie));
    if (!movie)
    {
        mm_chunk_index_close(index);
        return MM_MOVIE_MEMORY;
    }
    movie->stream = *stream;
    movie->index = index;
    movie->policy = *policy;
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

/* Return the first exact-type child and its existing target. */
static int child_entry(const MMChunkAccess *access, MMU32 parent, MMU32 slot, MMU32 type,
                       MMChunkEntry *entry, MMU32 *position)
{
    MMChunkChild child;
    int status = mm_chunk_child_type(access, parent, slot, type, &child);
    if (status == MM_CHUNK_NOT_FOUND || (status == MM_CHUNK_OK && child.type != type))
        return MM_CHUNK_NOT_FOUND;
    if (status != MM_CHUNK_OK || mm_chunk_find(access, type, child.number, position) != MM_CHUNK_OK)
        return MM_CHUNK_BAD_FILE;
    return mm_chunk_at(access, *position, entry);
}

static int load_scene(const MMMovieSession *movie, const MMChunkAccess *access, MMU32 slot,
                      MMU32 *position, MMMovieSessionScene *out)
{
    MMChunkEntry entry;
    ChunkPayload payload;
    unsigned char bytes[MM_SCENE_HEADER_BYTES];
    MMMemoryStream header;
    MMSceneState scene;
    int status = child_entry(access, movie->root_position, slot, SCENE_TYPE, &entry, position);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_SCENE_NOT_FOUND : MM_MOVIE_BAD_FILE;
    status = open_payload(&movie->stream, movie->index, &entry, MM_SCENE_HEADER_BYTES, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_read_exact(payload.stream, 0, bytes, sizeof(bytes)) ? MM_MOVIE_OK : MM_MOVIE_IO;
    free(payload.decoded);
    if (status != MM_MOVIE_OK)
        return status;
    mm_memory_stream_init(&header, bytes, sizeof(bytes));
    mm_scene_init(&scene, 1);
    if (!mm_scene_decode_header(&header.stream, &scene))
        return MM_MOVIE_BAD_FILE;
    out->number = entry.number;
    out->first_frame = scene.firstFrame;
    out->last_frame = scene.lastFrame;
    out->transition = scene.transition;
    return MM_MOVIE_OK;
}

int mm_movie_session_scene(const MMMovieSession *movie, MMU32 slot, MMMovieSessionScene *out)
{
    MMChunkAccess access;
    MMU32 position;
    if (!movie || !out)
        return MM_MOVIE_BAD_FILE;
    access = mm_chunk_index_access(movie->index);
    return load_scene(movie, &access, slot, &position, out);
}

typedef struct SceneEventGroup
{
    MMCollectionHeader header;
    const unsigned char *records, *locations;
} SceneEventGroup;

static MMU32 word(const unsigned char *p, int big)
{
    return big ? ((MMU32)p[0] << 24) | ((MMU32)p[1] << 16) | ((MMU32)p[2] << 8) | p[3]
               : p[0] | ((MMU32)p[1] << 8) | ((MMU32)p[2] << 16) | ((MMU32)p[3] << 24);
}

static const unsigned char *event_row(const SceneEventGroup *group, MMU32 row, MMI32 *size)
{
    const unsigned char *location = group->locations + row * 8;
    MMU32 offset = word(location, group->header.big_endian);
    MMU32 bytes = word(location + 4, group->header.big_endian);
    *size = -1;
    if (offset > group->header.first_bytes || bytes < MM_SCENE_EVENT_BYTES ||
        bytes > group->header.first_bytes - offset)
        return NULL;
    *size = (MMI32)(bytes - MM_SCENE_EVENT_BYTES);
    return group->records + offset;
}

static MMU32 event_width(void *p) { return ((const SceneEventGroup *)p)->header.width; }
static MMU32 event_count(void *p) { return ((const SceneEventGroup *)p)->header.count; }
static void event_fixed(void *p, MMU32 i, unsigned char out[8])
{
    MMI32 size;
    const unsigned char *row = event_row((const SceneEventGroup *)p, i, &size);
    if (row)
        memcpy(out, row, MM_SCENE_EVENT_BYTES);
    else
        memset(out, 0, MM_SCENE_EVENT_BYTES);
}
static MMI32 event_size(void *p, MMU32 i)
{
    MMI32 size;
    event_row((const SceneEventGroup *)p, i, &size);
    return size;
}
static const void *event_data(void *p, MMU32 i)
{
    MMI32 size;
    const unsigned char *row = event_row((const SceneEventGroup *)p, i, &size);
    return row ? row + MM_SCENE_EVENT_BYTES : NULL;
}

static int scan_camera_events(const MMMovieSession *movie, const MMChunkAccess *access, MMU32 scene,
                              int start, MMI32 frame, MMMovieCamera *camera)
{
    const MMSceneGroupServices groups = {event_width, event_count, event_fixed, event_size, event_data};
    MMChunkEntry entry;
    MMU32 position, i;
    ChunkPayload payload;
    SceneEventGroup group;
    unsigned char *bytes;
    const unsigned char *row;
    MMI32 size, kind;
    int status, big;
    if (child_entry(access, scene, start ? 1 : 0, start ? SCENE_START_TYPE : SCENE_FRAME_TYPE, &entry,
                    &position) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = open_payload(&movie->stream, movie->index, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_collection_header_read(payload.stream, MM_COLLECTION_GROUP, 0, &group.header);
    if (status != MM_STORAGE_OK || (size_t)payload.stream->length != payload.stream->length)
    {
        free(payload.decoded);
        return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_BAD_FILE;
    }
    bytes = payload.decoded;
    if (!bytes)
    {
        bytes = (unsigned char *)malloc((size_t)payload.stream->length);
        if (!bytes)
            return MM_MOVIE_MEMORY;
        if (!mm_read_exact(payload.stream, 0, bytes, (size_t)payload.stream->length))
        {
            free(bytes);
            return MM_MOVIE_IO;
        }
    }
    group.records = bytes + group.header.header_bytes;
    group.locations = group.records + group.header.first_bytes;
    big = group.header.big_endian;
    if (!mm_scene_valid_events(&group, big, start, &groups))
    {
        free(bytes);
        return MM_MOVIE_BAD_FILE;
    }
    for (i = 0; i < group.header.count; ++i)
    {
        row = event_row(&group, i, &size);
        kind = mm_signed_word(word(row + 4, big));
        if (kind == MM_SCENE_CHANGE_CAMERA && (start || mm_signed_word(word(row, big)) <= frame))
            camera->slot = word(row + 8, big);
        else if (kind == MM_SCENE_SET_BACKGROUND)
        {
            camera->background.source = mm_signed_word(word(row + 8, big));
            camera->background.type = word(row + 16, big);
            camera->background.number = word(row + 20, big);
        }
    }
    free(bytes);
    return MM_MOVIE_OK;
}

static int load_camera(const MMReadStream *stream, MMChunkIndex *index, MMMovieCamera *camera)
{
    MMChunkAccess access = mm_chunk_index_access(index);
    MMChunkEntry entry;
    MMU32 parent, position, slot;
    ChunkPayload payload;
    int status = mm_chunk_find(&access, BACKGROUND_TYPE, camera->background.number, &parent);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
    /* The background runtime admits contiguous CAM children from slot zero. */
    for (slot = 0; slot <= camera->slot; ++slot)
    {
        status = child_entry(&access, parent, slot, CAMERA_TYPE, &entry, &position);
        if (status != MM_CHUNK_OK)
            return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
    }
    status = open_payload(stream, index, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_background_camera_decode(payload.stream, &camera->data) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
    camera->number = entry.number;
    free(payload.decoded);
    return status;
}

int mm_movie_session_camera(const MMMovieSession *movie, MMU32 scene_slot, MMI32 frame,
                            MMMovieSourceRead source, void *context, MMMovieCamera *out)
{
    MMChunkAccess access;
    MMMovieSessionScene scene;
    MMMovieCamera camera;
    MMU32 position;
    MMReadStream stream;
    MMChunkIndex *index;
    int status;
    if (!movie || !out)
        return MM_MOVIE_BAD_FILE;
    access = mm_chunk_index_access(movie->index);
    status = load_scene(movie, &access, scene_slot, &position, &scene);
    if (status != MM_MOVIE_OK)
        return status;
    if (frame < scene.first_frame || frame > scene.last_frame)
        return MM_MOVIE_FRAME_OUT_OF_RANGE;
    memset(&camera, 0, sizeof(camera));
    status = scan_camera_events(movie, &access, position, 1, frame, &camera);
    if (status == MM_MOVIE_OK)
        status = scan_camera_events(movie, &access, position, 0, frame, &camera);
    if (status != MM_MOVIE_OK)
        return status;
    if (camera.background.type != BACKGROUND_TYPE || camera.background.source < 0)
        return MM_MOVIE_ASSET_NOT_FOUND;
    if (!camera.background.source)
        status = load_camera(&movie->stream, movie->index, &camera);
    else
    {
        if (!source)
            return MM_MOVIE_ASSET_NOT_FOUND;
        status = source(context, &camera.background, &stream);
        if (status != MM_MOVIE_OK)
            return status;
        status = open_index(&stream, &movie->policy, &index);
        if (status != MM_MOVIE_OK)
            return status;
        status = load_camera(&stream, index, &camera);
        mm_chunk_index_close(index);
    }
    if (status == MM_MOVIE_OK)
        *out = camera;
    return status;
}

#include "movie_session_internal.h"
#include "mm/background_resource.h"
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
#define STRING_TABLE_TYPE 0x47535420UL
#define LAST_AUTHORED_FRAME 2147483647L

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

static int stream_payload(const MMReadStream *stream, const MMChunkIndex *index, const MMChunkEntry *entry,
                          MMU32 exact_size, MMMoviePayload *payload)
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

typedef struct GraphPayload
{
    MMMoviePayload *out;
    MMU32 exact_size;
} GraphPayload;

static int capture_payload(void *context, const MMReadStream *stream)
{
    GraphPayload *capture = (GraphPayload *)context;
    MMMoviePayload *out = capture->out;
    size_t bytes = (size_t)stream->length;
    if (out->stream || out->decoded || bytes != stream->length || bytes >= MM_STORAGE_LIMIT ||
        (capture->exact_size && bytes != capture->exact_size))
        return MM_MOVIE_BAD_FILE;
    if (bytes)
    {
        out->decoded = (unsigned char *)malloc(bytes);
        if (!out->decoded)
            return MM_MOVIE_MEMORY;
        if (!mm_read_exact(stream, 0, out->decoded, bytes))
            return MM_MOVIE_IO;
    }
    mm_memory_stream_init(&out->memory, out->decoded, bytes);
    out->stream = &out->memory.stream;
    return MM_MOVIE_OK;
}

static int graph_payload(const MMMovieArchive *archive, MMMovieGraphBytes read, const MMChunkEntry *entry,
                          MMU32 exact_size, MMMoviePayload *out)
{
    GraphPayload capture;
    int status;
    out->decoded = NULL;
    out->stream = NULL;
    if (!read)
        return MM_MOVIE_BAD_FILE;
    capture.out = out;
    capture.exact_size = exact_size;
    status = read(archive->graph.context, entry, capture_payload, &capture);
    if (status == MM_MOVIE_OK && !out->stream)
        status = MM_MOVIE_BAD_FILE;
    if (status != MM_MOVIE_OK)
    {
        free(out->decoded);
        out->decoded = NULL;
    }
    return status;
}

int mm_session_payload(const MMMovieArchive *archive, const MMChunkEntry *entry, MMU32 exact_size,
                        MMMoviePayload *out)
{
    if (archive->index)
        return stream_payload(&archive->stream, archive->index, entry, exact_size, out);
    return graph_payload(archive, archive->graph.payload, entry, exact_size, out);
}

int mm_session_name(const MMMovieArchive *archive, MMU32 position, MMMoviePayload *out)
{
    const unsigned char *name;
    MMU32 bytes;
    MMChunkEntry entry;
    out->decoded = NULL;
    if (archive->index)
    {
        if (mm_chunk_index_name(archive->index, position, &name, &bytes) != MM_CHUNK_OK || !bytes ||
            !mm_memory_stream_init(&out->memory, name, bytes))
            return MM_MOVIE_BAD_FILE;
        out->stream = &out->memory.stream;
        return MM_MOVIE_OK;
    }
    if (mm_chunk_at(&archive->access, position, &entry) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    return graph_payload(archive, archive->graph.name, &entry, 0, out);
}

int mm_session_archive(const MMMovieSession *movie, const MMMovieSource *source, const MMMovieAsset *asset,
                       MMMovieArchive *out)
{
    int status;
    memset(out, 0, sizeof(*out));
    if (!asset || !asset->source)
    {
        out->graph = movie->graph;
        out->index = movie->index;
        if (out->index)
            out->stream = movie->stream;
    }
    else if (asset->source < 0 || !source)
        return MM_MOVIE_ASSET_NOT_FOUND;
    else if (source->graph)
    {
        status = source->graph(source->context, asset, &out->graph);
        if (status != MM_MOVIE_OK)
            return status;
    }
    else if (source->stream)
    {
        if (!movie->index)
            return MM_MOVIE_BAD_FILE;
        status = source->stream(source->context, asset, &out->stream);
        if (status == MM_MOVIE_OK)
            status = mm_session_index(&out->stream, &movie->policy, &out->owned_index);
        if (status != MM_MOVIE_OK)
            return status;
        out->index = out->owned_index;
    }
    else
        return MM_MOVIE_ASSET_NOT_FOUND;
    if (out->index)
        out->access = mm_chunk_index_access(out->index);
    else if (out->graph.access && out->graph.payload)
        out->graph.access(out->graph.context, &out->access);
    else
        return MM_MOVIE_BAD_FILE;
    return MM_MOVIE_OK;
}

void mm_session_archive_close(MMMovieArchive *archive)
{
    mm_chunk_index_close(archive->owned_index);
}

static int root_entry(const MMChunkAccess *access, MMU32 number, MMChunkEntry *entry, MMU32 *position)
{
    int status;
    if (number == 0xffffffffUL)
        status = mm_chunk_type_at(access, MOVIE_TYPE, 0, entry, position);
    else
    {
        status = mm_chunk_find(access, MOVIE_TYPE, number, position);
        if (status == MM_CHUNK_OK)
            status = mm_chunk_at(access, *position, entry);
    }
    return status == MM_CHUNK_OK ? MM_MOVIE_OK
         : status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ROOT_NOT_FOUND : MM_MOVIE_BAD_FILE;
}

static int read_root(const MMMovieChunkView *view, const MMChunkEntry *entry, MMU32 position,
                     MMMovieHeader *out)
{
    unsigned char bytes[MM_MOVIE_PREFIX_BYTES];
    MMMemoryStream stream;
    MMMovieHeader header;
    int status = view->read(view->context, position, entry, bytes, sizeof(bytes));
    if (status != MM_MOVIE_OK)
        return status;
    mm_memory_stream_init(&stream, bytes, sizeof(bytes));
    status = mm_movie_read_header(&stream.stream, &header);
    if (status == MM_MOVIE_OK && out)
        *out = header;
    return status;
}

int mm_movie_view_root(const MMMovieChunkView *view, MMU32 *number, MMMovieHeader *out)
{
    MMChunkEntry entry;
    MMU32 position;
    int status;
    if (!view || !view->read)
        return MM_MOVIE_BAD_FILE;
    status = root_entry(&view->chunks, number ? *number : 0xffffffffUL, &entry, &position);
    if (status != MM_MOVIE_OK)
        return status;
    if (number)
        *number = entry.number;
    return read_root(view, &entry, position, out);
}

static void stream_access(void *context, MMChunkAccess *out)
{
    const MMMovieSession *movie = (const MMMovieSession *)context;
    *out = mm_chunk_index_access(movie->index);
}

static int read_record(void *context, void *request, MMU32 position, const MMChunkEntry *entry,
                       void *bytes, MMU32 size)
{
    const MMMovieSession *movie = (const MMMovieSession *)context;
    MMMoviePayload payload;
    int status = stream_payload(&movie->stream, movie->index, entry, size, &payload);
    (void)position;
    (void)request;
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_read_exact(payload.stream, 0, bytes, size) ? MM_MOVIE_OK : MM_MOVIE_IO;
    free(payload.decoded);
    return status;
}

typedef struct GraphRead
{
    const MMMovieSessionGraph *graph;
    void *request;
} GraphRead;

static int graph_read(void *context, MMU32 position, const MMChunkEntry *entry, void *bytes, MMU32 size)
{
    const GraphRead *read = (const GraphRead *)context;
    return read->graph->read(read->graph->context, read->request, position, entry, bytes, size);
}

static void init_view(MMMovieChunkView *view, GraphRead *read, const MMMovieSession *movie, void *request)
{
    read->graph = &movie->graph;
    read->request = request;
    movie->graph.access(movie->graph.context, &view->chunks);
    view->context = read;
    view->read = graph_read;
}

static int load_root(MMMovieSession *movie, void *request, MMU32 *number)
{
    GraphRead read;
    MMMovieChunkView view;
    int status;
    init_view(&view, &read, movie, request);
    movie->root.number = number ? *number : 0xffffffffUL;
    status = mm_movie_view_root(&view, &movie->root.number, &movie->root.header);
    if (number)
        *number = movie->root.number;
    return status;
}

int mm_session_index(const MMReadStream *stream, const MMChunkPolicy *policy, MMChunkIndex **index)
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
    status = mm_session_index(stream, policy, &index);
    if (status != MM_MOVIE_OK)
        return status;
    movie = (MMMovieSession *)calloc(1, sizeof(*movie));
    if (!movie)
    {
        mm_chunk_index_close(index);
        return MM_MOVIE_MEMORY;
    }
    movie->stream = *stream;
    movie->index = index;
    movie->policy = *policy;
    movie->graph.context = movie;
    movie->graph.access = stream_access;
    movie->graph.read = read_record;
    status = load_root(movie, NULL, NULL);
    if (status != MM_MOVIE_OK)
    {
        mm_movie_session_close(movie);
        return status == MM_MOVIE_ROOT_NOT_FOUND ? MM_MOVIE_BAD_FILE : status;
    }
    *out = movie;
    return MM_MOVIE_OK;
}

int mm_movie_session_open_graph(const MMMovieSessionGraph *graph, void *request, MMU32 *number,
                                MMMovieSession **out)
{
    MMMovieSession *movie;
    int status;
    if (!graph || !graph->access || !graph->read || !out)
        return MM_MOVIE_BAD_FILE;
    movie = (MMMovieSession *)calloc(1, sizeof(*movie));
    if (!movie)
        return MM_MOVIE_MEMORY;
    movie->graph = *graph;
    status = load_root(movie, request, number);
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
int mm_session_child(const MMChunkAccess *access, MMU32 parent, MMU32 slot, MMU32 type,
                       MMChunkEntry *entry, MMU32 *position)
{
    MMChunkChild child;
    int status = mm_chunk_child_exact(access, parent, slot, type, &child);
    if (status == MM_CHUNK_NOT_FOUND)
        return MM_CHUNK_NOT_FOUND;
    if (status != MM_CHUNK_OK || mm_chunk_find(access, type, child.number, position) != MM_CHUNK_OK)
        return MM_CHUNK_BAD_FILE;
    return mm_chunk_at(access, *position, entry);
}

int mm_movie_view_source_table(const MMChunkAccess *access, MMU32 root, MMChunkEntry *entry, MMU32 *position)
{
    MMChunkEntry parent_entry;
    MMU32 parent, child;
    int status;
    if (!access || !entry || !position)
        return MM_MOVIE_BAD_FILE;
    status = root_entry(access, root, &parent_entry, &parent);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_session_child(access, parent, 1, STRING_TABLE_TYPE, entry, &child);
    if (status == MM_CHUNK_OK)
        *position = child;
    return status == MM_CHUNK_OK ? MM_MOVIE_OK
         : status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
}

static int scene_at_slot(const MMChunkAccess *access, MMU32 root_position, MMU32 slot, MMChunkEntry *entry,
                          MMU32 *position)
{
    int status = mm_session_child(access, root_position, slot, SCENE_TYPE, entry, position);
    return status == MM_CHUNK_OK ? MM_MOVIE_OK
         : status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_SCENE_NOT_FOUND : MM_MOVIE_BAD_FILE;
}

int mm_movie_view_scene_number(const MMChunkAccess *access, MMU32 root, MMU32 slot, MMU32 *number)
{
    MMChunkEntry entry;
    MMU32 parent, position;
    int status;
    if (!access || !number)
        return MM_MOVIE_BAD_FILE;
    status = root_entry(access, root, &entry, &parent);
    if (status == MM_MOVIE_OK)
        status = scene_at_slot(access, parent, slot, &entry, &position);
    if (status == MM_MOVIE_OK)
        *number = entry.number;
    return status;
}

int mm_movie_session_scene_number(const MMMovieSession *movie, MMU32 slot, MMU32 *number)
{
    MMChunkAccess access;
    if (!movie)
        return MM_MOVIE_BAD_FILE;
    movie->graph.access(movie->graph.context, &access);
    return mm_movie_view_scene_number(&access, movie->root.number, slot, number);
}

int mm_movie_session_source_table(const MMMovieSession *movie, MMChunkEntry *entry, MMU32 *position)
{
    MMChunkAccess access;
    if (!movie)
        return MM_MOVIE_BAD_FILE;
    movie->graph.access(movie->graph.context, &access);
    return mm_movie_view_source_table(&access, movie->root.number, entry, position);
}

static int read_scene(const MMMovieChunkView *view, const MMChunkEntry *entry, MMU32 position,
                      MMMovieSessionScene *out)
{
    unsigned char bytes[MM_SCENE_HEADER_BYTES];
    MMMemoryStream header;
    MMSceneState scene;
    int status = view->read(view->context, position, entry, bytes, sizeof(bytes));
    if (status != MM_MOVIE_OK)
        return status;
    mm_memory_stream_init(&header, bytes, sizeof(bytes));
    mm_scene_init(&scene, 1);
    if (!mm_scene_decode_header(&header.stream, &scene))
        return MM_MOVIE_BAD_FILE;
    out->number = entry->number;
    out->first_frame = scene.firstFrame;
    out->last_frame = scene.lastFrame;
    out->transition = scene.transition;
    return MM_MOVIE_OK;
}

int mm_movie_view_next_scene(const MMMovieChunkView *view, MMU32 root, MMU32 *cursor,
                             MMMovieSessionScene *out, MMU32 *slot)
{
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 parent, position, i;
    int status;
    if (!view || !view->read || !cursor || !out)
        return MM_MOVIE_BAD_FILE;
    status = root_entry(&view->chunks, root, &entry, &parent);
    if (status != MM_MOVIE_OK)
        return status;
    for (i = *cursor; i < entry.child_count; ++i)
    {
        if (mm_chunk_child_at(&view->chunks, parent, i, &child) != MM_CHUNK_OK)
            return MM_MOVIE_BAD_FILE;
        if (child.type != SCENE_TYPE)
            continue;
        if (mm_chunk_find(&view->chunks, child.type, child.number, &position) != MM_CHUNK_OK ||
            mm_chunk_at(&view->chunks, position, &entry) != MM_CHUNK_OK)
            return MM_MOVIE_BAD_FILE;
        status = read_scene(view, &entry, position, out);
        if (status == MM_MOVIE_OK)
        {
            *cursor = i + 1;
            if (slot)
                *slot = child.slot;
        }
        return status;
    }
    return MM_MOVIE_SCENE_NOT_FOUND;
}

int mm_session_scene(const MMMovieSession *movie, const MMChunkAccess *access, MMU32 slot,
                      MMU32 *position, MMMovieSessionScene *out)
{
    MMMovieChunkView view;
    GraphRead read;
    MMChunkEntry entry;
    MMU32 root;
    int status = root_entry(access, movie->root.number, &entry, &root);
    if (status == MM_MOVIE_OK)
        status = scene_at_slot(access, root, slot, &entry, position);
    if (status != MM_MOVIE_OK)
        return status;
    read.graph = &movie->graph;
    read.request = NULL;
    view.chunks = *access;
    view.context = &read;
    view.read = graph_read;
    return read_scene(&view, &entry, *position, out);
}

int mm_movie_session_scene(const MMMovieSession *movie, MMU32 slot, MMMovieSessionScene *out)
{
    MMChunkAccess access;
    MMU32 position;
    if (!movie || !out)
        return MM_MOVIE_BAD_FILE;
    movie->graph.access(movie->graph.context, &access);
    return mm_session_scene(movie, &access, slot, &position, out);
}

int mm_movie_session_next_scene(const MMMovieSession *movie, void *request, MMU32 *cursor,
                                MMMovieSessionScene *scene, MMU32 *slot)
{
    MMMovieChunkView view;
    GraphRead read;
    if (!movie)
        return MM_MOVIE_BAD_FILE;
    init_view(&view, &read, movie, request);
    return mm_movie_view_next_scene(&view, movie->root.number, cursor, scene, slot);
}

MMU32 mm_session_word(const unsigned char *p, int big)
{
    return big ? ((MMU32)p[0] << 24) | ((MMU32)p[1] << 16) | ((MMU32)p[2] << 8) | p[3]
               : p[0] | ((MMU32)p[1] << 8) | ((MMU32)p[2] << 16) | ((MMU32)p[3] << 24);
}

const unsigned char *mm_session_group_row(const MMMovieGroup *group, MMU32 row, MMI32 *size)
{
    const unsigned char *location;
    MMU32 offset, bytes;
    *size = -1;
    if (row >= group->header.count)
        return NULL;
    location = group->locations + row * 8;
    offset = mm_session_word(location, group->header.big_endian);
    bytes = mm_session_word(location + 4, group->header.big_endian);
    if (offset > group->header.first_bytes || bytes < group->header.width ||
        bytes > group->header.first_bytes - offset)
        return NULL;
    *size = (MMI32)(bytes - group->header.width);
    return group->records + offset;
}

static MMU32 event_width(void *p) { return ((const MMMovieGroup *)p)->header.width; }
static MMU32 event_count(void *p) { return ((const MMMovieGroup *)p)->header.count; }
static void event_fixed(void *p, MMU32 i, unsigned char out[8])
{
    MMI32 size;
    const unsigned char *row = mm_session_group_row((const MMMovieGroup *)p, i, &size);
    if (row)
        memcpy(out, row, MM_SCENE_EVENT_BYTES);
    else
        memset(out, 0, MM_SCENE_EVENT_BYTES);
}
static MMI32 event_size(void *p, MMU32 i)
{
    MMI32 size;
    mm_session_group_row((const MMMovieGroup *)p, i, &size);
    return size;
}
static const void *event_data(void *p, MMU32 i)
{
    MMI32 size;
    const unsigned char *row = mm_session_group_row((const MMMovieGroup *)p, i, &size);
    return row ? row + MM_SCENE_EVENT_BYTES : NULL;
}

int mm_session_group(const MMMovieArchive *archive, const MMChunkEntry *entry, MMMovieGroup *group)
{
    MMMoviePayload payload;
    unsigned char *bytes;
    int status = mm_session_payload(archive, entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_collection_header_read(payload.stream, MM_COLLECTION_GROUP, 0, &group->header);
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
    group->bytes = bytes;
    group->records = bytes + group->header.header_bytes;
    group->locations = group->records + group->header.first_bytes;
    return MM_MOVIE_OK;
}

int mm_session_scene_group(const MMMovieArchive *archive, MMU32 scene, int start, MMMovieGroup *group)
{
    const MMSceneGroupServices groups = {event_width, event_count, event_fixed, event_size, event_data};
    MMChunkEntry entry;
    MMU32 position;
    int status;
    if (mm_session_child(&archive->access, scene, start ? 1 : 0, start ? SCENE_START_TYPE : SCENE_FRAME_TYPE, &entry,
                         &position) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = mm_session_group(archive, &entry, group);
    if (status != MM_MOVIE_OK)
        return status;
    if (!mm_scene_valid_events(group, group->header.big_endian, start, &groups))
    {
        free(group->bytes);
        return MM_MOVIE_BAD_FILE;
    }
    return MM_MOVIE_OK;
}

MMI32 mm_movie_view_last_event(const MMMovieEventView *view, int kind, MMI32 frame, int include_frame,
                              MMI32 *event_frame)
{
    MMI32 i, row_frame;
    int row_kind;
    for (i = view->count; i > 0;)
    {
        --i;
        view->read(view->context, view->group, i, &row_frame, &row_kind);
        if (row_kind == kind && (row_frame < frame || (include_frame && row_frame == frame)))
        {
            if (event_frame)
                *event_frame = row_frame;
            return i;
        }
    }
    return -1;
}

int mm_movie_seek_frame(MMI32 frame, const MMMovieFrameServices *services)
{
    MMU32 i;
    void *context = services->context;
    int status = services->begin(context, frame);
    if (status)
        return status;
    for (i = 0; i < services->actor_count(context); ++i)
    {
        status = services->seek_actor(context, i, frame);
        if (status)
            return status;
    }
    return services->finish ? services->finish(context, frame) : 0;
}

static void read_scene_event(void *context, void *group, MMI32 index, MMI32 *frame, int *kind)
{
    const MMMovieGroup *events = (const MMMovieGroup *)group;
    MMI32 size;
    const unsigned char *row = mm_session_group_row(events, (MMU32)index, &size);
    (void)context;
    *frame = mm_signed_word(mm_session_word(row, events->header.big_endian));
    *kind = (int)mm_signed_word(mm_session_word(row + 4, events->header.big_endian));
}

static void scan_camera_events(const MMMovieGroup *group, int start, MMI32 frame, MMMovieCamera *camera)
{
    MMMovieEventView view;
    const unsigned char *row;
    MMI32 size, selected;
    int big = group->header.big_endian;
    view.context = NULL;
    view.group = (void *)group;
    view.count = (MMI32)group->header.count;
    view.read = read_scene_event;
    if (start)
        frame = LAST_AUTHORED_FRAME; /* A start event is independent of its stored frame. */
    selected = mm_movie_view_last_event(&view, MM_SCENE_CHANGE_CAMERA, frame, 1, NULL);
    if (selected >= 0)
    {
        row = mm_session_group_row(group, (MMU32)selected, &size);
        camera->slot = mm_session_word(row + 8, big);
    }
    if (start)
    {
        selected = mm_movie_view_last_event(&view, MM_SCENE_SET_BACKGROUND, frame, 1, NULL);
        if (selected >= 0)
        {
            row = mm_session_group_row(group, (MMU32)selected, &size);
            camera->background.source = mm_signed_word(mm_session_word(row + 8, big));
            camera->background.type = mm_session_word(row + 16, big);
            camera->background.number = mm_session_word(row + 20, big);
        }
    }
}

static int image_link(void *p, MMU32 camera, int depth, MMU32 *number)
{
    const MMMovieArchive *archive = (const MMMovieArchive *)p;
    return mm_background_camera_image_link(&archive->access, camera, depth, number) == MM_CHUNK_OK;
}

static int load_camera(const MMMovieArchive *archive, MMMovieCamera *camera)
{
    const MMChunkAccess *access = &archive->access;
    MMChunkEntry entry;
    MMU32 parent, position, slot;
    MMMoviePayload payload;
    MMBackgroundCameraBinding binding;
    int status = mm_chunk_find(access, MM_BACKGROUND, camera->background.number, &parent);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
    /* The background runtime admits contiguous CAM children from slot zero. */
    for (slot = 0; slot <= camera->slot; ++slot)
    {
        status = mm_session_child(access, parent, slot, MM_BACKGROUND_CAMERA, &entry, &position);
        if (status != MM_CHUNK_OK)
            return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
    }
    status = mm_session_payload(archive, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_background_camera_binding_read(payload.stream, entry.number, &binding) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
    free(payload.decoded);
    if (status == MM_MOVIE_OK)
    {
        mm_background_camera_binding_image(&binding, 0, image_link, (void *)archive);
        mm_background_camera_binding_image(&binding, 1, image_link, (void *)archive);
        camera->number = binding.number;
        camera->data = binding.data;
        camera->images = binding.images;
    }
    return status;
}

int mm_session_camera_at(const MMMovieSession *movie, const MMMovieArchive *archive, MMU32 scene,
                         const MMMovieGroup *starts, MMI32 frame, const MMMovieSource *source,
                         MMMovieCamera *out)
{
    MMMovieGroup frames;
    MMMovieCamera camera;
    MMMovieArchive content;
    int status;
    memset(&camera, 0, sizeof(camera));
    scan_camera_events(starts, 1, frame, &camera);
    status = mm_session_scene_group(archive, scene, 0, &frames);
    if (status != MM_MOVIE_OK)
        return status;
    scan_camera_events(&frames, 0, frame, &camera);
    free(frames.bytes);
    if (camera.background.type != MM_BACKGROUND || camera.background.source < 0)
        return MM_MOVIE_ASSET_NOT_FOUND;
    if (!camera.background.source)
        status = load_camera(archive, &camera);
    else
    {
        status = mm_session_archive(movie, source, &camera.background, &content);
        if (status != MM_MOVIE_OK)
            return status;
        status = load_camera(&content, &camera);
        mm_session_archive_close(&content);
    }
    if (status == MM_MOVIE_OK)
        *out = camera;
    return status;
}

int mm_movie_session_camera(const MMMovieSession *movie, MMU32 scene_slot, MMI32 frame,
                            MMMovieSourceRead source, void *context, MMMovieCamera *out)
{
    MMMovieArchive archive;
    MMMovieSource input;
    MMMovieSessionScene scene;
    MMMovieGroup starts;
    MMU32 position;
    int status;
    if (!movie || !out)
        return MM_MOVIE_BAD_FILE;
    status = mm_session_archive(movie, NULL, NULL, &archive);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_session_scene(movie, &archive.access, scene_slot, &position, &scene);
    if (status != MM_MOVIE_OK)
        return status;
    if (frame < scene.first_frame || frame > scene.last_frame)
        return MM_MOVIE_FRAME_OUT_OF_RANGE;
    status = mm_session_scene_group(&archive, position, 1, &starts);
    if (status != MM_MOVIE_OK)
        return status;
    input.stream = source;
    input.graph = NULL;
    input.context = context;
    status = mm_session_camera_at(movie, &archive, position, &starts, frame, &input, out);
    free(starts.bytes);
    return status;
}

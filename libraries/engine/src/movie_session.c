#include "movie_session_internal.h"
#include "mm/background_resource.h"
#include "mm/scene_data.h"
#include "mm/text_resource.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

#define MOVIE_TYPE 0x4d564945UL
#define STRING_TABLE_TYPE 0x47535420UL
#define ACTOR_TYPE 0x41435452UL
#define ACTOR_ROUTE_TYPE 0x50415448UL
#define ACTOR_EVENTS_TYPE 0x47474145UL
#define LAST_AUTHORED_FRAME 2147483647L

int mm_movie_block_payload(const MMChunkPayloadServices *services, void *archive,
                           const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    int result;
    if (!entry || mm_chunk_read_payload(services, archive, entry->type, entry->number,
                                         receive, out, &result) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    return result;
}

typedef struct MovieBlockRead
{
    void *bytes;
    MMU32 size;
} MovieBlockRead;

int mm_movie_read_fixed(const MMReadStream *stream, void *bytes, MMU32 size)
{
    if (!stream || stream->length != size)
        return MM_MOVIE_BAD_FILE;
    return mm_read_exact(stream, 0, bytes, size) ? MM_MOVIE_OK : MM_MOVIE_IO;
}

static int read_exact_payload(void *context, const MMReadStream *stream)
{
    MovieBlockRead *read = (MovieBlockRead *)context;
    return mm_movie_read_fixed(stream, read->bytes, read->size);
}

int mm_movie_block_read(const MMChunkPayloadServices *services, void *archive,
                        const MMChunkEntry *entry, void *bytes, MMU32 size)
{
    MovieBlockRead read;
    read.bytes = bytes;
    read.size = size;
    return mm_movie_block_payload(services, archive, entry, read_exact_payload, &read);
}

static int movie_chunk_status(int status)
{
    switch (status)
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

static int stream_payload(const MMReadStream *stream, const MMChunkIndex *index, const MMChunkEntry *entry,
                          MMU32 exact_size, MMMoviePayload *payload)
{
    return movie_chunk_status(mm_chunk_payload_open(stream, mm_chunk_index_header(index), entry,
                                                     exact_size, MM_STORAGE_LIMIT, payload));
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

static int read_movie_payload(void *context, const MMChunkEntry *entry, MMMovieReceiveBytes consume, void *out)
{
    const MMMovieSession *movie = (const MMMovieSession *)context;
    MMMoviePayload payload;
    int status = stream_payload(&movie->stream, movie->index, entry, 0, &payload);
    if (status == MM_MOVIE_OK)
    {
        status = consume(out, payload.stream);
        free(payload.decoded);
    }
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
    return movie_chunk_status(mm_chunk_index_open(stream, policy, index));
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
    movie->graph.payload = read_movie_payload;
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
        free(movie->render_bodies.data);
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

typedef struct ActorRead
{
    void *value;
    const MMActorRouteReader *route;
    const MMActorEventServices *events;
    int called;
} ActorRead;

static int actor_receive(void *context, const MMReadStream *stream)
{
    ActorRead *read = (ActorRead *)context;
    if (read->called)
        return MM_MOVIE_BAD_FILE;
    read->called = 1;
    if (read->route)
        return mm_actor_read_route_stream(stream, read->route, &read->value);
    if (read->events)
        return mm_actor_read_events_stream(stream, read->events, &read->value);
    return mm_actor_read_header(stream, read->value) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

static int actor_read(const MMMovieSessionGraph *graph, MMU32 number, MMU32 type, ActorRead *read)
{
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMU32 position, child;
    int status;
    if (!graph || !graph->access || !graph->payload)
        return MM_MOVIE_BAD_FILE;
    graph->access(graph->context, &chunks);
    if (mm_chunk_find(&chunks, ACTOR_TYPE, number, &position) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = type == ACTOR_TYPE ? mm_chunk_at(&chunks, position, &entry)
                                : mm_session_child(&chunks, position, 0, type, &entry, &child);
    if (status != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = graph->payload(graph->context, &entry, actor_receive, read);
    return status == MM_MOVIE_OK && !read->called ? MM_MOVIE_BAD_FILE : status;
}

int mm_movie_actor_header(const MMMovieSessionGraph *graph, MMU32 number, void *header)
{
    ActorRead read = {NULL, NULL, NULL, 0};
    read.value = header;
    return actor_read(graph, number, ACTOR_TYPE, &read);
}

int mm_movie_actor_route(const MMMovieSessionGraph *graph, MMU32 number, const MMActorRouteReader *reader, void **out)
{
    ActorRead read = {NULL, NULL, NULL, 0};
    int status;
    read.route = reader;
    status = actor_read(graph, number, ACTOR_ROUTE_TYPE, &read);
    if (status == MM_MOVIE_OK)
        *out = read.value;
    else if (read.value)
        reader->release(read.value);
    return status;
}

int mm_movie_actor_events(const MMMovieSessionGraph *graph, MMU32 number, const MMActorEventServices *services,
                          void **out)
{
    ActorRead read = {NULL, NULL, NULL, 0};
    int status;
    read.events = services;
    status = actor_read(graph, number, ACTOR_EVENTS_TYPE, &read);
    if (status == MM_MOVIE_OK)
        *out = read.value;
    else if (read.value)
        services->release(read.value);
    return status;
}

typedef struct ActorFontTag
{
    MMTextDescriptor descriptor;
    int loaded, valid_order;
} ActorFontTag;

static int receive_actor_font(void *context, const MMReadStream *stream)
{
    const unsigned short one = 1;
    ActorFontTag *font = (ActorFontTag *)context;
    unsigned char bytes[24];
    if (font->loaded || stream->length < sizeof(bytes))
        return MM_MOVIE_BAD_FILE;
    if (!mm_read_exact(stream, 0, bytes, sizeof(bytes)))
        return MM_MOVIE_IO;
    font->valid_order = mm_text_decode_descriptor(bytes, *(const unsigned char *)&one == 0, &font->descriptor);
    font->loaded = 1;
    return MM_MOVIE_OK;
}

static int actor_font(const MMMovieSessionGraph *graph, const MMChunkAccess *chunks,
                      const MMTag *tmpl, ActorFontTag *font)
{
    MMChunkEntry entry;
    MMU32 parent, position;
    int status = mm_chunk_find(chunks, tmpl->type, tmpl->number, &parent);
    if (status == MM_CHUNK_OK)
        status = mm_session_child(chunks, parent, 0, MM_TDT, &entry, &position);
    if (status == MM_CHUNK_NOT_FOUND)
        return MM_MOVIE_OK;
    if (status != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = graph->payload(graph->context, &entry, receive_actor_font, font);
    return status == MM_MOVIE_OK && !font->loaded ? MM_MOVIE_BAD_FILE : status;
}

static void *actor_tag_sink(void *context, MMU32 width)
{
    (void)width;
    return context;
}

static void release_actor_tag_sink(void *group) { (void)group; }

static int append_actor_event_tag(void *group, MMU32 row, MMU32 bytes, const void *data, const void *fixed)
{
    MMArray *tags = (MMArray *)group;
    const void *tag;
    MMI32 kind;
    (void)row;
    (void)bytes;
    memcpy(&kind, fixed, sizeof(kind));
    tag = mm_actor_payload_tag(kind, data);
    return !tag || mm_array_add(tags, tag, NULL) == MM_STORAGE_OK;
}

int mm_movie_actor_tags(const MMMovieSessionGraph *graph, MMU32 number, void *context,
                        int (*visit)(void *, const MMTag *), void (*diagnostic)(void *, int))
{
    MMActorHeader header;
    MMChunkAccess chunks;
    ActorFontTag font = {0};
    MMTag tmpl, tag;
    MMCollectionStorage storage;
    MMArray tags = {0};
    MMActorEventServices events = {0};
    MMU32 i;
    void *sink;
    int status = mm_movie_actor_header(graph, number, &header);
    if (status != MM_MOVIE_OK)
        return status;
    tmpl.sid = header.source_id;
    tmpl.cache = NULL;
    tmpl.type = header.template_type;
    tmpl.number = header.template_number;
    if (!tmpl.sid)
    {
        graph->access(graph->context, &chunks);
        if (mm_actor_template_link(&chunks, number, tmpl.sid, 0, &tmpl.number) != MM_CHUNK_OK && diagnostic)
            diagnostic(context, MM_ACTOR_TAG_TEMPLATE_LINK);
        status = actor_font(graph, &chunks, &tmpl, &font);
        if (status != MM_MOVIE_OK)
            return status;
        if (font.loaded && !font.valid_order && diagnostic)
            diagnostic(context, MM_ACTOR_TAG_TEXT_ORDER);
    }

    if (mm_storage_init(&storage, NULL) != MM_STORAGE_OK)
        return MM_MOVIE_MEMORY;
    tags.storage = &storage;
    tags.width = sizeof(MMTag);
    tags.free_count = -1;
    events.context = &tags;
    events.new_group = actor_tag_sink;
    events.insert = append_actor_event_tag;
    events.release = release_actor_tag_sink;
    status = mm_movie_actor_events(graph, number, &events, &sink);
    if (status == MM_MOVIE_OK && (!visit(context, &tmpl) || (font.loaded && !visit(context, &font.descriptor.font))))
        status = MM_MOVIE_IO;
    for (i = 0; status == MM_MOVIE_OK && i < tags.count; ++i)
    {
        memcpy(&tag, storage.buffers[0].data + (size_t)i * sizeof(tag), sizeof(tag));
        if (!visit(context, &tag))
            status = MM_MOVIE_IO;
    }
    mm_storage_dispose(&storage);
    return status;
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
    int status = mm_session_child(access, root_position, slot, MM_SCENE_CHUNK, entry, position);
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
        if (child.type != MM_SCENE_CHUNK)
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

unsigned char *mm_session_group_row(const MMMovieGroup *group, MMU32 row, MMI32 *size)
{
    MMU32 offset;
    *size = -1;
    if (!mm_collection_group_row(&group->header, group->records, row, &offset, size))
        return NULL;
    return group->records + offset;
}

void mm_session_scene_event(const MMMovieGroup *group, MMU32 index, MMMovieSceneEvent *out)
{
    unsigned char *row = mm_session_group_row(group, index, &out->bytes);
    out->big_endian = group->header.big_endian;
    out->frame = mm_signed_word(mm_session_word(row, out->big_endian));
    out->kind = mm_signed_word(mm_session_word(row + 4, out->big_endian));
    out->data = row + MM_SCENE_EVENT_BYTES;
}

MMU32 mm_movie_scene_event_word(const MMMovieSceneEvent *event, MMU32 offset)
{
    return mm_session_word(event->data + offset, event->big_endian);
}

void mm_movie_scene_event_tag(const MMMovieSceneEvent *event, MMTag *tag)
{
    tag->sid = mm_signed_word(mm_movie_scene_event_word(event, 0));
    tag->cache = NULL;
    tag->type = mm_movie_scene_event_word(event, 8);
    tag->number = mm_movie_scene_event_word(event, 12);
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
    return MM_MOVIE_OK;
}

static int read_template_table(void *context, int kind, MMPartIndexList **out)
{
    MMMovieTemplate *tmpl = (MMMovieTemplate *)context;
    MMPartIndexList *list = kind == MM_TEMPLATE_PARENTS ? &tmpl->parents : &tmpl->sets;
    MMMoviePayload payload;
    MMChunkEntry entry;
    MMU32 position;
    int status = mm_session_child(&tmpl->archive.access, tmpl->position, 0, mm_template_table_type(kind),
                                  &entry, &position);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
    status = mm_session_payload(&tmpl->archive, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    mm_part_indices_init(list, NULL);
    status = mm_part_indices_read(list, payload.stream);
    free(payload.decoded);
    if (status == MM_MOVIE_OK)
        *out = list;
    else
        mm_part_indices_dispose(list);
    return status;
}

static int read_template_costumes(void *context, MMI32 expected, MMTemplateCostumeTable **out)
{
    MMMovieTemplate *tmpl = (MMMovieTemplate *)context;
    MMMoviePayload payload;
    MMChunkEntry entry;
    MMU32 position;
    int status;
    status = mm_session_child(&tmpl->archive.access, tmpl->position, 0, MM_GGCM, &entry, &position);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_TEMPLATE_COSTUMES_ABSENT : MM_MOVIE_BAD_FILE;
    status = mm_session_payload(&tmpl->archive, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    mm_template_costumes_init(&tmpl->costumes, NULL);
    status = mm_template_costumes_read(&tmpl->costumes, payload.stream, expected);
    free(payload.decoded);
    if (status == MM_MOVIE_OK)
        *out = &tmpl->costumes;
    else
        mm_template_costumes_dispose(&tmpl->costumes);
    return status;
}

MMTemplateRead mm_session_template_reader(MMMovieTemplate *tmpl)
{
    MMTemplateRead reader = {NULL, read_template_table, read_template_costumes};
    reader.context = tmpl;
    return reader;
}

void mm_session_template_dispose(MMMovieTemplate *tmpl)
{
    mm_part_indices_dispose(tmpl->state.parents);
    mm_part_indices_dispose(tmpl->state.sets);
    mm_template_costumes_dispose(tmpl->state.costumes);
    mm_session_archive_close(&tmpl->archive);
}

static int tag_rows_valid(const MMMovieGroup *group)
{
    MMU32 i;
    MMI32 size;
    if (group->header.width != MM_SCENE_EVENT_BYTES)
        return 0;
    for (i = 0; i < group->header.count; ++i)
        if (!mm_session_group_row(group, i, &size))
            return 0;
    return 1;
}

static int load_scene_group(const MMMovieArchive *archive, MMU32 scene, int start, int for_tags, MMMovieGroup *group)
{
    const MMSceneGroupServices groups = {event_width, event_count, event_fixed, event_size, event_data};
    MMChunkEntry entry;
    MMU32 position;
    int status;
    status = mm_session_child(&archive->access, scene, start ? 1 : 0, start ? MM_SCENE_START_GROUP : MM_SCENE_FRAME_GROUP,
                              &entry, &position);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
    status = mm_session_group(archive, &entry, group);
    if (status != MM_MOVIE_OK)
        return status;
    if (for_tags ? !tag_rows_valid(group) : !mm_scene_valid_events(group, group->header.big_endian, start, &groups))
    {
        free(group->bytes);
        return MM_MOVIE_BAD_FILE;
    }
    return MM_MOVIE_OK;
}

int mm_session_scene_group(const MMMovieArchive *archive, MMU32 scene, int start, MMMovieGroup *group)
{
    int status = load_scene_group(archive, scene, start, 0, group);
    return status == MM_MOVIE_ASSET_NOT_FOUND ? MM_MOVIE_BAD_FILE : status;
}

static int scene_reader_header(void *context, MMU32 number, MMMovieSessionScene *out)
{
    const MMMovieSceneSource *source = (const MMMovieSceneSource *)context;
    const MMMovieSessionGraph *graph = source->movie ? &source->movie->graph : &source->graph;
    MMMovieChunkView view;
    GraphRead read;
    MMChunkEntry entry;
    MMU32 position;
    if (!graph->access || !graph->read)
        return MM_MOVIE_BAD_FILE;
    graph->access(graph->context, &view.chunks);
    if (mm_chunk_find(&view.chunks, MM_SCENE_CHUNK, number, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&view.chunks, position, &entry) != MM_CHUNK_OK)
        return MM_MOVIE_SCENE_NOT_FOUND;
    read.graph = graph;
    read.request = NULL;
    view.context = &read;
    view.read = graph_read;
    return read_scene(&view, &entry, position, out);
}

static int scene_reader_child(void *context, MMU32 scene, MMU32 slot, MMU32 type, MMU32 *number)
{
    const MMMovieSceneSource *source = (const MMMovieSceneSource *)context;
    const MMMovieSessionGraph *graph = source->movie ? &source->movie->graph : &source->graph;
    MMChunkAccess chunks;
    if (!graph->access)
        return 0;
    graph->access(graph->context, &chunks);
    return mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, scene, slot, type, number) == MM_CHUNK_OK;
}

static int scene_reader_open_group(void *context, MMU32 number, int start, int for_tags, MMMovieSceneGroup *out)
{
    const MMMovieSceneSource *source = (const MMMovieSceneSource *)context;
    MMMovieArchive archive;
    MMMovieGroup *group;
    MMU32 position;
    int status;
    if (source->movie)
    {
        status = mm_session_archive(source->movie, NULL, NULL, &archive);
        if (status != MM_MOVIE_OK)
            return status;
    }
    else
    {
        if (!source->graph.access || !source->graph.payload)
            return MM_MOVIE_BAD_FILE;
        memset(&archive, 0, sizeof(archive));
        archive.graph = source->graph;
        archive.graph.access(archive.graph.context, &archive.access);
    }
    if (mm_chunk_find(&archive.access, MM_SCENE_CHUNK, number, &position) != MM_CHUNK_OK)
        return MM_MOVIE_SCENE_NOT_FOUND;
    group = (MMMovieGroup *)malloc(sizeof(*group));
    if (!group)
        return MM_MOVIE_MEMORY;
    status = load_scene_group(&archive, position, start, for_tags, group);
    if (status != MM_MOVIE_OK)
    {
        free(group);
        return status;
    }
    out->handle = group;
    out->count = group->header.count;
    return MM_MOVIE_OK;
}

static int scene_reader_group(void *context, MMU32 number, int start, MMMovieSceneGroup *out)
{
    return scene_reader_open_group(context, number, start, 0, out);
}

static int scene_tag_group(void *context, MMU32 number, int start, MMMovieSceneGroup *out)
{
    return scene_reader_open_group(context, number, start, 1, out);
}

static void scene_reader_event(void *context, const MMMovieSceneGroup *group, MMU32 index, MMMovieSceneEvent *out)
{
    (void)context;
    mm_session_scene_event((const MMMovieGroup *)group->handle, index, out);
}

static void scene_reader_close(void *context, MMMovieSceneGroup *group)
{
    MMMovieGroup *events = (MMMovieGroup *)group->handle;
    (void)context;
    free(events->bytes);
    free(events);
    group->handle = NULL;
    group->count = 0;
}

MMMovieSceneReader mm_movie_scene_reader(MMMovieSceneSource *source)
{
    MMMovieSceneReader reader;
    reader.context = source;
    reader.header = scene_reader_header;
    reader.child = scene_reader_child;
    reader.group = scene_reader_group;
    reader.event = scene_reader_event;
    reader.close = scene_reader_close;
    return reader;
}

MMMovieSceneReader mm_movie_scene_tag_reader(MMMovieSceneSource *source)
{
    MMMovieSceneReader reader = mm_movie_scene_reader(source);
    reader.group = scene_tag_group;
    return reader;
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

static int seek_frame(MMMovieLiveFrame *live, MMI32 frame, const MMMovieFrameServices *services)
{
    MMU32 i;
    void *context = services->context;
    MMI32 previous = live && services->position ? *services->position : frame;
    int status = services->begin(context, frame);
    if (status)
    {
        if (live && services->position && *services->position != previous)
        {
            live->frame = *services->position;
            live->phase = MM_MOVIE_FRAME_PARTIAL;
        }
        return status;
    }
    if (live)
    {
        live->frame = frame;
        live->phase = MM_MOVIE_FRAME_PARTIAL;
    }
    for (i = 0; i < services->actor_count(context); ++i)
    {
        status = services->seek_actor(context, i, frame);
        if (status)
            return status;
    }
    if (live)
        live->phase = MM_MOVIE_FRAME_PREPARED;
    status = services->finish ? services->finish(context, frame) : MM_MOVIE_OK;
    if (live)
        live->phase = status ? MM_MOVIE_FRAME_PARTIAL : MM_MOVIE_FRAME_COMMITTED;
    if (status)
        return status;
    if (services->position)
        *services->position = frame;
    if (services->published)
        services->published(context);
    return MM_MOVIE_OK;
}

int mm_movie_seek_frame(MMI32 frame, const MMMovieFrameServices *services)
{
    return seek_frame(NULL, frame, services);
}

int mm_movie_session_seek_live(MMMovieSession *movie, MMMovieLiveFrame *live, MMI32 frame,
                                const MMMovieFrameServices *services)
{
    const MMMovieLiveFrame *parent;
    int status;
    if (!movie)
        return seek_frame(live, frame, services);
    parent = movie->seeking_frame;
    movie->seeking_frame = live;
    status = seek_frame(live, frame, services);
    movie->seeking_frame = parent;
    return status;
}

static void read_scene_event(void *context, void *group, MMI32 index, MMI32 *frame, int *kind)
{
    MMMovieSceneEvent event;
    (void)context;
    mm_session_scene_event((const MMMovieGroup *)group, (MMU32)index, &event);
    *frame = event.frame;
    *kind = event.kind;
}

static void scan_camera_events(const MMMovieGroup *group, int start, MMI32 frame, MMMovieCamera *camera)
{
    MMMovieEventView view;
    MMMovieSceneEvent event;
    MMI32 selected;
    view.context = NULL;
    view.group = (void *)group;
    view.count = (MMI32)group->header.count;
    view.read = read_scene_event;
    if (start)
        frame = LAST_AUTHORED_FRAME; /* A start event is independent of its stored frame. */
    selected = mm_movie_view_last_event(&view, MM_SCENE_CHANGE_CAMERA, frame, 1, NULL);
    if (selected >= 0)
    {
        mm_session_scene_event(group, (MMU32)selected, &event);
        camera->slot = mm_movie_scene_event_word(&event, 0);
    }
    if (start)
    {
        selected = mm_movie_view_last_event(&view, MM_SCENE_SET_BACKGROUND, frame, 1, NULL);
        if (selected >= 0)
        {
            mm_session_scene_event(group, (MMU32)selected, &event);
            camera->background.source = mm_signed_word(mm_movie_scene_event_word(&event, 0));
            camera->background.type = mm_movie_scene_event_word(&event, 8);
            camera->background.number = mm_movie_scene_event_word(&event, 12);
        }
    }
}

static int image_link(void *p, MMU32 camera, int depth, MMU32 *number)
{
    return mm_background_camera_image_link((const MMChunkAccess *)p, camera, depth, number) == MM_CHUNK_OK;
}

static int camera_entry(const MMChunkAccess *access, MMU32 background, MMU32 selected, MMChunkEntry *entry)
{
    MMU32 parent, position, slot;
    int status = mm_chunk_find(access, MM_BACKGROUND, background, &parent);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
    /* The background runtime admits contiguous CAM children from slot zero. */
    for (slot = 0;; ++slot)
    {
        status = mm_session_child(access, parent, slot, MM_BACKGROUND_CAMERA, entry, &position);
        if (status != MM_CHUNK_OK)
            return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
        if (slot == selected)
            return MM_MOVIE_OK;
    }
}

typedef struct CameraRead
{
    MMChunkAccess access;
    MMU32 number;
    MMMovieConsumeCamera consume;
    void *context;
    void (*diagnostic)(void *, int);
    int called;
} CameraRead;

static int read_camera_payload(void *context, const MMReadStream *stream)
{
    CameraRead *read = (CameraRead *)context;
    MMBackgroundCameraBinding binding;
    if (read->called)
        return MM_MOVIE_BAD_FILE;
    read->called = 1;
    if (stream->length < MM_BACKGROUND_CAMERA_BYTES)
    {
        if (read->diagnostic)
            read->diagnostic(read->context, MM_BACKGROUND_CAMERA_SHORT);
        return MM_MOVIE_BAD_FILE;
    }
    if ((stream->length - MM_BACKGROUND_CAMERA_BYTES) % MM_BACKGROUND_POINT_BYTES)
    {
        if (read->diagnostic)
            read->diagnostic(read->context, MM_BACKGROUND_CAMERA_TAIL);
        return MM_MOVIE_BAD_FILE;
    }
    if (!mm_background_camera_binding_read(stream, read->number, &binding))
        return MM_MOVIE_BAD_FILE;
    mm_background_camera_binding_image(&binding, 0, image_link, &read->access);
    mm_background_camera_binding_image(&binding, 1, image_link, &read->access);
    return read->consume(read->context, &binding, stream);
}

int mm_movie_camera_graph(const MMMovieSessionGraph *graph, MMU32 background, MMU32 slot,
                          MMMovieConsumeCamera consume, void *context, void (*diagnostic)(void *, int))
{
    CameraRead read;
    MMChunkEntry entry;
    int status;
    if (!graph || !graph->access || !graph->payload || !consume)
        return MM_MOVIE_BAD_FILE;
    graph->access(graph->context, &read.access);
    status = camera_entry(&read.access, background, slot, &entry);
    if (status != MM_MOVIE_OK)
        return status;
    read.number = entry.number;
    read.consume = consume;
    read.context = context;
    read.diagnostic = diagnostic;
    read.called = 0;
    status = graph->payload(graph->context, &entry, read_camera_payload, &read);
    return status == MM_MOVIE_OK && !read.called ? MM_MOVIE_BAD_FILE : status;
}

static int publish_camera(void *context, const MMBackgroundCameraBinding *binding, const MMReadStream *stream)
{
    MMMovieCamera *camera = (MMMovieCamera *)context;
    (void)stream;
    camera->number = binding->number;
    camera->data = binding->data;
    camera->images = binding->images;
    return MM_MOVIE_OK;
}

static int load_camera(const MMMovieArchive *archive, MMMovieCamera *camera)
{
    MMChunkEntry entry;
    MMMoviePayload payload;
    CameraRead read;
    int status;
    if (!archive->index)
        return mm_movie_camera_graph(&archive->graph, camera->background.number, camera->slot,
                                     publish_camera, camera, NULL);
    status = camera_entry(&archive->access, camera->background.number, camera->slot, &entry);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_session_payload(archive, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    read.access = archive->access;
    read.number = entry.number;
    read.consume = publish_camera;
    read.context = camera;
    read.diagnostic = NULL;
    read.called = 0;
    status = read_camera_payload(&read, payload.stream);
    free(payload.decoded);
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

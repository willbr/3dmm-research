/* Archive read policy converted from libraries/archive/src/archive.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/archive_reader.h"
#include "mm/actor_data.h"
#include "mm/chunk_owner.h"
#include "mm/scene_data.h"
#include "mm/string_ops.h"
#include "mm/string_wire.h"
#include "mm/text.h"
#include "mm/text_store.h"
#include <stdlib.h>
#include <string.h>

#define ACTOR_TYPE 0x41435452UL
#define CAPTION_TYPE 0x54424f58UL
#define RICH_TEXT_TYPE 0x52545854UL
#define STRING_TABLE_TYPE 0x47535420UL

struct MMArchiveReader
{
    void *archive;
    MMChunkAccess chunks;
    MMArchiveReaderOptions options;
    MMChunkPayloadCache payload;
    MMBlockIO io;
    MMChunkIndex *index;
    MMReadStream wire;
    MMChunkStreamPayload decoded;
    MMU32 decoded_position;
    MMMovieStringTable *strings;
    int string_status;
    MMMovieSession *movie;
};

static void clear_strings(void *context)
{
    MMArchiveReader *reader = (MMArchiveReader *)context;
    mm_movie_string_table_close(reader->strings);
    reader->strings = NULL;
    reader->string_status = -1;
}

static int accept_read(void *context)
{
    const MMArchiveReader *reader = (const MMArchiveReader *)context;
    return !reader->options.accept || reader->options.accept(reader->options.context);
}

static MMArchiveReader *allocate_reader(const MMArchiveReaderOptions *options)
{
    MMArchiveReader *reader = (MMArchiveReader *)calloc(1, sizeof(*reader));
    if (reader)
    {
        reader->options = *options;
        reader->string_status = -1;
        reader->decoded_position = 0xffffffffUL;
    }
    return reader;
}

int mm_archive_reader_open(const MMChunkAccess *chunks, void *archive, const MMChunkPayloadServices *services,
                           const MMArchiveReaderOptions *options, MMArchiveReader **out)
{
    MMArchiveReader *reader;
    if (!chunks || !archive || !options || !out || !mm_chunk_keys_ordered(chunks))
        return MM_ARCHIVE_INVALID_PAYLOAD;
    reader = allocate_reader(options);
    if (!reader)
        return MM_ARCHIVE_MEMORY;
    if (mm_chunk_payload_cache_init(&reader->payload, services) != MM_CHUNK_PAYLOAD_OK)
    {
        free(reader);
        return MM_ARCHIVE_INVALID_PAYLOAD;
    }
    reader->archive = archive;
    reader->chunks = *chunks;
    *out = reader;
    return MM_ARCHIVE_READ_OK;
}

int mm_archive_reader_open_owner(MMChunkOwnerRead *owner, const MMArchiveReaderOptions *options, MMArchiveReader **out)
{
    MMArchiveReaderOptions bound;
    MMChunkAccess access;
    if (!owner || !owner->storage || !options)
        return MM_ARCHIVE_INVALID_PAYLOAD;
    access = mm_chunk_owner_access(owner->storage);
    bound = *options;
    bound.name = mm_movie_owner_graph(owner).name;
    return mm_archive_reader_open(&access, owner, &owner->payloads, &bound, out);
}

int mm_archive_read_index(const MMReadStream *stream, const MMChunkPolicy *policy, MMChunkIndex **out)
{
    MMChunkIndex *index;
    MMChunkAccess chunks;
    int status;
    if (!out)
        return MM_ARCHIVE_INVALID_PAYLOAD;
    status = mm_chunk_index_open(stream, policy, &index);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_MEMORY ? MM_ARCHIVE_MEMORY : MM_ARCHIVE_INVALID_PAYLOAD;
    chunks = mm_chunk_index_access(index);
    status = mm_chunk_keys_ordered(&chunks) ?
                 mm_chunk_validate_graph(&chunks, policy->expanded_runtime ? MM_STORAGE_LIMIT : 65535UL) :
                 MM_CHUNK_BAD_FILE;
    if (status != MM_CHUNK_OK)
    {
        mm_chunk_index_close(index);
        return status == MM_CHUNK_MEMORY ? MM_ARCHIVE_MEMORY : MM_ARCHIVE_INVALID_PAYLOAD;
    }
    *out = index;
    return MM_ARCHIVE_READ_OK;
}

int mm_archive_reader_open_stream(const MMReadStream *stream, const MMChunkPolicy *policy,
                                  const MMArchiveReaderOptions *options, MMArchiveReader **out)
{
    MMArchiveReader *reader;
    MMChunkIndex *index;
    int status;
    if (!options || !out)
        return MM_ARCHIVE_INVALID_PAYLOAD;
    status = mm_archive_read_index(stream, policy, &index);
    if (status != MM_ARCHIVE_READ_OK)
        return status;
    reader = allocate_reader(options);
    if (!reader)
    {
        mm_chunk_index_close(index);
        return MM_ARCHIVE_MEMORY;
    }
    reader->wire = *stream;
    reader->index = index;
    reader->chunks = mm_chunk_index_access(index);
    *out = reader;
    return MM_ARCHIVE_READ_OK;
}

const MMChunkIndex *mm_archive_reader_wire_index(const MMArchiveReader *reader)
{
    return reader->index;
}

void mm_archive_reader_close(MMArchiveReader *reader)
{
    if (reader)
    {
        mm_movie_session_close(reader->movie);
        clear_strings(reader);
        if (reader->index)
        {
            free(reader->decoded.decoded);
            mm_chunk_index_close(reader->index);
        }
        else
            mm_chunk_payload_cache_clear(&reader->payload);
        free(reader);
    }
}

MMChunkAccess mm_archive_reader_access(const MMArchiveReader *reader)
{
    return reader->chunks;
}

int mm_archive_reader_read_stored(const MMArchiveReader *reader, const MMChunkEntry *entry,
                                  void *bytes, MMU32 count)
{
    MMReadSlice slice;
    if (reader->index)
        return mm_chunk_stored_slice(&reader->wire, mm_chunk_index_header(reader->index), entry, &slice) &&
               mm_read_exact(&slice.stream, 0, bytes, count);
    return mm_chunk_read_stored(reader->payload.services, reader->archive, entry->type, entry->number, bytes, count);
}

static int read_wire_payload(MMArchiveReader *reader, MMU32 position, const MMChunkEntry *entry, MMReadStream *out)
{
    MMU32 limit = reader->options.max_packed_bytes;
    unsigned char prefix[8];
    int packed = (entry->flags & MM_CHUNK_PACKED) != 0, status;
    if (reader->decoded_position != position)
    {
        clear_strings(reader);
        free(reader->decoded.decoded);
        reader->decoded.decoded = NULL;
        reader->decoded_position = 0xffffffffUL;
        if (packed)
        {
            if (!mm_archive_reader_read_stored(reader, entry, prefix, sizeof(prefix)))
                return MM_ARCHIVE_BAD_PACKED_HEADER;
            if (entry->bytes > limit || mm_chunk_packed_decoded_length(prefix) > limit)
                return MM_ARCHIVE_PACKED_LIMIT;
        }
        status = mm_chunk_payload_open(&reader->wire, mm_chunk_index_header(reader->index), entry, 0, limit,
                                       &reader->decoded);
        if (status == MM_CHUNK_MEMORY)
            return MM_ARCHIVE_MEMORY;
        if (status != MM_CHUNK_OK)
            return packed ? MM_ARCHIVE_DECODE_FAILED : MM_ARCHIVE_INVALID_PAYLOAD;
        if (!accept_read(reader))
        {
            free(reader->decoded.decoded);
            reader->decoded.decoded = NULL;
            return MM_ARCHIVE_INVALID_PAYLOAD;
        }
        reader->decoded_position = position;
    }
    *out = *reader->decoded.stream;
    return MM_ARCHIVE_READ_OK;
}

static int read_payload(MMArchiveReader *reader, MMU32 position, const MMChunkEntry *entry, MMReadStream *out)
{
    MMChunkPayloadCacheCallbacks callbacks;
    int status;
    if (reader->index)
        return read_wire_payload(reader, position, entry, out);
    callbacks.context = reader;
    callbacks.invalidate = clear_strings;
    callbacks.accept = accept_read;
    status = mm_chunk_payload_cache_load(&reader->payload, reader->archive, position, entry,
                                         reader->options.max_packed_bytes, &callbacks);
    switch (status)
    {
    case MM_CHUNK_PAYLOAD_OK:
        if (mm_chunk_payload_cache_stream(&reader->payload, &reader->io, out))
            return MM_ARCHIVE_READ_OK;
        return MM_ARCHIVE_INVALID_PAYLOAD;
    case MM_CHUNK_PAYLOAD_MISSING:
        return MM_ARCHIVE_MISSING_PAYLOAD;
    case MM_CHUNK_PAYLOAD_BAD_HEADER:
        return MM_ARCHIVE_BAD_PACKED_HEADER;
    case MM_CHUNK_PAYLOAD_LIMIT:
        return MM_ARCHIVE_PACKED_LIMIT;
    case MM_CHUNK_PAYLOAD_DECODE_FAILED:
        return MM_ARCHIVE_DECODE_FAILED;
    default:
        return MM_ARCHIVE_INVALID_PAYLOAD;
    }
}

int mm_archive_reader_payload(MMArchiveReader *reader, MMU32 position, MMReadStream *out)
{
    MMChunkEntry entry;
    if (mm_chunk_at(&reader->chunks, position, &entry) != MM_CHUNK_OK)
        return MM_ARCHIVE_INVALID_POSITION;
    return read_payload(reader, position, &entry, out);
}

static int decode_record(const MMChunkAccess *chunks, MMU32 position, const MMChunkEntry *entry,
                         const MMReadStream *stream, MMArchiveRecord *out)
{
    MMArchiveRecord record = {0};
    switch (entry->type)
    {
    case MM_MOVIE_CHUNK:
    {
        MMMovieHeader movie;
        if (mm_movie_read_header(stream, &movie) != MM_MOVIE_OK)
            return MM_ARCHIVE_INVALID_RECORD;
        record.kind = MM_ARCHIVE_MOVIE;
        record.value.movie.version = movie.current_version;
        record.value.movie.backward_version = movie.backward_version;
        break;
    }
    case MM_SCENE_CHUNK:
    {
        MMSceneState scene;
        mm_scene_init(&scene, 1);
        if (!mm_scene_decode_header(stream, &scene))
            return MM_ARCHIVE_INVALID_RECORD;
        record.kind = MM_ARCHIVE_SCENE;
        record.value.scene.first = scene.firstFrame;
        record.value.scene.last = scene.lastFrame;
        record.value.scene.transition = scene.transition;
        break;
    }
    case ACTOR_TYPE:
    {
        MMActorHeader actor;
        if (!mm_actor_read_header(stream, &actor))
            return MM_ARCHIVE_INVALID_RECORD;
        record.kind = MM_ARCHIVE_ACTOR;
        record.value.actor.id = actor.actor_id;
        record.value.actor.first = actor.first_frame;
        record.value.actor.last = actor.last_frame;
        break;
    }
    case CAPTION_TYPE:
    {
        MMTextBoxState caption;
        if (!mm_text_box_decode(stream, &caption) ||
            mm_chunk_child_exact(chunks, position, caption.child, RICH_TEXT_TYPE,
                                 &record.value.caption.text) != MM_CHUNK_OK)
            return MM_ARCHIVE_INVALID_RECORD;
        record.kind = MM_ARCHIVE_CAPTION;
        record.value.caption.first = caption.first;
        record.value.caption.end = caption.end;
        record.value.caption.story = caption.story;
        record.value.caption.left = caption.left;
        record.value.caption.top = caption.top;
        record.value.caption.right = caption.right;
        record.value.caption.bottom = caption.bottom;
        break;
    }
    default:
        record.kind = MM_ARCHIVE_OPAQUE;
        break;
    }
    *out = record;
    return MM_ARCHIVE_READ_OK;
}

int mm_archive_decode_record(const MMChunkAccess *chunks, MMU32 position, const MMReadStream *stream,
                             MMArchiveRecord *out)
{
    MMChunkEntry entry;
    if (mm_chunk_at(chunks, position, &entry) != MM_CHUNK_OK)
        return MM_ARCHIVE_INVALID_POSITION;
    if (!stream || !out)
        return MM_ARCHIVE_INVALID_RECORD;
    return decode_record(chunks, position, &entry, stream, out);
}

int mm_archive_reader_record(MMArchiveReader *reader, MMU32 position, MMArchiveRecord *out)
{
    MMChunkEntry entry;
    MMReadStream stream;
    int status;
    if (mm_chunk_at(&reader->chunks, position, &entry) != MM_CHUNK_OK)
        return MM_ARCHIVE_INVALID_POSITION;
    status = read_payload(reader, position, &entry, &stream);
    if (status != MM_ARCHIVE_READ_OK)
        return status;
    return decode_record(&reader->chunks, position, &entry, &stream, out);
}

int mm_archive_reader_strings(MMArchiveReader *reader, MMU32 position, const MMMovieStringTable **out)
{
    MMChunkEntry entry;
    MMReadStream stream;
    int status;
    if (mm_chunk_at(&reader->chunks, position, &entry) != MM_CHUNK_OK)
        return MM_ARCHIVE_INVALID_POSITION;
    if (entry.type != STRING_TABLE_TYPE)
        return MM_ARCHIVE_WRONG_TYPE;
    status = read_payload(reader, position, &entry, &stream);
    if (status != MM_ARCHIVE_READ_OK)
        return status;
    if (reader->string_status < 0)
    {
        const MMArchiveReaderOptions *options = &reader->options;
        if (stream.length > options->max_string_bytes)
            reader->string_status = MM_ARCHIVE_STRING_LIMIT;
        else
        {
            status = mm_movie_string_table_open(&stream, options->max_string_bytes, options->encoding,
                                                options->convert, options->context, &reader->strings);
            if (status == MM_MOVIE_MEMORY)
                return MM_ARCHIVE_MEMORY;
            reader->string_status = status == MM_MOVIE_OK && accept_read(reader) ?
                                        MM_ARCHIVE_READ_OK : MM_ARCHIVE_INVALID_STRINGS;
        }
    }
    if (reader->string_status == MM_ARCHIVE_READ_OK)
        *out = reader->strings;
    return reader->string_status;
}

static void graph_access(void *context, MMChunkAccess *out)
{
    *out = ((const MMArchiveReader *)context)->chunks;
}

static int directory_payload(MMArchiveReader *reader, MMU32 position, const MMChunkEntry *entry,
                              MMReadStream *stream, MMArchiveDirectoryError *error)
{
    const int status = read_payload(reader, position, entry, stream);
    if (status == MM_ARCHIVE_READ_OK)
        return MM_ARCHIVE_DIRECTORY_OK;
    if (error)
    {
        error->position = position;
        error->payload_status = status;
    }
    return MM_ARCHIVE_DIRECTORY_PAYLOAD;
}

static int graph_read(void *context, void *request, MMU32 position, const MMChunkEntry *entry,
                      void *out, MMU32 size)
{
    MMReadStream stream;
    if (directory_payload((MMArchiveReader *)context, position, entry, &stream,
                          (MMArchiveDirectoryError *)request) != MM_ARCHIVE_DIRECTORY_OK)
        return MM_MOVIE_IO;
    return mm_movie_read_fixed(&stream, out, size);
}

static int graph_payload(void *context, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    MMArchiveReader *reader = (MMArchiveReader *)context;
    MMReadStream stream;
    MMU32 position;
    int status;
    if (mm_chunk_find(&reader->chunks, entry->type, entry->number, &position) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = read_payload(reader, position, entry, &stream);
    if (status != MM_ARCHIVE_READ_OK)
        return status == MM_ARCHIVE_MEMORY ? MM_MOVIE_MEMORY : MM_MOVIE_BAD_FILE;
    return receive(out, &stream);
}

int mm_archive_reader_stored_payload(void *context, const MMChunkEntry *entry, MMReadReceiver receive, void *out)
{
    const MMArchiveReader *reader = (const MMArchiveReader *)context;
    MMReadSlice slice;
    int result;
    if (reader->index)
        return mm_chunk_stored_slice(&reader->wire, mm_chunk_index_header(reader->index), entry, &slice) ?
                   receive(out, &slice.stream) : MM_MOVIE_BAD_FILE;
    return mm_chunk_visit_stored(reader->payload.services, reader->archive, entry->type, entry->number,
                                  receive, out, &result) == MM_CHUNK_OK ? result : MM_MOVIE_BAD_FILE;
}

static int legacy_name(void *context, const char *text, void *out, MMU32 capacity, MMU32 *count)
{
    const MMArchiveReader *reader = (const MMArchiveReader *)context;
    const unsigned short order = 1;
    unsigned short characters[MM_STRING_MAX + 2];
    MMStringValue value = {0};
    MMStringWire wire = {0};
    wire.current_encoding = reader->options.encoding;
    wire.native_big_endian = *(const unsigned char *)&order == 0;
    value.buffer = wire.buffer = characters;
    value.unit = wire.unit = wire.current_encoding == MM_TEXT_UTF16_LE ||
                             wire.current_encoding == MM_TEXT_UTF16_BE ? 2 : 1;
    mm_string_from_ansi(&value, NULL, text, strlen(text));
    if (!mm_string_wire_get(&wire, out, capacity))
        return 0;
    *count = mm_string_wire_size(&wire);
    return 1;
}

int mm_archive_reader_name(MMArchiveReader *reader, MMU32 position, MMReadReceiver receive, void *out)
{
    MMChunkEntry entry;
    if (reader->index)
    {
        unsigned char scratch[MM_CHUNK_NAME_WIRE_BYTES];
        const unsigned char *bytes;
        MMU32 count;
        MMMemoryStream stream;
        MMChunkLegacyName convert = reader->options.legacy_name ? reader->options.legacy_name : legacy_name;
        void *context = reader->options.legacy_name ? reader->options.context : reader;
        if (mm_chunk_serialized_name(reader->index, position, convert, context, scratch, &bytes, &count) != MM_CHUNK_OK ||
            !count || !mm_memory_stream_init(&stream, bytes, count))
            return MM_MOVIE_BAD_FILE;
        return receive(out, &stream.stream);
    }
    if (!reader->options.name || mm_chunk_at(&reader->chunks, position, &entry) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    return reader->options.name(reader->archive, &entry, receive, out);
}

static int graph_name(void *context, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    MMArchiveReader *reader = (MMArchiveReader *)context;
    MMU32 position;
    if (mm_chunk_find(&reader->chunks, entry->type, entry->number, &position) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    return mm_archive_reader_name(reader, position, receive, out);
}

MMMovieSessionGraph mm_archive_reader_graph(MMArchiveReader *reader)
{
    MMMovieSessionGraph graph;
    graph.context = reader;
    graph.access = graph_access;
    graph.read = graph_read;
    graph.payload = graph_payload;
    graph.name = graph_name;
    return graph;
}

static int directory_sources(MMArchiveReader *reader, const MMMovieSession *movie, const MMArchiveDirectorySink *sink,
                              MMArchiveDirectoryError *error)
{
    MMMovieSourceTable table;
    MMMovieSourceName row = {0};
    MMChunkEntry entry;
    MMReadStream stream;
    MMU32 position, i;
    int status = mm_movie_session_source_table(movie, &entry, &position);
    if (status == MM_MOVIE_ASSET_NOT_FOUND)
    {
        row.id = MM_MOVIE_RETAIL_SOURCE_ID;
        row.encoding = MM_TEXT_WINDOWS;
        row.size = sizeof(MM_MOVIE_RETAIL_SOURCE_NAME) - 1;
        memcpy(row.bytes, MM_MOVIE_RETAIL_SOURCE_NAME, row.size);
        return sink->source(sink->context, &row) > 0 ? MM_ARCHIVE_DIRECTORY_OK : MM_ARCHIVE_DIRECTORY_CANCELLED;
    }
    if (status != MM_MOVIE_OK)
        return MM_ARCHIVE_DIRECTORY_SOURCE_LINK;
    status = directory_payload(reader, position, &entry, &stream, error);
    if (status != MM_ARCHIVE_DIRECTORY_OK)
        return status;
    error->position = position;
    if (stream.length > reader->options.max_string_bytes)
        return MM_ARCHIVE_DIRECTORY_SOURCE_LIMIT;
    status = mm_movie_source_table_open(&stream, reader->options.max_string_bytes, &table);
    if (status == MM_MOVIE_MEMORY)
        return MM_ARCHIVE_DIRECTORY_MEMORY;
    if (status != MM_MOVIE_OK)
        return MM_ARCHIVE_DIRECTORY_SOURCE_TABLE;
    for (i = 0; i < table.count; ++i)
    {
        if (mm_movie_source_table_at(&table, i, &row) != MM_MOVIE_OK)
            return MM_ARCHIVE_DIRECTORY_SOURCE_ROW;
        if (sink->source(sink->context, &row) <= 0)
            return MM_ARCHIVE_DIRECTORY_CANCELLED;
    }
    return MM_ARCHIVE_DIRECTORY_OK;
}

static int directory_rows(MMArchiveReader *reader, const MMMovieSession *movie, const MMArchiveDirectorySink *sink,
                           MMArchiveDirectoryError *error)
{
    MMMovieSessionScene scene;
    MMU32 cursor = 0, slot;
    int status;
    error->root = mm_movie_session_root(movie)->number;
    if (sink->root(sink->context, error->root) <= 0)
        return MM_ARCHIVE_DIRECTORY_CANCELLED;
    while ((status = mm_movie_session_next_scene(movie, error, &cursor, &scene, &slot)) == MM_MOVIE_OK)
        if (sink->scene(sink->context, slot, &scene) <= 0)
            return MM_ARCHIVE_DIRECTORY_CANCELLED;
    if (status == MM_MOVIE_MEMORY)
        return MM_ARCHIVE_DIRECTORY_MEMORY;
    if (error->payload_status != MM_ARCHIVE_READ_OK)
        return MM_ARCHIVE_DIRECTORY_PAYLOAD;
    if (status != MM_MOVIE_SCENE_NOT_FOUND)
        return MM_ARCHIVE_DIRECTORY_SCENE;
    return directory_sources(reader, movie, sink, error);
}

int mm_archive_reader_directory(MMArchiveReader *reader, const MMArchiveDirectorySink *sink,
                                MMArchiveDirectoryError *error)
{
    MMMovieSessionGraph graph;
    MMMovieSession *movie = reader->movie;
    int status;
    error->root = error->position = 0xffffffffUL;
    error->payload_status = MM_ARCHIVE_READ_OK;
    if (!movie)
    {
        graph = mm_archive_reader_graph(reader);
        status = mm_movie_session_open_graph(&graph, error, &error->root, &movie);
        if (status == MM_MOVIE_ROOT_NOT_FOUND)
            return MM_ARCHIVE_DIRECTORY_OK;
        if (status == MM_MOVIE_MEMORY)
            return MM_ARCHIVE_DIRECTORY_MEMORY;
        if (status != MM_MOVIE_OK)
            return error->payload_status == MM_ARCHIVE_READ_OK ? MM_ARCHIVE_DIRECTORY_ROOT :
                                                                MM_ARCHIVE_DIRECTORY_PAYLOAD;
    }
    status = directory_rows(reader, movie, sink, error);
    if (status == MM_ARCHIVE_DIRECTORY_OK)
        reader->movie = movie;
    else if (movie != reader->movie)
        mm_movie_session_close(movie);
    return status;
}

MMMovieSession *mm_archive_reader_movie(const MMArchiveReader *reader)
{
    return reader ? reader->movie : NULL;
}

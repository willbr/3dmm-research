/* Archive read policy converted from libraries/archive/src/archive.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/archive_reader.h"
#include "mm/actor_data.h"
#include "mm/scene_data.h"
#include "mm/text_store.h"
#include <stdlib.h>

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
    MMMovieStringTable *strings;
    int string_status;
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

int mm_archive_reader_open(const MMChunkAccess *chunks, void *archive, const MMChunkPayloadServices *services,
                           const MMArchiveReaderOptions *options, MMArchiveReader **out)
{
    MMArchiveReader *reader;
    if (!chunks || !archive || !options || !out || !mm_chunk_keys_ordered(chunks))
        return MM_ARCHIVE_INVALID_PAYLOAD;
    reader = (MMArchiveReader *)calloc(1, sizeof(*reader));
    if (!reader)
        return MM_ARCHIVE_MEMORY;
    if (mm_chunk_payload_cache_init(&reader->payload, services) != MM_CHUNK_PAYLOAD_OK)
    {
        free(reader);
        return MM_ARCHIVE_INVALID_PAYLOAD;
    }
    reader->archive = archive;
    reader->chunks = *chunks;
    reader->options = *options;
    reader->string_status = -1;
    *out = reader;
    return MM_ARCHIVE_READ_OK;
}

void mm_archive_reader_close(MMArchiveReader *reader)
{
    if (reader)
    {
        clear_strings(reader);
        mm_chunk_payload_cache_clear(&reader->payload);
        free(reader);
    }
}

static int read_payload(MMArchiveReader *reader, MMU32 position, const MMChunkEntry *entry, MMReadStream *out)
{
    MMChunkPayloadCacheCallbacks callbacks;
    int status;
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

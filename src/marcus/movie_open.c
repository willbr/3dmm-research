#include "movie_open.h"
#include "mm/collections.h"
#include "mm/content_vfs.h"
#include "mm/scene_data.h"
#include "mm/string_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MovieRead
{
    MarcusMovieOpen *movie;
    MarcusSceneRows *scenes;
    MMMovieSourceIds *source_ids;
    const MMPrivateArchiveProfile *profile;
    const struct MMTextHost *text;
    MarcusActorNames *names;
    int memory;
} MovieRead;

void marcus_movie_open_init(MarcusMovieOpen *movie)
{
    memset(movie, 0, sizeof(*movie));
    mm_storage_init(&movie->sources, NULL);
    movie->retail_source = MM_MOVIE_RETAIL_SOURCE_ID;
}

const MarcusMovieSource *marcus_movie_open_sources(const MarcusMovieOpen *movie)
{
    return (const MarcusMovieSource *)movie->sources.buffers[0].data;
}

void marcus_movie_open_dispose(MarcusMovieOpen *movie)
{
    const MarcusMovieSource *sources = marcus_movie_open_sources(movie);
    MMU32 i;
    for (i = 0; i < movie->source_count; ++i)
        free(sources[i].name);
    mm_storage_dispose(&movie->sources);
    movie->source_count = 0;
}

static int read_root(void *context, MMU32 number)
{
    (void)context;
    (void)number;
    return 1;
}

static int read_scene(void *context, MMU32 slot, const MMMovieSessionScene *scene)
{
    MovieRead *read = context;
    MarcusScene *rows;
    MarcusScene *row;
    const size_t index = read->scenes->count;
    (void)slot;
    read->scenes->storage.min_grow[0] = read->scenes->storage.buffers[0].capacity / 2;
    if (!marcus_scene_rows_resize(read->scenes, index + 1))
    {
        read->memory = 1;
        return 0;
    }
    rows = (MarcusScene *)read->scenes->storage.buffers[0].data;
    row = rows + index;
    memset(row, 0, sizeof(*row));
    row->chunk = scene->number;
    row->first_frame = scene->first_frame;
    row->last_frame = scene->last_frame;
    row->transition = scene->transition;
    return 1;
}

static int read_source(void *context, const MMMovieSourceName *source)
{
    MovieRead *read = context;
    MarcusMovieOpen *movie = read->movie;
    const MMPrivateArchiveProfile *profile = read->profile;
    MMArray array = {0};
    MarcusMovieSource row;
    unsigned short text[MM_STRING_MAX];
    char name[MM_MOVIE_SOURCE_NAME_BYTES], key[MM_MOVIE_SOURCE_NAME_BYTES];
    const MMU32 units = mm_movie_source_name_text(source, profile->encoding, profile->unit, profile->convert,
                                                  NULL, text, MM_STRING_MAX);
    if (!marcus_archive_text_utf8(read->text, text, units * profile->unit, profile->encoding,
                                 name, sizeof(name), &row.name_bytes))
        return 0;
    if (mm_movie_source_ids_add(read->source_ids, source->id, NULL) == MM_MOVIE_SOURCE_ID_MEMORY ||
        !(row.name = malloc(row.name_bytes + 1)))
    {
        read->memory = 1;
        return 0;
    }
    row.id = source->id;
    memcpy(row.name, name, row.name_bytes + 1);
    array.storage = &movie->sources;
    array.width = sizeof(row);
    array.count = movie->source_count;
    array.free_count = -1;
    movie->sources.min_grow[0] = movie->sources.buffers[0].capacity / 2;
    if (mm_array_add(&array, &row, NULL) != MM_STORAGE_OK)
    {
        free(row.name);
        read->memory = 1;
        return 0;
    }
    movie->source_count = array.count;
    if (!movie->retail_prepared && mm_vfs_source_key(name, NULL, key, sizeof(key)) &&
        !strcmp(key, MM_MOVIE_RETAIL_SOURCE_NAME))
    {
        movie->retail_prepared = 1;
        movie->retail_source = source->id;
    }
    return 1;
}

static int read_actor(void *context, MMI32 actor, const void *text, MMU32 units)
{
    MovieRead *read = context;
    const MMPrivateArchiveProfile *profile = read->profile;
    char name[MM_MOVIE_SOURCE_NAME_BYTES];
    size_t bytes;
    if (!marcus_archive_text_utf8(read->text, text, units * profile->unit, profile->encoding,
                                  name, sizeof(name), &bytes))
        return 0;
    if (marcus_actor_names_set(read->names, actor, name, bytes))
        return 1;
    read->memory = 1;
    return 0;
}

static const char *read_error(int status, int payload)
{
    switch (status)
    {
    case MM_ARCHIVE_DIRECTORY_ROOT: return "Invalid or unsupported record header.";
    case MM_ARCHIVE_DIRECTORY_SCENE: return "The movie has an invalid scene link or header.";
    case MM_ARCHIVE_DIRECTORY_SOURCE_LINK: return "The movie has an invalid source table link.";
    case MM_ARCHIVE_DIRECTORY_SOURCE_LIMIT: return "Source table exceeds this session's preview budget.";
    case MM_ARCHIVE_DIRECTORY_SOURCE_TABLE: return "Invalid movie source table.";
    case MM_ARCHIVE_DIRECTORY_SOURCE_ROW: return "Cannot read a movie source name.";
    case MM_ARCHIVE_DIRECTORY_INDEX: return "Cannot read the movie chunk index.";
    case MM_ARCHIVE_DIRECTORY_NO_SCENES: return "This document contains no movie scenes.";
    case MM_ARCHIVE_DIRECTORY_ROLLCALL: return "The movie roll-call record is invalid.";
    case MM_ARCHIVE_DIRECTORY_ROLLCALL_LIMIT: return "The movie roll-call table has too many entries.";
    case MM_ARCHIVE_DIRECTORY_PAYLOAD:
        switch (payload)
        {
        case MM_ARCHIVE_INVALID_POSITION: return "Chunk index is out of range.";
        case MM_ARCHIVE_MISSING_PAYLOAD: return "Cannot read this chunk.";
        case MM_ARCHIVE_BAD_PACKED_HEADER: return "Invalid compressed payload header.";
        case MM_ARCHIVE_PACKED_LIMIT: return "Packed payload exceeds this session's decode budget.";
        case MM_ARCHIVE_DECODE_FAILED: return "Cannot decode this payload.";
        case MM_ARCHIVE_INVALID_RECORD: return "Invalid or unsupported record header.";
        case MM_ARCHIVE_WRONG_TYPE: return "This chunk is not a General String Table.";
        case MM_ARCHIVE_STRING_LIMIT: return "String table exceeds this session's preview budget.";
        case MM_ARCHIVE_INVALID_STRINGS: return "Invalid or unsupported General String Table.";
        case MM_ARCHIVE_MEMORY: return "Insufficient memory to read the movie directory.";
        default: return "The legacy reader rejected this payload.";
        }
    default: return "Movie source names cannot be decoded.";
    }
}

const char *marcus_movie_open_read(MarcusMovieOpen *movie, MMArchiveReader *reader, MarcusSceneRows *scenes,
                                  MMMovieSourceIds *source_ids, const MMPrivateArchiveProfile *profile,
                                  const struct MMTextHost *text, MarcusActorNames *names)
{
    static const MMArchiveMovieOpenSink callbacks = {{NULL, read_root, read_scene, read_source}, read_actor};
    MMArchiveMovieOpenSink sink = callbacks;
    MMArchiveDirectoryError detail;
    const MMChunkAccess chunks = mm_archive_reader_access(reader);
    MovieRead read;
    MarcusScene *rows;
    MMU32 position;
    size_t i;
    int status;
    read.movie = movie;
    read.scenes = scenes;
    read.source_ids = source_ids;
    read.profile = profile;
    read.text = text;
    read.names = names;
    read.memory = 0;
    sink.directory.context = &read;
    status = mm_archive_reader_movie_open(reader, &sink, &movie->ids, &detail);
    if (read.memory || status == MM_ARCHIVE_DIRECTORY_MEMORY)
        return "Insufficient memory to read the movie directory.";
    if (status != MM_ARCHIVE_DIRECTORY_OK)
        return read_error(status, detail.payload_status);
    if (!movie->retail_prepared && !mm_movie_source_ids_retail_alias(source_ids, &movie->retail_source))
        return "No retail source ID is available.";
    rows = (MarcusScene *)scenes->storage.buffers[0].data;
    for (i = 0; i < scenes->count; ++i)
    {
        if (mm_chunk_find(&chunks, MM_SCENE_CHUNK, (MMU32)rows[i].chunk, &position) != MM_CHUNK_OK)
            return "Movie scene is missing from the index.";
        if (!marcus_archive_name_utf8(reader, position, profile, text, rows[i].name, sizeof(rows[i].name), 1))
            sprintf(rows[i].name, "Scene %lu", (unsigned long)(i + 1));
    }
    return NULL;
}

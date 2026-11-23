/* Archive read policy converted from libraries/archive/src/archive.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ARCHIVE_READER_H
#define MM_ENGINE_ARCHIVE_READER_H
#include "chunk_storage.h"
#include "chunk_loading.h"
#include "movie_session.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMArchiveReader MMArchiveReader;
typedef struct MMArchiveReaderOptions
{
    MMU32 max_packed_bytes, max_string_bytes;
    int encoding;
    MMStringConvert convert;
    void *context;
    int (*accept)(void *);
    MMMovieGraphBytes name; /* Optional. Receives the borrowed archive as context. */
    MMChunkLegacyName legacy_name; /* Optional native conversion for stream archives before CHN2 v3. */
} MMArchiveReaderOptions;

enum MMArchiveReadResult
{
    MM_ARCHIVE_READ_OK,
    MM_ARCHIVE_INVALID_POSITION,
    MM_ARCHIVE_MISSING_PAYLOAD,
    MM_ARCHIVE_INVALID_PAYLOAD,
    MM_ARCHIVE_BAD_PACKED_HEADER,
    MM_ARCHIVE_PACKED_LIMIT,
    MM_ARCHIVE_DECODE_FAILED,
    MM_ARCHIVE_INVALID_RECORD,
    MM_ARCHIVE_WRONG_TYPE,
    MM_ARCHIVE_STRING_LIMIT,
    MM_ARCHIVE_INVALID_STRINGS,
    MM_ARCHIVE_MEMORY
};

enum MMArchiveRecordKind
{
    MM_ARCHIVE_OPAQUE, MM_ARCHIVE_MOVIE, MM_ARCHIVE_SCENE, MM_ARCHIVE_ACTOR, MM_ARCHIVE_CAPTION
};

typedef struct MMArchiveRecord
{
    int kind;
    union
    {
        struct { int version, backward_version; } movie;
        struct { MMI32 first, last, transition; } scene;
        struct { MMI32 id, first, last; } actor;
        struct
        {
            MMI32 first, end, left, top, right, bottom;
            int story;
            MMChunkChild text;
        } caption;
    } value;
} MMArchiveRecord;

/* One reader per open archive. The archive, index, services and options'
 * callback context are borrowed. Index keys must be unique and ordered. Rows
 * must retain their position and identity. Reads are synchronous and
 * callbacks must not re-enter the reader.
 * A returned stream or table borrows the current cached chunk until a read
 * of another chunk or close. Outputs are published only on success. */
int mm_archive_reader_open(const MMChunkAccess *, void *archive, const MMChunkPayloadServices *,
                           const MMArchiveReaderOptions *, MMArchiveReader **);
/* Bind the index, payload services and names of a borrowed live owner. */
int mm_archive_reader_open_owner(struct MMChunkOwnerRead *, const MMArchiveReaderOptions *, MMArchiveReader **);
/* Open an owned, sorted index with retail graph validation. */
int mm_archive_read_index(const MMReadStream *, const MMChunkPolicy *, MMChunkIndex **);
/* Own an immutable index and decoded cache over borrowed physical bytes. The
 * caller keeps the stream context and file contents valid until close. */
int mm_archive_reader_open_stream(const MMReadStream *, const MMChunkPolicy *,
                                  const MMArchiveReaderOptions *, MMArchiveReader **);
/* Retain a previously validated index. The stream must be the same one used
 * to validate the index and its bytes must remain available until close. */
int mm_archive_reader_open_wire(const MMChunkIndex *, const MMReadStream *,
                                const MMArchiveReaderOptions *, MMArchiveReader **);
/* Borrow the validated wire index until close. Live readers have no wire index. */
const MMChunkIndex *mm_archive_reader_wire_index(const MMArchiveReader *);
void mm_archive_reader_close(MMArchiveReader *);
MMChunkAccess mm_archive_reader_access(const MMArchiveReader *);
int mm_archive_reader_read_stored(const MMArchiveReader *, const MMChunkEntry *, void *bytes, MMU32 count);
/* Graph-compatible stored read. The receiver borrows an independent extent
 * through the call and can consume decoded chunks through the same reader. */
int mm_archive_reader_stored_payload(void *, const MMChunkEntry *, MMReadReceiver, void *out);
int mm_archive_reader_name(MMArchiveReader *, MMU32 position, MMReadReceiver, void *out);

typedef struct MMArchiveCatalogLink
{
    MMChunkChild child;
    int target_present;
} MMArchiveCatalogLink;

enum MMArchiveCatalogResult
{
    MM_ARCHIVE_CATALOG_OK,
    MM_ARCHIVE_CATALOG_ENTRY,
    MM_ARCHIVE_CATALOG_CHILD,
    MM_ARCHIVE_CATALOG_MEMORY,
    MM_ARCHIVE_CATALOG_STOPPED
};

typedef int (*MMArchiveCatalogVisit)(void *, const MMChunkEntry *, void *encoded_name, MMU32 name_bytes,
                                     const MMArchiveCatalogLink *);
/* Visit chunks in index order, with child links sorted by slot, type and
 * number. Absent targets remain visible. Each row, encoded name and child
 * array is borrowed until the callback returns. Zero name bytes means no
 * readable name. Callbacks must not re-enter the reader. Return zero to stop.
 * A null visitor checks entries and child references without building rows
 * or decoding names. Missing targets remain allowed on this path.
 * On a child read failure, failed_parent
 * receives its entry if supplied. Stage output to discard a failed prefix. */
int mm_archive_reader_catalog(MMArchiveReader *, MMArchiveCatalogVisit, void *context, MMChunkEntry *failed_parent);

/* The graph borrows the reader. Pass NULL for a direct graph read's request. */
MMMovieSessionGraph mm_archive_reader_graph(MMArchiveReader *);
int mm_archive_reader_payload(MMArchiveReader *, MMU32 position, MMReadStream *);
int mm_archive_reader_record(MMArchiveReader *, MMU32 position, MMArchiveRecord *);
int mm_archive_reader_strings(MMArchiveReader *, MMU32 position, const MMMovieStringTable **);

/* Also usable with an immutable wire index and a decoded extent. Caption
 * headers require an exact RTXT link at the slot stored in the TBOX, but do
 * not read the linked text payload. */
int mm_archive_decode_record(const MMChunkAccess *, MMU32 position, const MMReadStream *, MMArchiveRecord *);

typedef struct MMArchiveDirectorySink
{
    void *context;
    int (*root)(void *, MMU32 number);
    int (*scene)(void *, MMU32 slot, const MMMovieSessionScene *);
    int (*source)(void *, const MMMovieSourceName *);
} MMArchiveDirectorySink;

enum MMArchiveDirectoryResult
{
    MM_ARCHIVE_DIRECTORY_OK,
    MM_ARCHIVE_DIRECTORY_ROOT,
    MM_ARCHIVE_DIRECTORY_SCENE,
    MM_ARCHIVE_DIRECTORY_SOURCE_LINK,
    MM_ARCHIVE_DIRECTORY_SOURCE_LIMIT,
    MM_ARCHIVE_DIRECTORY_SOURCE_TABLE,
    MM_ARCHIVE_DIRECTORY_SOURCE_ROW,
    MM_ARCHIVE_DIRECTORY_PAYLOAD,
    MM_ARCHIVE_DIRECTORY_MEMORY,
    MM_ARCHIVE_DIRECTORY_CANCELLED,
    MM_ARCHIVE_DIRECTORY_INDEX,
    MM_ARCHIVE_DIRECTORY_NO_SCENES,
    MM_ARCHIVE_DIRECTORY_ROLLCALL,
    MM_ARCHIVE_DIRECTORY_ROLLCALL_LIMIT
};

typedef struct MMArchiveDirectoryError
{
    MMU32 root, position;
    int payload_status;
} MMArchiveDirectoryError;

/* Visit the movie root, authored scene links and source rows in that order.
 * Rootless archives succeed without calling the sink. Callbacks borrow each
 * row, return positive to continue and must not re-enter. A failing call can
 * have visited a prefix, so callers stage their output. The reader retains its
 * movie session only after the whole visit succeeds. error is required. */
int mm_archive_reader_directory(MMArchiveReader *, const MMArchiveDirectorySink *, MMArchiveDirectoryError *error);

typedef struct MMArchiveMovieOpenSink
{
    MMArchiveDirectorySink directory;
    int (*actor_name)(void *, MMI32 actor, const void *text, MMU32 units);
} MMArchiveMovieOpenSink;

typedef struct MMArchiveMovieIds
{
    MMU32 next_scene, next_actor_chunk, next_actor_id;
    int scene_exhausted, actor_chunk_exhausted;
} MMArchiveMovieIds;

/* Validate the movie directory, roll-call entries and every ACTR header
 * before publishing the next available edit IDs and retaining the movie.
 * Rootless scene fixtures use SCEN index order and retail source 1. Actor
 * names are counted in the reader's current text encoding and borrow its
 * string table. The directory callback contract applies to all callbacks.
 * Failure leaves IDs and the previously retained movie unchanged. */
int mm_archive_reader_movie_open(MMArchiveReader *, const MMArchiveMovieOpenSink *, MMArchiveMovieIds *,
                                 MMArchiveDirectoryError *);
/* Borrow the retained movie until the reader closes. */
MMMovieSession *mm_archive_reader_movie(const MMArchiveReader *);

#ifdef __cplusplus
}
#endif
#endif

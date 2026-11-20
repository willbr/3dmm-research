/* Archive read policy converted from libraries/archive/src/archive.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ARCHIVE_READER_H
#define MM_ENGINE_ARCHIVE_READER_H
#include "chunk_storage.h"
#include "movie_data.h"
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
void mm_archive_reader_close(MMArchiveReader *);
int mm_archive_reader_payload(MMArchiveReader *, MMU32 position, MMReadStream *);
int mm_archive_reader_record(MMArchiveReader *, MMU32 position, MMArchiveRecord *);
int mm_archive_reader_strings(MMArchiveReader *, MMU32 position, const MMMovieStringTable **);

/* Also usable with an immutable wire index and a decoded extent. Caption
 * headers require an exact RTXT link at the slot stored in the TBOX, but do
 * not read the linked text payload. */
int mm_archive_decode_record(const MMChunkAccess *, MMU32 position, const MMReadStream *, MMArchiveRecord *);

#ifdef __cplusplus
}
#endif
#endif

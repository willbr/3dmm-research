/* Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_PORTABLE_H
#define MM_ENGINE_MOVIE_PORTABLE_H
#include "chunky_file.h"
#include "movie_archive_edit.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MM_PORTABLE_SOURCE_ID = 6 };
typedef enum MMPortableSourceResult
{
    MM_PORTABLE_SOURCE_OK,
    MM_PORTABLE_SOURCE_INVALID,
    MM_PORTABLE_SOURCE_CONFLICT,
    MM_PORTABLE_SOURCE_WRITE_FAILED,
    MM_PORTABLE_SOURCE_MISSING_ROOT,
    MM_PORTABLE_SOURCE_READ_FAILED
} MMPortableSourceResult;

/* Check every source-6 row using the content VFS alias rules. An existing
 * portable declaration leaves the table intact and skips the receiver. To add
 * a declaration, preserve the bytes and encoding of existing rows. A null
 * stream creates a new GST in the supplied system and byte order. */
MMPortableSourceResult mm_movie_ensure_portable_source(const MMReadStream *, int system, int big_endian,
                                                         MMByteReceiver, void *context);
MMPortableSourceResult mm_movie_archive_require_portable_source(const MMMovieArchiveEditor *);

/* Assign fresh high, even numbers in each chunk-type namespace. Avoid keys in
 * the original archive and all supplied portable indexes. The successful write
 * publishes one new number per old row in numbers. The source snapshot and
 * numbers remain unchanged on failure. */
int mm_movie_rekey_portable_chunks(const MMChunkIndex *, const MMChunkAccess *occupied, size_t occupied_count,
                                  MMU32 seed, MMU32 step, MMU32 *numbers, MMChunkIndexWrite, void *context);

#ifdef __cplusplus
}
#endif
#endif

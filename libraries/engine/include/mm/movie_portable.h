/* Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_PORTABLE_H
#define MM_ENGINE_MOVIE_PORTABLE_H
#include "read_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MM_PORTABLE_SOURCE_ID = 6 };
typedef enum MMPortableSourceResult
{
    MM_PORTABLE_SOURCE_OK,
    MM_PORTABLE_SOURCE_INVALID,
    MM_PORTABLE_SOURCE_CONFLICT,
    MM_PORTABLE_SOURCE_WRITE_FAILED
} MMPortableSourceResult;

/* Check every source-6 row using the content VFS alias rules. An existing
 * portable declaration leaves the table intact and skips the receiver. To add
 * a declaration, preserve the bytes and encoding of existing rows. A null
 * stream creates a new GST in the supplied system and byte order. */
MMPortableSourceResult mm_movie_ensure_portable_source(const MMReadStream *, int system, int big_endian,
                                                         MMByteReceiver, void *context);

#ifdef __cplusplus
}
#endif
#endif

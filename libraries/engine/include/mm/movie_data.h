/* Movie wire decoding converted from the original engine implementation.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_DATA_H
#define MM_ENGINE_MOVIE_DATA_H
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif

    enum MMMovieReadResult
    {
        MM_MOVIE_OK,
        MM_MOVIE_BAD_FILE,
        MM_MOVIE_BAD_VERSION,
        MM_MOVIE_IO,
        MM_MOVIE_MEMORY
    };
    enum
    {
        MM_MOVIE_CURRENT_VERSION = 2,
        MM_MOVIE_BACKWARD_VERSION = 2,
        MM_MOVIE_MINIMUM_VERSION = 1,
        MM_MOVIE_PREFIX_BYTES = 8,
        MM_MOVIE_ACTOR_BYTES = 28
    };
    typedef struct MMMovieHeader
    {
        int system, current_version, backward_version, big_endian;
    } MMMovieHeader;
    typedef struct MMMovieActor
    {
        MMI32 actor_id, reference_count, source_id;
        MMU32 browser_flags, template_type, template_number;
    } MMMovieActor;

    /* Preserve the retail version predicate: current >= minimum and backward <=
     * supported current. Do not impose new relationships between stored versions.
     * Outputs and actor_id_limit are unchanged on failure. The wire pointer slot
     * in a roll-call row is ignored; resource/tag ownership remains with the host. */
    int mm_movie_read_header(const MMReadStream *stream, MMMovieHeader *header);
    int mm_movie_read_actor(const MMReadStream *stream, int big_endian, MMMovieActor *actor,
                                  MMI32 *actor_id_limit);
    void mm_movie_write_actor(const MMMovieActor *, unsigned char bytes[28], int big_endian);

    /* General string-table layout validation used by Archive before the legacy
     * text transcoder. This retains its existing strict, dense-GST policy; Studio
     * roll-call loading still uses the legacy GST reader's acceptance rules.
     * max_bytes is the caller's existing decode budget, not a new engine limit. */
    int mm_movie_validate_strings(const MMReadStream *stream, MMU32 max_bytes);

#ifdef __cplusplus
}
#endif
#endif

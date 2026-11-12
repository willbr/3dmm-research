/* Movie wire decoding converted from the original engine implementation.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_DATA_H
#define THREE_MM_ENGINE_MOVIE_DATA_H
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif

    enum ThreeMMMovieReadResult
    {
        THREE_MM_MOVIE_OK,
        THREE_MM_MOVIE_BAD_FILE,
        THREE_MM_MOVIE_BAD_VERSION,
        THREE_MM_MOVIE_IO,
        THREE_MM_MOVIE_MEMORY
    };
    enum
    {
        THREE_MM_MOVIE_CURRENT_VERSION = 2,
        THREE_MM_MOVIE_BACKWARD_VERSION = 2,
        THREE_MM_MOVIE_MINIMUM_VERSION = 1,
        THREE_MM_MOVIE_PREFIX_BYTES = 8,
        THREE_MM_MOVIE_ACTOR_BYTES = 28
    };
    typedef struct ThreeMMMovieHeader
    {
        int system, current_version, backward_version, big_endian;
    } ThreeMMMovieHeader;
    typedef struct ThreeMMMovieActor
    {
        ThreeMMI32 actor_id, reference_count, source_id;
        ThreeMMU32 browser_flags, template_type, template_number;
    } ThreeMMMovieActor;

    /* Preserve the retail version predicate: current >= minimum and backward <=
     * supported current. Do not impose new relationships between stored versions.
     * Outputs and actor_id_limit are unchanged on failure. The wire pointer slot
     * in a roll-call row is ignored; resource/tag ownership remains with the host. */
    int three_mm_movie_read_header(const ThreeMMReadStream *stream, ThreeMMMovieHeader *header);
    int three_mm_movie_read_actor(const ThreeMMReadStream *stream, int big_endian, ThreeMMMovieActor *actor,
                                  ThreeMMI32 *actor_id_limit);
    void three_mm_movie_write_actor(const ThreeMMMovieActor *, unsigned char bytes[28], int big_endian);

    /* General string-table layout validation used by Archive before the legacy
     * text transcoder. This retains its existing strict, dense-GST policy; Studio
     * roll-call loading still uses the legacy GST reader's acceptance rules.
     * max_bytes is the caller's existing decode budget, not a new engine limit. */
    int three_mm_movie_validate_strings(const ThreeMMReadStream *stream, ThreeMMU32 max_bytes);

#ifdef __cplusplus
}
#endif
#endif

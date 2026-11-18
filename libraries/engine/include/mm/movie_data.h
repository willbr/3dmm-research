/* Movie wire decoding converted from the original engine implementation.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_DATA_H
#define MM_ENGINE_MOVIE_DATA_H
#include "read_stream.h"
#include "string_table.h"
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

    /* Strict, dense general string-table layout validation. max_bytes is the
     * caller's decode budget. Studio roll-call loading has its own acceptance rules. */
    int mm_movie_validate_strings(const MMReadStream *stream, MMU32 max_bytes);

    typedef struct MMMovieStringTable MMMovieStringTable;
    typedef struct MMMovieStringPage
    {
        MMU32 total, extra_bytes, first, count;
    } MMMovieStringPage;
    typedef struct MMMovieStringRow
    {
        const void *text;
        MMU32 units;
        const unsigned char *extra;
    } MMMovieStringRow;

    /* Copy a validated dense GST from the stream and convert it to the host's
     * current string encoding. The stream and converter are used only during
     * open. On failure, out is unchanged. Row pointers are borrowed until close. */
    int mm_movie_string_table_open(const MMReadStream *, MMU32 max_bytes, int encoding,
                                   MMStringConvert, void *context, MMMovieStringTable **out);
    void mm_movie_string_table_close(MMMovieStringTable *);
    void mm_movie_string_table_page(const MMMovieStringTable *, MMU32 first, MMU32 count, MMMovieStringPage *);
    int mm_movie_string_table_row(const MMMovieStringTable *, MMU32 index, MMMovieStringRow *);

#ifdef __cplusplus
}
#endif
#endif

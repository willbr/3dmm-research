/* Movie title policy converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_TITLE_H
#define MM_ENGINE_MOVIE_TITLE_H
#include "string_wire.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMovieTitle
    {
        unsigned short text[256]; /* Aligned storage for up to 255 native 1/2-byte units plus zero. */
        MMU32 count;
        unsigned unit;
    } MMMovieTitle;
    int mm_movie_title_init(MMMovieTitle *, unsigned unit);
    /* Counted copies allow overlap. Invalid input leaves the title unchanged.
     * Text is borrowed until mutation; no encoding/newline normalization occurs. */
    int mm_movie_title_set(MMMovieTitle *, const void *, MMU32 count);
    const void *mm_movie_title_text(const MMMovieTitle *);
    typedef struct MMChunkTitleIO
    {
        void *context;
        int encoding, swapped_unicode_encoding;
        MMI32 (*convert)(void *, const void *, MMU32 bytes, int source_encoding, void *, MMU32 capacity);
        int (*read)(void *file, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written);
        int (*write)(void *file, MMU32 type, MMU32 number, const void *wire, MMU32 bytes);
        void (*bad_string)(void *);
    } MMChunkTitleIO;
    /* Preserve the text codec and bind name I/O to a live MMResourceCache. */
    MMChunkTitleIO mm_resource_cache_title_io(MMChunkTitleIO);
    /* Return 1 for a valid wire name, including an explicitly empty name.
     * Malformed bytes clear the title and report the legacy diagnostic. */
    int mm_movie_title_read_wire(MMMovieTitle *, const void *bytes, MMU32 size, const MMChunkTitleIO *);
    /* A missing stored name clears the title. Empty titles remove the name. */
    void mm_chunk_title_read(MMMovieTitle *, void *file, MMU32 type, MMU32 number, const MMChunkTitleIO *);
    int mm_chunk_title_write(const MMMovieTitle *, void *file, MMU32 type, MMU32 number, const MMChunkTitleIO *);
    void mm_movie_title_strip_extension(MMMovieTitle *);
    typedef struct MMMovieTitleServices
    {
        void *context;
        void (*leaf)(void *, MMMovieTitle *);
        void (*default_name)(void *, MMMovieTitle *);
        void (*notify)(void *, MMMovieTitle *);
    } MMMovieTitleServices;
    /* With no file, retain nonempty title without notifying. A file replaces it
     * with its leaf, strips the last dot suffix (including a leading dot), then
     * chooses the default only if empty. Notification sees published state.
     * Synchronous host callbacks can replace the supplied title. */
    void mm_movie_title_choose(MMMovieTitle *, int has_file, const MMMovieTitleServices *);
#ifdef __cplusplus
}
#endif
#endif

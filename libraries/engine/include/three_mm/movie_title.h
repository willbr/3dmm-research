/* Movie title policy converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_TITLE_H
#define THREE_MM_ENGINE_MOVIE_TITLE_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMMovieTitle
    {
        unsigned short text[256]; /* Aligned storage for up to 255 native 1/2-byte units plus zero. */
        ThreeMMU32 count;
        unsigned unit;
    } ThreeMMMovieTitle;
    int three_mm_movie_title_init(ThreeMMMovieTitle *, unsigned unit);
    /* Counted copies allow overlap. Invalid input leaves the title unchanged.
     * Text is borrowed until mutation; no encoding/newline normalization occurs. */
    int three_mm_movie_title_set(ThreeMMMovieTitle *, const void *, ThreeMMU32 count);
    const void *three_mm_movie_title_text(const ThreeMMMovieTitle *);
    void three_mm_movie_title_strip_extension(ThreeMMMovieTitle *);
    typedef struct ThreeMMMovieTitleServices
    {
        void *context;
        void (*leaf)(void *, ThreeMMMovieTitle *);
        void (*default_name)(void *, ThreeMMMovieTitle *);
        void (*notify)(void *, ThreeMMMovieTitle *);
    } ThreeMMMovieTitleServices;
    /* With no file, retain nonempty title without notifying. A file replaces it
     * with its leaf, strips the last dot suffix (including a leading dot), then
     * chooses the default only if empty. Notification sees published state.
     * Synchronous host callbacks can replace the supplied title. */
    void three_mm_movie_title_choose(ThreeMMMovieTitle *, int has_file, const ThreeMMMovieTitleServices *);
#ifdef __cplusplus
}
#endif
#endif

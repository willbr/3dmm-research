/* Movie root/version policy converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_ROOT_H
#define MM_ENGINE_MOVIE_ROOT_H
#include "movie_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMovieRootServices
    {
        void *context;
        int (*first)(void *, MMU32 *number);
        int (*read)(void *, MMU32 number, MMReadStream *);
        int (*add)(void *, MMU32 bytes, MMU32 *number);
        int (*write)(void *, const void *, MMU32 bytes);
        void (*remove)(void *, MMU32 number);
        void (*error)(void *, int result); /* BAD_VERSION or BAD_FILE, after physical I/O diagnostics. */
    } MMMovieRootServices;
    /* First-root selection publishes number before header validation. A null number
     * still selects the first root. Header is unchanged on failure. Root creation
     * deletes an added chunk after write failure, then publishes the nil number.
     * Add failure retains the physical callback's output, with no extra cleanup. */
    int mm_movie_verify_root(MMU32 *number, MMMovieHeader *, const MMMovieRootServices *);
    int mm_movie_create_root(MMU32 *number, int system, int big_endian, const MMMovieRootServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Movie root/version policy converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_ROOT_H
#define THREE_MM_ENGINE_MOVIE_ROOT_H
#include "movie_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMMovieRootServices
    {
        void *context;
        int (*first)(void *, ThreeMMU32 *number);
        int (*read)(void *, ThreeMMU32 number, ThreeMMReadStream *);
        int (*add)(void *, ThreeMMU32 bytes, ThreeMMU32 *number);
        int (*write)(void *, const void *, ThreeMMU32 bytes);
        void (*remove)(void *, ThreeMMU32 number);
        void (*error)(void *, int result); /* BAD_VERSION or BAD_FILE, after physical I/O diagnostics. */
    } ThreeMMMovieRootServices;
    /* First-root selection publishes number before header validation. A null number
     * still selects the first root. Header is unchanged on failure. Root creation
     * deletes an added chunk after write failure, then publishes the nil number.
     * Add failure retains the physical callback's output, with no extra cleanup. */
    int three_mm_movie_verify_root(ThreeMMU32 *number, ThreeMMMovieHeader *, const ThreeMMMovieRootServices *);
    int three_mm_movie_create_root(ThreeMMU32 *number, int system, int big_endian, const ThreeMMMovieRootServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Movie root/version policy converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_ROOT_H
#define MM_ENGINE_MOVIE_ROOT_H
#include "movie_data.h"
#include "chunk_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    void mm_movie_encode_root(int system, int big_endian, unsigned char bytes[MM_MOVIE_PREFIX_BYTES]);
    /* A failed write discards the new root and sets number to nil. An extent
     * allocation failure uses the optional archive output diagnostic. */
    int mm_movie_create_root(const MMArchiveWriter *, MMU32 *number);
#ifdef __cplusplus
}
#endif
#endif

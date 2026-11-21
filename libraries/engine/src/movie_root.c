/* Movie root/version policy converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_root.h"
static void put16(unsigned char *p, unsigned value, int big)
{
    p[big ? 1 : 0] = (unsigned char)value;
    p[big ? 0 : 1] = (unsigned char)(value >> 8);
}
void mm_movie_encode_root(int system, int big, unsigned char bytes[MM_MOVIE_PREFIX_BYTES])
{
    put16(bytes, 1, big);
    put16(bytes + 2, (unsigned)system, big);
    put16(bytes + 4, MM_MOVIE_CURRENT_VERSION, big);
    put16(bytes + 6, MM_MOVIE_BACKWARD_VERSION, big);
}
int mm_movie_create_root(const MMArchiveWriter *archive, MMU32 *number)
{
    unsigned char bytes[MM_MOVIE_PREFIX_BYTES];
    MMOwnedBlock block;
    MMBlockIO io;
    int ok;
    if (!archive || !archive->payloads || !number ||
        mm_owned_block_init(&block, archive->payloads->handles, archive->payloads->files) != MM_DATA_OK)
        return 0;
    if (mm_archive_add_block(archive, MM_MOVIE_CHUNK, sizeof(bytes), number, &block) != MM_CHUNK_OK)
        return 0;
    mm_movie_encode_root(archive->system, archive->big_endian, bytes);
    ok = mm_owned_block_io(&block, &io) == MM_DATA_OK &&
         mm_block_write(&io, 0, bytes, sizeof(bytes), 0) == MM_DATA_OK;
    mm_owned_block_clear(&block);
    if (!ok)
    {
        mm_chunk_delete(&archive->edit, MM_MOVIE_CHUNK, *number);
        *number = 0xffffffffUL;
    }
    return ok;
}

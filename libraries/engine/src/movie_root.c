/* Movie root/version policy converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_root.h"
static void put16(unsigned char *p, unsigned value, int big)
{
    p[big ? 1 : 0] = (unsigned char)value;
    p[big ? 0 : 1] = (unsigned char)(value >> 8);
}
int mm_movie_verify_root(MMU32 *number, MMMovieHeader *header, const MMMovieRootServices *s)
{
    MMU32 root = number ? *number : 0xffffffffUL;
    MMReadStream stream;
    MMMovieHeader decoded;
    int result;
    if (root == 0xffffffffUL)
    {
        if (!s->first(s->context, &root))
        {
            s->error(s->context, MM_MOVIE_BAD_FILE);
            return 0;
        }
        if (number)
            *number = root;
    }
    if (!s->read(s->context, root, &stream))
    {
        s->error(s->context, MM_MOVIE_BAD_FILE);
        return 0;
    }
    result = mm_movie_read_header(&stream, &decoded);
    if (result != MM_MOVIE_OK)
    {
        s->error(s->context, result == MM_MOVIE_BAD_VERSION ? result : MM_MOVIE_BAD_FILE);
        return 0;
    }
    if (header)
        *header = decoded;
    return 1;
}
void mm_movie_encode_root(int system, int big, unsigned char bytes[MM_MOVIE_PREFIX_BYTES])
{
    put16(bytes, 1, big);
    put16(bytes + 2, (unsigned)system, big);
    put16(bytes + 4, MM_MOVIE_CURRENT_VERSION, big);
    put16(bytes + 6, MM_MOVIE_BACKWARD_VERSION, big);
}
int mm_movie_create_root(MMU32 *number, int system, int big, const MMMovieRootServices *s)
{
    unsigned char bytes[MM_MOVIE_PREFIX_BYTES];
    if (!s->add(s->context, sizeof(bytes), number))
        return 0;
    mm_movie_encode_root(system, big, bytes);
    if (s->write(s->context, bytes, sizeof(bytes)))
        return 1;
    s->remove(s->context, *number);
    *number = 0xffffffffUL;
    return 0;
}

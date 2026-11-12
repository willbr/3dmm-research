/* Movie root/version policy converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/movie_root.h"
static void put16(unsigned char *p, unsigned value, int big)
{
    p[big ? 1 : 0] = (unsigned char)value;
    p[big ? 0 : 1] = (unsigned char)(value >> 8);
}
int three_mm_movie_verify_root(ThreeMMU32 *number, ThreeMMMovieHeader *header, const ThreeMMMovieRootServices *s)
{
    ThreeMMU32 root = number ? *number : 0xffffffffUL;
    ThreeMMReadStream stream;
    ThreeMMMovieHeader decoded;
    int result;
    if (root == 0xffffffffUL)
    {
        if (!s->first(s->context, &root))
        {
            s->error(s->context, THREE_MM_MOVIE_BAD_FILE);
            return 0;
        }
        if (number)
            *number = root;
    }
    if (!s->read(s->context, root, &stream))
    {
        s->error(s->context, THREE_MM_MOVIE_BAD_FILE);
        return 0;
    }
    result = three_mm_movie_read_header(&stream, &decoded);
    if (result != THREE_MM_MOVIE_OK)
    {
        s->error(s->context, result == THREE_MM_MOVIE_BAD_VERSION ? result : THREE_MM_MOVIE_BAD_FILE);
        return 0;
    }
    if (header)
        *header = decoded;
    return 1;
}
int three_mm_movie_create_root(ThreeMMU32 *number, int system, int big, const ThreeMMMovieRootServices *s)
{
    unsigned char bytes[8];
    if (!s->add(s->context, 8, number))
        return 0;
    put16(bytes, 1, big);
    put16(bytes + 2, (unsigned)system, big);
    put16(bytes + 4, THREE_MM_MOVIE_CURRENT_VERSION, big);
    put16(bytes + 6, THREE_MM_MOVIE_BACKWARD_VERSION, big);
    if (s->write(s->context, bytes, 8))
        return 1;
    s->remove(s->context, *number);
    *number = 0xffffffffUL;
    return 0;
}

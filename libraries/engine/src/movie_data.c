/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_data.h"
#include <stdlib.h>

static unsigned int word16(const unsigned char *bytes, int big)
{
    return big ? (unsigned int)bytes[0] * 256U + bytes[1] : (unsigned int)bytes[1] * 256U + bytes[0];
}
static MMU32 word32(const unsigned char *bytes, int big)
{
    MMU32 value = 0;
    int index;
    for (index = 0; index < 4; ++index)
        value = (value << 8) | bytes[big ? index : 3 - index];
    return value;
}
static int signed16(unsigned int value)
{
    return value <= 32767U ? (int)value : -1 - (int)(65535U - value);
}
static MMI32 signed32(MMU32 value)
{
    return value <= 0x7fffffffUL ? (MMI32)value : -1 - (MMI32)((MMU32)0xffffffffUL - value);
}
int mm_movie_read_header(const MMReadStream *stream, MMMovieHeader *header)
{
    unsigned char bytes[MM_MOVIE_PREFIX_BYTES];
    MMMovieHeader result;
    if (!stream || !header || stream->length != sizeof(bytes))
        return MM_MOVIE_BAD_FILE;
    if (!mm_read_exact(stream, 0, bytes, sizeof(bytes)))
        return MM_MOVIE_IO;
    if (!((bytes[0] == 1 && bytes[1] == 0) || (bytes[0] == 0 && bytes[1] == 1)))
        return MM_MOVIE_BAD_FILE;
    result.big_endian = bytes[0] == 0;
    result.system = signed16(word16(bytes + 2, result.big_endian));
    result.current_version = signed16(word16(bytes + 4, result.big_endian));
    result.backward_version = signed16(word16(bytes + 6, result.big_endian));
    if (result.current_version < MM_MOVIE_MINIMUM_VERSION ||
        result.backward_version > MM_MOVIE_CURRENT_VERSION)
        return MM_MOVIE_BAD_VERSION;
    *header = result;
    return MM_MOVIE_OK;
}
int mm_movie_read_actor(const MMReadStream *stream, int big_endian, MMMovieActor *actor,
                              MMI32 *actor_id_limit)
{
    unsigned char bytes[MM_MOVIE_ACTOR_BYTES];
    MMMovieActor result;
    if (!stream || !actor || stream->length != sizeof(bytes))
        return MM_MOVIE_BAD_FILE;
    if (!mm_read_exact(stream, 0, bytes, sizeof(bytes)))
        return MM_MOVIE_IO;
    result.actor_id = signed32(word32(bytes, big_endian));
    result.reference_count = signed32(word32(bytes + 4, big_endian));
    result.browser_flags = word32(bytes + 8, big_endian);
    result.source_id = signed32(word32(bytes + 12, big_endian));
    result.template_type = word32(bytes + 20, big_endian);
    result.template_number = word32(bytes + 24, big_endian);
    if (result.actor_id == -1 || result.actor_id == 0x7fffffffL || result.source_id < 0)
        return MM_MOVIE_BAD_FILE;
    *actor = result;
    if (actor_id_limit && result.actor_id >= *actor_id_limit)
        *actor_id_limit = result.actor_id + 1;
    return MM_MOVIE_OK;
}

static void put_word(unsigned char *p, MMU32 value, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(value >> (i * 8));
}
void mm_movie_write_actor(const MMMovieActor *a, unsigned char bytes[28], int big)
{
    put_word(bytes, (MMU32)a->actor_id, big);
    put_word(bytes + 4, (MMU32)a->reference_count, big);
    put_word(bytes + 8, a->browser_flags, big);
    put_word(bytes + 12, (MMU32)a->source_id, big);
    put_word(bytes + 16, 0, big);
    put_word(bytes + 20, a->template_type, big);
    put_word(bytes + 24, a->template_number, big);
}
int mm_movie_validate_strings(const MMReadStream *stream, MMU32 max_bytes)
{
    unsigned char header[20], bytes[512], offset_bytes[4];
    unsigned char *starts;
    MMU32 stride, count, string_bytes, offset, length, index, at, table_start;
    unsigned int encoding;
    int big, wide, result = MM_MOVIE_BAD_FILE;
    if (!stream || stream->length < sizeof(header) || stream->length > max_bytes)
        return MM_MOVIE_BAD_FILE;
    if (!mm_read_exact(stream, 0, header, sizeof(header)))
        return MM_MOVIE_IO;
    big = header[0] == 0 && header[1] == 1;
    if (!big && !(header[0] == 1 && header[1] == 0))
        return MM_MOVIE_BAD_FILE;
    stride = word32(header + 4, big);
    count = word32(header + 8, big);
    string_bytes = word32(header + 12, big);
    if (stride < 4 || stride > 0x08000000UL || stride % 4 || count > 0x08000000UL || string_bytes > 0x08000000UL ||
        word32(header + 16, big) != 0xffffffffUL || string_bytes > stream->length - 20)
        return MM_MOVIE_BAD_FILE;
    table_start = 20 + string_bytes;
    /* Division/subtraction avoid overflowing count*stride on a C89 machine. */
    if (count != (stream->length - table_start) / stride || (stream->length - table_start) % stride)
        return MM_MOVIE_BAD_FILE;
    encoding = (unsigned int)header[2] + (unsigned int)header[3] * 256U;
    wide = encoding == 0x0404 || encoding == 0x0505;
    if ((!wide && encoding != 0x0202 && encoding != 0x0303) || (wide && string_bytes % 2))
        return MM_MOVIE_BAD_FILE;
    /* One bit per candidate string start, replacing Archive's vector<bool>. */
    length = string_bytes / 8 + (string_bytes % 8 != 0);
    if ((size_t)length != length)
        return MM_MOVIE_MEMORY;
    starts = length ? (unsigned char *)calloc((size_t)length, 1) : NULL;
    if (length && !starts)
        return MM_MOVIE_MEMORY;
    for (offset = 0; offset < string_bytes;)
    {
        starts[offset / 8] |= (unsigned char)(1U << (offset % 8));
        if (!mm_read_exact(stream, 20 + offset, bytes, wide ? 2 : 1))
        {
            result = MM_MOVIE_IO;
            goto done;
        }
        length = wide ? word16(bytes, encoding == 0x0404) : bytes[0];
        if (length > 255 || (length + 1) * (wide ? 2U : 1U) > string_bytes - offset)
            goto done;
        at = length * (wide ? 2U : 1U);
        if (!mm_read_exact(stream, 20 + offset + (wide ? 2U : 1U), bytes, (size_t)at))
        {
            result = MM_MOVIE_IO;
            goto done;
        }
        for (index = 0; index < at; index += wide ? 2 : 1)
            if (!bytes[index] && (!wide || !bytes[index + 1]))
                goto done;
        offset += (length + 1) * (wide ? 2U : 1U);
    }
    for (index = 0; index < count; ++index)
    {
        if (!mm_read_exact(stream, table_start + index * stride, offset_bytes, sizeof(offset_bytes)))
        {
            result = MM_MOVIE_IO;
            goto done;
        }
        offset = word32(offset_bytes, big);
        if (offset >= string_bytes || !(starts[offset / 8] & (1U << (offset % 8))))
            goto done;
    }
    result = MM_MOVIE_OK;
done:
    free(starts);
    return result;
}

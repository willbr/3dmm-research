#include "movie_session_internal.h"
#include "mm/text.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

#define STRING_TABLE_TYPE 0x47535420UL

static int utf8(const unsigned short *units, size_t count, char *out)
{
    size_t i, written = 0;
    MMU32 c, tail;
    for (i = 0; i < count; ++i)
    {
        c = units[i];
        if (c >= 0xd800 && c <= 0xdfff)
        {
            if (c > 0xdbff || i + 1 >= count)
                return 0;
            tail = units[++i];
            if (tail < 0xdc00 || tail > 0xdfff)
                return 0;
            c = 0x10000UL + ((c - 0xd800UL) << 10) + tail - 0xdc00UL;
        }
        if (c >= 0x10000)
        {
            out[written++] = (char)(0xf0 | (c >> 18));
            out[written++] = (char)(0x80 | ((c >> 12) & 63));
        }
        else if (c >= 0x800)
            out[written++] = (char)(0xe0 | (c >> 12));
        else if (c >= 0x80)
            out[written++] = (char)(0xc0 | (c >> 6));
        if (c >= 0x800)
            out[written++] = (char)(0x80 | ((c >> 6) & 63));
        out[written++] = (char)(c < 0x80 ? c : 0x80 | (c & 63));
    }
    out[written] = 0;
    return 1;
}

static int find_source(const MMReadStream *stream, MMI32 sid, char *out)
{
    MMCollectionHeader header;
    unsigned char bytes[510], row[8];
    unsigned short units[255];
    MMU32 table, offset, i;
    size_t unit, count;
    char name[MM_MOVIE_SOURCE_NAME_BYTES];
    int status = mm_movie_validate_strings(stream, 0x7fffffffUL);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_collection_header_read(stream, MM_COLLECTION_STRINGS, 0, &header);
    if (status != MM_STORAGE_OK || header.width != sizeof(row))
        return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_BAD_FILE;
    unit = header.system == MM_TEXT_UTF16_BE || header.system == MM_TEXT_UTF16_LE ? 2 : 1;
    table = header.header_bytes + header.first_bytes;
    for (i = 0; i < header.count; ++i)
    {
        if (!mm_read_exact(stream, table + i * sizeof(row), row, sizeof(row)))
            return MM_MOVIE_IO;
        if (mm_signed_word(mm_session_word(row + 4, header.big_endian)) != sid)
            continue;
        offset = header.header_bytes + mm_session_word(row, header.big_endian);
        if (!mm_read_exact(stream, offset, bytes, unit))
            return MM_MOVIE_IO;
        count = unit == 1 ? bytes[0] : header.system == MM_TEXT_UTF16_BE
                                            ? (size_t)bytes[0] * 256 + bytes[1]
                                            : (size_t)bytes[1] * 256 + bytes[0];
        if (!count || count > sizeof(units) / sizeof(units[0]))
            return MM_MOVIE_BAD_FILE;
        if (!mm_read_exact(stream, offset + unit, bytes, count * unit))
            return MM_MOVIE_IO;
        if (mm_text_utf16(NULL, bytes, count * unit, header.system, units, count) != count ||
            !utf8(units, count, name))
            return MM_MOVIE_BAD_FILE;
        memcpy(out, name, strlen(name) + 1);
        return MM_MOVIE_OK;
    }
    return MM_MOVIE_ASSET_NOT_FOUND;
}

int mm_movie_session_source_name(const MMMovieSession *movie, MMI32 source, char *name)
{
    MMChunkAccess access;
    MMChunkEntry entry;
    MMMoviePayload payload;
    MMU32 position;
    int status;
    if (!movie || !name || source <= 0)
        return MM_MOVIE_BAD_FILE;
    access = mm_chunk_index_access(movie->index);
    status = mm_session_child(&access, movie->root_position, 1, STRING_TABLE_TYPE, &entry, &position);
    if (status == MM_CHUNK_NOT_FOUND)
    {
        if (source != 2)
            return MM_MOVIE_ASSET_NOT_FOUND;
        memcpy(name, "3dmovie", sizeof("3dmovie"));
        return MM_MOVIE_OK;
    }
    if (status != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = mm_session_payload(&movie->stream, movie->index, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    status = find_source(payload.stream, source, name);
    free(payload.decoded);
    return status;
}

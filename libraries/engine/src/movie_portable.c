/* Licensed under the MIT License. */
#include "mm/movie_portable.h"
#include "mm/movie_session.h"
#include "mm/collections.h"
#include "mm/content_vfs.h"
#include "mm/text.h"
#include <stdlib.h>
#include <string.h>

static const char portable_name[] = "Virtual3dmm/v3dmm";

static void put(unsigned char *bytes, MMU32 value, unsigned width, int big_endian)
{
    unsigned i;
    for (i = 0; i < width; ++i)
        bytes[big_endian ? width - i - 1 : i] = (unsigned char)(value >> (i * 8));
}

static MMPortableSourceResult check_sources(const MMReadStream *stream, int *present)
{
    MMMovieSourceTable table;
    MMMovieSourceName row;
    char name[MM_MOVIE_SOURCE_NAME_BYTES], key[sizeof(name)];
    MMU32 i;
    if (mm_movie_source_table_open(stream, 0x7fffffffUL, &table) != MM_MOVIE_OK)
        return MM_PORTABLE_SOURCE_INVALID;
    for (i = 0; i < table.count; ++i)
    {
        if (mm_movie_source_table_at(&table, i, &row) != MM_MOVIE_OK)
            return MM_PORTABLE_SOURCE_INVALID;
        if (row.id != MM_PORTABLE_SOURCE_ID)
            continue;
        if (mm_movie_source_name_utf8(&row, name) != MM_MOVIE_OK ||
            !mm_vfs_source_key(name, NULL, key, sizeof(key)) || strcmp(key, "v3dmm"))
            return MM_PORTABLE_SOURCE_CONFLICT;
        *present = 1;
    }
    return MM_PORTABLE_SOURCE_OK;
}

MMPortableSourceResult mm_movie_ensure_portable_source(const MMReadStream *stream, int system, int big_endian,
                                                        MMByteReceiver receive, void *context)
{
    MMCollectionHeader header = {0};
    MMU32 old_bytes, old_rows, length, i;
    unsigned unit;
    int present = 0, string_big;
    unsigned char *data, *text, *row;
    MMPortableSourceResult result;
    if (stream)
    {
        result = check_sources(stream, &present);
        if (result != MM_PORTABLE_SOURCE_OK || present)
            return result;
        if (mm_collection_header_read(stream, MM_COLLECTION_STRINGS, 0, &header) != MM_STORAGE_OK)
            return MM_PORTABLE_SOURCE_INVALID;
    }
    else
    {
        header.kind = MM_COLLECTION_STRINGS;
        header.width = 8;
        header.free_count = -1;
        header.system = system;
        header.big_endian = big_endian;
    }
    string_big = header.system == MM_TEXT_UTF16_BE;
    if (string_big || header.system == MM_TEXT_UTF16_LE)
        unit = 2;
    else if (header.system == MM_TEXT_MAC || header.system == MM_TEXT_WINDOWS)
        unit = 1;
    else
        return MM_PORTABLE_SOURCE_INVALID;
    old_bytes = header.bytes;
    old_rows = header.second_bytes;
    header.bytes += unit * sizeof(portable_name);
    ++header.count;
    if (mm_collection_layout(&header) != MM_STORAGE_OK)
        return MM_PORTABLE_SOURCE_WRITE_FAILED;
    length = header.header_bytes + header.first_bytes + header.second_bytes;
    if ((size_t)length != length)
        return MM_PORTABLE_SOURCE_WRITE_FAILED;
    data = (unsigned char *)malloc((size_t)length);
    if (!data)
        return MM_PORTABLE_SOURCE_WRITE_FAILED;
    result = MM_PORTABLE_SOURCE_WRITE_FAILED;
    if (mm_collection_header_write(&header, data, length) != MM_STORAGE_OK)
        goto done;
    text = data + header.header_bytes;
    row = text + header.first_bytes;
    if (stream && (!mm_read_exact(stream, header.header_bytes, text, old_bytes) ||
                   !mm_read_exact(stream, header.header_bytes + old_bytes, row, old_rows)))
    {
        result = MM_PORTABLE_SOURCE_INVALID;
        goto done;
    }
    text += old_bytes;
    put(text, sizeof(portable_name) - 1, unit, string_big);
    for (i = 0; i < sizeof(portable_name) - 1; ++i)
        put(text + (i + 1) * unit, (unsigned char)portable_name[i], unit, string_big);
    row += old_rows;
    put(row, old_bytes, 4, header.big_endian);
    put(row + 4, MM_PORTABLE_SOURCE_ID, 4, header.big_endian);
    if (receive(context, data, length))
        result = MM_PORTABLE_SOURCE_OK;
done:
    free(data);
    return result;
}

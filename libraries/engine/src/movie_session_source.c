#include "movie_session_internal.h"
#include "mm/text.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

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

int mm_movie_source_table_open(const MMReadStream *stream, MMU32 max_bytes, MMMovieSourceTable *out)
{
    MMCollectionHeader header;
    MMMovieSourceTable table;
    int status;
    if (!out)
        return MM_MOVIE_BAD_FILE;
    status = mm_movie_validate_strings(stream, max_bytes);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_collection_header_read(stream, MM_COLLECTION_STRINGS, 0, &header);
    if (status != MM_STORAGE_OK || header.width != 8)
        return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_BAD_FILE;
    table.stream = stream;
    table.count = header.count;
    table.strings_offset = header.header_bytes;
    table.rows_offset = header.header_bytes + header.first_bytes;
    table.unit = header.system == MM_TEXT_UTF16_BE || header.system == MM_TEXT_UTF16_LE ? 2 : 1;
    table.big_endian = header.big_endian;
    table.encoding = header.system;
    *out = table;
    return MM_MOVIE_OK;
}

static int row_header(const MMMovieSourceTable *table, MMU32 row, MMU32 *offset, MMI32 *sid)
{
    unsigned char bytes[8];
    if (!table || row >= table->count)
        return MM_MOVIE_ASSET_NOT_FOUND;
    if (!mm_read_exact(table->stream, table->rows_offset + row * sizeof(bytes), bytes, sizeof(bytes)))
        return MM_MOVIE_IO;
    *offset = table->strings_offset + mm_session_word(bytes, table->big_endian);
    *sid = mm_signed_word(mm_session_word(bytes + 4, table->big_endian));
    return MM_MOVIE_OK;
}

static int read_name(const MMMovieSourceTable *table, MMU32 offset, MMI32 sid, MMMovieSourceName *out)
{
    MMMovieSourceName name;
    unsigned char length[2];
    MMU32 count;
    if (!mm_read_exact(table->stream, offset, length, table->unit))
        return MM_MOVIE_IO;
    count = table->unit == 1 ? length[0] : table->encoding == MM_TEXT_UTF16_BE
                                          ? (MMU32)length[0] * 256 + length[1]
                                          : (MMU32)length[1] * 256 + length[0];
    if (count > 255)
        return MM_MOVIE_BAD_FILE;
    memset(&name, 0, sizeof(name));
    name.id = sid;
    name.encoding = table->encoding;
    name.size = count * table->unit;
    if (!mm_read_exact(table->stream, offset + table->unit, name.bytes, name.size))
        return MM_MOVIE_IO;
    *out = name;
    return MM_MOVIE_OK;
}

int mm_movie_source_table_at(const MMMovieSourceTable *table, MMU32 row, MMMovieSourceName *out)
{
    MMU32 offset;
    MMI32 sid;
    int status;
    if (!out)
        return MM_MOVIE_BAD_FILE;
    status = row_header(table, row, &offset, &sid);
    return status == MM_MOVIE_OK ? read_name(table, offset, sid, out) : status;
}

int mm_movie_source_name_utf8(const MMMovieSourceName *row, char out[MM_MOVIE_SOURCE_NAME_BYTES])
{
    unsigned short units[255];
    char name[MM_MOVIE_SOURCE_NAME_BYTES];
    size_t count;
    if (!row || !out || row->size > sizeof(row->bytes))
        return MM_MOVIE_BAD_FILE;
    count = mm_text_utf16(NULL, row->bytes, row->size, row->encoding, units, sizeof(units) / sizeof(units[0]));
    if (!count || !utf8(units, count, name))
        return MM_MOVIE_BAD_FILE;
    memcpy(out, name, strlen(name) + 1);
    return MM_MOVIE_OK;
}

static int find_source(const MMReadStream *stream, MMI32 sid, char *out)
{
    MMMovieSourceTable table;
    MMMovieSourceName row;
    MMU32 offset, i;
    MMI32 id;
    int status = mm_movie_source_table_open(stream, 0x7fffffffUL, &table);
    if (status != MM_MOVIE_OK)
        return status;
    for (i = 0; i < table.count; ++i)
    {
        status = row_header(&table, i, &offset, &id);
        if (status != MM_MOVIE_OK)
            return status;
        if (id != sid)
            continue;
        status = read_name(&table, offset, id, &row);
        if (status != MM_MOVIE_OK)
            return status;
        return mm_movie_source_name_utf8(&row, out);
    }
    return MM_MOVIE_ASSET_NOT_FOUND;
}

int mm_movie_session_source_name(const MMMovieSession *movie, MMI32 source, char *name)
{
    MMMovieArchive archive;
    MMChunkEntry entry;
    MMMoviePayload payload;
    MMU32 position;
    int status;
    if (!movie || !name || source <= 0)
        return MM_MOVIE_BAD_FILE;
    status = mm_session_archive(movie, NULL, NULL, &archive);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_movie_session_source_table(movie, &entry, &position);
    if (status == MM_MOVIE_ASSET_NOT_FOUND)
    {
        if (source != MM_MOVIE_RETAIL_SOURCE_ID)
            return MM_MOVIE_ASSET_NOT_FOUND;
        memcpy(name, MM_MOVIE_RETAIL_SOURCE_NAME, sizeof(MM_MOVIE_RETAIL_SOURCE_NAME));
        return MM_MOVIE_OK;
    }
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_session_payload(&archive, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    status = find_source(payload.stream, source, name);
    free(payload.decoded);
    return status;
}

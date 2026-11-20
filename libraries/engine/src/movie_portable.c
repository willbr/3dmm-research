/* Licensed under the MIT License. */
#include "mm/movie_portable.h"
#include "mm/movie_session.h"
#include "mm/collections.h"
#include "mm/content_vfs.h"
#include "mm/text.h"
#include <stdlib.h>
#include <string.h>

static const char portable_name[] = "Virtual3dmm/v3dmm";
#define MOVIE_TYPE 0x4d564945UL
#define SOURCE_TABLE_TYPE 0x47535420UL

typedef struct PortableSourceEdit
{
    const MMMovieArchiveEditor *editor;
    MMChunkChild previous;
    MMU32 movie;
    int had_source;
} PortableSourceEdit;

static int receive_source(void *context, const unsigned char *data, MMU32 bytes)
{
    const PortableSourceEdit *edit = (const PortableSourceEdit *)context;
    return mm_movie_archive_replace_child(edit->editor, MOVIE_TYPE, edit->movie, &edit->previous,
                                          edit->had_source, data, bytes);
}

MMPortableSourceResult mm_movie_archive_require_portable_source(const MMMovieArchiveEditor *editor)
{
    MMChunkAccess chunks;
    MMChunkEntry root;
    MMReadStream stream;
    PortableSourceEdit edit = {0};
    MMU32 position;
    chunks = mm_chunk_edit_access(&editor->edit);
    if (mm_chunk_type_at(&chunks, MOVIE_TYPE, 0, &root, &position) != MM_CHUNK_OK)
        return MM_PORTABLE_SOURCE_MISSING_ROOT;
    edit.editor = editor;
    edit.movie = root.number;
    edit.previous.type = SOURCE_TABLE_TYPE;
    edit.previous.slot = 1;
    edit.had_source = mm_chunk_link_number(&chunks, MOVIE_TYPE, root.number, 1, SOURCE_TABLE_TYPE,
                                            &edit.previous.number) == MM_CHUNK_OK;
    if (edit.had_source && !editor->read(editor->context, SOURCE_TABLE_TYPE, edit.previous.number, &stream))
        return MM_PORTABLE_SOURCE_READ_FAILED;
    return mm_movie_ensure_portable_source(edit.had_source ? &stream : NULL, editor->system, editor->big_endian,
                                           receive_source, &edit);
}

static int portable_key_status(const MMChunkAccess *source, const MMChunkAccess *occupied,
                               size_t count, MMU32 type, MMU32 number)
{
    MMU32 position;
    size_t i;
    int status = mm_chunk_find(source, type, number, &position);
    for (i = 0; i < count && status == MM_CHUNK_NOT_FOUND; ++i)
        status = mm_chunk_find(&occupied[i], type, number, &position);
    return status;
}

int mm_movie_rekey_portable_chunks(const MMChunkIndex *index, const MMChunkAccess *occupied, size_t occupied_count,
                                  MMU32 seed, MMU32 step, MMU32 *numbers, MMChunkIndexWrite writer, void *context)
{
    const MMU32 mask = 0x3fffffffUL;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMU32 *planned, position, type = 0, slot = 0, stride = 0, visited = 0, number;
    size_t i;
    int status;
    if (!index || (occupied_count && !occupied) || !writer)
        return MM_CHUNK_BAD_FILE;
    access = mm_chunk_index_access(index);
    if (!mm_chunk_keys_ordered(&access) || (access.count && !numbers))
        return MM_CHUNK_BAD_FILE;
    for (i = 0; i < occupied_count; ++i)
        if (!mm_chunk_keys_ordered(&occupied[i]))
            return MM_CHUNK_BAD_FILE;
    if (!access.count)
        return mm_chunk_index_rekey(index, NULL, writer, context);
    if ((size_t)access.count > (size_t)-1 / sizeof(*planned))
        return MM_CHUNK_MEMORY;
    planned = (MMU32 *)malloc((size_t)access.count * sizeof(*planned));
    if (!planned)
        return MM_CHUNK_MEMORY;
    for (position = 0; position < access.count; ++position)
    {
        status = mm_chunk_at(&access, position, &entry);
        if (status != MM_CHUNK_OK)
            goto done;
        if (!position || entry.type != type)
        {
            type = entry.type;
            slot = (seed ^ type) & mask;
            /* An odd stride visits each slot exactly once per type. */
            stride = ((step ^ (type >> 1)) & mask) | 1U;
            visited = 0;
        }
        do
        {
            if (visited > mask)
            {
                status = MM_CHUNK_BAD_FILE;
                goto done;
            }
            number = 0x80000000UL | (slot << 1);
            slot = (slot + stride) & mask;
            ++visited;
            status = portable_key_status(&access, occupied, occupied_count, type, number);
        } while (status == MM_CHUNK_OK);
        if (status != MM_CHUNK_NOT_FOUND)
            goto done;
        planned[position] = number;
    }
    status = mm_chunk_index_rekey(index, planned, writer, context);
    if (status == MM_CHUNK_OK)
        memcpy(numbers, planned, (size_t)access.count * sizeof(*planned));
done:
    free(planned);
    return status;
}

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

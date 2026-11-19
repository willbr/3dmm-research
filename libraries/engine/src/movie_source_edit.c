#include "mm/movie_archive_edit.h"
#include "mm/content_vfs.h"
#include "mm/movie_session.h"
#include "mm/rollcall_storage.h"
#include "mm/string_ops.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

#define MOVIE_TYPE 0x4d564945UL
#define SOURCE_TABLE_TYPE 0x47535420UL
#define SOURCE_TABLE_SLOT 1

static const char retail_name[] = "3D Movie Maker/3DMOVIE";

static MMI32 read_id(const unsigned char bytes[4], int big)
{
    MMU32 id;
    int i;
    id = 0;
    for (i = 0; i < 4; ++i)
        id = (id << 8) | bytes[big ? i : 3 - i];
    return mm_signed_word(id);
}

static void write_id(unsigned char bytes[4], MMI32 id, int big)
{
    MMU32 word;
    int i;
    word = (MMU32)id;
    for (i = 0; i < 4; ++i)
    {
        bytes[big ? 3 - i : i] = (unsigned char)word;
        word >>= 8;
    }
}

static int append_source(MMRollCallTable *table, const MMTextHost *host, const char *name, MMI32 id, int big)
{
    unsigned short units[257];
    MMStringValue text = {0};
    unsigned char bytes[4];
    const unsigned char *chars;
    MMU32 count;
    text.buffer = units;
    text.unit = table->unit;
    mm_string_from_ansi(&text, host, name, strlen(name));
    chars = (const unsigned char *)units;
    count = table->unit == 1 ? chars[0] : units[0];
    write_id(bytes, id, big);
    return mm_rollcall_table_add(table, chars + table->unit, count, bytes);
}

static int match_source(MMRollCallTable *table, const MMMovieArchiveEditor *editor, const MMTextHost *host,
                        const char *key, int input_big, MMI32 *matched, MMI32 *largest)
{
    unsigned short units[257];
    MMStringValue text = {0};
    char ansi[256], folded[sizeof(ansi) + 32];
    unsigned char bytes[4];
    const void *name;
    MMU32 row, characters;
    MMI32 id;
    text.buffer = units;
    text.unit = table->unit;
    for (row = 0; row < table->count; ++row)
    {
        if (!mm_rollcall_table_extra(table, row, bytes) || !mm_rollcall_table_name(table, row, &name, &characters))
            return 0;
        id = read_id(bytes, input_big);
        if (input_big != editor->big_endian)
        {
            write_id(bytes, id, editor->big_endian);
            if (!mm_rollcall_table_set_extra(table, row, bytes))
                return 0;
        }
        if (id > *largest)
            *largest = id;
        if (id <= 0)
            continue;
        mm_string_value_set(&text, name, characters);
        mm_string_to_ansi(&text, host, ansi);
        folded[0] = '\0';
        mm_vfs_source_key(ansi, NULL, folded, sizeof(folded));
        if (!strcmp(folded, key))
            *matched = id;
    }
    return 1;
}

int mm_movie_archive_register_source(const MMMovieArchiveEditor *editor, const MMTextHost *host, MMU32 movie,
                                     MMI32 fallback_retail, const char *name, MMI32 *source_id)
{
    MMChunkAccess chunks;
    MMReadStream stream;
    MMStorageWriter writer;
    MMRollCallTable table;
    MMU32 old_number, number, size;
    MMI32 id, largest;
    int present, result, input_big;
    char *key;
    size_t capacity;
    if (!name)
        name = retail_name;
    capacity = strlen(name);
    if (capacity > (size_t)-1 - 32)
        return MM_MOVIE_SOURCE_EDIT_MEMORY;
    capacity += 32;
    chunks = editor->chunks(editor->context);
    present = mm_chunk_link_number(&chunks, MOVIE_TYPE, movie, SOURCE_TABLE_SLOT, SOURCE_TABLE_TYPE,
                                   &old_number) == MM_CHUNK_OK;
    if (present && !editor->read(editor->context, SOURCE_TABLE_TYPE, old_number, &stream))
        return MM_MOVIE_SOURCE_EDIT_READ;
    key = (char *)malloc(capacity);
    if (!key)
        return MM_MOVIE_SOURCE_EDIT_MEMORY;
    key[0] = '\0';
    mm_vfs_source_key(name, NULL, key, capacity);
    if (mm_rollcall_table_init(&table, 4, editor->text_unit, editor->system, 0, NULL) != MM_STORAGE_OK)
    {
        free(key);
        return MM_MOVIE_SOURCE_EDIT_INVALID;
    }
    id = -1;
    largest = MM_MOVIE_RETAIL_SOURCE_ID;
    if (present)
    {
        result = MM_MOVIE_SOURCE_EDIT_INVALID;
        if (mm_rollcall_table_read(&table, &stream, editor->convert_text, editor->context, &input_big) !=
                MM_STORAGE_OK || table.stride != 8 ||
            !match_source(&table, editor, host, key, input_big, &id, &largest))
            goto done;
    }
    else
    {
        result = MM_MOVIE_SOURCE_EDIT_MEMORY;
        if (!append_source(&table, host, retail_name, fallback_retail, editor->big_endian))
            goto done;
        if (!strcmp(key, MM_MOVIE_RETAIL_SOURCE_NAME))
            id = fallback_retail;
    }
    if (id < 0)
    {
        result = MM_MOVIE_SOURCE_EDIT_IDS;
        if (largest == 0x7fffffffL)
            goto done;
        id = largest + 1;
        result = MM_MOVIE_SOURCE_EDIT_MEMORY;
        if (!append_source(&table, host, name, id, editor->big_endian))
            goto done;
    }
    result = MM_MOVIE_SOURCE_EDIT_WRITE;
    size = mm_rollcall_table_wire_size(&table);
    if (!size || !editor->add(editor->context, SOURCE_TABLE_TYPE, size, &number, &writer) ||
        mm_rollcall_table_write(&table, &writer, editor->big_endian) != MM_STORAGE_OK)
        goto done;
    if (present)
        editor->unlink(editor->context, MOVIE_TYPE, movie, SOURCE_TABLE_TYPE, old_number, SOURCE_TABLE_SLOT);
    result = MM_MOVIE_SOURCE_EDIT_LINK;
    if (!editor->adopt(editor->context, MOVIE_TYPE, movie, SOURCE_TABLE_TYPE, number, SOURCE_TABLE_SLOT))
        goto done;
    *source_id = id;
    result = MM_MOVIE_SOURCE_EDIT_OK;
done:
    mm_rollcall_table_dispose(&table);
    free(key);
    return result;
}

static int source_order(const void *a, const void *b)
{
    const MMMovieArchiveSource *left = (const MMMovieArchiveSource *)a;
    const MMMovieArchiveSource *right = (const MMMovieArchiveSource *)b;
    if (left->id != right->id)
        return left->id < right->id ? -1 : 1;
    return left->ordinal < right->ordinal ? -1 : left->ordinal != right->ordinal;
}

void mm_movie_archive_prepare_source_map(MMMovieArchiveSourceMap *map)
{
    size_t i;
    for (i = 0; i < map->count; ++i)
    {
        map->sources[i].mapped = 0;
        map->sources[i].ordinal = i;
    }
    if (map->count > 1)
        qsort(map->sources, map->count, sizeof(*map->sources), source_order);
    map->result = MM_MOVIE_SOURCE_EDIT_OK;
}

int mm_movie_archive_map_source(void *context, MMI32 id, MMI32 *out)
{
    MMMovieArchiveSourceMap *map = (MMMovieArchiveSourceMap *)context;
    MMMovieArchiveSource *source;
    size_t first, end, mid;
    first = 0;
    end = map->count;
    while (first < end)
    {
        mid = first + (end - first) / 2;
        if (map->sources[mid].id < id)
            first = mid + 1;
        else
            end = mid;
    }
    if (first == map->count || map->sources[first].id != id)
    {
        map->result = MM_MOVIE_SOURCE_EDIT_UNKNOWN;
        return 0;
    }
    source = &map->sources[first];
    map->result = source->mapped > 0 ? MM_MOVIE_SOURCE_EDIT_OK :
        mm_movie_archive_register_source(map->destination, map->text, map->movie, map->fallback_retail,
                                          source->name, &source->mapped);
    if (map->result != MM_MOVIE_SOURCE_EDIT_OK)
        return 0;
    *out = source->mapped;
    return 1;
}

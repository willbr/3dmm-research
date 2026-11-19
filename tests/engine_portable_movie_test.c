/* Licensed under the MIT License. */
#include "mm/chunk_edit.h"
#include "mm/collections.h"
#include "mm/movie_archive_edit.h"
#include "mm/movie_portable.h"
#include "mm/movie_session.h"
#include "mm/scene_data.h"
#include "mm/text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Output
{
    unsigned char data[160];
    MMU32 length;
    int calls;
} Output;

static int receive(void *context, const unsigned char *data, MMU32 length)
{
    Output *out = (Output *)context;
    assert(length <= sizeof(out->data));
    memcpy(out->data, data, length);
    out->length = length;
    ++out->calls;
    return 1;
}

static void word(unsigned char *bytes, MMU32 value, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i) bytes[big ? 3 - i : i] = (unsigned char)(value >> (8 * i));
}

static void start_events(int big)
{
    MMCollectionHeader header = {0};
    unsigned char input[104] = {0}, expected[104];
    unsigned char *records = input + 20;
    MMMemoryStream stream;
    Output out = {0};
    header.kind = MM_COLLECTION_GROUP;
    header.count = 3;
    header.bytes = 60;
    header.width = 8;
    header.free_count = -1;
    header.big_endian = big;
    header.system = MM_TEXT_WINDOWS;
    assert(mm_collection_header_write(&header, input, sizeof(input)) == MM_STORAGE_OK);
    /* Physical row order differs from event order. */
    word(records + 4, MM_SCENE_SET_BACKGROUND, big);
    word(records + 8, 2, big);
    word(records + 12, 0x12345678, big);
    word(records + 16, 0x424b4744UL, big);
    word(records + 20, 14, big);
    word(records + 28, MM_SCENE_ADD_ACTOR, big);
    word(records + 32, 9, big);
    memcpy(records + 36, records, 24);
    word(records + 60, 24, big);
    word(records + 64, 12, big);
    word(records + 68, 36, big);
    word(records + 72, 24, big);
    word(records + 76, 0, big);
    word(records + 80, 24, big);
    memcpy(expected, input, sizeof(input));
    word(expected + 28, 6, big);
    word(expected + 32, 0, big);
    word(expected + 40, 0x87654322UL, big);
    memcpy(expected + 64, expected + 28, 16);
    assert(mm_memory_stream_init(&stream, input, sizeof(input)));
    assert(mm_scene_retarget_start_background(&stream.stream, 6, 0x87654322UL, receive, &out));
    assert(out.calls == 1 && out.length == sizeof(input) && !memcmp(expected, out.data, sizeof(input)));
    assert(memcmp(out.data, input, sizeof(input)));
    word(records + 80, 23, big);
    assert(!mm_scene_retarget_start_background(&stream.stream, 6, 0, receive, &out));
    assert(out.calls == 1);
}

static void sources(int big, int system)
{
    unsigned char input[40] = {0};
    unsigned unit = system == MM_TEXT_UTF16_BE ? 2 : 1;
    MMCollectionHeader header = {0};
    MMMemoryStream stream, saved;
    MMMovieSourceTable table;
    MMMovieSourceName row;
    char name[MM_MOVIE_SOURCE_NAME_BYTES];
    Output out = {0};
    header.kind = MM_COLLECTION_STRINGS;
    header.width = 8;
    header.count = 1;
    header.bytes = 2 * unit;
    header.free_count = -1;
    header.system = system;
    header.big_endian = big;
    assert(mm_collection_header_write(&header, input, sizeof(input)) == MM_STORAGE_OK);
    input[20 + unit - 1] = 1;
    input[20 + 2 * unit - 1] = 'X';
    word(input + 24 + 2 * unit, 2, big);
    assert(mm_memory_stream_init(&stream, input, 28 + 2 * unit));
    assert(mm_movie_ensure_portable_source(&stream.stream, MM_TEXT_MAC, !big, receive, &out) == MM_PORTABLE_SOURCE_OK);
    assert(out.calls == 1 && mm_memory_stream_init(&saved, out.data, out.length));
    assert(mm_movie_source_table_open(&saved.stream, sizeof(out.data), &table) == MM_MOVIE_OK &&
        table.count == 2 && table.big_endian == big && table.encoding == system);
    assert(mm_movie_source_table_at(&table, 0, &row) == MM_MOVIE_OK &&
        row.id == 2 && row.size == unit && !memcmp(row.bytes, input + 20 + unit, unit));
    assert(mm_movie_source_table_at(&table, 1, &row) == MM_MOVIE_OK && row.id == MM_PORTABLE_SOURCE_ID &&
        mm_movie_source_name_utf8(&row, name) == MM_MOVIE_OK && !strcmp(name, "Virtual3dmm/v3dmm"));
    assert(mm_movie_ensure_portable_source(&saved.stream, system, big, receive, &out) == MM_PORTABLE_SOURCE_OK && out.calls == 1);
    word(out.data + table.rows_offset + 4, MM_PORTABLE_SOURCE_ID, big);
    assert(mm_movie_ensure_portable_source(&saved.stream, system, big, receive, &out) == MM_PORTABLE_SOURCE_CONFLICT &&
        out.calls == 1);
    assert(mm_movie_ensure_portable_source(NULL, system, big, receive, &out) == MM_PORTABLE_SOURCE_OK && out.calls == 2);
    assert(mm_memory_stream_init(&saved, out.data, out.length) &&
        mm_movie_source_table_open(&saved.stream, sizeof(out.data), &table) == MM_MOVIE_OK && table.count == 1);
}

typedef struct Links
{
    char calls[4];
    int count, fail;
} Links;
static int write_child(void *context, MMU32 type, const unsigned char *data, MMU32 size, MMU32 *number)
{
    Links *links = (Links *)context;
    assert(type == 1 && *data == 99 && size == 1);
    links->calls[links->count++] = 'W';
    *number = 12;
    return links->fail != 1;
}
static int adopt_child(void *context, MMU32 type, MMU32 number, const MMChunkChild *child)
{
    Links *links = (Links *)context;
    assert(type == 2 && number == 3 && child->type == 1 && child->number == 12 && child->slot == 1);
    links->calls[links->count++] = 'A';
    return links->fail != 2;
}
static void remove_child(void *context, MMU32 type, MMU32 number, const MMChunkChild *child, int linked)
{
    Links *links = (Links *)context;
    assert(type == 2 && number == 3 && child->type == 1 && child->slot == 1);
    assert(child->number == (linked ? 11 : 12));
    links->calls[links->count++] = linked ? 'L' : 'D';
}
static void replacement(void)
{
    Links links = {{0}, 0, 0};
    MMChunkChildPayload services = {NULL, write_child, adopt_child, remove_child};
    MMChunkChild child = {1, 11, 1};
    unsigned char data = 99;
    services.context = &links;
    assert(mm_chunk_replace_child_bytes(2, 3, &child, 1, &data, 1, &services) &&
        links.count == 3 && !memcmp(links.calls, "WAL", 3));
    links.count = 0;
    links.fail = 1;
    assert(!mm_chunk_replace_child_bytes(2, 3, &child, 1, &data, 1, &services) && links.count == 1);
    links.count = 0;
    links.fail = 2;
    assert(!mm_chunk_replace_child_bytes(2, 3, &child, 1, &data, 1, &services) &&
        links.count == 3 && !memcmp(links.calls, "WAD", 3));
}

#define MOVIE_TYPE 0x4d564945UL
#define SOURCE_TYPE 0x47535420UL
typedef struct SourceEdit
{
    MMMemoryStream input;
    Output output;
    int has_table, fail_link, step;
} SourceEdit;

static int source_root(void *context, MMU32 position, MMChunkEntry *entry)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(position == 0);
    memset(entry, 0, sizeof(*entry));
    entry->type = MOVIE_TYPE;
    entry->number = 2;
    entry->child_count = edit->has_table;
    return 1;
}
static int source_child(void *context, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    assert(parent == 0 && position == 0 && ((SourceEdit *)context)->has_table);
    child->type = SOURCE_TYPE;
    child->number = 7;
    child->slot = 1;
    return 1;
}
static MMChunkAccess source_chunks(void *context)
{
    MMChunkAccess chunks = {NULL, 1, source_root, source_child};
    chunks.context = context;
    return chunks;
}
static int source_read(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(type == SOURCE_TYPE && number == 7 && edit->has_table && edit->step == 0);
    *stream = edit->input.stream;
    return 1;
}
static int source_write(void *context, MMU32 offset, const void *bytes, size_t count)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(edit->step == 1 && offset <= edit->output.length && count <= edit->output.length - offset);
    memcpy(edit->output.data + offset, bytes, count);
    return 1;
}
static int source_add(void *context, MMU32 type, MMU32 count, MMU32 *number, MMStorageWriter *writer)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(type == SOURCE_TYPE && edit->step == 0 && count <= sizeof(edit->output.data));
    edit->step = 1;
    edit->output.length = count;
    *number = 8;
    writer->context = edit;
    writer->length = count;
    writer->write_at = source_write;
    return 1;
}
static int source_adopt(void *context, MMU32 parent_type, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(parent_type == MOVIE_TYPE && parent == 2 && type == SOURCE_TYPE && number == 8 && slot == 1);
    assert(edit->step == (edit->has_table ? 2 : 1));
    edit->step = 3;
    return !edit->fail_link;
}
static void source_unlink(void *context, MMU32 parent_type, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(parent_type == MOVIE_TYPE && parent == 2 && type == SOURCE_TYPE && number == 7 && slot == 1);
    assert(edit->step == 1 && edit->has_table);
    edit->step = 2;
}

static void source_row(const MMMovieSourceTable *table, MMU32 index, MMI32 id, const char *expected)
{
    MMMovieSourceName row;
    char name[MM_MOVIE_SOURCE_NAME_BYTES];
    assert(mm_movie_source_table_at(table, index, &row) == MM_MOVIE_OK && row.id == id);
    assert(mm_movie_source_name_utf8(&row, name) == MM_MOVIE_OK && !strcmp(name, expected));
}

static void register_source(void)
{
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, source_chunks,
                                   source_read, source_add, source_adopt, source_unlink, NULL, NULL, NULL, NULL};
    SourceEdit edit = {0};
    MMMemoryStream saved;
    MMMovieSourceTable table;
    MMCollectionHeader header = {0};
    static const char names[] = "\0043DMM\006ignore\0073DMOVIE";
    unsigned char input[20 + sizeof(names) - 1 + 24] = {0};
    unsigned char *rows;
    MMI32 id;
    editor.context = &edit;
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 1, "3dmm", &id) == MM_MOVIE_SOURCE_EDIT_OK);
    assert(id == 1 && edit.step == 3 && mm_memory_stream_init(&saved, edit.output.data, edit.output.length));
    assert(mm_movie_source_table_open(&saved.stream, sizeof(edit.output.data), &table) == MM_MOVIE_OK);
    assert(table.count == 1);
    source_row(&table, 0, 1, "3D Movie Maker/3DMOVIE");
    edit.step = 0;
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 2, "Doraemon Character Kit", &id) ==
           MM_MOVIE_SOURCE_EDIT_OK);
    assert(id == 3 && mm_memory_stream_init(&saved, edit.output.data, edit.output.length));
    assert(mm_movie_source_table_open(&saved.stream, sizeof(edit.output.data), &table) == MM_MOVIE_OK);
    assert(table.count == 2);
    source_row(&table, 0, 2, "3D Movie Maker/3DMOVIE");
    source_row(&table, 1, 3, "Doraemon Character Kit");

    header.kind = MM_COLLECTION_STRINGS;
    header.width = 8;
    header.count = 3;
    header.bytes = sizeof(names) - 1;
    header.free_count = -1;
    header.system = MM_TEXT_MAC;
    header.big_endian = 1;
    assert(mm_collection_header_write(&header, input, sizeof(input)) == MM_STORAGE_OK);
    memcpy(input + 20, names, sizeof(names) - 1);
    rows = input + 20 + sizeof(names) - 1;
    word(rows + 4, 17, 1);
    word(rows + 8, 5, 1);
    word(rows + 12, (MMU32)-4, 1);
    word(rows + 16, 12, 1);
    word(rows + 20, 11, 1);
    assert(mm_memory_stream_init(&edit.input, input, sizeof(input)));
    edit.has_table = 1;
    edit.step = 0;
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 2, NULL, &id) == MM_MOVIE_SOURCE_EDIT_OK);
    assert(id == 11 && edit.step == 3 && mm_memory_stream_init(&saved, edit.output.data, edit.output.length));
    assert(mm_movie_source_table_open(&saved.stream, sizeof(edit.output.data), &table) == MM_MOVIE_OK);
    assert(table.count == 3 && !table.big_endian && table.encoding == MM_TEXT_WINDOWS);
    source_row(&table, 0, 17, "3DMM");
    source_row(&table, 1, -4, "ignore");
    source_row(&table, 2, 11, "3DMOVIE");
    edit.step = 0;
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 2, "new", &id) == MM_MOVIE_SOURCE_EDIT_OK);
    assert(id == 18 && edit.step == 3);
    edit.step = 0;
    edit.fail_link = 1;
    id = 123;
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 2, "new", &id) == MM_MOVIE_SOURCE_EDIT_LINK);
    assert(id == 123 && edit.step == 3);
    edit.step = 0;
    word(rows + 4, 0x7fffffffUL, 1);
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 2, "new", &id) == MM_MOVIE_SOURCE_EDIT_IDS);
    assert(id == 123 && edit.step == 0);
}

static void map_sources(void)
{
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, source_chunks,
                                   source_read, source_add, source_adopt, source_unlink, NULL, NULL, NULL, NULL};
    MMMovieArchiveSource sources[] = {{9, "3DMM", 0, 0}, {4, "Doraemon Character Kit", 0, 0},
                                     {9, "other", 0, 0}};
    MMMovieArchiveSourceMap map = {NULL, NULL, NULL, 3, 2, 1, 0};
    SourceEdit edit = {0};
    MMI32 id = 123;
    editor.context = &edit;
    map.destination = &editor;
    map.sources = sources;
    mm_movie_archive_prepare_source_map(&map);
    assert(!mm_movie_archive_map_source(&map, 5, &id));
    assert(map.result == MM_MOVIE_SOURCE_EDIT_UNKNOWN && id == 123 && edit.step == 0);
    assert(mm_movie_archive_map_source(&map, 9, &id));
    assert(map.result == MM_MOVIE_SOURCE_EDIT_OK && id == 1 && edit.step == 3);
    assert(mm_movie_archive_map_source(&map, 9, &id) && id == 1 && edit.step == 3);
    edit.step = 0;
    edit.fail_link = 1;
    assert(!mm_movie_archive_map_source(&map, 4, &id));
    assert(map.result == MM_MOVIE_SOURCE_EDIT_LINK && id == 1 && edit.step == 3);
    edit.step = edit.fail_link = 0;
    assert(mm_movie_archive_map_source(&map, 4, &id) && id == 3 && edit.step == 3);
}

int main(void)
{
    start_events(0);
    start_events(1);
    sources(0, MM_TEXT_WINDOWS);
    sources(1, MM_TEXT_UTF16_BE);
    replacement();
    register_source();
    map_sources();
    puts("Portable movie source and background wire edits and link replacement passed.");
    return 0;
}

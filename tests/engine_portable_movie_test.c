/* Licensed under the MIT License. */
#include "chunk_edit_fixture.h"
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

static int map_background(void *context, MMU32 number, MMU32 *out)
{
    (void)context;
    assert(number == 14);
    *out = 21;
    return 1;
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
    assert(mm_scene_rebind_start_backgrounds(&stream.stream, map_background, receive, &out) && out.calls == 1);
    word(records + 44, 0, big);
    memcpy(expected, input, sizeof(input));
    word(expected + 76, 21, big);
    assert(mm_scene_rebind_start_backgrounds(&stream.stream, map_background, receive, &out));
    assert(out.calls == 2 && !memcmp(expected, out.data, sizeof(input)));
    word(records + 80, 23, big);
    assert(!mm_scene_retarget_start_background(&stream.stream, 6, 0, receive, &out));
    assert(out.calls == 2);
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
    ChunkIndexFixture index;
    char calls[4];
    int count, fail;
} Links;
static int write_child(void *context, MMU32 offset, const void *data, size_t size)
{
    Links *links = (Links *)context;
    assert(!offset && *(const unsigned char *)data == 99 && size == 1);
    links->calls[links->count++] = 'W';
    return links->fail != 1;
}
static int add_child(void *context, MMU32 type, MMU32 size, MMU32 *number, MMStorageWriter *writer)
{
    Links *links = context;
    assert(type == 1 && size == 1);
    if (links->fail == 3)
        return 0;
    *number = 12;
    fixture_chunk_add(&links->index, type, *number);
    writer->context = links;
    writer->length = size;
    writer->write_at = write_child;
    return 1;
}
static int adopt_child(void *context, MMU32 number, const MMChunkChild *child)
{
    Links *links = (Links *)context;
    assert(number == 3 && child->type == 1 && child->number == 12 && child->slot == 1);
    links->calls[links->count++] = 'A';
    return links->fail != 2;
}
static void remove_child(void *context, MMU32 number, const MMChunkChild *child)
{
    Links *links = (Links *)context;
    assert(number == 3 && child->type == 1 && child->slot == 1 && child->number == 11);
    links->calls[links->count++] = 'L';
}
static void drop_child(void *context, const MMChunkEntry *entry)
{
    Links *links = context;
    assert(entry->type == 1 && entry->number == 12 && !entry->parent_count);
    links->calls[links->count++] = 'D';
}
static void replacement(void)
{
    static const char *orders[] = {"WAL", "WD", "WAD", ""};
    const MMChunkChild child = {1, 11, 1};
    const unsigned char data = 99;
    MMMovieArchiveEditor editor = {0};
    Links links;
    MMChunkAccess index;
    MMChunkEntry entry;
    MMU32 row, number;
    int fail;
    editor.context = &links;
    editor.add = add_child;
    editor.edit = fixture_chunk_edit(&links.index);
    for (fail = 0; fail < 4; ++fail)
    {
        memset(&links, 0, sizeof(links));
        links.fail = fail;
        fixture_chunk_add(&links.index, 1, 11);
        fixture_chunk_add(&links.index, 2, 3);
        fixture_chunk_add(&links.index, 2, 4);
        assert(mm_chunk_adopt(&editor.edit, 2, 3, &child, 1) == MM_CHUNK_OK);
        assert(mm_chunk_adopt(&editor.edit, 2, 4, &child, 1) == MM_CHUNK_OK);
        links.index.observer = &links;
        links.index.before_insert = adopt_child;
        links.index.before_unlink = remove_child;
        links.index.before_delete = drop_child;
        assert(mm_movie_archive_replace_child(&editor, 2, 3, &child, 1, &data, 1) == !fail);
        assert((size_t)links.count == strlen(orders[fail]) && !memcmp(links.calls, orders[fail], links.count));
        index = fixture_chunk_access(&links.index);
        assert(mm_chunk_link_number(&index, 2, 3, 1, 1, &number) == MM_CHUNK_OK &&
               number == (fail ? 11 : 12));
        assert(mm_chunk_link_number(&index, 2, 4, 1, 1, &number) == MM_CHUNK_OK && number == 11);
        assert(mm_chunk_find(&index, 1, 11, &row) == MM_CHUNK_OK &&
               mm_chunk_at(&index, row, &entry) == MM_CHUNK_OK && entry.parent_count == (fail ? 2U : 1U));
        assert(index.count == (fail ? 3U : 4U));
    }
}

#define MOVIE_TYPE 0x4d564945UL
#define SOURCE_TYPE 0x47535420UL
typedef struct SourceEdit
{
    MMMemoryStream input;
    Output output;
    ChunkIndexFixture index;
    int has_table, fail_link, step, portable, fail_write, discards;
} SourceEdit;

static MMChunkAccess source_chunks(void *context)
{
    return fixture_chunk_access(&((SourceEdit *)context)->index);
}
static int source_read(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(type == SOURCE_TYPE && (number == 7 || (edit->portable && number == 8)) &&
           edit->has_table && edit->step == 0);
    *stream = edit->input.stream;
    return 1;
}
static int source_write(void *context, MMU32 offset, const void *bytes, size_t count)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(edit->step == 1 && offset <= edit->output.length && count <= edit->output.length - offset);
    if (edit->fail_write) return 0;
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
    fixture_chunk_add(&edit->index, type, *number);
    writer->context = edit;
    writer->length = count;
    writer->write_at = source_write;
    return 1;
}
static int insert_source(void *context, MMU32 parent, const MMChunkChild *child)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(parent == 2 && child->type == SOURCE_TYPE && child->number == 8 && child->slot == 1);
    assert(edit->step == (edit->has_table && !edit->portable ? 2 : 1));
    edit->step = 3;
    return !edit->fail_link;
}
static void unlink_source(void *context, MMU32 parent, const MMChunkChild *child)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(parent == 2 && child->type == SOURCE_TYPE && child->number == 7 && child->slot == 1);
    assert(edit->step == (edit->portable ? 3 : 1) && edit->has_table);
    edit->step = edit->portable ? 4 : 2;
}

static void erase_source(void *context, const MMChunkEntry *entry)
{
    SourceEdit *edit = (SourceEdit *)context;
    assert(entry->type == SOURCE_TYPE && !entry->parent_count);
    if (entry->number == 8)
    {
        assert(edit->portable);
        ++edit->discards;
    }
    else
        assert(entry->number == 7 && edit->step == (edit->portable ? 4 : 2));
}

static void prepare_source(SourceEdit *source)
{
    const MMChunkChild previous = {SOURCE_TYPE, 7, 1};
    const MMChunkEdit edit = fixture_chunk_edit(&source->index);
    memset(&source->index, 0, sizeof(source->index));
    fixture_chunk_add(&source->index, MOVIE_TYPE, 2);
    if (source->has_table)
    {
        fixture_chunk_add(&source->index, previous.type, previous.number);
        assert(mm_chunk_adopt(&edit, MOVIE_TYPE, 2, &previous, 1) == MM_CHUNK_OK);
    }
    source->index.observer = source;
    source->index.before_insert = insert_source;
    source->index.before_unlink = unlink_source;
    source->index.before_delete = erase_source;
    source->step = 0;
}

static MMMovieArchiveEditor source_editor(SourceEdit *source)
{
    MMMovieArchiveEditor editor = {0};
    editor.context = source;
    editor.text_unit = 1;
    editor.system = MM_TEXT_WINDOWS;
    editor.chunks = source_chunks;
    editor.read = source_read;
    editor.add = source_add;
    editor.edit = fixture_chunk_edit(&source->index);
    return editor;
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
    SourceEdit edit = {0};
    const MMMovieArchiveEditor editor = source_editor(&edit);
    MMMemoryStream saved;
    MMMovieSourceTable table;
    MMCollectionHeader header = {0};
    static const char names[] = "\0043DMM\006ignore\0073DMOVIE";
    unsigned char input[20 + sizeof(names) - 1 + 24] = {0};
    unsigned char *rows;
    MMI32 id;
    prepare_source(&edit);
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 1, "3dmm", &id) == MM_MOVIE_SOURCE_EDIT_OK);
    assert(id == 1 && edit.step == 3 && mm_memory_stream_init(&saved, edit.output.data, edit.output.length));
    assert(mm_movie_source_table_open(&saved.stream, sizeof(edit.output.data), &table) == MM_MOVIE_OK);
    assert(table.count == 1);
    source_row(&table, 0, 1, "3D Movie Maker/3DMOVIE");
    prepare_source(&edit);
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
    prepare_source(&edit);
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 2, NULL, &id) == MM_MOVIE_SOURCE_EDIT_OK);
    assert(id == 11 && edit.step == 3 && mm_memory_stream_init(&saved, edit.output.data, edit.output.length));
    assert(mm_movie_source_table_open(&saved.stream, sizeof(edit.output.data), &table) == MM_MOVIE_OK);
    assert(table.count == 3 && !table.big_endian && table.encoding == MM_TEXT_WINDOWS);
    source_row(&table, 0, 17, "3DMM");
    source_row(&table, 1, -4, "ignore");
    source_row(&table, 2, 11, "3DMOVIE");
    prepare_source(&edit);
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 2, "new", &id) == MM_MOVIE_SOURCE_EDIT_OK);
    assert(id == 18 && edit.step == 3);
    prepare_source(&edit);
    edit.fail_link = 1;
    id = 123;
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 2, "new", &id) == MM_MOVIE_SOURCE_EDIT_LINK);
    assert(id == 123 && edit.step == 3);
    prepare_source(&edit);
    word(rows + 4, 0x7fffffffUL, 1);
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 2, "new", &id) == MM_MOVIE_SOURCE_EDIT_IDS);
    assert(id == 123 && edit.step == 0);
}

static void map_sources(void)
{
    MMMovieArchiveSource sources[] = {{9, "3DMM", 0, 0}, {4, "Doraemon Character Kit", 0, 0},
                                     {9, "other", 0, 0}};
    MMMovieArchiveSourceMap map = {NULL, NULL, NULL, 3, 2, 1, 0};
    SourceEdit edit = {0};
    const MMMovieArchiveEditor editor = source_editor(&edit);
    MMI32 id = 123;
    prepare_source(&edit);
    map.destination = &editor;
    map.sources = sources;
    mm_movie_archive_prepare_source_map(&map);
    assert(!mm_movie_archive_map_source(&map, 5, &id));
    assert(map.result == MM_MOVIE_SOURCE_EDIT_UNKNOWN && id == 123 && edit.step == 0);
    assert(mm_movie_archive_map_source(&map, 9, &id));
    assert(map.result == MM_MOVIE_SOURCE_EDIT_OK && id == 1 && edit.step == 3);
    assert(mm_movie_archive_map_source(&map, 9, &id) && id == 1 && edit.step == 3);
    prepare_source(&edit);
    edit.fail_link = 1;
    assert(!mm_movie_archive_map_source(&map, 4, &id));
    assert(map.result == MM_MOVIE_SOURCE_EDIT_LINK && id == 1 && edit.step == 3);
    edit.fail_link = 0;
    prepare_source(&edit);
    assert(mm_movie_archive_map_source(&map, 4, &id) && id == 3 && edit.step == 3);
}

static void publish_portable_source(void)
{
    SourceEdit edit = {0};
    Output old;
    MMMovieSourceTable table;
    MMMemoryStream output;
    const MMMovieArchiveEditor editor = source_editor(&edit);
    MMChunkAccess index;
    MMU32 number;
    MMI32 id;
    prepare_source(&edit);
    assert(mm_movie_archive_register_source(&editor, NULL, 2, 1, NULL, &id) == MM_MOVIE_SOURCE_EDIT_OK);
    old = edit.output;
    assert(mm_memory_stream_init(&edit.input, old.data, old.length));
    edit.has_table = edit.portable = 1;
    prepare_source(&edit);
    edit.fail_write = 1;
    assert(mm_movie_archive_require_portable_source(&editor) == MM_PORTABLE_SOURCE_WRITE_FAILED &&
           edit.step == 1 && edit.discards == 1);
    edit.step = edit.fail_write = 0;
    edit.fail_link = 1;
    assert(mm_movie_archive_require_portable_source(&editor) == MM_PORTABLE_SOURCE_WRITE_FAILED &&
           edit.step == 3 && edit.discards == 2);
    index = fixture_chunk_access(&edit.index);
    assert(index.count == 2 && mm_chunk_link_number(&index, MOVIE_TYPE, 2, 1, SOURCE_TYPE, &number) == MM_CHUNK_OK &&
           number == 7);
    edit.step = edit.fail_link = 0;
    assert(mm_movie_archive_require_portable_source(&editor) == MM_PORTABLE_SOURCE_OK &&
           edit.step == 4 && edit.discards == 2);
    index = fixture_chunk_access(&edit.index);
    assert(index.count == 2 && mm_chunk_link_number(&index, MOVIE_TYPE, 2, 1, SOURCE_TYPE, &number) == MM_CHUNK_OK &&
           number == 8);
    assert(mm_memory_stream_init(&output, edit.output.data, edit.output.length));
    assert(mm_movie_source_table_open(&output.stream, output.stream.length, &table) == MM_MOVIE_OK && table.count == 2);
    source_row(&table, 0, 1, "3D Movie Maker/3DMOVIE");
    source_row(&table, 1, MM_PORTABLE_SOURCE_ID, "Virtual3dmm/v3dmm");
    edit.input = output;
    edit.step = 0;
    assert(mm_movie_archive_require_portable_source(&editor) == MM_PORTABLE_SOURCE_OK && edit.step == 0);
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
    publish_portable_source();
    puts("Portable movie source and background wire edits and link replacement passed.");
    return 0;
}

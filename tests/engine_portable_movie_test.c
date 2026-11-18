/* Licensed under the MIT License. */
#include "mm/chunk_edit.h"
#include "mm/collections.h"
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

int main(void)
{
    start_events(0);
    start_events(1);
    sources(0, MM_TEXT_WINDOWS);
    sources(1, MM_TEXT_UTF16_BE);
    replacement();
    puts("Portable movie source and background wire edits and link replacement passed.");
    return 0;
}

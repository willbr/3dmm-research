/* Licensed under the MIT License. */
#include "mm/vmm_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const unsigned char fixture[] = {
    'v', '3', 'd', 'm', 'm', 1, 2, 0, 0, 0,
    0, 0, 0, 0, 4, 0, 'b', 'a', 's', 'e',
    3, 0, 0, 0, 2, 0, 'V', 0xe9,
    0xf0, 0x0b, 7, 8, 9, 0xfe, 0xeb, 'C', 'H', 'N', '2', 1, 2
};
enum { ARCHIVE_START = 30, ARCHIVE_END = 33, MOVIE_START = 35 };

static int read_fixture(void *context, MMWide offset, void *bytes, size_t count)
{
    assert(offset.high == 0 && offset.low < sizeof(fixture) && count <= sizeof(fixture) - offset.low);
    memcpy(bytes, (const unsigned char *)context + offset.low, count);
    return 1;
}

static int read_sparse(void *context, MMWide offset, void *bytes, size_t count)
{
    if (offset.high == 0 && offset.low < ARCHIVE_START && count <= ARCHIVE_START - offset.low)
        return read_fixture(context, offset, bytes, count);
    /* The only reads in the second word are the end marker and the movie magic. */
    assert(offset.high == 1 && offset.low >= ARCHIVE_START - 1 && offset.low <= ARCHIVE_START + 1);
    memcpy(bytes, fixture + ARCHIVE_END + (offset.low - (ARCHIVE_START - 1)), count);
    return 1;
}

typedef struct Output
{
    unsigned char bytes[sizeof(fixture)];
    size_t length, limit;
    int begins, archives, movies;
} Output;

static int begin(void *context)
{
    Output *out = (Output *)context;
    ++out->begins;
    out->length = 0;
    return 1;
}

static int write_bytes(void *context, const void *bytes, size_t count)
{
    Output *out = (Output *)context;
    if (count > out->limit - out->length) return 0;
    memcpy(out->bytes + out->length, bytes, count);
    out->length += count;
    return 1;
}

static int copy_archive(void *context, size_t index)
{
    Output *out = (Output *)context;
    assert(index == 1);
    ++out->archives;
    return write_bytes(context, fixture + ARCHIVE_START, ARCHIVE_END - ARCHIVE_START);
}

static int copy_movie(void *context)
{
    Output *out = (Output *)context;
    ++out->movies;
    return write_bytes(context, fixture + MOVIE_START, sizeof(fixture) - MOVIE_START);
}

int main(void)
{
    unsigned char input[sizeof(fixture)];
    MMVmmReader reader = {0};
    MMVmmDirectory directory = {0};
    MMVmmEntry malformed;
    Output output = {0};
    MMVmmWriter writer = {NULL, begin, write_bytes, copy_archive, copy_movie};
    MMU32 truncated;
    writer.context = &output;
    memcpy(input, fixture, sizeof(input));
    reader.context = input;
    reader.length.low = sizeof(input);
    reader.read_at = read_fixture;
    assert(mm_vmm_read(&reader, 1, &directory) == MM_VMM_DEPENDENCY_COUNT);
    assert(mm_vmm_read(&reader, 2, &directory) == MM_VMM_OK);
    assert(directory.count == 2 && strcmp(directory.entries[0].name, "base") == 0);
    assert(directory.entries[0].archive_length == 0 && directory.entries[0].archive_offset.low == 0);
    assert(directory.entries[1].name_length == 2 && strcmp(directory.entries[1].name, "V\xe9") == 0);
    assert(directory.entries[1].archive_length == 3 && directory.entries[1].archive_offset.low == ARCHIVE_START);
    assert(directory.movie_offset.low == MOVIE_START && directory.movie_length.low == sizeof(fixture) - MOVIE_START);
    output.limit = sizeof(fixture);
    malformed = directory.entries[0];
    malformed.name = "a\0b";
    malformed.name_length = 3;
    assert(mm_vmm_write(&writer, &malformed, 1) == MM_VMM_OUTPUT_ENTRY && output.begins == 0);
    assert(mm_vmm_write(&writer, directory.entries, directory.count) == MM_VMM_OK);
    assert(output.begins == 1 && output.archives == 1 && output.movies == 1);
    assert(output.length == sizeof(fixture) && memcmp(output.bytes, fixture, sizeof(fixture)) == 0);
    output.limit = ARCHIVE_START + 1;
    assert(mm_vmm_write(&writer, directory.entries, directory.count) == MM_VMM_WRITE_FAILED);
    assert(output.archives == 2 && output.movies == 1);
    mm_vmm_dispose(&directory);
    for (truncated = 0; truncated < MOVIE_START + 4; ++truncated)
    {
        reader.length.low = truncated;
        assert(mm_vmm_read(&reader, 2, &directory) != MM_VMM_OK);
        assert(!directory.entries && directory.count == 0);
    }
    reader.length.low = sizeof(input);
    input[27] = 0;
    assert(mm_vmm_read(&reader, 2, &directory) == MM_VMM_INVALID_NAME);
    input[27] = fixture[27];
    input[28] = 0;
    assert(mm_vmm_read(&reader, 2, &directory) == MM_VMM_START_MARKER);
    input[28] = fixture[28];
    input[ARCHIVE_END] = 0;
    assert(mm_vmm_read(&reader, 2, &directory) == MM_VMM_END_MARKER);
    input[ARCHIVE_END] = fixture[ARCHIVE_END];
    input[MOVIE_START] = 0;
    assert(mm_vmm_read(&reader, 2, &directory) == MM_VMM_MOVIE_MAGIC);

    /* A maximum-sized embedded range carries into the high word. The reader
     * skips its payload and publishes offsets without staging a large file. */
    memset(input + 20, 0xff, 4);
    reader.length.high = 1;
    reader.length.low = ARCHIVE_START + 1 + sizeof(fixture) - MOVIE_START;
    reader.read_at = read_sparse;
    assert(mm_vmm_read(&reader, 2, &directory) == MM_VMM_OK);
    assert(directory.entries[1].archive_length == (MMU32)0xffffffffUL);
    assert(directory.entries[1].archive_offset.high == 0 && directory.entries[1].archive_offset.low == ARCHIVE_START);
    assert(directory.movie_offset.high == 1 && directory.movie_offset.low == ARCHIVE_START + 1);
    assert(directory.movie_length.high == 0 && directory.movie_length.low == sizeof(fixture) - MOVIE_START);
    mm_vmm_dispose(&directory);
    puts("VMM v1 streaming directory, byte layout, output failure and wide ranges passed.");
    return 0;
}

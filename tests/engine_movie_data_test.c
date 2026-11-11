#include "three_mm/movie_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, int line)
{
    if (!condition)
    {
        fprintf(stderr, "Movie C89 check failed at line %d\n", line);
        exit(1);
    }
}
#define CHECK(value) check((value), __LINE__)

static void put16(unsigned char *bytes, unsigned int value, int big)
{
    bytes[big ? 1 : 0] = (unsigned char)value;
    bytes[big ? 0 : 1] = (unsigned char)(value >> 8);
}
static void put32(unsigned char *bytes, ThreeMMU32 value, int big)
{
    int index;
    for (index = 0; index < 4; ++index)
        bytes[big ? 3 - index : index] = (unsigned char)(value >> (index * 8));
}
typedef struct ReadProbe
{
    int calls, fail;
    unsigned char bytes[64];
} ReadProbe;
static int read_probe(void *context, ThreeMMU32 offset, void *bytes, size_t count)
{
    ReadProbe *probe = (ReadProbe *)context;
    ++probe->calls;
    if (probe->fail)
        return 0;
    memcpy(bytes, probe->bytes + offset, count);
    return 1;
}
static void streams(void)
{
    ReadProbe probe;
    ThreeMMReadStream stream;
    ThreeMMReadSlice slice, nested;
    ThreeMMMemoryStream memory;
    unsigned char bytes[4];
    memset(&probe, 0, sizeof(probe));
    probe.bytes[13] = 42;
    stream.context = &probe;
    stream.length = 64;
    stream.read_at = read_probe;
    CHECK(!three_mm_read_exact(&stream, 65, bytes, 0));
    CHECK(!three_mm_read_exact(&stream, 0xffffffffUL, bytes, 4));
    CHECK(!three_mm_read_exact(&stream, 63, bytes, 4));
    CHECK(!three_mm_read_exact(&stream, 0, NULL, 1));
    CHECK(three_mm_read_exact(&stream, 64, NULL, 0));
    CHECK(probe.calls == 0);
    CHECK(three_mm_read_slice_init(&slice, &stream, 10, 8));
    CHECK(three_mm_read_slice_init(&nested, &slice.stream, 2, 4));
    CHECK(three_mm_read_exact(&nested.stream, 1, bytes, 1) && bytes[0] == 42);
    CHECK(!three_mm_read_exact(&nested.stream, 3, bytes, 2));
    CHECK(probe.calls == 1);
    CHECK(!three_mm_read_slice_init(&nested, &stream, 0xfffffff0UL, 32));
    CHECK(nested.stream.length == 4 && nested.offset == 2);
    CHECK(!three_mm_read_slice_init(&nested, &nested.stream, 0, 1));
    probe.fail = 1;
    CHECK(!three_mm_read_exact(&nested.stream, 0, bytes, 1));
    CHECK(probe.calls == 2);
    CHECK(three_mm_memory_stream_init(&memory, probe.bytes, sizeof(probe.bytes)));
    CHECK(!three_mm_memory_stream_init(&memory, NULL, 1));
    CHECK(memory.stream.length == sizeof(probe.bytes));
}
static void headers(void)
{
    int big, current, back, result;
    const int versions[] = {-32768, -1, 0, 1, 2, 3, 32767};
    unsigned char bytes[9];
    ThreeMMMemoryStream memory;
    ThreeMMMovieHeader header;
    ReadProbe probe;
    ThreeMMReadStream failing;
    for (big = 0; big <= 1; ++big)
        for (current = 0; current < 7; ++current)
            for (back = 0; back < 7; ++back)
            {
                put16(bytes, 1, big);
                put16(bytes + 2, 0x0303, big);
                put16(bytes + 4, (unsigned int)versions[current], big);
                put16(bytes + 6, (unsigned int)versions[back], big);
                CHECK(three_mm_memory_stream_init(&memory, bytes, 8));
                header.system = 1234;
                result = three_mm_movie_read_header(&memory.stream, &header);
                CHECK(result ==
                      (versions[current] >= 1 && versions[back] <= 2 ? THREE_MM_MOVIE_OK : THREE_MM_MOVIE_BAD_VERSION));
                if (result == THREE_MM_MOVIE_OK)
                    CHECK(header.current_version == versions[current] && header.backward_version == versions[back] &&
                          header.big_endian == big && header.system == 0x0303);
                else
                    CHECK(header.system == 1234);
            }
    CHECK(three_mm_memory_stream_init(&memory, bytes, 7));
    CHECK(three_mm_movie_read_header(&memory.stream, &header) == THREE_MM_MOVIE_BAD_FILE);
    CHECK(three_mm_memory_stream_init(&memory, bytes, 9));
    CHECK(three_mm_movie_read_header(&memory.stream, &header) == THREE_MM_MOVIE_BAD_FILE);
    CHECK(three_mm_memory_stream_init(&memory, bytes, 8));
    bytes[0] = bytes[1] = 0;
    CHECK(three_mm_movie_read_header(&memory.stream, &header) == THREE_MM_MOVIE_BAD_FILE);
    memset(&probe, 0, sizeof(probe));
    probe.fail = 1;
    failing.context = &probe;
    failing.length = 8;
    failing.read_at = read_probe;
    CHECK(three_mm_movie_read_header(&failing, &header) == THREE_MM_MOVIE_IO);
}
static void actors(void)
{
    const ThreeMMI32 ids[] = {-1, -2, -749027345L, (-2147483647L - 1), 0, 2147483646L, 2147483647L};
    unsigned char bytes[28];
    ThreeMMMemoryStream memory;
    ThreeMMMovieActor actor;
    ThreeMMI32 limit;
    int big, index, bad_source, result;
    for (big = 0; big <= 1; ++big)
        for (index = 0; index < 7; ++index)
            for (bad_source = 0; bad_source <= 1; ++bad_source)
            {
                put32(bytes, (ThreeMMU32)ids[index], big);
                put32(bytes + 4, (ThreeMMU32)-3, big);
                put32(bytes + 8, 0xabcdef01UL, big);
                put32(bytes + 12, bad_source ? 0xffffffffUL : 0, big);
                put32(bytes + 16, 0xdeadbeefUL, big);
                put32(bytes + 20, 0x544d504cUL, big);
                put32(bytes + 24, 0x12345678UL, big);
                CHECK(three_mm_memory_stream_init(&memory, bytes, sizeof(bytes)));
                actor.actor_id = 123;
                limit = 7;
                result = three_mm_movie_read_actor(&memory.stream, big, &actor, &limit);
                if (bad_source || ids[index] == -1 || ids[index] == 2147483647L)
                    CHECK(result == THREE_MM_MOVIE_BAD_FILE && limit == 7 && actor.actor_id == 123);
                else
                {
                    CHECK(result == THREE_MM_MOVIE_OK && actor.actor_id == ids[index] && actor.reference_count == -3);
                    CHECK(actor.browser_flags == 0xabcdef01UL && actor.source_id == 0 &&
                          actor.template_type == 0x544d504cUL && actor.template_number == 0x12345678UL);
                    CHECK(limit == (ids[index] >= 7 ? ids[index] + 1 : 7));
                }
            }
}
static void strings(void)
{
    unsigned char bytes[64];
    ThreeMMMemoryStream memory;
    ThreeMMReadStream failing;
    ReadProbe probe;
    int big, encoding, wide;
    unsigned int encodings[] = {0x0202, 0x0303, 0x0404, 0x0505};
    ThreeMMU32 string_bytes;
    for (big = 0; big <= 1; ++big)
        for (encoding = 0; encoding < 4; ++encoding)
        {
            memset(bytes, 0, sizeof(bytes));
            wide = encoding >= 2;
            string_bytes = wide ? 4 : 2;
            put16(bytes, 1, big);
            put16(bytes + 2, encodings[encoding], big);
            put32(bytes + 4, 32, big);
            put32(bytes + 8, 1, big);
            put32(bytes + 12, string_bytes, big);
            put32(bytes + 16, 0xffffffffUL, big);
            if (wide)
            {
                put16(bytes + 20, 1, encoding == 2);
                put16(bytes + 22, 'A', encoding == 2);
            }
            else
            {
                bytes[20] = 1;
                bytes[21] = 'A';
            }
            put32(bytes + 20 + string_bytes, 0, big);
            CHECK(three_mm_memory_stream_init(&memory, bytes, 20 + string_bytes + 32));
            CHECK(three_mm_movie_validate_strings(&memory.stream, 64) == THREE_MM_MOVIE_OK);
            CHECK(three_mm_movie_validate_strings(&memory.stream, 20) == THREE_MM_MOVIE_BAD_FILE);
            /* An entry pointing into a string must not reach the legacy reader. */
            put32(bytes + 20 + string_bytes, wide ? 2 : 1, big);
            CHECK(three_mm_movie_validate_strings(&memory.stream, 64) == THREE_MM_MOVIE_BAD_FILE);
            put32(bytes + 20 + string_bytes, 0, big);
            bytes[wide && encoding == 2 ? 23 : wide ? 22 : 21] = 0;
            CHECK(three_mm_movie_validate_strings(&memory.stream, 64) == THREE_MM_MOVIE_BAD_FILE);
        }
    /* Overflowing index counts/strides and truncated strings are rejected. */
    put32(bytes + 4, 0xfffffffcUL, 1);
    CHECK(three_mm_movie_validate_strings(&memory.stream, 64) == THREE_MM_MOVIE_BAD_FILE);
    memset(bytes, 0, sizeof(bytes));
    put16(bytes, 1, 0);
    put16(bytes + 2, 0x0303, 0);
    put32(bytes + 4, 4, 0);
    put32(bytes + 16, 0xffffffffUL, 0);
    CHECK(three_mm_memory_stream_init(&memory, bytes, 20));
    CHECK(three_mm_movie_validate_strings(&memory.stream, 64) == THREE_MM_MOVIE_OK);
    memset(&probe, 0, sizeof(probe));
    probe.fail = 1;
    failing.context = &probe;
    failing.length = 20;
    failing.read_at = read_probe;
    CHECK(three_mm_movie_validate_strings(&failing, 64) == THREE_MM_MOVIE_IO);
}
int main(void)
{
    streams();
    headers();
    actors();
    strings();
    return 0;
}

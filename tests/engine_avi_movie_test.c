#include "mm/avi_movie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "AVI backend check failed at line %d\n", line);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(condition) check((condition), __LINE__)

typedef struct Fixture
{
    unsigned char wire[512];
    size_t used, first_packet, second_packet;
    int attempts, fail_at, allocations;
} Fixture;

static void put32(unsigned char *p, MMU32 value)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[i] = (unsigned char)(value >> (8 * i));
}

static void add(Fixture *f, const void *bytes, size_t count)
{
    CHECK(count <= sizeof(f->wire) - f->used);
    memcpy(f->wire + f->used, bytes, count);
    f->used += count;
}

static size_t start_chunk(Fixture *f, const char *id, const char *list_type)
{
    static const unsigned char zero[4] = {0};
    size_t start = f->used;
    add(f, id, 4);
    add(f, zero, 4);
    if (list_type)
        add(f, list_type, 4);
    return start;
}

static void end_chunk(Fixture *f, size_t start)
{
    const unsigned char padding = 0;
    size_t bytes = f->used - start - 8;
    put32(f->wire + start + 4, (MMU32)bytes);
    if (bytes & 1)
        add(f, &padding, 1);
}

static void chunk(Fixture *f, const char *id, const void *data, size_t bytes)
{
    size_t start = start_chunk(f, id, NULL);
    if (bytes)
        add(f, data, bytes);
    end_chunk(f, start);
}

static void build(Fixture *f)
{
    unsigned char sh[48] = {0}, sf[72] = {0};
    static const unsigned char junk[3] = {1, 2, 3};
    static const unsigned char first[2] = {7, 0x80}, skip[2] = {1, 0x84};
    static const unsigned char change[8] = {7, 1, 0, 0, 11, 22, 33, 255};
    static const unsigned char sound[2] = {128, 129};
    size_t riff, headers, stream, movi, record;
    riff = start_chunk(f, "RIFF", "AVI ");
    headers = start_chunk(f, "LIST", "hdrl");
    stream = start_chunk(f, "LIST", "strl");
    memcpy(sh, "vidsmsvc", 8);
    put32(sh + 20, 166667);
    put32(sh + 24, 1000000);
    put32(sh + 28, 3);
    put32(sh + 32, 3);
    put32(sf, 40);
    put32(sf + 4, 4);
    put32(sf + 8, 4);
    sf[12] = 1;
    sf[14] = 8;
    memcpy(sf + 16, "CrAm", 4);
    put32(sf + 32, 8);
    put32(sf + 68, 0xff070807UL); /* Ignore the reserved byte. */
    chunk(f, "strh", sh, sizeof(sh));
    chunk(f, "strf", sf, sizeof(sf));
    end_chunk(f, stream);

    stream = start_chunk(f, "LIST", "strl");
    memset(sh, 0, sizeof(sh));
    memset(sf, 0, sizeof(sf));
    memcpy(sh, "auds", 4);
    put32(sh + 20, 1);
    put32(sh + 24, 22050);
    put32(sh + 28, 81);
    put32(sh + 44, 1);
    sf[0] = sf[2] = sf[12] = 1;
    sf[14] = 8;
    put32(sf + 4, 22050);
    put32(sf + 8, 22050);
    chunk(f, "strh", sh, sizeof(sh));
    chunk(f, "strf", sf, 16);
    end_chunk(f, stream);
    end_chunk(f, headers);
    chunk(f, "JUNK", junk, sizeof(junk));
    movi = start_chunk(f, "LIST", "movi");
    record = start_chunk(f, "LIST", "rec ");
    f->first_packet = f->used + 8;
    chunk(f, "00dc", first, sizeof(first));
    chunk(f, "01wb", sound, 1);
    chunk(f, "00pc", change, sizeof(change));
    f->second_packet = f->used + 8;
    chunk(f, "00dc", skip, sizeof(skip));
    chunk(f, "01wb", sound + 1, 1);
    chunk(f, "00dc", NULL, 0);
    end_chunk(f, record);
    end_chunk(f, movi);
    end_chunk(f, riff);
}

static void *allocate(void *context, size_t bytes)
{
    Fixture *f = (Fixture *)context;
    void *result;
    if (++f->attempts == f->fail_at)
        return NULL;
    result = malloc(bytes);
    if (result)
        ++f->allocations;
    return result;
}

static void release(void *context, void *value)
{
    Fixture *f = (Fixture *)context;
    CHECK(value != NULL && f->allocations > 0);
    --f->allocations;
    free(value);
}

static void test_movie(void)
{
    Fixture f = {0};
    MMAviAllocator allocator = {0, allocate, release};
    MMAviMovie *movie = NULL, *original;
    MMAviInfo info;
    MMAviAudio audio;
    unsigned char frame[64], previous[64];
    int i;
    allocator.context = &f;
    build(&f);
    CHECK(mm_avi_read(&movie, f.wire, f.used, &allocator));
    CHECK(f.allocations == 5);
    original = movie;
    audio = mm_avi_audio(movie);
    info = mm_avi_info(movie);
    CHECK(info.width == 4 && info.height == 4 && info.frames == 3 && info.start == 3);
    CHECK(mm_avi_time_for_frame(movie, 0) == 500 && mm_avi_time_for_frame(movie, 3) == 1000);
    CHECK(mm_avi_frame_for_time(movie, 500) == -1 && mm_avi_frame_for_time(movie, 501) == 0);
    CHECK(mm_avi_frame_for_time(movie, (-2147483647L - 1L)) == -4);
    CHECK(audio.rate == 22050 && audio.start == 81 && audio.pcm_bytes == 2 && audio.pcm[1] == 129);
    for (i = 1; i <= 5; ++i)
    {
        f.attempts = 0;
        f.fail_at = i;
        CHECK(!mm_avi_read(&movie, f.wire, f.used, &allocator));
        CHECK(movie == original && mm_avi_audio(movie).pcm == audio.pcm && f.allocations == 5);
    }
    f.fail_at = 0;
    f.wire[f.first_packet] = 5;
    CHECK(mm_avi_frame_bytes(movie, 2) == sizeof(frame));
    CHECK(mm_avi_decode_frame(movie, 2, frame, sizeof(frame)));
    for (i = 0; i < 64; i += 4)
        CHECK(frame[i] == 33 && frame[i + 1] == 22 && frame[i + 2] == 11 && frame[i + 3] == 0);
    CHECK(mm_avi_decode_frame(movie, 0, frame, sizeof(frame)));
    CHECK(frame[0] == 7 && frame[1] == 8 && frame[2] == 7 && frame[3] == 0);
    memcpy(previous, frame, sizeof(frame));
    CHECK(!mm_avi_decode_frame(movie, 0, frame, sizeof(frame) - 1));
    CHECK(!memcmp(frame, previous, sizeof(frame)));

    f.wire[f.first_packet] = 7;
    f.wire[f.second_packet] = 0;
    CHECK(mm_avi_read(&movie, f.wire, f.used, &allocator) && f.allocations == 5);
    CHECK(!mm_avi_decode_frame(movie, 2, frame, sizeof(frame)));
    CHECK(!memcmp(frame, previous, sizeof(frame)));
    CHECK(mm_avi_decode_frame(movie, 0, frame, sizeof(frame)) && !memcmp(frame, previous, sizeof(frame)));
    mm_avi_close(movie);
    CHECK(f.allocations == 0);
}

int main(void)
{
    unsigned char pixels[32] = {7, 0x80}, previous[32];
    static const unsigned char broken[] = {8, 0x80, 1, 0x90, 2};
    int i;
    CHECK(mm_avi_decode_video1(pixels, 2, 4, 4, pixels, 16));
    for (i = 0; i < 16; ++i)
        CHECK(pixels[i] == 7);
    memcpy(previous, pixels, sizeof(pixels));
    CHECK(!mm_avi_decode_video1(broken, sizeof(broken), 8, 4, pixels, sizeof(pixels)));
    CHECK(!memcmp(pixels, previous, sizeof(pixels)));
    test_movie();
    return EXIT_SUCCESS;
}

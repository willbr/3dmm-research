#include "three_mm/codec.h"
#include "three_mm/data_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "codec check %d: %s\n", __LINE__, #x);                                                     \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
static ThreeMMU32 random_state;
static ThreeMMU32 random_word(void)
{
    random_state = random_state * 1103515245UL + 12345;
    return random_state;
}
static void fill(unsigned char *p, ThreeMMU32 n, int kind)
{
    ThreeMMU32 i, v;
    random_state = 0x13579;
    for (i = 0; i < n; ++i)
    {
        switch (kind)
        {
        case 0:
            p[i] = (unsigned char)random_word();
            break;
        case 1:
            v = random_word();
            p[i] = (v & 15) ? 0 : (unsigned char)random_word();
            break;
        case 2:
            p[i] = (unsigned char)(random_word() & 15);
            break;
        case 3:
            p[i] = (unsigned char)(((i / 32) & 1) ? random_word() : i);
            break;
        case 4:
            p[i] = i < 5000 ? (unsigned char)random_word() : p[i - 4500];
            break;
        default:
            break;
        }
    }
    if (kind == 5)
    {
        random_state = 0x2468;
        for (i = 0; i < n; ++i)
            p[i] = (unsigned char)(random_word() >> 16);
    }
}
static ThreeMMU32 hash = 2166136261UL;
static void hash_byte(unsigned char n)
{
    hash = (hash ^ n) * 16777619UL;
}
static void hash_word(ThreeMMU32 n)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        hash_byte((unsigned char)(n >> (8 * i)));
}
/* Frozen before migration from the native x64 codec, including exact encoded
 * bytes, failed capacity checks and successful decoded bytes/counts. */
static void golden(void)
{
    static const ThreeMMU32 sizes[] = {1, 2, 3, 7, 8, 15, 31, 64, 255, 256, 511, 4095, 4096, 4097, 8192, 16384};
    static const ThreeMMI32 formats[] = {THREE_MM_CODEC_KCDC, THREE_MM_CODEC_KCD2};
    unsigned char input[16384], output[32768], decoded[16384];
    unsigned f, s, kind, c;
    ThreeMMU32 capacities[4], i;
    ThreeMMI32 written, read;
    int ok;
    for (f = 0; f < 2; ++f)
        for (s = 0; s < sizeof(sizes) / sizeof(*sizes); ++s)
            for (kind = 0; kind < 6; ++kind)
            {
                fill(input, sizes[s], kind);
                capacities[0] = 7;
                capacities[1] = sizes[s];
                capacities[2] = sizes[s] + 16;
                capacities[3] = sizes[s] * 2;
                for (c = 0; c < 4; ++c)
                {
                    ok = three_mm_codec_encode(formats[f], input, sizes[s], output, capacities[c], &written, NULL) ==
                         THREE_MM_DATA_OK;
                    hash_byte((unsigned char)ok);
                    if (!ok)
                        continue;
                    hash_word(written);
                    for (i = 0; i < (ThreeMMU32)written; ++i)
                        hash_byte(output[i]);
                    ok = three_mm_codec_decode(formats[f], output, written, decoded, sizes[s], &read) ==
                         THREE_MM_DATA_OK;
                    hash_byte((unsigned char)ok);
                    if (ok)
                    {
                        hash_word(read);
                        for (i = 0; i < (ThreeMMU32)read; ++i)
                            hash_byte(decoded[i]);
                    }
                }
            }
    CHECK(hash == 0x5da3057cUL);
}
/* Independent bitstream constructor exercises all offset tiers, overlap,
 * non-byte-aligned KCD2 literal splits and format-specific termination. */
typedef struct Fixture
{
    unsigned char bytes[12000];
    unsigned bit;
} Fixture;
static void bits(Fixture *f, ThreeMMU32 n, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i, ++f->bit)
        if ((n >> i) & 1)
            f->bytes[f->bit / 8] |= (unsigned char)(1u << (f->bit % 8));
}
static void length(Fixture *f, ThreeMMU32 n)
{
    unsigned count = 0;
    ThreeMMU32 v = n;
    while (v >>= 1)
        ++count;
    bits(f, 0xffffffffUL, count);
    bits(f, n << 1, count + 1);
}
static unsigned char pattern(ThreeMMU32 i)
{
    return (unsigned char)(i * 13 + 7);
}
static void literals(Fixture *f, ThreeMMU32 n, int second, ThreeMMU32 start)
{
    ThreeMMU32 i;
    unsigned split;
    if (!second)
    {
        for (i = 0; i < n; ++i)
            bits(f, (ThreeMMU32)pattern(start + i) << 1, 9);
        return;
    }
    length(f, n);
    bits(f, 0, 1);
    split = f->bit % 8;
    if (split)
        bits(f, pattern(start + n - 1), 8 - split);
    else
        split = 8;
    for (i = 1; i < n; ++i)
        bits(f, pattern(start + i - 1), 8);
    bits(f, pattern(start + n - 1) >> (8 - split), split);
}
static ThreeMMU32 tail(Fixture *f)
{
    if (f->bit % 8)
        bits(f, 0xff, 8 - f->bit % 8);
    bits(f, 0xffffffffUL, 32);
    bits(f, 0xffff, 16);
    return f->bit / 8;
}
static void fixtures(void)
{
    const ThreeMMU32 offsets[] = {1, 65, 577, 4673};
    const unsigned widths[] = {6, 9, 12, 20};
    unsigned tier, second, i;
    ThreeMMU32 n, total;
    ThreeMMI32 written;
    unsigned char output[10000];
    Fixture f;
    for (second = 0; second < 2; ++second)
        for (tier = 0; tier < 4; ++tier)
        {
            memset(&f, 0, sizeof(f));
            f.bit = 8;
            if (offsets[tier] > 4095)
            {
                literals(&f, 2300, second, 0);
                literals(&f, offsets[tier] - 2300, second, 2300);
            }
            else
                literals(&f, offsets[tier], second, 0);
            if (second)
                length(&f, 7);
            bits(&f, (1u << (tier + 1)) - 1, tier == 3 ? 4 : tier + 2);
            bits(&f, 0, widths[tier]);
            if (!second)
                length(&f, 7);
            total = offsets[tier] + 8 + (tier == 3);
            if (!second)
                bits(&f, 0xffffff, 24);
            n = tail(&f);
            CHECK(three_mm_codec_decode(second ? THREE_MM_CODEC_KCD2 : THREE_MM_CODEC_KCDC, f.bytes, n, output,
                                        sizeof(output), &written) == THREE_MM_DATA_OK);
            CHECK((ThreeMMU32)written == total);
            for (i = 0; i < total; ++i)
                CHECK(output[i] == pattern(i % offsets[tier]));
            written = 123;
            CHECK(three_mm_codec_decode(second ? THREE_MM_CODEC_KCD2 : THREE_MM_CODEC_KCDC, f.bytes, n, output,
                                        total - 1, &written) != THREE_MM_DATA_OK &&
                  written == 123);
            f.bytes[n - 1] = 0;
            CHECK(three_mm_codec_decode(second ? THREE_MM_CODEC_KCD2 : THREE_MM_CODEC_KCDC, f.bytes, n, output,
                                        sizeof(output), &written) != THREE_MM_DATA_OK);
        }
}
typedef struct Heap
{
    int calls, fail, live;
} Heap;
static void *allocate(void *context, void *old, ThreeMMU32 old_size, ThreeMMU32 size, int priority)
{
    Heap *h = (Heap *)context;
    void *p;
    CHECK(!old && !old_size && priority == THREE_MM_STORAGE_NORMAL);
    if (++h->calls == h->fail)
        return NULL;
    p = malloc(size);
    CHECK(p);
    ++h->live;
    return p;
}
static void release(void *context, void *p, ThreeMMU32 size)
{
    Heap *h = (Heap *)context;
    (void)size;
    --h->live;
    free(p);
}
static void failures(void)
{
    unsigned char source[64], output[128];
    ThreeMMI32 written;
    ThreeMMStorageAllocator a;
    Heap h;
    int i;
    memset(source, 0, sizeof(source));
    a.context = &h;
    a.resize = allocate;
    a.release = release;
    for (i = 1; i <= 2; ++i)
    {
        memset(&h, 0, sizeof(h));
        h.fail = i;
        written = 123;
        CHECK(three_mm_codec_encode(THREE_MM_CODEC_KCD2, source, 64, output, 128, &written, &a) ==
              THREE_MM_DATA_MEMORY);
        CHECK(!h.live && h.calls == i && written == 123);
    }
    memset(&h, 0, sizeof(h));
    CHECK(three_mm_codec_encode(THREE_MM_CODEC_KCD2, source, 64, output, 128, &written, &a) == THREE_MM_DATA_OK &&
          !h.live);
    CHECK(three_mm_codec_encode(THREE_MM_CODEC_KCD2, source, THREE_MM_STORAGE_LIMIT / 4, output, 128, &written, &a) ==
          THREE_MM_DATA_ARGUMENT);
}
static void malformed(void)
{
    unsigned char input[96], output[64 + 2];
    ThreeMMI32 written;
    unsigned i, n, iteration;
    int result;
    random_state = 123;
    for (iteration = 0; iteration < 12000; ++iteration)
    {
        n = 8 + random_word() % 89;
        for (i = 0; i < n; ++i)
            input[i] = (unsigned char)(random_word() >> 16);
        input[0] = 0;
        memset(input + n - 6, 0xff, 6);
        memset(output, 0xa5, sizeof(output));
        written = 123;
        result = three_mm_codec_decode(iteration & 1 ? THREE_MM_CODEC_KCD2 : THREE_MM_CODEC_KCDC, input, n, output + 1,
                                       64, &written);
        CHECK(output[0] == 0xa5 && output[65] == 0xa5);
        if (result == THREE_MM_DATA_OK)
            CHECK(written >= 0 && written <= 64);
        else
            CHECK(written == 123);
    }
}
typedef struct Encoder
{
    int missing, fail, wrong, finds, calls;
} Encoder;
static void *find_encoder(void *context, ThreeMMI32 format)
{
    Encoder *e = (Encoder *)context;
    CHECK(format == 0x41424344);
    ++e->finds;
    return e->missing ? NULL : e;
}
static int encode_payload(void *context, ThreeMMI32 format, const void *input, ThreeMMU32 bytes, void *output,
                          ThreeMMU32 capacity, ThreeMMI32 *written)
{
    Encoder *e = (Encoder *)context;
    CHECK(format == 0x41424344 && input && bytes == 32 && capacity == 23);
    ++e->calls;
    if (e->fail)
    {
        *written = 2;
        return 0;
    }
    memcpy(output, "abc", 3);
    *written = e->wrong ? 24 : 3;
    return 1;
}
static void envelope(void)
{
    Encoder e;
    ThreeMMPackedDecoder encoder;
    unsigned char source[32], output[64];
    const unsigned char expected[] = {0x41, 0x42, 0x43, 0x44, 0, 0, 0, 32, 'a', 'b', 'c'};
    ThreeMMI32 written = 123;
    memset(&e, 0, sizeof(e));
    memset(source, 0, sizeof(source));
    encoder.context = &e;
    encoder.find = find_encoder;
    encoder.convert = encode_payload;
    CHECK(three_mm_packed_encode(&encoder, 0x41424344, source, 9, NULL, 0, &written) == THREE_MM_DATA_NOT_ENCODED &&
          written == 123 && !e.finds);
    CHECK(three_mm_packed_encode(&encoder, 0x41424344, source, 32, NULL, 0, &written) == THREE_MM_DATA_OK &&
          written == 31 && e.finds == 1 && !e.calls);
    CHECK(three_mm_packed_encode(&encoder, 0x41424344, source, 32, output, 64, &written) == THREE_MM_DATA_OK &&
          written == 11 && !memcmp(output, expected, 11));
    e.missing = 1;
    written = 123;
    CHECK(three_mm_packed_encode(&encoder, 0x41424344, source, 32, output, 64, &written) == THREE_MM_DATA_NOT_ENCODED &&
          written == 123);
    e.missing = 0;
    e.fail = 1;
    CHECK(three_mm_packed_encode(&encoder, 0x41424344, source, 32, output, 64, &written) == THREE_MM_DATA_CODEC &&
          written == 2);
    e.fail = 0;
    e.wrong = 1;
    CHECK(three_mm_packed_encode(&encoder, 0x41424344, source, 32, output, 64, &written) == THREE_MM_DATA_WRONG_SIZE);
}
int main(void)
{
    golden();
    fixtures();
    failures();
    malformed();
    envelope();
    return 0;
}

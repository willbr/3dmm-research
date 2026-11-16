#include "mm/string_value.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static MMU32 hash;
static unsigned get(const void *buffer, unsigned unit, MMI32 i)
{
    return unit == 1 ? ((const unsigned char *)buffer)[i] : ((const unsigned short *)buffer)[i];
}
static void put(void *buffer, unsigned unit, MMI32 i, unsigned value)
{
    if (unit == 1) ((unsigned char *)buffer)[i] = (unsigned char)value;
    else ((unsigned short *)buffer)[i] = (unsigned short)value;
}
static void word(MMU32 value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) hash = (hash ^ ((value >> (8 * i)) & 255)) * 16777619UL;
}
static void snapshot(const MMStringValue *s, int result)
{
    MMI32 i, count = (MMI32)get(s->buffer, s->unit, 0);
    assert(count <= 255 && !get(s->buffer, s->unit, count + 1));
    word((MMU32)result);
    word((MMU32)count);
    for (i = 0; i <= count; ++i) word(get(s->buffer, s->unit, i + 1));
}
/* This corpus is frozen against the original native String implementation.
 * Repeat it using both character widths, with guards around mutable storage. */
static void corpus(unsigned unit)
{
    unsigned char narrow[259], input8[512], copy8[257];
    unsigned short wide[259], input16[512], copy16[257];
    void *input = unit == 1 ? (void *)input8 : (void *)input16;
    MMStringValue s = {0}, copy = {0};
    MMI32 lengths[] = {0, 1, 10, 127, 254, 255};
    MMI32 counts[] = {0, 1, 3, 126, 127, 254, 255, 256, 511};
    unsigned specials[] = {'\r', '\n', '\t', '"', '\\'};
    const char *numbers[] = {"", " ", "0", "0x", "0X ", "00", "08", "09", "1", "2", "19", "a", "z",
        "  - + - 123  ", "+", "-", "--123", "-1", "1  ", "1 2", "1\t", "0x10", "0xffffffff",
        "2147483648", "4294967295", "-4294967296", "8589934593", "-2147483649"};
    unsigned li, ci, ni, si;
    MMI32 i, length, count, position, radix, number;
    int result;
    memset(narrow, 0xa5, sizeof(narrow));
    memset(wide, 0xa5, sizeof(wide));
    s.buffer = unit == 1 ? (void *)(narrow + 1) : (void *)(wide + 1);
    s.unit = copy.unit = unit;
    copy.buffer = unit == 1 ? (void *)copy8 : (void *)copy16;
    hash = 2166136261UL;
    for (i = 0; i < 512; ++i) put(input, unit, i, (unsigned)('A' + i % 26));
    for (li = 0; li < sizeof(lengths) / sizeof(lengths[0]); ++li) {
        length = lengths[li];
        mm_string_value_set(&s, input, length);
        mm_string_value_copy(&copy, s.buffer);
        snapshot(&copy, 1);
        mm_string_value_copy(&s, copy.buffer);
        snapshot(&s, 1);
        mm_string_value_copy(&s, s.buffer);
        assert(mm_string_value_equal(&s, input, length));
        for (ci = 0; ci < sizeof(counts) / sizeof(counts[0]); ++ci) {
            count = counts[ci];
            mm_string_value_set(&s, input, count);
            snapshot(&s, 1);
            mm_string_value_set(&s, input, length);
            result = mm_string_value_append(&s, input, count);
            snapshot(&s, result);
            for (position = 0; position <= length; ++position) {
                mm_string_value_set(&s, input, length);
                mm_string_value_delete(&s, position, count);
                snapshot(&s, 1);
                mm_string_value_set(&s, input, length);
                result = mm_string_value_insert(&s, position, input, count);
                snapshot(&s, result);
            }
            mm_string_value_set(&s, input, length);
            result = mm_string_value_insert(&s, -10, input, count);
            snapshot(&s, result);
            mm_string_value_set(&s, input, length);
            result = mm_string_value_insert(&s, length + 10, input, count);
            snapshot(&s, result);
        }
    }
    for (si = 0; si < sizeof(specials) / sizeof(specials[0]); ++si) {
        for (length = 0; length <= 255; ++length) {
            for (position = 0; position < length; ++position) {
                for (i = 0; i < length; ++i) put(input, unit, i, i == position ? specials[si] : 'a');
                mm_string_value_set(&s, input, length);
                result = mm_string_value_expand(&s);
                snapshot(&s, result);
            }
        }
    }
    for (ni = 0; ni < sizeof(numbers) / sizeof(numbers[0]); ++ni) {
        length = (MMI32)strlen(numbers[ni]);
        for (i = 0; i < length; ++i) put(input, unit, i, (unsigned char)numbers[ni][i]);
        mm_string_value_set(&s, input, length);
        for (radix = 0; radix < 36; ++radix) {
            if (radix == 1) continue;
            number = 1234567;
            result = mm_string_value_number(&s, radix, &number);
            word((MMU32)result);
            if (result) word((MMU32)number);
            else assert(number == 1234567);
        }
    }
    printf("String unit %u fingerprint: %08lX\n", unit, (unsigned long)hash);
    assert(hash == 0xC67CD6FDUL);
    assert(narrow[0] == 0xa5 && narrow[258] == 0xa5);
    assert(wide[0] == 0xa5a5 && wide[258] == 0xa5a5);
    mm_string_value_clear(&s);
    assert(!get(s.buffer, unit, 0) && !get(s.buffer, unit, 1));
    assert(mm_string_value_equal(&s, NULL, 0));
    assert(mm_string_value_append(&s, NULL, 0));
    assert(mm_string_value_insert(&s, 10, NULL, 0));
}
static void wide_values(void)
{
    unsigned short buffer[257], input[] = {0x1234, '\t', 0xffff, '\r', '\n', '"', '\\'};
    unsigned short expected[] = {0x1234, '\\', 't', 0xffff, '\\', 'n', '\n', '\\', '"', '\\', '\\'};
    MMStringValue s = {0};
    MMI32 number = 42;
    s.buffer = buffer; s.unit = 2;
    mm_string_value_set(&s, input, 7);
    assert(mm_string_value_expand(&s));
    assert(mm_string_value_equal(&s, expected, 11));
    assert(!mm_string_value_number(&s, 10, &number) && number == 42);
    /* Multiple escapes can force partial publication after the first escape. */
    memset(buffer, 0, sizeof(buffer));
    buffer[0] = 255;
    for (number = 1; number <= 255; ++number) buffer[number] = '\\';
    assert(!mm_string_value_expand(&s));
    assert(buffer[0] == 254 && buffer[255] == 0);
    for (number = 1; number <= 254; ++number) assert(buffer[number] == '\\');
}
typedef struct CopyObservation {
    unsigned char *buffer;
    unsigned calls;
} CopyObservation;
static void observe_copy(void *context, const void *source, void *destination, size_t bytes)
{
    CopyObservation *observation = (CopyObservation *)context;
    unsigned char *buffer = observation->buffer;
    /* The old length and shifted tail are visible before the insertion copy. */
    assert(buffer[0] == 4 && buffer[4] == 'b' && buffer[5] == 'c' && buffer[6] == 'd');
    assert(destination == buffer + 2 && bytes == 2 && !memcmp(source, "XY", 2));
    observation->calls += 1;
}
static void copy_observation(void)
{
    unsigned char buffer[257] = {4, 'a', 'b', 'c', 'd', 0};
    CopyObservation observation;
    MMStringValue s;
    observation.buffer = buffer; observation.calls = 0;
    s.buffer = buffer; s.context = &observation; s.unit = 1; s.check_copy = observe_copy;
    assert(mm_string_value_insert(&s, 1, "XY", 2));
    assert(observation.calls == 1 && mm_string_value_equal(&s, "aXYbcd", 6));
    assert(mm_string_value_append(&s, NULL, 0) && observation.calls == 1);
}
int main(void)
{
    assert(sizeof(unsigned short) == 2);
    corpus(1);
    corpus(2);
    wide_values();
    copy_observation();
    return 0;
}

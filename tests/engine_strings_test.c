#include "three_mm/string_table.h"
#include "three_mm/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "string check failed at %d: %s\n", __LINE__, #x);                                          \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Storage
{
    unsigned char *strings, *entries;
    int fail, grows;
} Storage;
static int reserve(void *context, ThreeMMStringTable *t, ThreeMMU32 strings, ThreeMMU32 entries)
{
    Storage *s = (Storage *)context;
    unsigned char *a, *b;
    if (s->fail)
        return 0;
    a = (unsigned char *)malloc(strings ? strings : 1);
    b = (unsigned char *)malloc(entries ? entries : 1);
    CHECK(a && b);
    if (t->bytes)
        memcpy(a, t->strings, t->bytes);
    if (t->count)
        memcpy(b, t->entries, t->count * t->stride);
    free(s->strings);
    free(s->entries);
    s->strings = t->strings = a;
    s->entries = t->entries = b;
    t->string_capacity = strings;
    t->entry_capacity = entries;
    ++s->grows;
    return 1;
}
static void init(ThreeMMStringTable *t, Storage *s, int allocated, unsigned unit)
{
    memset(t, 0, sizeof(*t));
    memset(s, 0, sizeof(*s));
    t->stride = 8;
    t->free_count = allocated ? 0 : -1;
    t->unit_bytes = unit;
    t->context = s;
    t->reserve = reserve;
    t->trash_unused = 1;
}
static void expect(const ThreeMMStringTable *t, ThreeMMU32 i, const void *bytes, ThreeMMU32 n, ThreeMMU32 extra)
{
    const void *value;
    ThreeMMU32 count, actual;
    CHECK(three_mm_strings_get(t, i, &value, &count));
    CHECK(count == n && (!n || !memcmp(value, bytes, n * t->unit_bytes)));
    memcpy(&actual, t->entries + i * t->stride + 4, 4);
    CHECK(actual == extra);
}
static int compare(void *unused, const void *a, ThreeMMU32 na, const void *b, ThreeMMU32 nb)
{
    int value;
    (void)unused;
    if (na != nb)
        return na < nb ? -1 : 1; /* Legacy internal sorting is length first. */
    value = memcmp(a, b, na);
    return value < 0 ? -1 : value > 0 ? 1 : 0;
}
static size_t convert(void *unused, const void *bytes, size_t count, int source, void *output, size_t capacity)
{
    (void)unused;
    return three_mm_text_ansi(NULL, bytes, count, source, (char *)output, capacity);
}
static void mutations(void)
{
    ThreeMMStringTable t;
    Storage s;
    ThreeMMU32 one = 1, two = 2, three = 3, pos = 999;
    const void *value = NULL;
    ThreeMMU32 n = 999;
    unsigned char snapshot[64];
    init(&t, &s, 0, 1);
    CHECK(three_mm_strings_add(&t, "cat", 3, &one, &pos) && pos == 0);
    CHECK(three_mm_strings_insert(&t, 0, "A", 1, &two));
    CHECK(three_mm_strings_insert(&t, 1, "", 0, &three));
    CHECK(t.count == 3 && t.bytes == 7);
    expect(&t, 0, "A", 1, two);
    expect(&t, 1, "", 0, three);
    expect(&t, 2, "cat", 3, one);
    CHECK(three_mm_strings_move(&t, 0, 3));
    expect(&t, 2, "A", 1, two);
    CHECK(three_mm_strings_move(&t, 2, 0));
    expect(&t, 0, "A", 1, two);
    CHECK(three_mm_strings_put(&t, 2, "dog", 3));
    expect(&t, 2, "dog", 3, one);
    memcpy(snapshot, t.strings, t.bytes);
    s.fail = 1;
    CHECK(!three_mm_strings_put(&t, 2, "longer", 6));
    CHECK(!three_mm_strings_insert(&t, 1, "x", 1, &one));
    CHECK(t.count == 3 && t.bytes == 7 && !memcmp(snapshot, t.strings, t.bytes));
    s.fail = 0;
    CHECK(three_mm_strings_put(&t, 2, "longer", 6));
    CHECK(three_mm_strings_put(&t, 0, "B", 1));
    CHECK(three_mm_strings_delete(&t, 1));
    expect(&t, 0, "B", 1, two);
    expect(&t, 1, "longer", 6, one);
    CHECK(three_mm_strings_put(&t, 1, "", 0));
    CHECK(three_mm_strings_find(&t, "B", 1, NULL, NULL, &pos) && pos == 0);
    CHECK(!three_mm_strings_find(&t, "Q", 1, NULL, NULL, &pos) && pos == 2);
    CHECK(!three_mm_strings_get(&t, 99, &value, &n) && value == NULL && n == 999);
    memcpy(snapshot, t.entries, t.count * t.stride);
    three_mm_strings_swap_offsets(&t);
    three_mm_strings_swap_offsets(&t);
    CHECK(!memcmp(snapshot, t.entries, t.count * t.stride));
    CHECK(three_mm_strings_delete(&t, 0) && three_mm_strings_delete(&t, 0));
    CHECK(!t.count && !t.bytes);
    CHECK(three_mm_strings_insert(&t, 0, "a", 1, &one));
    CHECK(three_mm_strings_insert(&t, 1, "b", 1, &two));
    CHECK(three_mm_strings_insert(&t, 2, "b", 1, &three));
    CHECK(three_mm_strings_find(&t, "b", 1, compare, NULL, &pos) && pos == 1);
    CHECK(!three_mm_strings_find(&t, "z", 1, compare, NULL, &pos) && pos == 3);
    CHECK(!three_mm_strings_find(&t, "", 0, compare, NULL, &pos) && pos == 0);
    free(s.strings);
    free(s.entries);
    init(&t, &s, 1, 1);
    CHECK(three_mm_strings_add(&t, "a", 1, &one, &pos));
    CHECK(three_mm_strings_add(&t, "b", 1, &two, &pos));
    CHECK(three_mm_strings_add(&t, "c", 1, &three, &pos));
    CHECK(three_mm_strings_delete(&t, 0));
    CHECK(t.free_count == 1 && !three_mm_strings_get(&t, 0, &value, &n));
    s.fail = 1;
    CHECK(!three_mm_strings_add(&t, "a long string", 13, &one, &pos));
    CHECK(t.count == 3 && t.free_count == 1);
    s.fail = 0;
    CHECK(three_mm_strings_add(&t, "d", 1, &one, &pos) && pos == 0 && !t.free_count);
    expect(&t, 0, "d", 1, one);
    CHECK(three_mm_strings_delete(&t, 1));
    CHECK(three_mm_strings_delete(&t, 2));
    CHECK(t.count == 1 && t.free_count == 0);
    CHECK(three_mm_strings_delete(&t, 0));
    CHECK(!t.count && !t.bytes && !t.free_count);
    free(s.strings);
    free(s.entries);
}
static ThreeMMU32 hash(const char *p, unsigned n)
{
    ThreeMMU32 h = 2166136261UL;
    unsigned i;
    for (i = 0; i < n; ++i)
        h = (h ^ (unsigned char)p[i]) * 16777619UL;
    return h;
}
static void text(void)
{
    char input[256], output[512];
    unsigned short wide[256];
    const unsigned char be[] = {0, 'A', 0, 0, 0x20, 0xac, 0xd8, 0, 0xdc, 0};
    unsigned i;
    for (i = 0; i < 256; ++i)
        input[i] = (char)i;
    CHECK(three_mm_text_ansi(NULL, input + 128, 128, THREE_MM_TEXT_MAC, output, sizeof(output)) == 128);
    CHECK(hash(output, 128) == 0x90f1dd00UL);
    memcpy(output, input + 128, 128);
    three_mm_text_map_bytes(output, 128, 0);
    CHECK(hash(output, 128) == 0x3e631f88UL);
    CHECK(three_mm_text_utf16(NULL, input, 256, THREE_MM_TEXT_WINDOWS, wide, 256) == 256);
    CHECK(wide[128] == 0x20ac && wide[233] == 0xe9);
    CHECK(three_mm_text_ansi(NULL, be, sizeof(be), THREE_MM_TEXT_UTF16_BE, NULL, 0) == 4);
    memset(output, 0x17, sizeof(output));
    CHECK(!three_mm_text_ansi(NULL, be, sizeof(be), THREE_MM_TEXT_UTF16_BE, output, 3) && output[0] == 0x17);
    CHECK(three_mm_text_ansi(NULL, be, sizeof(be), THREE_MM_TEXT_UTF16_BE, output, sizeof(output)) == 4);
    CHECK(output[0] == 'A' && output[1] == 0 && (unsigned char)output[2] == 128 && output[3] == '?');
    CHECK(!three_mm_text_utf16(NULL, be, 3, THREE_MM_TEXT_UTF16_BE, wide, 256));
    CHECK(!three_mm_text_ansi(NULL, be, sizeof(be), 0, output, sizeof(output)));
    CHECK(!three_mm_text_ansi(NULL, NULL, 1, THREE_MM_TEXT_WINDOWS, output, sizeof(output)));
    CHECK(three_mm_text_compare("a", 1, "A", 1, 0, 1) == 0);
    CHECK(three_mm_text_compare("a", 1, "A", 1, 0, 0) > 0);
}
static void translation(void)
{
    ThreeMMStringTable t;
    Storage s;
    ThreeMMU32 extra = 0x12345678UL;
    const unsigned short original[] = {'A', 0x20ac};
    const unsigned char raw[] = {0, 2, 0, 'A', 0x20, 0xac};
    const unsigned char expected[] = {'A', 128};
    const char mac[] = {(char)0x8e};
    init(&t, &s, 0, 2);
    CHECK(three_mm_strings_add(&t, original, 2, &extra, NULL));
    expect(&t, 0, original, 2, extra);
    CHECK(three_mm_strings_put(&t, 0, original, 1));
    CHECK(three_mm_strings_delete(&t, 0));
    free(s.strings);
    free(s.entries);
    init(&t, &s, 0, 1);
    CHECK(three_mm_strings_add(&t, mac, 1, &extra, NULL));
    CHECK(three_mm_strings_translate(&t, THREE_MM_TEXT_MAC, THREE_MM_TEXT_WINDOWS, convert, NULL));
    expect(&t, 0, "\351", 1, extra);
    CHECK(three_mm_strings_translate_same(&t, THREE_MM_TEXT_MAC, THREE_MM_TEXT_WINDOWS, 0));
    expect(&t, 0, mac, 1, extra);
    CHECK(reserve(&s, &t, sizeof(raw), 8));
    memcpy(t.strings, raw, sizeof(raw));
    t.bytes = sizeof(raw);
    CHECK(three_mm_strings_translate(&t, THREE_MM_TEXT_UTF16_BE, THREE_MM_TEXT_WINDOWS, convert, NULL));
    expect(&t, 0, expected, 2, extra);
    free(s.strings);
    free(s.entries);
}
int main(void)
{
    mutations();
    text();
    translation();
    return 0;
}

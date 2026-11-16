#include "mm/string_ops.h"
#include "string_ops_cases.h"
#include <assert.h>
#include <stdio.h>

static MMU32 hash;
static void word(void *context, MMU32 value)
{
    unsigned i;
    (void)context;
    for (i = 0; i < 4; ++i) hash = (hash ^ ((value >> (8 * i)) & 255)) * 16777619UL;
}
static int valid(void *context, unsigned char *p, int terminated)
{ return mm_string_counted_valid((MMStringOps *)context, p, terminated); }
static MMU32 length(void *context, unsigned char *p)
{ (void)context; return (MMU32)mm_string_zero_length(p, 1); }
static int equal(void *context, unsigned char *a, MMI32 na, unsigned char *b, MMI32 nb)
{ return mm_string_raw_equal((MMStringOps *)context, a, (MMU32)na, b, (MMU32)nb); }
static MMU32 compare(void *context, unsigned char *a, MMI32 na, unsigned char *b, MMI32 nb)
{ return mm_string_internal_compare((MMStringOps *)context, a, (MMU32)na, b, (MMU32)nb); }
static MMU32 flags(void *context, unsigned value)
{ (void)context; return mm_string_character_flags(value); }
static void corpus(void)
{
    MMStringOps ops = {0};
    StringOpsCases cases;
    ops.unit = 1; ops.signed_units = 1;
    cases.context = &ops; cases.word = word; cases.valid = valid; cases.length = length;
    cases.equal = equal; cases.compare = compare; cases.flags = flags;
    hash = 2166136261UL;
    string_ops_cases(&cases);
    printf("String ops fingerprint: %08lX\n", (unsigned long)hash);
    assert(hash == 0xDEBC3794UL);
}
typedef struct Probe {
    char events[64]; unsigned n, upper_calls, calls;
    size_t counts[8]; int result;
} Probe;
static void event(Probe *p, char e)
{ assert(p->n < sizeof(p->events) - 1); p->events[p->n++] = e; p->events[p->n] = 0; }
static void check_range(void *context, const void *p, size_t bytes)
{ (void)p; (void)bytes; event((Probe *)context, 'R'); }
static void check_copy(void *context, const void *source, void *destination, size_t bytes)
{ (void)source; (void)destination; assert(bytes <= 510); event((Probe *)context, 'C'); }
static void check_counted(void *context, const void *p)
{ (void)p; event((Probe *)context, 'V'); }
static void uppercase(void *context, void *buffer, size_t count)
{
    Probe *p = (Probe *)context;
    assert(p->upper_calls < 8); p->counts[p->upper_calls++] = count;
    event(p, 'U'); mm_text_case_bytes((char *)buffer, count, 1);
}
static int collation(void *context, const void *a, size_t na, const void *b, size_t nb, int ignore)
{
    Probe *p = (Probe *)context;
    (void)a; (void)na; (void)b; (void)nb; (void)ignore;
    event(p, 'X'); return p->result;
}
static void policies(void)
{
    MMStringOps ops = {0}; Probe p;
    unsigned char a[511], b[511], counted[] = {2, 'A', 'B', 0}, x = 128, y = 127;
    unsigned i;
    int ranks[] = {-1, 0, 1, 2, 7};
    MMU32 expected[] = {MM_STRING_LESS, MM_STRING_EQUAL, MM_STRING_GREATER, MM_STRING_LESS, MM_STRING_LESS};
    ops.unit = 1; ops.signed_units = 1; ops.context = &p;
    ops.check_range = check_range; ops.check_copy = check_copy; ops.check_counted = check_counted;
    ops.upper = uppercase; ops.compare = collation;
    memset(a, 'A', sizeof(a)); memset(b, 'a', sizeof(b)); memset(&p, 0, sizeof(p));
    assert(mm_string_user_equal(&ops, a, 511, b, 511, 1));
    assert(!strcmp(p.events, "CCUURRCCUURRCCUURR") && p.upper_calls == 6);
    assert(p.counts[0] == 255 && p.counts[1] == 255 && p.counts[2] == 255 && p.counts[3] == 255 && p.counts[4] == 1 && p.counts[5] == 1);
    memset(&p, 0, sizeof(p)); b[0] = 'Z';
    assert(!mm_string_user_equal(&ops, a, 511, b, 511, 1) && !strcmp(p.events, "CCUURR"));
    memset(&p, 0, sizeof(p));
    assert(!mm_string_user_equal(&ops, a, 511, b, 511, 0) && !strcmp(p.events, "RR"));
    memset(&p, 0, sizeof(p));
    assert(!mm_string_user_equal(&ops, a, 510, b, 511, 1) && !p.n);
    assert(mm_string_user_equal(&ops, NULL, 0, NULL, 0, 1) && !p.n);
    assert(mm_string_counted_valid(&ops, counted, 1) && !strcmp(p.events, "RV"));
    memset(&p, 0, sizeof(p)); counted[2] = 0;
    assert(!mm_string_counted_valid(&ops, counted, 1) && !strcmp(p.events, "R"));
    for (i = 0; i < sizeof(ranks) / sizeof(ranks[0]); ++i) {
        memset(&p, 0, sizeof(p)); p.result = ranks[i];
        assert(mm_string_user_compare(&ops, "z", 1, "aa", 2, 1) == expected[i]);
        assert(!strcmp(p.events, "X"));
    }
    memset(&p, 0, sizeof(p)); p.result = 2;
    assert(mm_string_user_compare(&ops, &x, 1, &y, 1, 0) == MM_STRING_LESS && !strcmp(p.events, "XRR"));
    ops.signed_units = 0;
    assert(mm_string_internal_compare(&ops, &x, 1, &y, 1) == MM_STRING_GREATER);
}
static void wide_rules(void)
{
    unsigned short values[] = {0, 1, 127, 128, 255, 256, 0x1234, 0x7fff, 0x8000, 0xffff}, counted[257];
    MMStringOps ops = {0}; Probe p;
    unsigned i, j, count;
    ops.unit = 2;
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (j = 0; j < sizeof(values) / sizeof(values[0]); ++j) {
            assert(mm_string_internal_compare(&ops, values + i, 1, values + j, 1) ==
                   (i == j ? MM_STRING_EQUAL : i < j ? MM_STRING_LESS : MM_STRING_GREATER));
            assert(mm_string_raw_equal(&ops, values + i, 1, values + j, 1) == (i == j));
        }
    }
    for (count = 0; count <= 255; ++count) {
        counted[0] = (unsigned short)count;
        for (i = 1; i <= count; ++i) counted[i] = 0x1234;
        counted[count + 1] = 0;
        assert(mm_string_counted_valid(&ops, counted, 1));
        if (count) { counted[count] = 0; assert(!mm_string_counted_valid(&ops, counted, 1)); }
    }
    memset(&p, 0, sizeof(p)); ops.context = &p; ops.check_range = check_range;
    counted[0] = 256;
    assert(!mm_string_counted_valid(&ops, counted, 1) && !p.n);
    assert(!mm_string_character_flags(256) && !mm_string_character_flags(0xffff));
}
static int native_compare(void *context, const void *a, size_t na, const void *b, size_t nb, int wide, int ignore)
{
    Probe *p = (Probe *)context;
    (void)a; (void)b;
    assert(na == 1 && nb == 1 && wide && ignore); ++p->calls; return p->result;
}
static void map_case(void *context, char *text, size_t count, int upper)
{
    MMTextCaseMaps *maps = (MMTextCaseMaps *)context;
    unsigned i;
    assert(count == 256 && text == (upper ? maps->upper : maps->lower));
    for (i = 0; i < 256; ++i) {
        assert((unsigned char)text[i] == i);
        if (upper) assert((unsigned char)maps->lower[i] == i);
        else assert((unsigned char)maps->upper[i] == (i ^ 0x5a));
    }
    for (i = 0; i < 256; ++i) text[i] = (char)(i ^ (upper ? 0x5a : 0xa5));
}
static void text_services(void)
{
    Probe p = {{0},0,0,0,{0},0};
    MMTextCompareHost host; MMTextCaseMaps maps;
    char text[256]; unsigned i; int rank;
    host.context = &p; host.compare = native_compare;
    for (rank = 0; rank <= 3; ++rank) {
        p.result = rank;
        assert(mm_text_compare_host(&host, text, 1, text, 1, 1, 1) == (rank ? rank - 2 : 2));
    }
    assert(p.calls == 4);
    assert(mm_text_compare_host(&host, NULL, 0, NULL, (size_t)0x80000000UL, 1, 1) == -1);
    assert(mm_text_compare_host(&host, NULL, 1, NULL, (size_t)0x80000000UL, 1, 1) == 2 && p.calls == 4);
    assert(mm_text_compare_host(NULL, "A", 1, "a", 1, 0, 1) == 0);
    mm_text_case_maps_init(&maps, &maps, map_case);
    for (i = 0; i < 256; ++i) text[i] = (char)i;
    mm_text_case_maps_apply(&maps, text, 256, 1);
    for (i = 0; i < 256; ++i) assert((unsigned char)text[i] == (i ^ 0x5a));
    for (i = 0; i < 256; ++i) text[i] = (char)i;
    mm_text_case_maps_apply(&maps, text, 256, 0);
    for (i = 0; i < 256; ++i) assert((unsigned char)text[i] == (i ^ 0xa5));
    mm_text_case_maps_init(&maps, NULL, NULL);
    assert(maps.upper['a'] == 'A' && maps.lower['Z'] == 'z');
}
static void encoding(void)
{
    char input[256], output[258], bytes[257], expected[256], long_input[257];
    unsigned short wide[259], original[] = {0x1234, 0x8000, 0x00ff, 0};
    MMStringValue s = {0}; size_t count, i; int current, to_current;
    s.unit = 2; s.buffer = wide + 1;
    for (count = 0; count <= 255; ++count) {
        for (i = 0; i < count; ++i) input[i] = (char)('A' + i % 26);
        input[count] = 0;
        wide[0] = wide[258] = 0xa5a5;
        assert(mm_string_from_ansi(&s, NULL, input, count) == (count < 255));
        assert(wide[0] == 0xa5a5 && wide[258] == 0xa5a5);
        if (count == 255) assert(wide[1] == 0 && wide[2] == 0);
        else {
            assert(wide[1] == count && !wide[count + 2]);
            memset(output, 0x17, sizeof(output));
            assert(mm_string_to_ansi(&s, NULL, output + 1));
            assert(!memcmp(output + 1, input, count + 1) && output[0] == 0x17 && output[count + 2] == 0x17);
        }
        s.unit = 1; s.buffer = bytes;
        assert(mm_string_from_ansi(&s, NULL, input, count)); assert(mm_string_to_ansi(&s, NULL, output));
        assert((unsigned char)bytes[0] == count && !memcmp(output, input, count + 1));
        s.unit = 2; s.buffer = wide + 1;
    }
    wide[1] = 255;
    for (i = 2; i <= 256; ++i) wide[i] = 'A';
    wide[257] = 0; memset(output, 0x17, sizeof(output));
    assert(!mm_string_to_ansi(&s, NULL, output));
    assert(output[0] == 0 && output[1] == 0x17);
    memcpy(wide, original, sizeof(original));
    mm_string_translate(wide, 4, 2, MM_TEXT_UTF16_BE, MM_TEXT_UTF16_LE, 1);
    assert(wide[0] == 0x3412 && wide[1] == 0x0080 && wide[2] == 0xff00 && !wide[3]);
    mm_string_translate(wide, 4, 2, MM_TEXT_UTF16_BE, MM_TEXT_UTF16_LE, 0);
    assert(!memcmp(wide, original, sizeof(original)));
    for (current = MM_TEXT_MAC; current <= MM_TEXT_WINDOWS; current += MM_TEXT_WINDOWS - MM_TEXT_MAC) {
        for (to_current = 0; to_current <= 1; ++to_current) {
            for (i = 0; i < 256; ++i) input[i] = expected[i] = (char)i;
            mm_text_map_bytes(expected, 256, current == MM_TEXT_MAC ? !to_current : to_current);
            mm_string_translate(input, 256, 1, current == MM_TEXT_MAC ? MM_TEXT_WINDOWS : MM_TEXT_MAC, current, to_current);
            assert(!memcmp(input, expected, 256));
            mm_string_translate(input, 256, 1, current, current, to_current);
            assert(!memcmp(input, expected, 256));
        }
    }
    memset(long_input, 'A', 256); long_input[256] = 0;
    s.unit = 1; s.buffer = bytes;
    assert(!mm_string_from_ansi(&s, NULL, long_input, 256));
    assert((unsigned char)bytes[0] == 255 && bytes[255] == 'A' && bytes[256] == 0);
}
static size_t decode(void *context, const void *input, size_t bytes, int source, unsigned short *output, size_t capacity)
{
    Probe *p = (Probe *)context; size_t i;
    assert(bytes == 5 && source == MM_TEXT_WINDOWS && capacity == 255 && !memcmp(input, "Test", 5));
    ++p->calls;
    if (p->result) return 0;
    for (i = 0; i < bytes; ++i) output[i] = ((const unsigned char *)input)[i];
    return bytes;
}
static size_t encode(void *context, const unsigned short *input, size_t count, char *output, size_t capacity)
{
    Probe *p = (Probe *)context; size_t i;
    assert(count == 5 && capacity == 255);
    for (i = 0; i < count; ++i) assert(input[i] == (unsigned char)"Test"[i]);
    ++p->calls;
    if (p->result) return 0;
    for (i = 0; i < count; ++i) output[i] = (char)input[i];
    return count;
}
static void native_encoding(void)
{
    Probe p = {{0},0,0,0,{0},0};
    unsigned short buffer[257] = {4, 'o', 'l', 'd', '!', 0};
    char output[256];
    MMStringValue value = {0}; MMTextHost host;
    value.unit = 2; value.buffer = buffer;
    host.context = &p; host.decode = decode; host.encode = encode;
    assert(mm_string_from_ansi(&value, &host, "Test", 4));
    assert(buffer[0] == 4 && !buffer[5] && p.calls == 1);
    assert(mm_string_to_ansi(&value, &host, output));
    assert(!memcmp(output, "Test", 5) && p.calls == 2);
    p.result = 1;
    assert(!mm_string_from_ansi(&value, &host, "Test", 4));
    assert(!buffer[0] && !buffer[1] && buffer[2] == 'e' && p.calls == 3);
    assert(mm_string_from_ansi(&value, NULL, "Test", 4));
    memset(output, 0x17, sizeof(output));
    assert(!mm_string_to_ansi(&value, &host, output));
    assert(!output[0] && output[1] == 0x17 && p.calls == 4);
}
int main(void)
{
    corpus(); policies(); wide_rules(); text_services(); encoding(); native_encoding();
    return 0;
}

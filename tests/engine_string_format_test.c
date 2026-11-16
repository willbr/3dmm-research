#include "mm/string_format.h"
#include "string_format_cases.h"
#include <assert.h>

typedef struct FormatFixture {
    MMStringValue output;
    MMStringFormatArgument values[6];
    unsigned controls[6], total, calls, bad, fail_at;
    MMI32 old_count;
    const char *bad_prefix;
} FormatFixture;
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
static void convert(void *out, unsigned unit, const char *text)
{
    MMI32 i = 0;
    do { put(out, unit, i, (unsigned char)text[i]); } while (text[i++]);
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
    word((MMU32)result); word((MMU32)count);
    for (i = 0; i <= count; ++i) word(get(s->buffer, s->unit, i + 1));
}
static int next(void *context, unsigned control, MMStringFormatArgument *argument)
{
    FormatFixture *f = (FormatFixture *)context;
    unsigned index = f->calls++;
    assert(get(f->output.buffer, f->output.unit, 0) == (unsigned)f->old_count);
    assert(index < f->total && control == f->controls[index]);
    if (f->calls == f->fail_at) return 0;
    *argument = f->values[index];
    return 1;
}
static void bad(void *context)
{
    FormatFixture *f = (FormatFixture *)context;
    MMI32 i;
    assert(get(f->output.buffer, f->output.unit, 0) == (unsigned)f->old_count);
    for (i = 0; f->bad_prefix && f->bad_prefix[i] != '%'; ++i)
        assert(get(f->output.buffer, f->output.unit, i + 1) == (unsigned char)f->bad_prefix[i]);
    ++f->bad;
}
static void run_case(void *context, const FormatCase *test)
{
    unsigned unit = *(const unsigned *)context;
    unsigned char narrow[259], pattern8[256], text8[301], old8[9];
    unsigned short wide[259], pattern16[256], text16[301], old16[9];
    void *pattern = unit == 1 ? (void *)pattern8 : (void *)pattern16;
    void *text = unit == 1 ? (void *)text8 : (void *)text16;
    void *old = unit == 1 ? (void *)old8 : (void *)old16;
    FormatFixture f;
    MMStringFormatArguments arguments;
    int result;
    memset(&f, 0, sizeof(f));
    memset(narrow, 0xa5, sizeof(narrow)); memset(wide, 0xa5, sizeof(wide));
    f.output.buffer = unit == 1 ? (void *)(narrow + 1) : (void *)(wide + 1);
    f.output.unit = unit; f.old_count = 8;
    convert(old, unit, "previous"); mm_string_value_set(&f.output, old, 8);
    convert(pattern, unit, test->pattern);
    f.total = test->control && test->control != '%' ? 1 : 0;
    f.controls[0] = test->control; f.values[0].number = test->number;
    if (test->control == 's' || test->control == 'z') {
        convert(text, unit, test->text);
        f.values[0].text = text;
        f.values[0].count = test->control == 's' && test->count > 255 ? 255 : test->count;
    }
    f.bad_prefix = test->invalid ? test->pattern : NULL;
    arguments.context = &f; arguments.next = next; arguments.bad_format = bad;
    result = mm_string_format(&f.output, pattern, (MMI32)strlen(test->pattern), &arguments);
    assert(f.bad == (unsigned)test->invalid && (!test->invalid || !result));
    assert(f.calls == f.total);
    assert(narrow[0] == 0xa5 && narrow[258] == 0xa5);
    assert(wide[0] == 0xa5a5 && wide[258] == 0xa5a5);
    snapshot(&f.output, result);
}
static void mixed(unsigned unit)
{
    unsigned char out8[257], pattern8[256], counted8[8], raw8[4];
    unsigned short out16[257], pattern16[256], counted16[8], raw16[4];
    void *pattern = unit == 1 ? (void *)pattern8 : (void *)pattern16;
    void *counted = unit == 1 ? (void *)counted8 : (void *)counted16;
    void *raw = unit == 1 ? (void *)raw8 : (void *)raw16;
    const char *format = "[%s]%+05d/%z/%f/%c/%%/%u";
    const char *expected = "[counted]-0012/raw/AB\1D/C/%/4294967295";
    MMI32 i;
    FormatFixture f;
    MMStringFormatArguments arguments;
    int result;
    memset(&f, 0, sizeof(f));
    f.output.buffer = unit == 1 ? (void *)out8 : (void *)out16; f.output.unit = unit;
    mm_string_value_clear(&f.output);
    convert(pattern, unit, format); convert(counted, unit, "counted"); convert(raw, unit, "raw");
    f.total = 6;
    for (i = 0; i < 6; ++i) f.controls[i] = (unsigned)"sdzfcu"[i];
    f.values[0].text = counted; f.values[0].count = 7;
    f.values[1].number = (MMU32)0 - 12;
    f.values[2].text = raw; f.values[2].count = 3;
    f.values[3].number = 0x41420044UL;
    f.values[4].number = 'C'; f.values[5].number = 0xffffffffUL;
    arguments.context = &f; arguments.next = next; arguments.bad_format = bad;
    result = mm_string_format(&f.output, pattern, (MMI32)strlen(format), &arguments);
    assert(result && f.calls == 6 && !f.bad);
    for (i = 0; expected[i]; ++i) assert(get(f.output.buffer, unit, i + 1) == (unsigned char)expected[i]);
    assert(get(f.output.buffer, unit, 0) == (unsigned)i);
    snapshot(&f.output, result);
}
static void corpus(unsigned unit)
{
    hash = 2166136261UL;
    format_cases(&unit, run_case);
    mixed(unit);
    printf("String format unit %u fingerprint: %08lX\n", unit, (unsigned long)hash);
    assert(hash == 0x086D60CBUL);
}
static void observe_copy(void *context, const void *source, void *destination, size_t bytes)
{
    unsigned *calls = (unsigned *)context;
    unsigned char *buffer = (unsigned char *)destination - 4;
    assert(buffer[0] == 8 && buffer[1] == '+' && buffer[2] == '0' && buffer[3] == '0');
    assert(bytes == 2 && !memcmp(source, "12", 2));
    ++*calls;
}
static void boundaries(void)
{
    unsigned char output[257];
    unsigned short wide[257], pattern[] = {'%', 'c', '/', '%', 'z'}, text[] = {0x3456, 0xffff};
    FormatFixture f;
    MMStringFormatArguments arguments;
    unsigned copies = 0;
    memset(&f, 0, sizeof(f));
    f.output.buffer = output; f.output.unit = 1; f.old_count = 8;
    mm_string_value_set(&f.output, "previous", 8);
    f.total = 2; f.controls[0] = f.controls[1] = 'd';
    f.values[0].number = f.values[1].number = 12; f.fail_at = 2;
    arguments.context = &f; arguments.next = next; arguments.bad_format = bad;
    assert(!mm_string_format(&f.output, "%d/%d", 5, &arguments));
    assert(f.calls == 2 && !f.bad && mm_string_value_equal(&f.output, "12/", 3));
    mm_string_value_set(&f.output, "previous", 8);
    f.calls = 0; f.fail_at = 0; f.total = 1;
    assert(!mm_string_format(&f.output, "%255d", 5, &arguments));
    assert(f.calls == 1 && !get(output, 1, 0));
    mm_string_value_set(&f.output, "previous", 8); f.calls = 0;
    f.output.context = &copies; f.output.check_copy = observe_copy;
    assert(mm_string_format(&f.output, "%+05d", 5, &arguments));
    assert(copies == 1 && mm_string_value_equal(&f.output, "+0012", 5));
    f.output.check_copy = NULL; f.output.context = NULL; f.old_count = 5; f.calls = 0;
    f.controls[0] = 'u';
    assert(mm_string_format(&f.output, "%4294967297u", 12, &arguments));
    assert(f.calls == 1 && mm_string_value_equal(&f.output, "12", 2));
    f.old_count = 2; f.calls = 0;
    assert(mm_string_format(&f.output, "%2147483648u", 12, &arguments));
    assert(f.calls == 1 && mm_string_value_equal(&f.output, "12", 2));
    /* A dedicated wide source preserves non-ASCII characters without encoding. */
    memset(&f, 0, sizeof(f)); f.output.buffer = wide; f.output.unit = 2;
    mm_string_value_clear(&f.output); f.total = 2; f.controls[0] = 'c'; f.controls[1] = 'z';
    f.values[0].number = 0xabcd; f.values[1].text = text; f.values[1].count = 2;
    assert(mm_string_format(&f.output, pattern, 5, &arguments));
    assert(wide[0] == 4 && wide[1] == 0xabcd && wide[2] == '/' && wide[3] == 0x3456 && wide[4] == 0xffff && !wide[5]);
}
int main(void)
{
    assert(sizeof(unsigned short) == 2);
    corpus(1); corpus(2); boundaries();
    return 0;
}

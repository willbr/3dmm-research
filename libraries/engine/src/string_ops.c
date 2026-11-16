/* String rules converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/string_ops.h"
#include <string.h>
static unsigned get(const void *p, unsigned unit, size_t i)
{
    return unit == 1 ? ((const unsigned char *)p)[i] : ((const unsigned short *)p)[i];
}
static MMI32 signed_unit(unsigned value, unsigned unit, int is_signed)
{
    MMU32 modulus = unit == 1 ? 256 : 65536UL;
    return is_signed && value >= modulus / 2 ? (MMI32)value - (MMI32)modulus : (MMI32)value;
}
static void range(const MMStringOps *s, const void *p, size_t bytes)
{
    if (s->check_range) s->check_range(s->context, p, bytes);
}
size_t mm_string_zero_length(const void *p, unsigned unit)
{
    size_t i = 0;
    while (get(p, unit, i)) ++i;
    return i;
}
int mm_string_counted_valid(const MMStringOps *s, const void *p, int terminated)
{
    unsigned count = get(p, s->unit, 0), i;
    if (count > 255) return 0;
    range(s, p, (count + 1) * s->unit);
    for (i = 1; i <= count; ++i) if (!get(p, s->unit, i)) return 0;
    if (!terminated) return 1;
    if (s->check_counted) s->check_counted(s->context, p);
    return !get(p, s->unit, count + 1);
}
int mm_string_raw_equal(const MMStringOps *s, const void *a, MMU32 na, const void *b, MMU32 nb)
{
    size_t bytes;
    if (na != nb) return 0;
    bytes = (size_t)na * s->unit;
    range(s, a, bytes); range(s, b, bytes);
    return !bytes || !memcmp(a, b, bytes);
}
MMU32 mm_string_internal_compare(const MMStringOps *s, const void *a, MMU32 na, const void *b, MMU32 nb)
{
    MMU32 i;
    unsigned x, y;
    if (na != nb) return na < nb ? MM_STRING_LESS : MM_STRING_GREATER;
    range(s, a, (size_t)na * s->unit); range(s, b, (size_t)nb * s->unit);
    for (i = 0; i < na; ++i) {
        x = get(a, s->unit, i); y = get(b, s->unit, i);
        if (x != y) return signed_unit(x, s->unit, s->signed_units) < signed_unit(y, s->unit, s->signed_units) ?
                          MM_STRING_LESS : MM_STRING_GREATER;
    }
    return MM_STRING_EQUAL;
}
static void copy(const MMStringOps *s, void *out, const void *in, size_t bytes)
{
    if (s->check_copy) s->check_copy(s->context, in, out, bytes);
    memcpy(out, in, bytes);
}
static void upper(const MMStringOps *s, void *p, size_t count)
{
    if (s->upper) s->upper(s->context, p, count);
    else if (s->unit == 1) mm_text_case_bytes((char *)p, count, 1);
    else mm_text_case_utf16((unsigned short *)p, count, 1);
}
int mm_string_user_equal(const MMStringOps *s, const void *a, MMU32 na, const void *b, MMU32 nb, int ignore_case)
{
    union Scratch { unsigned char narrow[255]; unsigned short wide[255]; } left, right;
    void *x = s->unit == 1 ? (void *)left.narrow : (void *)left.wide;
    void *y = s->unit == 1 ? (void *)right.narrow : (void *)right.wide;
    MMU32 count;
    size_t bytes;
    if (!ignore_case) return mm_string_raw_equal(s, a, na, b, nb);
    if (na != nb) return 0;
    while (na) {
        count = na < 255 ? na : 255;
        bytes = (size_t)count * s->unit;
        copy(s, x, a, bytes); copy(s, y, b, bytes);
        upper(s, x, count); upper(s, y, count);
        if (!mm_string_raw_equal(s, x, count, y, count)) return 0;
        a = (const unsigned char *)a + bytes; b = (const unsigned char *)b + bytes; na -= count;
    }
    return 1;
}
MMU32 mm_string_user_compare(const MMStringOps *s, const void *a, MMU32 na, const void *b, MMU32 nb, int ignore_case)
{
    int result = s->compare ? s->compare(s->context, a, na, b, nb, ignore_case) :
                 mm_text_compare(a, na, b, nb, s->unit == 2, ignore_case);
    if (result == -1) return MM_STRING_LESS;
    if (!result) return MM_STRING_EQUAL;
    if (result == 1) return MM_STRING_GREATER;
    return mm_string_internal_compare(s, a, na, b, nb);
}
void mm_string_translate(void *p, MMU32 count, unsigned unit, int encoding, int current, int to_current)
{
    MMU32 i;
    unsigned short *wide = (unsigned short *)p;
    if (encoding == current) return;
    if (unit == 1) mm_text_map_bytes(p, count, current == MM_TEXT_MAC ? !to_current : to_current);
    else for (i = 0; i < count; ++i) wide[i] = (unsigned short)((wide[i] >> 8) | (wide[i] << 8));
}
MMU32 mm_string_character_flags(MMU32 value)
{
    switch (value) {
    case 13: return 2 | 4 | 16;
    case 10: return 8 | 16;
    case 9: return 32 | 4 | 16;
    case 32: return 1 | 4;
    case 127: return 16;
    default: return value < 32 ? 16 : 0;
    }
}
int mm_string_from_ansi(const MMStringValue *s, const MMTextHost *host, const char *input, size_t chars)
{
    size_t count;
    if (s->unit == 1) {
        mm_string_value_set(s, input, (MMI32)(chars > 255 ? 255 : chars));
        return chars <= 255;
    }
    count = mm_text_utf16(host, input, chars + 1, MM_TEXT_WINDOWS, (unsigned short *)s->buffer + 1, 255);
    if (!count) { mm_string_value_clear(s); return 0; }
    ((unsigned short *)s->buffer)[0] = (unsigned short)(count - 1);
    return 1;
}
int mm_string_to_ansi(const MMStringValue *s, const MMTextHost *host, char *output)
{
    size_t count = get(s->buffer, s->unit, 0) + 1;
    const void *input = (const unsigned char *)s->buffer + s->unit;
    if (s->unit == 1) {
        if (s->check_copy) s->check_copy(s->context, input, output, count);
        memcpy(output, input, count);
    } else if (!mm_text_ansi(host, input, count * 2, MM_TEXT_UTF16_LE, output, 255)) {
        output[0] = 0; return 0;
    }
    return 1;
}

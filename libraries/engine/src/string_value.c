/* Counted string operations converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/string_value.h"
#include <string.h>

static unsigned get_unit(const void *buffer, unsigned unit, MMI32 position)
{
    return unit == 1 ? ((const unsigned char *)buffer)[position] :
                      ((const unsigned short *)buffer)[position];
}
static void put_unit(void *buffer, unsigned unit, MMI32 position, unsigned value)
{
    if (unit == 1) ((unsigned char *)buffer)[position] = (unsigned char)value;
    else ((unsigned short *)buffer)[position] = (unsigned short)value;
}
static MMI32 length(const MMStringValue *s)
{
    return (MMI32)get_unit(s->buffer, s->unit, 0);
}
static unsigned char *at(const MMStringValue *s, MMI32 position)
{
    return (unsigned char *)s->buffer + (size_t)position * s->unit;
}
static void finish(const MMStringValue *s, MMI32 count)
{
    put_unit(s->buffer, s->unit, 0, (unsigned)count);
    put_unit(s->buffer, s->unit, count + 1, 0);
}
static void copy(const MMStringValue *s, void *destination, const void *source, size_t bytes)
{
    if (s->check_copy) s->check_copy(s->context, source, destination, bytes);
    memmove(destination, source, bytes);
}
void mm_string_value_clear(const MMStringValue *s)
{
    finish(s, 0);
}
void mm_string_value_copy(const MMStringValue *s, const void *source)
{
    copy(s, s->buffer, source, (get_unit(source, s->unit, 0) + 2) * s->unit);
}
void mm_string_value_set(const MMStringValue *s, const void *source, MMI32 count)
{
    if (count > 255) count = 255;
    if (count > 0) copy(s, at(s, 1), source, (size_t)count * s->unit);
    else count = 0;
    finish(s, count);
}
void mm_string_value_delete(const MMStringValue *s, MMI32 position, MMI32 count)
{
    MMI32 current = length(s);
    if (position < 0 || position >= current) return;
    if (count >= current - position) finish(s, position);
    else {
        memmove(at(s, position + 1), at(s, position + count + 1),
                (size_t)(current - position - count) * s->unit);
        finish(s, current - count);
    }
}
int mm_string_value_append(const MMStringValue *s, const void *source, MMI32 count)
{
    MMI32 current = length(s);
    int result = 1;
    if (count > 255 - current) { count = 255 - current; result = 0; }
    if (count > 0) {
        copy(s, at(s, current + 1), source, (size_t)count * s->unit);
        finish(s, current + count);
    }
    return result;
}
int mm_string_value_insert(const MMStringValue *s, MMI32 position, const void *source, MMI32 count)
{
    MMI32 current = length(s);
    int result = 1;
    if (position < 0) position = 0;
    if (position > current) position = current;
    if (count > 255 - position) {
        current = position; count = 255 - position; result = 0;
    } else if (count > 255 - current) {
        current = 255 - count - position; result = 0;
    }
    if (count > 0) {
        if (current > position)
            memmove(at(s, position + count + 1), at(s, position + 1),
                    (size_t)(current - position) * s->unit);
        copy(s, at(s, position + 1), source, (size_t)count * s->unit);
        finish(s, current + count);
    }
    return result;
}
int mm_string_value_equal(const MMStringValue *s, const void *source, MMI32 count)
{
    return count == length(s) && (count == 0 || !memcmp(at(s, 1), source, (size_t)count * s->unit));
}
int mm_string_value_number(const MMStringValue *s, MMI32 base, MMI32 *output)
{
    MMI32 position = 1, digit;
    MMU32 value = 0;
    unsigned ch;
    int negative = 0;
    if (base < 2) {
        for (;;) {
            ch = get_unit(s->buffer, s->unit, position);
            if (ch == '-') negative = !negative;
            else if (ch != '+' && ch != ' ') break;
            ++position;
        }
    }
    if (!get_unit(s->buffer, s->unit, position)) return 0;
    if (!base) {
        if (get_unit(s->buffer, s->unit, position) == '0') {
            ch = get_unit(s->buffer, s->unit, ++position);
            if (ch == 'x' || ch == 'X') { ++position; base = 16; }
            else base = 8;
        } else base = 10;
    }
    while ((ch = get_unit(s->buffer, s->unit, position++)) != 0) {
        if (ch >= '0' && ch <= '9') digit = (MMI32)(ch - '0');
        else {
            if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
            else if (ch < 'a' || ch > 'z') {
                for (; ch; ch = get_unit(s->buffer, s->unit, position++))
                    if (ch != ' ') return 0;
                break;
            }
            digit = (MMI32)(ch - 'a' + 10);
        }
        if (digit > base) return 0;
        value = value * (MMU32)base + (MMU32)digit;
    }
    if (negative) value = (MMU32)0 - value;
    *output = value <= 0x7fffffffUL ? (MMI32)value : -(MMI32)(~value) - 1;
    return 1;
}
int mm_string_value_expand(const MMStringValue *s)
{
    unsigned short wide[255];
    unsigned char narrow[255];
    void *buffer = s->unit == 1 ? (void *)narrow : (void *)wide;
    MMI32 i, count = length(s), out = 0;
    unsigned ch;
    int any = 0, result = 0;
    for (i = 0; i < count; ++i) {
        if (out >= 255) goto done;
        ch = get_unit(s->buffer, s->unit, i + 1);
        switch (ch) {
        case '\t': ch = 't'; goto escape;
        case '\r': ch = 'n'; goto escape;
        case '\\': case '"':
        escape:
            if (out + 1 >= 255) goto done;
            put_unit(buffer, s->unit, out++, '\\');
            any = 1;
            break;
        }
        put_unit(buffer, s->unit, out++, ch);
    }
    result = 1;
done:
    if (any) mm_string_value_set(s, buffer, out);
    return result;
}

/* String formatting converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/string_format.h"
#include <string.h>

static unsigned get(const void *buffer, unsigned unit, MMI32 i)
{
    return unit == 1 ? ((const unsigned char *)buffer)[i] : ((const unsigned short *)buffer)[i];
}
static void put(void *buffer, unsigned unit, MMI32 i, unsigned value)
{
    if (unit == 1) ((unsigned char *)buffer)[i] = (unsigned char)value;
    else ((unsigned short *)buffer)[i] = (unsigned short)value;
}
static MMI32 signed_bits(MMU32 value)
{
    return value <= 0x7fffffffUL ? (MMI32)value : -(MMI32)(~value) - 1;
}
int mm_string_format(const MMStringValue *s, const void *pattern, MMI32 pattern_count,
                     const MMStringFormatArguments *arguments)
{
    enum { pad_first = 0x0321, pad_second = 0x0312, pad_last = 0x0132 };
    unsigned char narrow[32];
    unsigned short wide[32];
    void *scratch = s->unit == 1 ? (void *)narrow : (void *)wide;
    const void *text;
    MMStringFormatArgument argument;
    MMU32 value, radix, width_bits, padding, field_count;
    MMI32 in = 0, out = 0, count, first, width;
    unsigned ch, sign, pad, order;
    void *destination;
    size_t bytes;
    int result = 0;
    while (in < pattern_count && out < 255) {
        ch = get(pattern, s->unit, in++);
        if (ch != '%') { put(s->buffer, s->unit, ++out, ch); continue; }
        if (in >= pattern_count) goto bad;
        ch = get(pattern, s->unit, in++);
        if (ch == '%') { put(s->buffer, s->unit, ++out, '%'); continue; }
        order = pad_first; sign = 0; pad = ' ';
        /* The original positional branch never advances past '<' and fails. */
        if (ch == '<') goto bad;
        for (;;) {
            if (ch == '-') order = pad_last;
            else if (ch == '+') sign = '+';
            else if (ch == '0') { pad = '0'; order = pad_second; }
            else break;
            if (in >= pattern_count) goto bad;
            ch = get(pattern, s->unit, in++);
        }
        width_bits = 0;
        while (ch >= '0' && ch <= '9') {
            width_bits = width_bits * 10 + ch - '0';
            if (in >= pattern_count) goto bad;
            ch = get(pattern, s->unit, in++);
        }
        width = signed_bits(width_bits);
        switch (ch) {
        case 'c': case 's': case 'z': case 'f': case 'x': case 'd': case 'u': break;
        default: goto bad;
        }
        memset(&argument, 0, sizeof(argument));
        if (!arguments->next(arguments->context, ch, &argument)) goto done;
        text = scratch;
        switch (ch) {
        case 'c':
            put(scratch, s->unit, 0, (unsigned)argument.number);
            count = 1;
            break;
        case 's': case 'z':
            text = argument.text; count = argument.count;
            break;
        case 'f':
            value = argument.number;
            for (count = 4; count-- > 0; value >>= 8) {
                ch = (unsigned)(value & 255);
                put(scratch, s->unit, count, ch ? ch : 1);
            }
            count = 4;
            break;
        default:
            value = argument.number;
            radix = ch == 'x' ? 16 : 10;
            if (ch == 'x' && width > 0 && width < 8)
                value &= ((MMU32)1 << (width * 4)) - 1;
            if (ch == 'd' && signed_bits(value) < 0) {
                sign = '-'; value = (MMU32)0 - value;
            }
            first = 32;
            do {
                put(scratch, s->unit, --first, (unsigned)"0123456789ABCDEF"[value % radix]);
                value /= radix;
            } while (value);
            text = (const unsigned char *)scratch + (size_t)first * s->unit;
            count = 32 - first;
            break;
        }
        field_count = (MMU32)count + (sign != 0);
        padding = width > 0 && (MMU32)width > field_count ? (MMU32)width - field_count : 0;
        if ((MMU32)(255 - out) <= field_count + padding) goto done;
        while (order) {
            switch (order & 15) {
            case 1:
                for (; padding; --padding) put(s->buffer, s->unit, ++out, pad);
                break;
            case 2:
                if (sign) put(s->buffer, s->unit, ++out, sign);
                break;
            case 3:
                destination = (unsigned char *)s->buffer + (size_t)(out + 1) * s->unit;
                bytes = (size_t)count * s->unit;
                if (s->check_copy) s->check_copy(s->context, text, destination, bytes);
                if (bytes) memmove(destination, text, bytes);
                out += count;
                break;
            }
            order >>= 4;
        }
    }
    result = in == pattern_count;
    goto done;
bad:
    if (arguments->bad_format) arguments->bad_format(arguments->context);
done:
    put(s->buffer, s->unit, 0, (unsigned)out);
    put(s->buffer, s->unit, out + 1, 0);
    return result;
}

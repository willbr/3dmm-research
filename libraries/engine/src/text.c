/* Conversion policy derived from Kauai platform_text.cpp and utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/text.h"
#include <stdlib.h>
#include <string.h>
#include "legacy_text_maps.h"
#include "legacy_byte_maps.h"

int mm_text_encode_utf8(const unsigned short *units, size_t count, void *context,
                        int (*write)(void *, const char *, size_t))
{
    size_t i, used;
    MMU32 scalar, tail;
    char octets[4];
    int result;

    for (i = 0; i < count; ++i)
    {
        scalar = units[i];
        if (scalar >= 0xd800 && scalar <= 0xdfff)
        {
            if (scalar > 0xdbff || i + 1 >= count)
                return 0;
            tail = units[++i];
            if (tail < 0xdc00 || tail > 0xdfff)
                return 0;
            scalar = 0x10000UL + ((scalar - 0xd800UL) << 10) + tail - 0xdc00UL;
        }
        used = 0;
        if (scalar >= 0x10000UL)
        {
            octets[used++] = (char)(0xf0 | (scalar >> 18));
            octets[used++] = (char)(0x80 | ((scalar >> 12) & 63));
        }
        else if (scalar >= 0x800)
            octets[used++] = (char)(0xe0 | (scalar >> 12));
        else if (scalar >= 0x80)
            octets[used++] = (char)(0xc0 | (scalar >> 6));
        if (scalar >= 0x800)
            octets[used++] = (char)(0x80 | ((scalar >> 6) & 63));
        octets[used++] = (char)(scalar < 0x80 ? scalar : 0x80 | (scalar & 63));
        result = write(context, octets, used);
        if (result <= 0)
            return result;
    }
    return 1;
}

int mm_text_decode_utf8(const char *bytes, size_t count, void *context,
                        int (*write)(void *, const unsigned short *, size_t))
{
    const unsigned char *text = (const unsigned char *)bytes;
    size_t i = 0, used;
    MMU32 scalar, minimum;
    unsigned remaining, next;
    unsigned short units[2];
    int result;

    while (i < count)
    {
        scalar = text[i++];
        remaining = 0;
        minimum = 0;
        if (scalar >= 0xc2 && scalar <= 0xdf)
        {
            scalar &= 0x1f;
            remaining = 1;
            minimum = 0x80;
        }
        else if (scalar >= 0xe0 && scalar <= 0xef)
        {
            scalar &= 0x0f;
            remaining = 2;
            minimum = 0x800;
        }
        else if (scalar >= 0xf0 && scalar <= 0xf4)
        {
            scalar &= 7;
            remaining = 3;
            minimum = 0x10000UL;
        }
        else if (scalar >= 0x80)
            return 0;
        if (remaining > count - i)
            return 0;
        while (remaining)
        {
            next = text[i++];
            if ((next & 0xc0) != 0x80)
                return 0;
            scalar = (scalar << 6) | (next & 63);
            --remaining;
        }
        if (scalar < minimum || scalar > 0x10ffffUL || (scalar >= 0xd800 && scalar <= 0xdfff))
            return 0;
        used = 1;
        if (scalar >= 0x10000UL)
        {
            scalar -= 0x10000UL;
            units[0] = (unsigned short)(0xd800 | (scalar >> 10));
            units[1] = (unsigned short)(0xdc00 | (scalar & 0x3ff));
            used = 2;
        }
        else
            units[0] = (unsigned short)scalar;
        result = write(context, units, used);
        if (result <= 0)
            return result;
    }
    return 1;
}

typedef struct UTF16Buffer
{
    unsigned short *units;
    size_t capacity, count;
} UTF16Buffer;

static int collect_units(void *context, const unsigned short *units, size_t count)
{
    UTF16Buffer *buffer = context;
    if (count > buffer->capacity - buffer->count)
        return -1;
    memcpy(buffer->units + buffer->count, units, count * sizeof(*units));
    buffer->count += count;
    return 1;
}

int mm_text_read_utf8(const char *bytes, size_t length, unsigned short *units, size_t capacity, size_t *count)
{
    UTF16Buffer buffer;
    int result;
    buffer.units = units;
    buffer.capacity = capacity;
    buffer.count = 0;
    result = mm_text_decode_utf8(bytes, length, &buffer, collect_units);
    if (result == 1)
        *count = buffer.count;
    return result;
}

static int wide(int encoding)
{
    return encoding == MM_TEXT_UTF16_LE || encoding == MM_TEXT_UTF16_BE;
}
static unsigned short unit(const unsigned char *bytes, int encoding)
{
    return (unsigned short)(encoding == MM_TEXT_UTF16_LE ? bytes[0] | (bytes[1] << 8)
                                                               : (bytes[0] << 8) | bytes[1]);
}
static unsigned short map_unit(unsigned short value, int upper)
{
    unsigned i;
    for (i = 0; i < 256; ++i)
        if (windows1252[i] == value)
            return windows1252[upper ? upper1252[i] : lower1252[i]];
    return value;
}
typedef struct Utf8Buffer
{
    char *data;
    size_t capacity, count;
} Utf8Buffer;

static int collect_utf8(void *context, const char *bytes, size_t count)
{
    Utf8Buffer *out = context;
    if (count > out->capacity - out->count)
        return -1;
    memcpy(out->data + out->count, bytes, count);
    out->count += count;
    return 1;
}

int mm_text_write_utf8(const unsigned short *units, size_t length, char *bytes, size_t capacity, size_t *count)
{
    Utf8Buffer buffer;
    int result;
    buffer.data = bytes;
    buffer.capacity = capacity;
    buffer.count = 0;
    result = mm_text_encode_utf8(units, length, &buffer, collect_utf8);
    if (result == 1)
        *count = buffer.count;
    return result;
}

size_t mm_text_utf16(const MMTextHost *host, const void *input, size_t bytes, int source,
                           unsigned short *output, size_t capacity)
{
    const unsigned char *data = (const unsigned char *)input;
    const unsigned short *map;
    size_t i, count;
    if ((!input && bytes) || bytes > 2147483647UL)
        return 0;
    if (wide(source))
    {
        if (bytes % 2)
            return 0;
        count = bytes / 2;
        if (!output)
            return count;
        if (capacity < count)
            return 0;
        for (i = 0; i < count; ++i)
            output[i] = unit(data + i * 2, source);
        return count;
    }
    if (source != MM_TEXT_WINDOWS && source != MM_TEXT_MAC)
        return 0;
    if (host && host->decode)
        return host->decode(host->context, input, bytes, source, output, capacity);
    if (!output)
        return bytes;
    if (capacity < bytes)
        return 0;
    map = source == MM_TEXT_WINDOWS ? windows1252 : macRoman;
    for (i = 0; i < bytes; ++i)
        output[i] = map[data[i]];
    return bytes;
}
size_t mm_text_ansi(const MMTextHost *host, const void *input, size_t bytes, int source, char *output,
                          size_t capacity)
{
    const unsigned char *data = (const unsigned char *)input;
    unsigned short *units;
    size_t i, count, needed = 0;
    unsigned b, mapped;
    if ((!input && bytes) || bytes > 2147483647UL)
        return 0;
    if (source == MM_TEXT_WINDOWS || source == MM_TEXT_MAC)
    {
        if (!output)
            return bytes;
        if (capacity < bytes)
            return 0;
        if (bytes)
            memmove(output, input, bytes);
        if (source == MM_TEXT_MAC)
            mm_text_map_bytes(output, bytes, 1);
        return bytes;
    }
    if (!wide(source) || bytes % 2)
        return 0;
    count = bytes / 2;
    if (host && host->encode)
    {
        if (!bytes)
            return 0;
        units = (unsigned short *)malloc(bytes);
        if (!units)
            return 0;
        mm_text_utf16(NULL, input, bytes, source, units, count);
        needed = host->encode(host->context, units, count, output, capacity);
        free(units);
        return needed;
    }
    /* Measure first, so even a surrogate pair and a short buffer never cause a
     * partial write. The portable policy substitutes one '?' for a valid pair. */
    for (i = 0; i < count; ++i)
    {
        unsigned short value = unit(data + i * 2, source);
        ++needed;
        if (value >= 0xd800 && value <= 0xdbff && i + 1 < count && unit(data + (i + 1) * 2, source) >= 0xdc00 &&
            unit(data + (i + 1) * 2, source) <= 0xdfff)
            ++i;
    }
    if (!output)
        return needed;
    if (capacity < needed)
        return 0;
    needed = 0;
    for (i = 0; i < count; ++i)
    {
        unsigned short value = unit(data + i * 2, source);
        mapped = 256;
        for (b = 0; b < 256; ++b)
            if (windows1252[b] == value)
            {
                mapped = b;
                break;
            }
        output[needed++] = mapped < 256 ? (char)mapped : '?';
        if (value >= 0xd800 && value <= 0xdbff && i + 1 < count && unit(data + (i + 1) * 2, source) >= 0xdc00 &&
            unit(data + (i + 1) * 2, source) <= 0xdfff)
            ++i;
    }
    return needed;
}

size_t mm_text_ansi_from_units(const MMTextHost *host, const unsigned short *units, size_t count,
                              char *output, size_t capacity, int exact)
{
    const unsigned short order = 1;
    unsigned short *round_trip;
    size_t bytes, i;
    int encoding;
    if (!count || count > 2147483647UL / 2 || (exact && !output))
        return 0;
    if (host && host->encode)
        bytes = host->encode(host->context, units, count, output, capacity);
    else
    {
        encoding = *(const unsigned char *)&order ? MM_TEXT_UTF16_LE : MM_TEXT_UTF16_BE;
        bytes = mm_text_ansi(NULL, units, count * 2, encoding, output, capacity);
    }
    if (!bytes || !exact)
        return bytes;
    if (!host || !host->decode)
    {
        if (bytes != count)
            return 0;
        for (i = 0; i < count; ++i)
            if (windows1252[(unsigned char)output[i]] != units[i])
                return 0;
        return bytes;
    }
    round_trip = malloc(count * sizeof(*units));
    if (!round_trip)
        return 0;
    if (mm_text_utf16(host, output, bytes, MM_TEXT_WINDOWS, round_trip, count) != count ||
        memcmp(units, round_trip, count * sizeof(*units)))
        bytes = 0;
    free(round_trip);
    return bytes;
}

void mm_text_map_bytes(void *text, size_t count, int mac_to_windows)
{
    unsigned char *bytes = (unsigned char *)text;
    const unsigned char *map = mac_to_windows ? _mpchschsMacToWin : _mpchschsWinToMac;
    size_t i;
    for (i = 0; i < count; ++i)
        if (bytes[i] >= 128)
            bytes[i] = map[bytes[i] - 128];
}
void mm_text_case_bytes(char *text, size_t count, int upper)
{
    size_t i;
    for (i = 0; i < count; ++i)
        text[i] = (char)(upper ? upper1252[(unsigned char)text[i]] : lower1252[(unsigned char)text[i]]);
}
void mm_text_case_utf16(unsigned short *text, size_t count, int upper)
{
    size_t i;
    for (i = 0; i < count; ++i)
        text[i] = map_unit(text[i], upper);
}
int mm_text_compare(const void *a, size_t na, const void *b, size_t nb, int is_wide, int ignore_case)
{
    size_t i;
    unsigned short left, right;
    for (i = 0; i < na && i < nb; ++i)
    {
        if (is_wide)
        {
            left = ((const unsigned short *)a)[i];
            right = ((const unsigned short *)b)[i];
            if (ignore_case)
            {
                left = map_unit(left, 1);
                right = map_unit(right, 1);
            }
        }
        else
        {
            unsigned char x = ((const unsigned char *)a)[i], y = ((const unsigned char *)b)[i];
            if (ignore_case)
            {
                x = upper1252[x];
                y = upper1252[y];
            }
            left = windows1252[x];
            right = windows1252[y];
        }
        if (left != right)
            return left < right ? -1 : 1;
    }
    return na == nb ? 0 : na < nb ? -1 : 1;
}
int mm_text_compare_host(const MMTextCompareHost *host, const void *a, size_t na, const void *b, size_t nb, int wide, int ignore_case)
{
    int result;
    if (!na || !nb) return na == nb ? 0 : na < nb ? -1 : 1;
    if (!host) return mm_text_compare(a, na, b, nb, wide, ignore_case);
    if (na > 0x7fffffffUL || nb > 0x7fffffffUL) return 2;
    result = host->compare(host->context, a, na, b, nb, wide, ignore_case);
    return result ? result - 2 : 2;
}
void mm_text_case_maps_init(MMTextCaseMaps *maps, void *context, void (*map)(void *, char *, size_t, int))
{
    unsigned i;
    for (i = 0; i < 256; ++i) maps->upper[i] = maps->lower[i] = (char)i;
    if (map) { map(context, maps->upper, 256, 1); map(context, maps->lower, 256, 0); }
    else { mm_text_case_bytes(maps->upper, 256, 1); mm_text_case_bytes(maps->lower, 256, 0); }
}
void mm_text_case_maps_apply(const MMTextCaseMaps *maps, char *text, size_t count, int upper)
{
    size_t i;
    const char *map = upper ? maps->upper : maps->lower;
    for (i = 0; i < count; ++i) text[i] = map[(unsigned char)text[i]];
}

/* Conversion policy derived from Kauai platform_text.cpp and utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/text.h"
#include <stdlib.h>
#include <string.h>
#include "legacy_text_maps.h"
#include "legacy_byte_maps.h"

static int wide(int encoding)
{
    return encoding == THREE_MM_TEXT_UTF16_LE || encoding == THREE_MM_TEXT_UTF16_BE;
}
static unsigned short unit(const unsigned char *bytes, int encoding)
{
    return (unsigned short)(encoding == THREE_MM_TEXT_UTF16_LE ? bytes[0] | (bytes[1] << 8)
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
size_t three_mm_text_utf16(const ThreeMMTextHost *host, const void *input, size_t bytes, int source,
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
    if (source != THREE_MM_TEXT_WINDOWS && source != THREE_MM_TEXT_MAC)
        return 0;
    if (host && host->decode)
        return host->decode(host->context, input, bytes, source, output, capacity);
    if (!output)
        return bytes;
    if (capacity < bytes)
        return 0;
    map = source == THREE_MM_TEXT_WINDOWS ? windows1252 : macRoman;
    for (i = 0; i < bytes; ++i)
        output[i] = map[data[i]];
    return bytes;
}
size_t three_mm_text_ansi(const ThreeMMTextHost *host, const void *input, size_t bytes, int source, char *output,
                          size_t capacity)
{
    const unsigned char *data = (const unsigned char *)input;
    unsigned short *units;
    size_t i, count, needed = 0;
    unsigned b, mapped;
    if ((!input && bytes) || bytes > 2147483647UL)
        return 0;
    if (source == THREE_MM_TEXT_WINDOWS || source == THREE_MM_TEXT_MAC)
    {
        if (!output)
            return bytes;
        if (capacity < bytes)
            return 0;
        if (bytes)
            memmove(output, input, bytes);
        if (source == THREE_MM_TEXT_MAC)
            three_mm_text_map_bytes(output, bytes, 1);
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
        three_mm_text_utf16(NULL, input, bytes, source, units, count);
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
void three_mm_text_map_bytes(void *text, size_t count, int mac_to_windows)
{
    unsigned char *bytes = (unsigned char *)text;
    const unsigned char *map = mac_to_windows ? _mpchschsMacToWin : _mpchschsWinToMac;
    size_t i;
    for (i = 0; i < count; ++i)
        if (bytes[i] >= 128)
            bytes[i] = map[bytes[i] - 128];
}
void three_mm_text_case_bytes(char *text, size_t count, int upper)
{
    size_t i;
    for (i = 0; i < count; ++i)
        text[i] = (char)(upper ? upper1252[(unsigned char)text[i]] : lower1252[(unsigned char)text[i]]);
}
void three_mm_text_case_utf16(unsigned short *text, size_t count, int upper)
{
    size_t i;
    for (i = 0; i < count; ++i)
        text[i] = map_unit(text[i], upper);
}
int three_mm_text_compare(const void *a, size_t na, const void *b, size_t nb, int is_wide, int ignore_case)
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

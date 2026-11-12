/* Collection wire policy converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/collections.h"
#include <string.h>
#define LIMIT THREE_MM_STORAGE_LIMIT
static ThreeMMU32 u32(const unsigned char *p, int big)
{
    if (big)
        return ((ThreeMMU32)p[0] << 24) | ((ThreeMMU32)p[1] << 16) | ((ThreeMMU32)p[2] << 8) | p[3];
    return p[0] | ((ThreeMMU32)p[1] << 8) | ((ThreeMMU32)p[2] << 16) | ((ThreeMMU32)p[3] << 24);
}
static ThreeMMI32 signed32(ThreeMMU32 n)
{
    return n <= 0x7fffffffUL ? (ThreeMMI32)n : -1 - (ThreeMMI32)(0xffffffffUL - n);
}
static void put32(unsigned char *p, ThreeMMU32 n, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(n >> (8 * i));
}
int three_mm_collection_layout(ThreeMMCollectionHeader *header)
{
    ThreeMMCollectionHeader h;
    if (!header)
        return THREE_MM_STORAGE_ARGUMENT;
    h = *header;
    if (h.count > 0x7fffffffUL || h.width > 0x7fffffffUL)
        return THREE_MM_STORAGE_ARGUMENT;
    if (h.kind == THREE_MM_COLLECTION_STRINGS)
    {
        if (h.width < 4 || h.width >= LIMIT || h.width % 4 || h.count >= LIMIT || h.count > (LIMIT - 1) / h.width ||
            h.bytes >= LIMIT)
            return THREE_MM_STORAGE_ARGUMENT;
        h.header_bytes = 20;
        h.first_bytes = h.bytes;
        h.second_bytes = h.count * h.width;
    }
    else if (h.kind == THREE_MM_COLLECTION_GROUP)
    {
        if (h.width >= LIMIT || h.bytes >= LIMIT || h.count > (LIMIT - 1) / 8)
            return THREE_MM_STORAGE_ARGUMENT;
        h.header_bytes = 20;
        h.first_bytes = h.bytes;
        h.second_bytes = h.count * 8;
    }
    else if (h.kind == THREE_MM_COLLECTION_ARRAY || h.kind == THREE_MM_COLLECTION_ALLOCATED_ARRAY)
    {
        if (!h.width || h.count > (LIMIT - 1) / h.width)
            return THREE_MM_STORAGE_ARGUMENT;
        h.header_bytes = h.kind == THREE_MM_COLLECTION_ARRAY ? 12 : 16;
        h.first_bytes = h.count * h.width;
        h.second_bytes = h.kind == THREE_MM_COLLECTION_ARRAY ? 0 : (h.count + 7) / 8;
        h.bytes = h.first_bytes;
    }
    else
        return THREE_MM_STORAGE_ARGUMENT;
    *header = h;
    return THREE_MM_STORAGE_OK;
}
int three_mm_collection_header_read(const ThreeMMReadStream *stream, int kind, int allocated,
                                    ThreeMMCollectionHeader *out)
{
    unsigned char p[20];
    ThreeMMCollectionHeader h;
    ThreeMMU32 n;
    int result;
    if (!stream || !out || kind < THREE_MM_COLLECTION_ARRAY || kind > THREE_MM_COLLECTION_STRINGS)
        return THREE_MM_STORAGE_ARGUMENT;
    n = kind >= THREE_MM_COLLECTION_GROUP ? 20 : kind == THREE_MM_COLLECTION_ARRAY ? 12 : 16;
    if (stream->length < n)
        return THREE_MM_STORAGE_ARGUMENT;
    if (!three_mm_read_exact(stream, 0, p, n))
        return THREE_MM_STORAGE_IO;
    memset(&h, 0, sizeof(h));
    h.kind = kind;
    if (p[0] == 0 && p[1] == 1)
        h.big_endian = 1;
    else if (p[0] != 1 || p[1] != 0)
        return THREE_MM_STORAGE_ARGUMENT;
    h.system = h.big_endian ? (p[2] << 8) | p[3] : p[2] | (p[3] << 8);
    if (kind == THREE_MM_COLLECTION_STRINGS)
    {
        h.width = u32(p + 4, h.big_endian);
        h.count = u32(p + 8, h.big_endian);
        h.bytes = u32(p + 12, h.big_endian);
        h.free_count = signed32(u32(p + 16, h.big_endian));
        if ((h.free_count == -1) != (!allocated) ||
            (h.free_count != -1 && (h.free_count < 0 || (ThreeMMU32)h.free_count >= (h.count ? h.count : 1))) ||
            h.bytes < h.count - (h.free_count > 0 ? (ThreeMMU32)h.free_count : 0))
            return THREE_MM_STORAGE_ARGUMENT;
    }
    else if (kind == THREE_MM_COLLECTION_GROUP)
    {
        h.count = u32(p + 4, h.big_endian);
        h.bytes = u32(p + 8, h.big_endian);
        h.free_count = signed32(u32(p + 12, h.big_endian));
        h.width = u32(p + 16, h.big_endian);
        if ((h.free_count == -1) != (!allocated) ||
            (h.free_count != -1 && (h.free_count < 0 || (ThreeMMU32)h.free_count >= h.count)))
            return THREE_MM_STORAGE_ARGUMENT;
    }
    else
    {
        h.width = u32(p + 4, h.big_endian);
        h.count = u32(p + 8, h.big_endian);
        if (h.count > 0x7fffffffUL)
            return THREE_MM_STORAGE_ARGUMENT;
        h.free_count = kind == THREE_MM_COLLECTION_ARRAY ? -1 : signed32(u32(p + 12, h.big_endian));
        if (kind == THREE_MM_COLLECTION_ALLOCATED_ARRAY && h.free_count >= (ThreeMMI32)(h.count ? h.count : 1))
            return THREE_MM_STORAGE_ARGUMENT;
    }
    result = three_mm_collection_layout(&h);
    if (result != THREE_MM_STORAGE_OK || h.first_bytes > stream->length - n ||
        h.second_bytes != stream->length - n - h.first_bytes)
        return THREE_MM_STORAGE_ARGUMENT;
    *out = h;
    return THREE_MM_STORAGE_OK;
}
int three_mm_collection_header_write(const ThreeMMCollectionHeader *header, unsigned char *output, size_t capacity)
{
    ThreeMMCollectionHeader h;
    int result;
    if (!header || !output)
        return THREE_MM_STORAGE_ARGUMENT;
    h = *header;
    result = three_mm_collection_layout(&h);
    if (result != THREE_MM_STORAGE_OK || capacity < h.header_bytes || h.system < 0 || h.system > 65535)
        return THREE_MM_STORAGE_ARGUMENT;
    output[0] = (unsigned char)(h.big_endian ? 0 : 1);
    output[1] = (unsigned char)(h.big_endian ? 1 : 0);
    output[h.big_endian ? 2 : 3] = (unsigned char)(h.system >> 8);
    output[h.big_endian ? 3 : 2] = (unsigned char)h.system;
    if (h.kind == THREE_MM_COLLECTION_STRINGS)
    {
        put32(output + 4, h.width, h.big_endian);
        put32(output + 8, h.count, h.big_endian);
        put32(output + 12, h.bytes, h.big_endian);
        put32(output + 16, (ThreeMMU32)h.free_count, h.big_endian);
    }
    else if (h.kind == THREE_MM_COLLECTION_GROUP)
    {
        put32(output + 4, h.count, h.big_endian);
        put32(output + 8, h.bytes, h.big_endian);
        put32(output + 12, (ThreeMMU32)h.free_count, h.big_endian);
        put32(output + 16, h.width, h.big_endian);
    }
    else
    {
        put32(output + 4, h.width, h.big_endian);
        put32(output + 8, h.count, h.big_endian);
        if (h.kind == THREE_MM_COLLECTION_ALLOCATED_ARRAY)
            put32(output + 12, (ThreeMMU32)h.free_count, h.big_endian);
    }
    return THREE_MM_STORAGE_OK;
}

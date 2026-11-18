/* Collection wire policy converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/collections.h"
#include <string.h>
#define LIMIT MM_STORAGE_LIMIT
static MMU32 u32(const unsigned char *p, int big)
{
    if (big)
        return ((MMU32)p[0] << 24) | ((MMU32)p[1] << 16) | ((MMU32)p[2] << 8) | p[3];
    return p[0] | ((MMU32)p[1] << 8) | ((MMU32)p[2] << 16) | ((MMU32)p[3] << 24);
}
static MMI32 signed32(MMU32 n)
{
    return n <= 0x7fffffffUL ? (MMI32)n : -1 - (MMI32)(0xffffffffUL - n);
}
static void put32(unsigned char *p, MMU32 n, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(n >> (8 * i));
}
int mm_collection_layout(MMCollectionHeader *header)
{
    MMCollectionHeader h;
    if (!header)
        return MM_STORAGE_ARGUMENT;
    h = *header;
    if (h.count > 0x7fffffffUL || h.width > 0x7fffffffUL)
        return MM_STORAGE_ARGUMENT;
    if (h.kind == MM_COLLECTION_STRINGS)
    {
        if (h.width < 4 || h.width >= LIMIT || h.width % 4 || h.count >= LIMIT || h.count > (LIMIT - 1) / h.width ||
            h.bytes >= LIMIT)
            return MM_STORAGE_ARGUMENT;
        h.header_bytes = 20;
        h.first_bytes = h.bytes;
        h.second_bytes = h.count * h.width;
    }
    else if (h.kind == MM_COLLECTION_GROUP)
    {
        if (h.width >= LIMIT || h.bytes >= LIMIT || h.count > (LIMIT - 1) / 8)
            return MM_STORAGE_ARGUMENT;
        h.header_bytes = 20;
        h.first_bytes = h.bytes;
        h.second_bytes = h.count * 8;
    }
    else if (h.kind == MM_COLLECTION_ARRAY || h.kind == MM_COLLECTION_ALLOCATED_ARRAY)
    {
        if (!h.width || h.count > (LIMIT - 1) / h.width)
            return MM_STORAGE_ARGUMENT;
        h.header_bytes = h.kind == MM_COLLECTION_ARRAY ? 12 : 16;
        h.first_bytes = h.count * h.width;
        h.second_bytes = h.kind == MM_COLLECTION_ARRAY ? 0 : (h.count + 7) / 8;
        h.bytes = h.first_bytes;
    }
    else
        return MM_STORAGE_ARGUMENT;
    *header = h;
    return MM_STORAGE_OK;
}
int mm_collection_header_read(const MMReadStream *stream, int kind, int allocated,
                                    MMCollectionHeader *out)
{
    unsigned char p[20];
    MMCollectionHeader h;
    MMU32 n;
    int result;
    if (!stream || !out || kind < MM_COLLECTION_ARRAY || kind > MM_COLLECTION_STRINGS)
        return MM_STORAGE_ARGUMENT;
    n = kind >= MM_COLLECTION_GROUP ? 20 : kind == MM_COLLECTION_ARRAY ? 12 : 16;
    if (stream->length < n)
        return MM_STORAGE_ARGUMENT;
    if (!mm_read_exact(stream, 0, p, n))
        return MM_STORAGE_IO;
    memset(&h, 0, sizeof(h));
    h.kind = kind;
    if (p[0] == 0 && p[1] == 1)
        h.big_endian = 1;
    else if (p[0] != 1 || p[1] != 0)
        return MM_STORAGE_ARGUMENT;
    h.system = h.big_endian ? (p[2] << 8) | p[3] : p[2] | (p[3] << 8);
    if (kind == MM_COLLECTION_STRINGS)
    {
        h.width = u32(p + 4, h.big_endian);
        h.count = u32(p + 8, h.big_endian);
        h.bytes = u32(p + 12, h.big_endian);
        h.free_count = signed32(u32(p + 16, h.big_endian));
        if ((h.free_count == -1) != (!allocated) ||
            (h.free_count != -1 && (h.free_count < 0 || (MMU32)h.free_count >= (h.count ? h.count : 1))) ||
            h.bytes < h.count - (h.free_count > 0 ? (MMU32)h.free_count : 0))
            return MM_STORAGE_ARGUMENT;
    }
    else if (kind == MM_COLLECTION_GROUP)
    {
        h.count = u32(p + 4, h.big_endian);
        h.bytes = u32(p + 8, h.big_endian);
        h.free_count = signed32(u32(p + 12, h.big_endian));
        h.width = u32(p + 16, h.big_endian);
        if ((h.free_count == -1) != (!allocated) ||
            (h.free_count != -1 && (h.free_count < 0 || (MMU32)h.free_count >= h.count)))
            return MM_STORAGE_ARGUMENT;
    }
    else
    {
        h.width = u32(p + 4, h.big_endian);
        h.count = u32(p + 8, h.big_endian);
        if (h.count > 0x7fffffffUL)
            return MM_STORAGE_ARGUMENT;
        h.free_count = kind == MM_COLLECTION_ARRAY ? -1 : signed32(u32(p + 12, h.big_endian));
        if (kind == MM_COLLECTION_ALLOCATED_ARRAY && h.free_count >= (MMI32)(h.count ? h.count : 1))
            return MM_STORAGE_ARGUMENT;
    }
    result = mm_collection_layout(&h);
    if (result != MM_STORAGE_OK || h.first_bytes > stream->length - n ||
        h.second_bytes != stream->length - n - h.first_bytes)
        return MM_STORAGE_ARGUMENT;
    *out = h;
    return MM_STORAGE_OK;
}
int mm_collection_group_row(const MMCollectionHeader *header, const unsigned char *records, MMU32 row,
                            MMU32 *offset, MMI32 *bytes)
{
    const unsigned char *location;
    MMU32 start, size;
    if (row >= header->count)
        return 0;
    location = records + header->first_bytes + row * 8;
    start = u32(location, header->big_endian);
    size = u32(location + 4, header->big_endian);
    if (start > header->first_bytes || size < header->width || size > header->first_bytes - start)
        return 0;
    *offset = start;
    *bytes = (MMI32)(size - header->width);
    return 1;
}

int mm_collection_header_write(const MMCollectionHeader *header, unsigned char *output, size_t capacity)
{
    MMCollectionHeader h;
    int result;
    if (!header || !output)
        return MM_STORAGE_ARGUMENT;
    h = *header;
    result = mm_collection_layout(&h);
    if (result != MM_STORAGE_OK || capacity < h.header_bytes || h.system < 0 || h.system > 65535)
        return MM_STORAGE_ARGUMENT;
    output[0] = (unsigned char)(h.big_endian ? 0 : 1);
    output[1] = (unsigned char)(h.big_endian ? 1 : 0);
    output[h.big_endian ? 2 : 3] = (unsigned char)(h.system >> 8);
    output[h.big_endian ? 3 : 2] = (unsigned char)h.system;
    if (h.kind == MM_COLLECTION_STRINGS)
    {
        put32(output + 4, h.width, h.big_endian);
        put32(output + 8, h.count, h.big_endian);
        put32(output + 12, h.bytes, h.big_endian);
        put32(output + 16, (MMU32)h.free_count, h.big_endian);
    }
    else if (h.kind == MM_COLLECTION_GROUP)
    {
        put32(output + 4, h.count, h.big_endian);
        put32(output + 8, h.bytes, h.big_endian);
        put32(output + 12, (MMU32)h.free_count, h.big_endian);
        put32(output + 16, h.width, h.big_endian);
    }
    else
    {
        put32(output + 4, h.width, h.big_endian);
        put32(output + 8, h.count, h.big_endian);
        if (h.kind == MM_COLLECTION_ALLOCATED_ARRAY)
            put32(output + 12, (MMU32)h.free_count, h.big_endian);
    }
    return MM_STORAGE_OK;
}

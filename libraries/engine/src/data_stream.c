/* Stream/block policy converted from Kauai stream.cpp, file.cpp and codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/data_stream.h"
#include <string.h>
#define LIMIT MM_STORAGE_LIMIT
int mm_range_contains(MMU32 length, MMU32 offset, MMU32 bytes)
{
    return offset <= length && bytes <= length - offset;
}
static int block_valid(const MMBlockView *b)
{
    if (!b || b->kind < MM_BLOCK_EMPTY || b->kind > MM_BLOCK_FILE || b->start > 0x7fffffffUL ||
        b->length > 0x7fffffffUL)
        return 0;
    if (b->kind == MM_BLOCK_EMPTY)
        return !b->length && !b->start;
    if (b->kind == MM_BLOCK_MEMORY)
        return b->memory && b->capacity < LIMIT && b->start <= b->capacity && b->length <= b->capacity - b->start;
    return 1;
}
int mm_block_check(const MMBlockView *b, MMU32 offset, MMU32 bytes, int packed_ok,
                         MMU32 *physical)
{
    if (!block_valid(b))
        return MM_DATA_ARGUMENT;
    if (b->packed && !packed_ok)
        return MM_DATA_PACKED;
    if (!mm_range_contains(b->length, offset, bytes) || offset > 0x7fffffffUL - b->start)
        return MM_DATA_ARGUMENT;
    if (physical)
        *physical = b->start + offset;
    return MM_DATA_OK;
}
int mm_block_move_min(MMBlockView *b, MMI32 delta)
{
    MMU32 n;
    if (!block_valid(b) || b->kind == MM_BLOCK_EMPTY)
        return MM_DATA_ARGUMENT;
    if (delta < 0)
    {
        n = 0 - (MMU32)delta;
        if (n > b->start || n > 0x7fffffffUL - b->length)
            return MM_DATA_ARGUMENT;
        b->start -= n;
        b->length += n;
    }
    else
    {
        n = (MMU32)delta;
        if (n > b->length || n > 0x7fffffffUL - b->start)
            return MM_DATA_ARGUMENT;
        b->start += n;
        b->length -= n;
    }
    return MM_DATA_OK;
}
int mm_block_move_lim(MMBlockView *b, MMI32 delta)
{
    MMU32 n;
    if (!block_valid(b) || b->kind == MM_BLOCK_EMPTY)
        return MM_DATA_ARGUMENT;
    if (delta < 0)
    {
        n = 0 - (MMU32)delta;
        if (n > b->length || (b->kind == MM_BLOCK_FILE && b->start >= LIMIT && n <= b->start - LIMIT))
            return MM_DATA_ARGUMENT;
        b->length -= n;
    }
    else
    {
        n = (MMU32)delta;
        if (n > 0x7fffffffUL - b->length)
            return MM_DATA_ARGUMENT;
        if (b->kind == MM_BLOCK_MEMORY)
        {
            if (n > b->capacity - b->start - b->length)
                return MM_DATA_ARGUMENT;
        }
        else if (b->start >= LIMIT || n >= LIMIT - b->start)
            return MM_DATA_ARGUMENT;
        b->length += n;
    }
    return MM_DATA_OK;
}
int mm_block_unpack(MMBlockView *b, const MMBlockDecoder *decoder)
{
    void *handle;
    MMU32 length;
    if (!block_valid(b))
        return MM_DATA_ARGUMENT;
    if (!b->packed)
        return MM_DATA_OK;
    if (b->kind == MM_BLOCK_EMPTY || b->length <= 8)
        return MM_DATA_CODEC;
    /* The old HqFree(false) normalization rejects a packed subrange. Preserve
     * that rejection, retaining ownership instead of passing a nil HQ onward. */
    if (b->kind == MM_BLOCK_MEMORY && (b->start || b->length != b->capacity))
        return MM_DATA_PACKED;
    if (!decoder || !decoder->decode || !decoder->release || !decoder->size ||
        (b->kind == MM_BLOCK_FILE && !decoder->read_file))
        return MM_DATA_ARGUMENT;
    handle = b->memory;
    if (b->kind == MM_BLOCK_FILE)
    {
        handle = NULL;
        if (!decoder->read_file(decoder->context, &handle))
        {
            if (handle)
                decoder->release(decoder->context, handle);
            return MM_DATA_IO;
        }
    }
    if (!decoder->decode(decoder->context, &handle))
    {
        if (b->kind == MM_BLOCK_FILE && handle)
            decoder->release(decoder->context, handle);
        return MM_DATA_CODEC;
    }
    /* The decoder contract supplies an owned, nonempty, bounded result. */
    length = decoder->size(decoder->context, handle);
    b->kind = MM_BLOCK_MEMORY;
    b->memory = handle;
    b->start = 0;
    b->length = b->capacity = length;
    b->packed = 0;
    return MM_DATA_OK;
}
static MMI32 signed_word(const unsigned char *p)
{
    MMU32 n = ((MMU32)p[0] << 24) | ((MMU32)p[1] << 16) | ((MMU32)p[2] << 8) | p[3];
    return n <= 0x7fffffffUL ? (MMI32)n : -1 - (MMI32)(0xffffffffUL - n);
}
int mm_packed_format(const MMReadStream *stream, MMI32 *format)
{
    unsigned char bytes[4];
    if (!stream || !format || stream->length < 8)
        return MM_DATA_ARGUMENT;
    if (!mm_read_exact(stream, 0, bytes, 4))
        return MM_DATA_IO;
    *format = signed_word(bytes);
    return MM_DATA_OK;
}
int mm_packed_decode(const MMPackedDecoder *decoder, const void *input, MMU32 bytes, void *output,
                           MMU32 capacity, MMI32 *written)
{
    const unsigned char *p = (const unsigned char *)input;
    MMI32 format, expected;
    void *codec;
    if (!input || !written || bytes <= 8 || bytes >= LIMIT || !decoder || !decoder->find || !decoder->convert)
        return MM_DATA_ARGUMENT;
    format = signed_word(p);
    expected = signed_word(p + 4);
    *written = expected;
    if (expected <= 0 || (output ? (MMU32)expected > capacity : (MMU32)expected >= LIMIT))
        return MM_DATA_CODEC;
    codec = decoder->find(decoder->context, format);
    if (!codec)
        return MM_DATA_CODEC;
    if (!output)
        return MM_DATA_OK;
    if (!decoder->convert(codec, format, p + 8, bytes - 8, output, (MMU32)expected, written))
        return MM_DATA_CODEC;
    return *written == expected ? MM_DATA_OK : MM_DATA_WRONG_SIZE;
}
int mm_packed_encode(const MMPackedDecoder *encoder, MMI32 format, const void *input, MMU32 bytes,
                           void *output, MMU32 capacity, MMI32 *written)
{
    void *codec;
    unsigned char *p = (unsigned char *)output;
    unsigned i;
    if (!encoder || !encoder->find || !encoder->convert || !input || !written || !bytes || bytes >= LIMIT ||
        capacity >= LIMIT)
        return MM_DATA_ARGUMENT;
    if (!output || capacity >= bytes)
        capacity = bytes - 1;
    if (capacity <= 8 || !(codec = encoder->find(encoder->context, format)))
        return MM_DATA_NOT_ENCODED;
    if (!output)
    {
        *written = (MMI32)capacity;
        return MM_DATA_OK;
    }
    if (!encoder->convert(codec, format, input, bytes, p + 8, capacity - 8, written))
        return MM_DATA_CODEC;
    if (*written < 1 || (MMU32)*written > capacity - 8)
        return MM_DATA_WRONG_SIZE;
    *written += 8;
    for (i = 0; i < 4; ++i)
    {
        p[i] = (unsigned char)((MMU32)format >> (24 - i * 8));
        p[4 + i] = (unsigned char)(bytes >> (24 - i * 8));
    }
    return MM_DATA_OK;
}
int mm_byte_buffer_init(MMByteBuffer *b, const MMStorageAllocator *allocator)
{
    int result;
    if (!b)
        return MM_STORAGE_ARGUMENT;
    result = mm_storage_init(&b->storage, allocator);
    if (result == MM_STORAGE_OK)
        b->length = b->min_grow = 0;
    return result;
}
int mm_byte_buffer_reserve(MMByteBuffer *b, MMU32 bytes, int shrink)
{
    MMU32 current, candidate;
    int result;
    if (!b || bytes >= LIMIT || b->length > b->storage.buffers[0].capacity || (shrink && bytes < b->length))
        return MM_STORAGE_ARGUMENT;
    current = b->storage.buffers[0].capacity;
    if (bytes <= current && (!shrink || bytes == current))
        return MM_STORAGE_OK;
    if (!bytes)
        return shrink ? mm_storage_resize_exact(&b->storage, 0, 0, MM_STORAGE_NORMAL) : MM_STORAGE_OK;
    candidate = bytes;
    if (b->min_grow < LIMIT - current && current + b->min_grow > bytes)
        candidate = current + b->min_grow;
    result = mm_storage_resize_exact(&b->storage, 0, candidate, MM_STORAGE_NORMAL);
    if (result != MM_STORAGE_OK && candidate > bytes)
        result = mm_storage_resize_exact(&b->storage, 0, bytes, MM_STORAGE_NORMAL);
    return result;
}
int mm_byte_buffer_replace(MMByteBuffer *b, MMU32 offset, MMU32 remove, const void *input,
                                 MMU32 bytes)
{
    unsigned char *p;
    MMU32 length;
    int result;
    if (!b || !mm_range_contains(b->length, offset, remove) || (!input && bytes) || bytes >= LIMIT ||
        bytes > LIMIT - 1 - (b->length - remove))
        return MM_STORAGE_ARGUMENT;
    length = b->length - remove + bytes;
    result = mm_byte_buffer_reserve(b, length, 0);
    if (result != MM_STORAGE_OK)
        return result;
    p = b->storage.buffers[0].data;
    if (offset < b->length - remove && bytes != remove)
        memmove(p + offset + bytes, p + offset + remove, b->length - remove - offset);
    if (bytes)
        memcpy(p + offset, input, bytes);
    b->length = length;
    return MM_STORAGE_OK;
}
int mm_byte_buffer_read(const MMByteBuffer *b, MMU32 offset, void *output, MMU32 bytes)
{
    if (!b || !mm_range_contains(b->length, offset, bytes) || (!output && bytes))
        return MM_STORAGE_ARGUMENT;
    if (bytes)
        memcpy(output, b->storage.buffers[0].data + offset, bytes);
    return MM_STORAGE_OK;
}
int mm_byte_buffer_write(MMByteBuffer *b, MMU32 offset, const MMStorageWriter *writer)
{
    int result, ok;
    if (!b || !writer || !writer->write_at || !mm_range_contains(b->length, offset, writer->length))
        return MM_STORAGE_ARGUMENT;
    if (!writer->length)
        return MM_STORAGE_OK;
    result = mm_storage_lock(&b->storage, 0);
    if (result != MM_STORAGE_OK)
        return result;
    ok = writer->write_at(writer->context, 0, b->storage.buffers[0].data + offset, writer->length);
    mm_storage_unlock(&b->storage, 0);
    return ok ? MM_STORAGE_OK : MM_STORAGE_IO;
}
int mm_piece_find(const MMPieceReader *reader, MMU32 offset, MMU32 *index, MMU32 *start,
                        MMU32 *piece_bytes)
{
    MMU32 i, left = offset, n;
    if (!reader || !index || !start || offset > reader->length || (reader->count && !reader->piece_size))
        return MM_DATA_ARGUMENT;
    if (reader->length)
        for (i = 0; i < reader->count; ++i)
        {
            if (!reader->piece_size(reader->context, i, &n))
                return MM_DATA_IO;
            if (n > left)
            {
                *index = i;
                *start = offset - left;
                if (piece_bytes)
                    *piece_bytes = n;
                return MM_DATA_OK;
            }
            left -= n;
        }
    else
        i = 0;
    *index = i;
    *start = reader->length;
    if (piece_bytes)
        *piece_bytes = 0;
    return MM_DATA_OK;
}
int mm_piece_read(const MMPieceReader *reader, MMU32 offset, void *output, MMU32 bytes)
{
    MMU32 i, start, n, take;
    unsigned char *p = (unsigned char *)output;
    int result, failed = 0;
    if (!reader || !reader->read_piece || (!output && bytes) || !mm_range_contains(reader->length, offset, bytes))
        return MM_DATA_ARGUMENT;
    result = mm_piece_find(reader, offset, &i, &start, NULL);
    if (result != MM_DATA_OK)
        return result;
    if (start > offset)
        return MM_DATA_ARGUMENT;
    offset -= start;
    for (; bytes && i < reader->count; ++i)
    {
        if (!reader->piece_size(reader->context, i, &n))
            return MM_DATA_IO;
        if (offset >= n)
            return MM_DATA_ARGUMENT;
        take = bytes < n - offset ? bytes : n - offset;
        if (!reader->read_piece(reader->context, i, offset, p, take))
        {
            memset(p, 0xa3, take);
            failed = 1;
        }
        p += take;
        bytes -= take;
        offset = 0;
    }
    if (bytes)
        return MM_DATA_ARGUMENT;
    return failed ? MM_DATA_IO : MM_DATA_OK;
}

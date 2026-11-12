/* Stream/block policy converted from Kauai stream.cpp, file.cpp and codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/data_stream.h"
#include <string.h>
#define LIMIT THREE_MM_STORAGE_LIMIT
int three_mm_range_contains(ThreeMMU32 length, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    return offset <= length && bytes <= length - offset;
}
static int block_valid(const ThreeMMBlockView *b)
{
    if (!b || b->kind < THREE_MM_BLOCK_EMPTY || b->kind > THREE_MM_BLOCK_FILE || b->start > 0x7fffffffUL ||
        b->length > 0x7fffffffUL)
        return 0;
    if (b->kind == THREE_MM_BLOCK_EMPTY)
        return !b->length && !b->start;
    if (b->kind == THREE_MM_BLOCK_MEMORY)
        return b->memory && b->capacity < LIMIT && b->start <= b->capacity && b->length <= b->capacity - b->start;
    return 1;
}
int three_mm_block_check(const ThreeMMBlockView *b, ThreeMMU32 offset, ThreeMMU32 bytes, int packed_ok,
                         ThreeMMU32 *physical)
{
    if (!block_valid(b))
        return THREE_MM_DATA_ARGUMENT;
    if (b->packed && !packed_ok)
        return THREE_MM_DATA_PACKED;
    if (!three_mm_range_contains(b->length, offset, bytes) || offset > 0x7fffffffUL - b->start)
        return THREE_MM_DATA_ARGUMENT;
    if (physical)
        *physical = b->start + offset;
    return THREE_MM_DATA_OK;
}
int three_mm_block_move_min(ThreeMMBlockView *b, ThreeMMI32 delta)
{
    ThreeMMU32 n;
    if (!block_valid(b) || b->kind == THREE_MM_BLOCK_EMPTY)
        return THREE_MM_DATA_ARGUMENT;
    if (delta < 0)
    {
        n = 0 - (ThreeMMU32)delta;
        if (n > b->start || n > 0x7fffffffUL - b->length)
            return THREE_MM_DATA_ARGUMENT;
        b->start -= n;
        b->length += n;
    }
    else
    {
        n = (ThreeMMU32)delta;
        if (n > b->length || n > 0x7fffffffUL - b->start)
            return THREE_MM_DATA_ARGUMENT;
        b->start += n;
        b->length -= n;
    }
    return THREE_MM_DATA_OK;
}
int three_mm_block_move_lim(ThreeMMBlockView *b, ThreeMMI32 delta)
{
    ThreeMMU32 n;
    if (!block_valid(b) || b->kind == THREE_MM_BLOCK_EMPTY)
        return THREE_MM_DATA_ARGUMENT;
    if (delta < 0)
    {
        n = 0 - (ThreeMMU32)delta;
        if (n > b->length || (b->kind == THREE_MM_BLOCK_FILE && b->start >= LIMIT && n <= b->start - LIMIT))
            return THREE_MM_DATA_ARGUMENT;
        b->length -= n;
    }
    else
    {
        n = (ThreeMMU32)delta;
        if (n > 0x7fffffffUL - b->length)
            return THREE_MM_DATA_ARGUMENT;
        if (b->kind == THREE_MM_BLOCK_MEMORY)
        {
            if (n > b->capacity - b->start - b->length)
                return THREE_MM_DATA_ARGUMENT;
        }
        else if (b->start >= LIMIT || n >= LIMIT - b->start)
            return THREE_MM_DATA_ARGUMENT;
        b->length += n;
    }
    return THREE_MM_DATA_OK;
}
int three_mm_block_unpack(ThreeMMBlockView *b, const ThreeMMBlockDecoder *decoder)
{
    void *handle;
    ThreeMMU32 length;
    if (!block_valid(b))
        return THREE_MM_DATA_ARGUMENT;
    if (!b->packed)
        return THREE_MM_DATA_OK;
    if (b->kind == THREE_MM_BLOCK_EMPTY || b->length <= 8)
        return THREE_MM_DATA_CODEC;
    /* The old HqFree(false) normalization rejects a packed subrange. Preserve
     * that rejection, retaining ownership instead of passing a nil HQ onward. */
    if (b->kind == THREE_MM_BLOCK_MEMORY && (b->start || b->length != b->capacity))
        return THREE_MM_DATA_PACKED;
    if (!decoder || !decoder->decode || !decoder->release || !decoder->size ||
        (b->kind == THREE_MM_BLOCK_FILE && !decoder->read_file))
        return THREE_MM_DATA_ARGUMENT;
    handle = b->memory;
    if (b->kind == THREE_MM_BLOCK_FILE)
    {
        handle = NULL;
        if (!decoder->read_file(decoder->context, &handle))
        {
            if (handle)
                decoder->release(decoder->context, handle);
            return THREE_MM_DATA_IO;
        }
    }
    if (!decoder->decode(decoder->context, &handle))
    {
        if (b->kind == THREE_MM_BLOCK_FILE && handle)
            decoder->release(decoder->context, handle);
        return THREE_MM_DATA_CODEC;
    }
    /* The decoder contract supplies an owned, nonempty, bounded result. */
    length = decoder->size(decoder->context, handle);
    b->kind = THREE_MM_BLOCK_MEMORY;
    b->memory = handle;
    b->start = 0;
    b->length = b->capacity = length;
    b->packed = 0;
    return THREE_MM_DATA_OK;
}
static ThreeMMI32 signed_word(const unsigned char *p)
{
    ThreeMMU32 n = ((ThreeMMU32)p[0] << 24) | ((ThreeMMU32)p[1] << 16) | ((ThreeMMU32)p[2] << 8) | p[3];
    return n <= 0x7fffffffUL ? (ThreeMMI32)n : -1 - (ThreeMMI32)(0xffffffffUL - n);
}
int three_mm_packed_format(const ThreeMMReadStream *stream, ThreeMMI32 *format)
{
    unsigned char bytes[4];
    if (!stream || !format || stream->length < 8)
        return THREE_MM_DATA_ARGUMENT;
    if (!three_mm_read_exact(stream, 0, bytes, 4))
        return THREE_MM_DATA_IO;
    *format = signed_word(bytes);
    return THREE_MM_DATA_OK;
}
int three_mm_packed_decode(const ThreeMMPackedDecoder *decoder, const void *input, ThreeMMU32 bytes, void *output,
                           ThreeMMU32 capacity, ThreeMMI32 *written)
{
    const unsigned char *p = (const unsigned char *)input;
    ThreeMMI32 format, expected;
    void *codec;
    if (!input || !written || bytes <= 8 || bytes >= LIMIT || !decoder || !decoder->find || !decoder->convert)
        return THREE_MM_DATA_ARGUMENT;
    format = signed_word(p);
    expected = signed_word(p + 4);
    *written = expected;
    if (expected <= 0 || (output ? (ThreeMMU32)expected > capacity : (ThreeMMU32)expected >= LIMIT))
        return THREE_MM_DATA_CODEC;
    codec = decoder->find(decoder->context, format);
    if (!codec)
        return THREE_MM_DATA_CODEC;
    if (!output)
        return THREE_MM_DATA_OK;
    if (!decoder->convert(codec, format, p + 8, bytes - 8, output, (ThreeMMU32)expected, written))
        return THREE_MM_DATA_CODEC;
    return *written == expected ? THREE_MM_DATA_OK : THREE_MM_DATA_WRONG_SIZE;
}
int three_mm_packed_encode(const ThreeMMPackedDecoder *encoder, ThreeMMI32 format, const void *input, ThreeMMU32 bytes,
                           void *output, ThreeMMU32 capacity, ThreeMMI32 *written)
{
    void *codec;
    unsigned char *p = (unsigned char *)output;
    unsigned i;
    if (!encoder || !encoder->find || !encoder->convert || !input || !written || !bytes || bytes >= LIMIT ||
        capacity >= LIMIT)
        return THREE_MM_DATA_ARGUMENT;
    if (!output || capacity >= bytes)
        capacity = bytes - 1;
    if (capacity <= 8 || !(codec = encoder->find(encoder->context, format)))
        return THREE_MM_DATA_NOT_ENCODED;
    if (!output)
    {
        *written = (ThreeMMI32)capacity;
        return THREE_MM_DATA_OK;
    }
    if (!encoder->convert(codec, format, input, bytes, p + 8, capacity - 8, written))
        return THREE_MM_DATA_CODEC;
    if (*written < 1 || (ThreeMMU32)*written > capacity - 8)
        return THREE_MM_DATA_WRONG_SIZE;
    *written += 8;
    for (i = 0; i < 4; ++i)
    {
        p[i] = (unsigned char)((ThreeMMU32)format >> (24 - i * 8));
        p[4 + i] = (unsigned char)(bytes >> (24 - i * 8));
    }
    return THREE_MM_DATA_OK;
}
int three_mm_byte_buffer_init(ThreeMMByteBuffer *b, const ThreeMMStorageAllocator *allocator)
{
    int result;
    if (!b)
        return THREE_MM_STORAGE_ARGUMENT;
    result = three_mm_storage_init(&b->storage, allocator);
    if (result == THREE_MM_STORAGE_OK)
        b->length = b->min_grow = 0;
    return result;
}
int three_mm_byte_buffer_reserve(ThreeMMByteBuffer *b, ThreeMMU32 bytes, int shrink)
{
    ThreeMMU32 current, candidate;
    int result;
    if (!b || bytes >= LIMIT || b->length > b->storage.buffers[0].capacity || (shrink && bytes < b->length))
        return THREE_MM_STORAGE_ARGUMENT;
    current = b->storage.buffers[0].capacity;
    if (bytes <= current && (!shrink || bytes == current))
        return THREE_MM_STORAGE_OK;
    if (!bytes)
        return shrink ? three_mm_storage_resize_exact(&b->storage, 0, 0, THREE_MM_STORAGE_NORMAL) : THREE_MM_STORAGE_OK;
    candidate = bytes;
    if (b->min_grow < LIMIT - current && current + b->min_grow > bytes)
        candidate = current + b->min_grow;
    result = three_mm_storage_resize_exact(&b->storage, 0, candidate, THREE_MM_STORAGE_NORMAL);
    if (result != THREE_MM_STORAGE_OK && candidate > bytes)
        result = three_mm_storage_resize_exact(&b->storage, 0, bytes, THREE_MM_STORAGE_NORMAL);
    return result;
}
int three_mm_byte_buffer_replace(ThreeMMByteBuffer *b, ThreeMMU32 offset, ThreeMMU32 remove, const void *input,
                                 ThreeMMU32 bytes)
{
    unsigned char *p;
    ThreeMMU32 length;
    int result;
    if (!b || !three_mm_range_contains(b->length, offset, remove) || (!input && bytes) || bytes >= LIMIT ||
        bytes > LIMIT - 1 - (b->length - remove))
        return THREE_MM_STORAGE_ARGUMENT;
    length = b->length - remove + bytes;
    result = three_mm_byte_buffer_reserve(b, length, 0);
    if (result != THREE_MM_STORAGE_OK)
        return result;
    p = b->storage.buffers[0].data;
    if (offset < b->length - remove && bytes != remove)
        memmove(p + offset + bytes, p + offset + remove, b->length - remove - offset);
    if (bytes)
        memcpy(p + offset, input, bytes);
    b->length = length;
    return THREE_MM_STORAGE_OK;
}
int three_mm_byte_buffer_read(const ThreeMMByteBuffer *b, ThreeMMU32 offset, void *output, ThreeMMU32 bytes)
{
    if (!b || !three_mm_range_contains(b->length, offset, bytes) || (!output && bytes))
        return THREE_MM_STORAGE_ARGUMENT;
    if (bytes)
        memcpy(output, b->storage.buffers[0].data + offset, bytes);
    return THREE_MM_STORAGE_OK;
}
int three_mm_byte_buffer_write(ThreeMMByteBuffer *b, ThreeMMU32 offset, const ThreeMMStorageWriter *writer)
{
    int result, ok;
    if (!b || !writer || !writer->write_at || !three_mm_range_contains(b->length, offset, writer->length))
        return THREE_MM_STORAGE_ARGUMENT;
    if (!writer->length)
        return THREE_MM_STORAGE_OK;
    result = three_mm_storage_lock(&b->storage, 0);
    if (result != THREE_MM_STORAGE_OK)
        return result;
    ok = writer->write_at(writer->context, 0, b->storage.buffers[0].data + offset, writer->length);
    three_mm_storage_unlock(&b->storage, 0);
    return ok ? THREE_MM_STORAGE_OK : THREE_MM_STORAGE_IO;
}
int three_mm_piece_find(const ThreeMMPieceReader *reader, ThreeMMU32 offset, ThreeMMU32 *index, ThreeMMU32 *start,
                        ThreeMMU32 *piece_bytes)
{
    ThreeMMU32 i, left = offset, n;
    if (!reader || !index || !start || offset > reader->length || (reader->count && !reader->piece_size))
        return THREE_MM_DATA_ARGUMENT;
    if (reader->length)
        for (i = 0; i < reader->count; ++i)
        {
            if (!reader->piece_size(reader->context, i, &n))
                return THREE_MM_DATA_IO;
            if (n > left)
            {
                *index = i;
                *start = offset - left;
                if (piece_bytes)
                    *piece_bytes = n;
                return THREE_MM_DATA_OK;
            }
            left -= n;
        }
    else
        i = 0;
    *index = i;
    *start = reader->length;
    if (piece_bytes)
        *piece_bytes = 0;
    return THREE_MM_DATA_OK;
}
int three_mm_piece_read(const ThreeMMPieceReader *reader, ThreeMMU32 offset, void *output, ThreeMMU32 bytes)
{
    ThreeMMU32 i, start, n, take;
    unsigned char *p = (unsigned char *)output;
    int result, failed = 0;
    if (!reader || !reader->read_piece || (!output && bytes) || !three_mm_range_contains(reader->length, offset, bytes))
        return THREE_MM_DATA_ARGUMENT;
    result = three_mm_piece_find(reader, offset, &i, &start, NULL);
    if (result != THREE_MM_DATA_OK)
        return result;
    if (start > offset)
        return THREE_MM_DATA_ARGUMENT;
    offset -= start;
    for (; bytes && i < reader->count; ++i)
    {
        if (!reader->piece_size(reader->context, i, &n))
            return THREE_MM_DATA_IO;
        if (offset >= n)
            return THREE_MM_DATA_ARGUMENT;
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
        return THREE_MM_DATA_ARGUMENT;
    return failed ? THREE_MM_DATA_IO : THREE_MM_DATA_OK;
}

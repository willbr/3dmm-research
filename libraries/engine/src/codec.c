/* Kauai codec converted from codkauai.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/codec.h"
#include "three_mm/data_stream.h"
#include <string.h>
#define LIMIT THREE_MM_STORAGE_LIMIT
#define SENTINEL (-858993460)
#define OFFSET_LIMIT 0x101240
typedef struct Bits
{
    unsigned char *data;
    ThreeMMU32 size, byte;
    unsigned bit;
} Bits;
/* Keep the original low-bit-first byte writer, including partial writes when
 * a later capacity check fails. Output is unspecified on failed encoding. */
static int put(Bits *b, ThreeMMU32 value, unsigned count)
{
    unsigned bytes;
    if (b->bit)
    {
        b->data[b->byte] = (unsigned char)((b->data[b->byte] & ((1u << b->bit) - 1)) | (value << b->bit));
        if (b->bit + count < 8)
        {
            b->bit += count;
            return 1;
        }
        count -= 8 - b->bit;
        value >>= 8 - b->bit;
        ++b->byte;
        b->bit = 0;
    }
    bytes = count / 8;
    count %= 8;
    if (bytes + (count != 0) > b->size - b->byte)
        return 0;
    while (bytes--)
    {
        b->data[b->byte++] = (unsigned char)value;
        value >>= 8;
    }
    if (count)
    {
        b->data[b->byte] = (unsigned char)value;
        b->bit = count;
    }
    return 1;
}
static int put_length(Bits *b, ThreeMMU32 value)
{
    unsigned count = 0;
    ThreeMMU32 n = value;
    while (n >>= 1)
        ++count;
    return (!count || put(b, 0xffffffffUL, count)) && put(b, value << 1, count + 1);
}
static int valid(ThreeMMI32 format, const void *input, ThreeMMU32 bytes, void *output, ThreeMMU32 capacity,
                 ThreeMMI32 *written)
{
    return (format == THREE_MM_CODEC_KCDC || format == THREE_MM_CODEC_KCD2) && input && output && written && bytes &&
           bytes < LIMIT && capacity && capacity < LIMIT;
}
int three_mm_codec_encode(ThreeMMI32 format, const void *input, ThreeMMU32 bytes, void *output, ThreeMMU32 capacity,
                          ThreeMMI32 *written, const ThreeMMStorageAllocator *allocator)
{
    ThreeMMCollectionStorage scratch;
    ThreeMMI32 *next, *starts;
    const unsigned char *source = (const unsigned char *)input;
    ThreeMMI32 position, match, test, length, minimum, n, max_match, run = 0, i;
    ThreeMMU32 code, encoded_length;
    unsigned bits, bit;
    unsigned char new_byte, last_byte;
    Bits b;
    int result = THREE_MM_DATA_CODEC;
    if (!valid(format, input, bytes, output, capacity, written) || bytes > (LIMIT - 1) / 4)
        return THREE_MM_DATA_ARGUMENT;
    if (capacity <= 7)
        return THREE_MM_DATA_CODEC;
    if (three_mm_storage_init(&scratch, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_DATA_ARGUMENT;
    if (three_mm_storage_resize_exact(&scratch, 0, bytes * 4, THREE_MM_STORAGE_NORMAL) != THREE_MM_STORAGE_OK ||
        three_mm_storage_resize_exact(&scratch, 1, 0x40000, THREE_MM_STORAGE_NORMAL) != THREE_MM_STORAGE_OK)
    {
        result = THREE_MM_DATA_MEMORY;
        goto done;
    }
    next = (ThreeMMI32 *)scratch.buffers[0].data;
    starts = (ThreeMMI32 *)scratch.buffers[1].data;
    /* Use a signed constant rather than relying on signed integer byte layout. */
    for (i = 0; i < 0x10000; ++i)
        starts[i] = SENTINEL;
    for (position = 0; position < (ThreeMMI32)bytes - 1; ++position)
    {
        n = ((unsigned)source[position] << 8) | source[position + 1];
        next[position] = starts[n];
        starts[n] = position;
    }
    next[bytes - 1] = SENTINEL;
    three_mm_storage_resize_exact(&scratch, 1, 0, THREE_MM_STORAGE_NORMAL);
    b.data = (unsigned char *)output;
    b.size = capacity;
    b.byte = 0;
    b.bit = 0;
    put(&b, 0, 8);
    for (position = 0;; position += length)
    {
        if (position >= (ThreeMMI32)bytes)
        {
            if (format == THREE_MM_CODEC_KCDC)
                break;
            length = 0;
            goto store;
        }
        test = next[position];
        length = 1;
        match = position;
        minimum = position - OFFSET_LIMIT;
        if (test > minimum)
        {
            max_match = (ThreeMMI32)bytes - position;
            if (max_match > 4096)
                max_match = 4096;
            new_byte = source[position + 1];
            last_byte = source[position];
            for (; test > minimum; test = next[test])
            {
                if (source[test + length] != new_byte || source[test + length - 1] != last_byte)
                    continue;
                for (n = 0; n < max_match && source[test + n] == source[position + n]; ++n)
                {
                }
                if (n <= length)
                    continue;
                /* Original nearest/longest-match and deep-offset preference. */
                if (position - test < 0x1241 || n - length > 1 || position - match >= 0x241)
                {
                    length = n;
                    match = test;
                    if (length == max_match)
                        break;
                    new_byte = source[position + length];
                    last_byte = source[position + length - 1];
                }
            }
        }
    store:
        if (format == THREE_MM_CODEC_KCD2 && length != 1 && run)
        {
            if (!put_length(&b, run) || !put(&b, 0, 1))
                goto done;
            bit = b.bit;
            if (bit)
            {
                if (!put(&b, source[position - 1], 8 - bit))
                    goto done;
            }
            else
                bit = 8;
            for (i = position - run; i < position - 1; ++i)
                if (!put(&b, source[i], 8))
                    goto done;
            if (!put(&b, source[position - 1] >> (8 - bit), bit))
                goto done;
            run = 0;
        }
        if (!length)
            break;
        if (length == 1)
        {
            if (format == THREE_MM_CODEC_KCD2)
                ++run;
            else if (!put(&b, (ThreeMMU32)source[position] << 1, 9))
                goto done;
            continue;
        }
        code = position - match;
        encoded_length = length - 1;
        if (code < 0x41)
        {
            bits = 8;
            code = ((code - 1) << 2) | 1;
        }
        else if (code < 0x241)
        {
            bits = 12;
            code = ((code - 0x41) << 3) | 3;
        }
        else if (code < 0x1241)
        {
            bits = 16;
            code = ((code - 0x241) << 4) | 7;
        }
        else
        {
            bits = 24;
            code = ((code - 0x1241) << 4) | 15;
            --encoded_length;
        }
        if (format == THREE_MM_CODEC_KCDC)
        {
            if (!put(&b, code, bits) || !put_length(&b, encoded_length))
                goto done;
        }
        else if (!put_length(&b, encoded_length) || !put(&b, code, bits))
            goto done;
    }
    if (b.bit)
        put(&b, 0xff, 8 - b.bit);
    if (!put(&b, 0xffffffffUL, 32) || !put(&b, 0xffff, 16))
        goto done;
    *written = (ThreeMMI32)b.byte;
    result = THREE_MM_DATA_OK;
done:
    three_mm_storage_dispose(&scratch);
    return result;
}
typedef struct Reader
{
    const unsigned char *data;
    ThreeMMU32 size, byte;
    unsigned bit;
} Reader;
static int get(Reader *r, unsigned count, ThreeMMU32 *value)
{
    unsigned i;
    ThreeMMU32 n = 0;
    for (i = 0; i < count; ++i)
    {
        if (r->byte >= r->size)
            return 0;
        n |= (ThreeMMU32)((r->data[r->byte] >> r->bit) & 1) << i;
        if (++r->bit == 8)
        {
            r->bit = 0;
            ++r->byte;
        }
    }
    *value = n;
    return 1;
}
/* Twelve one bits terminate KCD2; KCDC rejects this length prefix. */
static int get_length(Reader *r, ThreeMMU32 *length)
{
    unsigned bits = 0;
    ThreeMMU32 bit, n;
    for (;;)
    {
        if (!get(r, 1, &bit))
            return 0;
        if (!bit)
            break;
        if (++bits > 11)
            return 2;
    }
    if (!get(r, bits, &n))
        return 0;
    *length = ((ThreeMMU32)1 << bits) + n;
    return 1;
}
static int get_offset(Reader *r, ThreeMMU32 *offset, int *deep)
{
    unsigned tier;
    ThreeMMU32 bit, n;
    static const unsigned widths[] = {6, 9, 12, 20};
    static const ThreeMMU32 bases[] = {1, 0x41, 0x241, 0x1241};
    for (tier = 0; tier < 3; ++tier)
    {
        if (!get(r, 1, &bit))
            return 0;
        if (!bit)
            break;
    }
    if (!get(r, widths[tier], &n))
        return 0;
    *deep = tier == 3;
    *offset = n + bases[tier];
    return 1;
}
int three_mm_codec_decode(ThreeMMI32 format, const void *input, ThreeMMU32 bytes, void *output, ThreeMMU32 capacity,
                          ThreeMMI32 *written)
{
    Reader r;
    unsigned char *dest = (unsigned char *)output;
    ThreeMMU32 position = 0, n, offset, bit, last, high, i;
    unsigned split;
    int result, deep;
    if (!valid(format, input, bytes, output, capacity, written))
        return THREE_MM_DATA_ARGUMENT;
    if (bytes <= 7 || ((const unsigned char *)input)[0])
        return THREE_MM_DATA_CODEC;
    for (i = bytes - 6; i < bytes; ++i)
        if (((const unsigned char *)input)[i] != 0xff)
            return THREE_MM_DATA_CODEC;
    r.data = (const unsigned char *)input;
    r.size = bytes;
    r.byte = 1;
    r.bit = 0;
    for (;;)
    {
        if (format == THREE_MM_CODEC_KCD2)
        {
            result = get_length(&r, &n);
            if (result == 2)
                break;
            if (!result || !get(&r, 1, &bit))
                return THREE_MM_DATA_CODEC;
            if (!bit)
            {
                /* Retain the original literal payload/tail separation. */
                i = r.byte - (r.bit == 0);
                if (n > capacity - position || i > bytes - 6 || n + 1 > bytes - 6 - i)
                    return THREE_MM_DATA_CODEC;
                split = r.bit ? r.bit : 8;
                if (!get(&r, 8 - split, &last))
                    return THREE_MM_DATA_CODEC;
                for (i = 1; i < n; ++i)
                {
                    if (!get(&r, 8, &high))
                        return THREE_MM_DATA_CODEC;
                    dest[position++] = (unsigned char)high;
                }
                if (!get(&r, split, &high))
                    return THREE_MM_DATA_CODEC;
                dest[position++] = (unsigned char)(last | (high << (8 - split)));
                continue;
            }
            if (!get_offset(&r, &offset, &deep))
                return THREE_MM_DATA_CODEC;
            n += 1 + deep;
        }
        else
        {
            if (!get(&r, 1, &bit))
                return THREE_MM_DATA_CODEC;
            if (!bit)
            {
                if (position >= capacity || !get(&r, 8, &n))
                    return THREE_MM_DATA_CODEC;
                dest[position++] = (unsigned char)n;
                continue;
            }
            if (!get_offset(&r, &offset, &deep))
                return THREE_MM_DATA_CODEC;
            if (deep && offset == OFFSET_LIMIT)
                break;
            if (get_length(&r, &n) != 1)
                return THREE_MM_DATA_CODEC;
            n += 1 + deep;
        }
        if (n > capacity - position || offset > position)
            return THREE_MM_DATA_CODEC;
        while (n--)
        {
            dest[position] = dest[position - offset];
            ++position;
        }
    }
    *written = (ThreeMMI32)position;
    return THREE_MM_DATA_OK;
}

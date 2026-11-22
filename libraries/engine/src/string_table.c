/* Storage policy converted from Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/string_table.h"
#include "mm/collection_storage.h"
#include "mm/text.h"
#include <stdlib.h>
#include <string.h>
#define LIMIT 0x08000000UL
#define FREE_OFFSET ((MMU32)0xffffffffUL)
static MMU32 offset_at(const MMStringTable *t, MMU32 i)
{
    MMU32 value;
    memcpy(&value, t->entries + (size_t)i * t->stride, 4);
    return value;
}
static void set_offset(MMStringTable *t, MMU32 i, MMU32 value)
{
    memcpy(t->entries + (size_t)i * t->stride, &value, 4);
}
static int shape(const MMStringTable *t)
{
    return t && (t->unit_bytes == 1 || t->unit_bytes == 2) && t->stride >= 4 && t->stride < LIMIT &&
           t->count <= (LIMIT - 1) / t->stride && t->bytes < LIMIT && t->count * t->stride <= t->entry_capacity &&
           t->bytes <= t->string_capacity && (!t->bytes || t->strings) && (!t->count || t->entries);
}
int mm_strings_get(const MMStringTable *t, MMU32 i, const void **text, MMU32 *count)
{
    MMU32 offset, n;
    unsigned short wide_count;
    if (!shape(t) || i >= t->count || !text || !count)
        return 0;
    offset = offset_at(t, i);
    if (offset > t->bytes || t->unit_bytes > t->bytes - offset)
        return 0;
    if (t->unit_bytes == 1)
        n = t->strings[offset];
    else
    {
        memcpy(&wide_count, t->strings + offset, 2);
        n = wide_count;
    }
    if (n > 255 || n > (t->bytes - offset) / t->unit_bytes - 1)
        return 0;
    *text = t->strings + offset + t->unit_bytes;
    *count = n;
    return 1;
}
static int ensure(MMStringTable *t, MMU32 bytes, MMU32 count)
{
    if (bytes >= LIMIT || count > (LIMIT - 1) / t->stride)
        return 0;
    if (bytes <= t->string_capacity && count * t->stride <= t->entry_capacity)
        return 1;
    return t->reserve && t->reserve(t->context, t, bytes, count * t->stride) && bytes <= t->string_capacity &&
           count * t->stride <= t->entry_capacity;
}
int mm_strings_reserve_storage(void *context, MMStringTable *table, MMU32 bytes, MMU32 entries)
{
    MMCollectionStorage *storage = context;
    int status = mm_storage_ensure(storage, bytes, entries, 0);
    table->strings = storage->buffers[0].data;
    table->entries = storage->buffers[1].data;
    table->string_capacity = storage->buffers[0].capacity;
    table->entry_capacity = storage->buffers[1].capacity;
    return status == MM_STORAGE_OK;
}
static void trash(const MMStringTable *t, void *data, size_t bytes)
{
    if (t->trash_unused && bytes)
        memset(data, 0xa3, bytes);
}
static void append(MMStringTable *t, const void *text, MMU32 count)
{
    unsigned short wide_count = (unsigned short)count;
    if (t->unit_bytes == 1)
        t->strings[t->bytes] = (unsigned char)count;
    else
        memcpy(t->strings + t->bytes, &wide_count, 2);
    if (count)
        memcpy(t->strings + t->bytes + t->unit_bytes, text, count * t->unit_bytes);
    t->bytes += (count + 1) * t->unit_bytes;
}
static void remove_string(MMStringTable *t, MMU32 offset, MMU32 bytes)
{
    MMU32 i, other;
    if (offset + bytes < t->bytes)
    {
        memmove(t->strings + offset, t->strings + offset + bytes, t->bytes - offset - bytes);
        for (i = 0; i < t->count; ++i)
        {
            other = offset_at(t, i);
            if (other != FREE_OFFSET && other > offset)
                set_offset(t, i, other - bytes);
        }
    }
    t->bytes -= bytes;
}
static void entry(MMStringTable *t, MMU32 i, const void *extra)
{
    unsigned char *out = t->entries + (size_t)i * t->stride + 4;
    set_offset(t, i, t->bytes);
    if (extra && t->stride > 4)
        memcpy(out, extra, t->stride - 4);
    else
        trash(t, out, t->stride - 4);
}
int mm_strings_insert(MMStringTable *t, MMU32 i, const void *text, MMU32 n, const void *extra)
{
    if (!shape(t) || t->free_count != -1 || i > t->count || n > 255 || (!text && n))
        return 0;
    if (!ensure(t, t->bytes + (n + 1) * t->unit_bytes, t->count + 1))
        return 0;
    memmove(t->entries + (size_t)(i + 1) * t->stride, t->entries + (size_t)i * t->stride,
            (size_t)(t->count - i) * t->stride);
    entry(t, i, extra);
    ++t->count;
    append(t, text, n);
    return 1;
}
int mm_strings_add(MMStringTable *t, const void *text, MMU32 n, const void *extra, MMU32 *position)
{
    MMU32 i;
    if (!shape(t) || n > 255 || (!text && n))
        return 0;
    i = t->count;
    if (t->free_count == -1)
    {
        if (!mm_strings_insert(t, i, text, n, extra))
            return 0;
    }
    else
    {
        if (t->free_count > 0)
            for (i = 0; i < t->count && offset_at(t, i) != FREE_OFFSET; ++i)
            {
            }
        if (!ensure(t, t->bytes + (n + 1) * t->unit_bytes, t->count + (i == t->count)))
            return 0;
        if (i == t->count)
            ++t->count;
        else
            --t->free_count;
        entry(t, i, extra);
        append(t, text, n);
    }
    if (position)
        *position = i;
    return 1;
}
int mm_strings_put(MMStringTable *t, MMU32 i, const void *text, MMU32 n)
{
    const void *old;
    MMU32 count, offset;
    if ((!text && n) || n > 255 || !mm_strings_get(t, i, &old, &count))
        return 0;
    if (n == count)
    {
        if (n)
            memcpy((void *)old, text, n * t->unit_bytes);
        return 1;
    }
    if (n > count && !ensure(t, t->bytes + (n - count) * t->unit_bytes, t->count))
        return 0;
    offset = offset_at(t, i);
    remove_string(t, offset, (count + 1) * t->unit_bytes);
    set_offset(t, i, t->bytes);
    append(t, text, n);
    return 1;
}
int mm_strings_delete(MMStringTable *t, MMU32 i)
{
    const void *text;
    MMU32 count, offset, old_count;
    if (!mm_strings_get(t, i, &text, &count))
        return 0;
    offset = offset_at(t, i);
    old_count = t->count;
    if (t->free_count == -1)
    {
        --t->count;
        memmove(t->entries + (size_t)i * t->stride, t->entries + (size_t)(i + 1) * t->stride,
                (size_t)(t->count - i) * t->stride);
        trash(t, t->entries + (size_t)t->count * t->stride, t->stride);
    }
    else if (i == t->count - 1)
    {
        --t->count;
        while (t->count && offset_at(t, t->count - 1) == FREE_OFFSET)
        {
            --t->count;
            --t->free_count;
        }
        trash(t, t->entries + (size_t)t->count * t->stride, (size_t)(old_count - t->count) * t->stride);
    }
    else
    {
        set_offset(t, i, FREE_OFFSET);
        trash(t, t->entries + (size_t)i * t->stride + 4, t->stride - 4);
        ++t->free_count;
    }
    remove_string(t, offset, (count + 1) * t->unit_bytes);
    return 1;
}
int mm_strings_move(MMStringTable *t, MMU32 source, MMU32 target)
{
    MMU32 byte;
    unsigned char saved, *entries;
    if (!shape(t) || t->free_count != -1 || source >= t->count || target > t->count)
        return 0;
    if (target > source)
        --target;
    if (source == target)
        return 1;
    /* Rotate one column at a time: no allocation or failure path for Move. */
    entries = t->entries;
    for (byte = 0; byte < t->stride; ++byte)
    {
        MMU32 i;
        saved = entries[(size_t)source * t->stride + byte];
        if (source < target)
            for (i = source; i < target; ++i)
                entries[(size_t)i * t->stride + byte] = entries[(size_t)(i + 1) * t->stride + byte];
        else
            for (i = source; i > target; --i)
                entries[(size_t)i * t->stride + byte] = entries[(size_t)(i - 1) * t->stride + byte];
        entries[(size_t)target * t->stride + byte] = saved;
    }
    return 1;
}
int mm_strings_find(const MMStringTable *t, const void *text, MMU32 n, MMStringCompare compare,
                          void *context, MMU32 *position)
{
    MMU32 lo = 0, hi, i, count;
    const void *value;
    int order;
    if (!shape(t) || (!text && n) || !position)
        return 0;
    hi = t->count;
    if (!compare)
    {
        for (i = 0; i < hi; ++i)
            if (mm_strings_get(t, i, &value, &count) && n == count &&
                (!n || !memcmp(text, value, n * t->unit_bytes)))
            {
                *position = i;
                return 1;
            }
        *position = hi;
        return 0;
    }
    while (lo < hi)
    {
        i = lo + (hi - lo) / 2;
        if (!mm_strings_get(t, i, &value, &count))
            return 0;
        order = compare(context, text, n, value, count);
        if (order < 0)
            hi = i;
        else if (order > 0)
            lo = i + 1;
        else
        {
            *position = i;
            return 1;
        }
    }
    *position = lo;
    return 0;
}
void mm_strings_swap_offsets(MMStringTable *t)
{
    MMU32 i;
    for (i = 0; i < t->count; ++i)
    {
        unsigned char *p = t->entries + (size_t)i * t->stride, temp;
        temp = p[0];
        p[0] = p[3];
        p[3] = temp;
        temp = p[1];
        p[1] = p[2];
        p[2] = temp;
    }
}
static unsigned encoding_unit(int encoding)
{
    if (encoding == MM_TEXT_WINDOWS || encoding == MM_TEXT_MAC)
        return 1;
    if (encoding == MM_TEXT_UTF16_LE || encoding == MM_TEXT_UTF16_BE)
        return 2;
    return 0;
}
int mm_strings_wire_system(int current, int requested, int *stored)
{
    unsigned unit = encoding_unit(current);
    int mac;
    if (!stored || !unit || !encoding_unit(requested))
        return 0;
    mac = requested == MM_TEXT_MAC || requested == MM_TEXT_UTF16_BE;
    *stored = unit == 1 ? (mac ? MM_TEXT_MAC : MM_TEXT_WINDOWS)
                        : (mac ? MM_TEXT_UTF16_BE : MM_TEXT_UTF16_LE);
    return 1;
}
int mm_strings_translate_same(MMStringTable *t, int other, int current, int to_current)
{
    MMU32 offset = 0, n, bytes, i;
    unsigned short wide_count;
    unsigned char *p, temp;
    if (!shape(t) || encoding_unit(other) != t->unit_bytes || encoding_unit(current) != t->unit_bytes)
        return 0;
    if (other == current)
        return 1;
    /* Prefix lengths remain native endian, as in TranslateSt. Only character
     * units are swapped; the offset words are handled separately by the host. */
    while (offset < t->bytes)
    {
        if (t->bytes - offset < t->unit_bytes)
            return 0;
        p = t->strings + offset;
        if (t->unit_bytes == 1)
            n = *p;
        else
        {
            memcpy(&wide_count, p, 2);
            n = wide_count;
        }
        bytes = (n + 1) * t->unit_bytes;
        if (bytes > t->bytes - offset)
            return 0;
        p += t->unit_bytes;
        if (t->unit_bytes == 1)
            mm_text_map_bytes(p, n, current == MM_TEXT_WINDOWS ? to_current : !to_current);
        else
            for (i = 0; i < n; ++i)
            {
                temp = p[i * 2];
                p[i * 2] = p[i * 2 + 1];
                p[i * 2 + 1] = temp;
            }
        offset += bytes;
    }
    return 1;
}
int mm_strings_translate(MMStringTable *t, int source, int current, MMStringConvert convert,
                               void *context)
{
    unsigned source_unit = encoding_unit(source);
    unsigned char *old;
    MMU32 old_bytes, i, offset, n;
    unsigned short wide_count, buffer[256];
    size_t converted;
    int ok = 0;
    if (!shape(t) || !source_unit || encoding_unit(current) != t->unit_bytes)
        return 0;
    if (source_unit == t->unit_bytes)
        return mm_strings_translate_same(t, source, current, 1);
    if (!t->bytes)
        return 1;
    if (!convert)
        return 0;
    old = (unsigned char *)malloc(t->bytes);
    if (!old)
        return 0;
    old_bytes = t->bytes;
    memcpy(old, t->strings, old_bytes);
    t->bytes = 0;
    for (i = 0; i < t->count; ++i)
    {
        offset = offset_at(t, i);
        set_offset(t, i, t->bytes);
        if (offset > old_bytes || source_unit > old_bytes - offset)
            goto done;
        if (source_unit == 1)
            n = old[offset];
        else
        {
            wide_count = (unsigned short)(source == MM_TEXT_UTF16_LE ? old[offset] | (old[offset + 1] << 8)
                                                                           : (old[offset] << 8) | old[offset + 1]);
            n = wide_count;
        }
        if (n > 255 || n > (old_bytes - offset) / source_unit - 1)
            goto done;
        converted = convert(context, old + offset + source_unit, n * source_unit, source, buffer, 255);
        if (converted > 255 || !ensure(t, t->bytes + ((MMU32)converted + 1) * t->unit_bytes, t->count))
            goto done;
        append(t, buffer, (MMU32)converted);
    }
    ok = 1;
done:
    free(old);
    return ok;
}

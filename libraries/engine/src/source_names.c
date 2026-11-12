/* Source-name policy converted from engine tagman.cpp and Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/source_names.h"
#include "three_mm/wide.h"
#include <string.h>
static int reserve(void *context, ThreeMMStringTable *view, ThreeMMU32 first, ThreeMMU32 second)
{
    ThreeMMSourceNames *n = (ThreeMMSourceNames *)context;
    int result = three_mm_storage_ensure(&n->storage, first, second, 0);
    view->strings = n->storage.buffers[0].data;
    view->entries = n->storage.buffers[1].data;
    view->string_capacity = n->storage.buffers[0].capacity;
    view->entry_capacity = n->storage.buffers[1].capacity;
    return result == THREE_MM_STORAGE_OK;
}
static ThreeMMStringTable view(ThreeMMSourceNames *n)
{
    ThreeMMStringTable t;
    memset(&t, 0, sizeof(t));
    t.strings = n->storage.buffers[0].data;
    t.entries = n->storage.buffers[1].data;
    t.bytes = n->bytes;
    t.count = n->count;
    t.stride = 8;
    t.free_count = -1;
    t.unit_bytes = n->unit;
    t.string_capacity = n->storage.buffers[0].capacity;
    t.entry_capacity = n->storage.buffers[1].capacity;
    t.trash_unused = n->trash_unused;
    t.context = n;
    t.reserve = reserve;
    return t;
}
int three_mm_source_names_init(ThreeMMSourceNames *n, unsigned unit, int encoding, int trash,
                               const ThreeMMStorageAllocator *allocator)
{
    if (!n || (unit != 1 && unit != 2))
        return THREE_MM_STORAGE_ARGUMENT;
    memset(n, 0, sizeof(*n));
    if (three_mm_storage_init(&n->storage, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_STORAGE_ARGUMENT;
    n->unit = unit;
    n->encoding = encoding;
    n->trash_unused = trash;
    n->storage.min_grow[0] = 120;
    n->storage.min_grow[1] = 96;
    return THREE_MM_STORAGE_OK;
}
int three_mm_source_names_dispose(ThreeMMSourceNames *n)
{
    int result = three_mm_storage_dispose(&n->storage);
    if (result == THREE_MM_STORAGE_OK)
        n->count = n->bytes = 0;
    return result;
}
int three_mm_source_names_at(ThreeMMSourceNames *n, ThreeMMU32 i, const void **text, ThreeMMU32 *count, ThreeMMI32 *sid)
{
    ThreeMMStringTable t = view(n);
    if (!sid || !three_mm_strings_get(&t, i, text, count))
        return 0;
    memcpy(sid, t.entries + i * 8 + 4, 4);
    return 1;
}
int three_mm_source_names_find_id(ThreeMMSourceNames *n, ThreeMMI32 sid, ThreeMMU32 *position)
{
    ThreeMMU32 i;
    ThreeMMI32 value;
    for (i = 0; i < n->count; ++i)
    {
        memcpy(&value, n->storage.buffers[1].data + i * 8 + 4, 4);
        if (value == sid)
        {
            if (position)
                *position = i;
            return 1;
        }
    }
    return 0;
}
int three_mm_source_names_add(ThreeMMSourceNames *n, ThreeMMI32 sid, const void *text, ThreeMMU32 count)
{
    ThreeMMStringTable t;
    int ok;
    if (three_mm_source_names_find_id(n, sid, NULL))
        return 1;
    t = view(n);
    ok = three_mm_strings_add(&t, text, count, &sid, NULL);
    n->count = t.count;
    n->bytes = t.bytes;
    return ok;
}
int three_mm_source_names_merge(ThreeMMSourceNames *n, const ThreeMMSourceNameInput *input, int swap)
{
    ThreeMMU32 i, count, value;
    ThreeMMI32 sid;
    const void *text;
    for (i = 0; i < input->count; ++i)
    {
        input->get(input->context, i, &text, &count, &sid);
        if (swap)
        {
            value = (ThreeMMU32)sid;
            value = (value >> 24) | ((value >> 8) & 0xff00U) | ((value << 8) & 0xff0000UL) | (value << 24);
            sid = three_mm_signed_word(value);
        }
        if (sid < 0 && input->invalid_sid)
            input->invalid_sid(input->context, sid);
        if (!three_mm_source_names_add(n, sid, text, count))
            return 0;
    }
    return 1;
}
static unsigned character(const void *text, ThreeMMU32 i, unsigned unit)
{
    unsigned short value;
    if (unit == 1)
        return ((const unsigned char *)text)[i];
    memcpy(&value, (const unsigned char *)text + i * 2, 2);
    return value;
}
ThreeMMSourceNameParts three_mm_source_name_split(const void *text, ThreeMMU32 count, unsigned unit)
{
    ThreeMMSourceNameParts p;
    ThreeMMU32 i, end;
    p.long_count = p.short_count = count;
    p.short_offset = 0;
    for (i = 0; i < count && character(text, i, unit); ++i)
    {
        if (character(text, i, unit) != '/')
            continue;
        end = i + 1;
        while (end < count && character(text, end, unit))
            ++end;
        p.long_count = i;
        p.short_offset = i + 1;
        p.short_count = end - i - 1;
        break;
    }
    return p;
}
int three_mm_source_names_find_name(ThreeMMSourceNames *n, const void *text, ThreeMMU32 count,
                                    ThreeMMSourceNameEqual equal, void *context, ThreeMMI32 *sid)
{
    ThreeMMU32 i, length;
    ThreeMMI32 value;
    const void *name;
    ThreeMMSourceNameParts parts;
    for (i = 0; i < n->count; ++i)
    {
        if (!three_mm_source_names_at(n, i, &name, &length, &value))
            return 0;
        if (equal(context, name, length, text, count))
        {
            *sid = value;
            return 1;
        }
        parts = three_mm_source_name_split(name, length, n->unit);
        if (equal(context, name, parts.long_count, text, count) ||
            equal(context, (const unsigned char *)name + parts.short_offset * n->unit, parts.short_count, text, count))
        {
            *sid = value;
            return 1;
        }
    }
    return 0;
}
int three_mm_source_names_merged(ThreeMMSourceNames *n, ThreeMMI32 sid, const void **text, ThreeMMU32 *count)
{
    ThreeMMU32 i;
    ThreeMMI32 value;
    for (i = 0; i < n->count; ++i)
    {
        if (!three_mm_source_names_at(n, i, text, count, &value))
            return 0;
        if (value == sid)
            return 1;
    }
    return 0;
}
int three_mm_source_names_resolve(ThreeMMSourceNames *n, ThreeMMI32 sid, void *path, ThreeMMSourceNameProvider provider,
                                  void *context)
{
    const void *text;
    ThreeMMU32 count;
    unsigned short scratch[256];
    ThreeMMSourceNameParts p;
    if (!provider || !three_mm_source_names_merged(n, sid, &text, &count))
        return 0;
    memcpy(scratch, text, count * n->unit);
    text = scratch;
    p = three_mm_source_name_split(text, count, n->unit);
    return provider(context, text, p.long_count, (const unsigned char *)text + p.short_offset * n->unit, p.short_count,
                    path);
}
static ThreeMMCollectionHeader header(const ThreeMMSourceNames *n)
{
    ThreeMMCollectionHeader h;
    memset(&h, 0, sizeof(h));
    h.kind = THREE_MM_COLLECTION_STRINGS;
    h.width = 8;
    h.count = n->count;
    h.bytes = n->bytes;
    h.free_count = -1;
    h.system = n->encoding;
    return h;
}
ThreeMMU32 three_mm_source_names_wire_size(const ThreeMMSourceNames *n)
{
    ThreeMMCollectionHeader h = header(n);
    if (three_mm_collection_layout(&h) != THREE_MM_STORAGE_OK)
        return 0;
    return h.header_bytes + h.first_bytes + h.second_bytes;
}
int three_mm_source_names_write(ThreeMMSourceNames *n, const ThreeMMStorageWriter *writer, int big)
{
    ThreeMMCollectionHeader h = header(n);
    ThreeMMStringTable t = view(n);
    unsigned char bytes[20];
    const unsigned short one = 1;
    int swap = (big != 0) != (*(const unsigned char *)&one == 0), result;
    h.big_endian = big != 0;
    result = three_mm_collection_layout(&h);
    if (result != THREE_MM_STORAGE_OK)
        return result;
    result = three_mm_collection_header_write(&h, bytes, sizeof(bytes));
    if (result != THREE_MM_STORAGE_OK)
        return result;
    if (swap)
        three_mm_strings_swap_offsets(&t);
    result = three_mm_storage_write(&n->storage, writer, bytes, h.header_bytes, h.first_bytes, h.second_bytes);
    if (swap)
        three_mm_strings_swap_offsets(&t);
    return result;
}

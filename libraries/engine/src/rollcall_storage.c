/* Roll-call storage converted from engine movie_data.cpp and Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/rollcall_storage.h"
#include "three_mm/text.h"
#include <string.h>
static int reserve(void *context, ThreeMMStringTable *view, ThreeMMU32 first, ThreeMMU32 second)
{
    ThreeMMRollCallTable *t = (ThreeMMRollCallTable *)context;
    int result = three_mm_storage_ensure(&t->storage, first, second, 0);
    view->strings = t->storage.buffers[0].data;
    view->entries = t->storage.buffers[1].data;
    view->string_capacity = t->storage.buffers[0].capacity;
    view->entry_capacity = t->storage.buffers[1].capacity;
    return result == THREE_MM_STORAGE_OK;
}
static ThreeMMStringTable view(ThreeMMRollCallTable *t)
{
    ThreeMMStringTable v;
    memset(&v, 0, sizeof(v));
    v.strings = t->storage.buffers[0].data;
    v.entries = t->storage.buffers[1].data;
    v.bytes = t->bytes;
    v.count = t->count;
    v.stride = t->stride;
    v.free_count = -1;
    v.unit_bytes = t->unit;
    v.string_capacity = t->storage.buffers[0].capacity;
    v.entry_capacity = t->storage.buffers[1].capacity;
    v.trash_unused = t->trash_unused;
    v.context = t;
    v.reserve = reserve;
    return v;
}
static void apply(ThreeMMRollCallTable *t, const ThreeMMStringTable *v)
{
    t->count = v->count;
    t->bytes = v->bytes;
}
int three_mm_rollcall_table_init(ThreeMMRollCallTable *t, ThreeMMU32 extra, unsigned unit, int encoding, int trash,
                                 const ThreeMMStorageAllocator *a)
{
    if (!t || (unit != 1 && unit != 2) || extra % 4 || extra > THREE_MM_STORAGE_LIMIT - 8)
        return THREE_MM_STORAGE_ARGUMENT;
    memset(t, 0, sizeof(*t));
    if (three_mm_storage_init(&t->storage, a) != THREE_MM_STORAGE_OK)
        return THREE_MM_STORAGE_ARGUMENT;
    t->stride = extra + 4;
    t->unit = unit;
    t->encoding = encoding;
    t->trash_unused = trash;
    t->storage.min_grow[0] = 120;
    t->storage.min_grow[1] = t->stride <= THREE_MM_STORAGE_LIMIT / 12 ? t->stride * 12 : THREE_MM_STORAGE_LIMIT - 1;
    return THREE_MM_STORAGE_OK;
}
int three_mm_rollcall_table_dispose(ThreeMMRollCallTable *t)
{
    int result = three_mm_storage_dispose(&t->storage);
    if (result == THREE_MM_STORAGE_OK)
        t->count = t->bytes = 0;
    return result;
}
int three_mm_rollcall_table_name(ThreeMMRollCallTable *t, ThreeMMU32 i, const void **text, ThreeMMU32 *count)
{
    ThreeMMStringTable v = view(t);
    return three_mm_strings_get(&v, i, text, count);
}
int three_mm_rollcall_table_extra(ThreeMMRollCallTable *t, ThreeMMU32 i, void *out)
{
    if (i >= t->count || (!out && t->stride > 4))
        return 0;
    if (t->stride > 4)
        memcpy(out, t->storage.buffers[1].data + i * t->stride + 4, t->stride - 4);
    return 1;
}
int three_mm_rollcall_table_set_extra(ThreeMMRollCallTable *t, ThreeMMU32 i, const void *extra)
{
    if (i >= t->count || (!extra && t->stride > 4))
        return 0;
    if (t->stride > 4)
        memcpy(t->storage.buffers[1].data + i * t->stride + 4, extra, t->stride - 4);
    return 1;
}
int three_mm_rollcall_table_add(ThreeMMRollCallTable *t, const void *text, ThreeMMU32 count, const void *extra)
{
    ThreeMMStringTable v = view(t);
    int ok = three_mm_strings_add(&v, text, count, extra, NULL);
    apply(t, &v);
    return ok;
}
int three_mm_rollcall_table_rename(ThreeMMRollCallTable *t, ThreeMMU32 i, const void *text, ThreeMMU32 count)
{
    ThreeMMStringTable v = view(t);
    int ok = three_mm_strings_put(&v, i, text, count);
    apply(t, &v);
    return ok;
}
int three_mm_rollcall_table_delete(ThreeMMRollCallTable *t, ThreeMMU32 i)
{
    ThreeMMStringTable v = view(t);
    int ok = three_mm_strings_delete(&v, i);
    apply(t, &v);
    return ok;
}
static ThreeMMCollectionHeader header(const ThreeMMRollCallTable *t)
{
    ThreeMMCollectionHeader h;
    memset(&h, 0, sizeof(h));
    h.kind = THREE_MM_COLLECTION_STRINGS;
    h.width = t->stride;
    h.count = t->count;
    h.bytes = t->bytes;
    h.free_count = -1;
    h.system = t->encoding;
    return h;
}
int three_mm_rollcall_table_read(ThreeMMRollCallTable *t, const ThreeMMReadStream *stream, ThreeMMStringConvert convert,
                                 void *context, int *big)
{
    ThreeMMCollectionHeader h;
    ThreeMMStringTable v;
    int result, ok;
    const unsigned short one = 1;
    if (t->count || t->bytes)
        return THREE_MM_STORAGE_ARGUMENT;
    result = three_mm_collection_header_read(stream, THREE_MM_COLLECTION_STRINGS, 0, &h);
    if (result != THREE_MM_STORAGE_OK)
        return result;
    result = three_mm_storage_read(&t->storage, stream, h.header_bytes, h.first_bytes, h.second_bytes);
    if (result != THREE_MM_STORAGE_OK)
        return result;
    t->stride = h.width;
    t->count = h.count;
    t->bytes = h.bytes;
    v = view(t);
    if ((h.big_endian != 0) != (*(const unsigned char *)&one == 0))
        three_mm_strings_swap_offsets(&v);
    ok = h.system == t->encoding || three_mm_strings_translate(&v, h.system, t->encoding, convert, context);
    apply(t, &v);
    if (!ok)
        return THREE_MM_STORAGE_ARGUMENT;
    /* Different-width translation formerly compacted the host's two buffers. */
    if ((h.system == THREE_MM_TEXT_MAC || h.system == THREE_MM_TEXT_WINDOWS ? 1U : 2U) != t->unit)
        three_mm_storage_ensure(&t->storage, t->bytes, t->count * t->stride, 1);
    if (big)
        *big = h.big_endian;
    return THREE_MM_STORAGE_OK;
}
ThreeMMU32 three_mm_rollcall_table_wire_size(const ThreeMMRollCallTable *t)
{
    ThreeMMCollectionHeader h = header(t);
    if (three_mm_collection_layout(&h) != THREE_MM_STORAGE_OK)
        return 0;
    return h.header_bytes + h.first_bytes + h.second_bytes;
}
int three_mm_rollcall_table_write(ThreeMMRollCallTable *t, const ThreeMMStorageWriter *writer, int big)
{
    ThreeMMCollectionHeader h = header(t);
    ThreeMMStringTable v = view(t);
    unsigned char bytes[20];
    const unsigned short one = 1;
    int swap = (big != 0) != (*(const unsigned char *)&one == 0), result;
    h.big_endian = big != 0;
    result = three_mm_collection_header_write(&h, bytes, sizeof(bytes));
    if (result != THREE_MM_STORAGE_OK)
        return result;
    if (swap)
        three_mm_strings_swap_offsets(&v);
    result = three_mm_storage_write(&t->storage, writer, bytes, 20, t->bytes, t->count * t->stride);
    if (swap)
        three_mm_strings_swap_offsets(&v);
    return result;
}

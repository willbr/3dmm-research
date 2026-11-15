/* Roll-call storage converted from engine movie_data.cpp and Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/rollcall_storage.h"
#include "mm/text.h"
#include <string.h>
static int reserve(void *context, MMStringTable *view, MMU32 first, MMU32 second)
{
    MMRollCallTable *t = (MMRollCallTable *)context;
    int result = mm_storage_ensure(&t->storage, first, second, 0);
    view->strings = t->storage.buffers[0].data;
    view->entries = t->storage.buffers[1].data;
    view->string_capacity = t->storage.buffers[0].capacity;
    view->entry_capacity = t->storage.buffers[1].capacity;
    return result == MM_STORAGE_OK;
}
static MMStringTable view(MMRollCallTable *t)
{
    MMStringTable v;
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
static void apply(MMRollCallTable *t, const MMStringTable *v)
{
    t->count = v->count;
    t->bytes = v->bytes;
}
int mm_rollcall_table_init(MMRollCallTable *t, MMU32 extra, unsigned unit, int encoding, int trash,
                                 const MMStorageAllocator *a)
{
    if (!t || (unit != 1 && unit != 2) || extra % 4 || extra > MM_STORAGE_LIMIT - 8)
        return MM_STORAGE_ARGUMENT;
    memset(t, 0, sizeof(*t));
    if (mm_storage_init(&t->storage, a) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    t->stride = extra + 4;
    t->unit = unit;
    t->encoding = encoding;
    t->trash_unused = trash;
    t->storage.min_grow[0] = 120;
    t->storage.min_grow[1] = t->stride <= MM_STORAGE_LIMIT / 12 ? t->stride * 12 : MM_STORAGE_LIMIT - 1;
    return MM_STORAGE_OK;
}
int mm_rollcall_table_dispose(MMRollCallTable *t)
{
    int result = mm_storage_dispose(&t->storage);
    if (result == MM_STORAGE_OK)
        t->count = t->bytes = 0;
    return result;
}
int mm_rollcall_table_name(MMRollCallTable *t, MMU32 i, const void **text, MMU32 *count)
{
    MMStringTable v = view(t);
    return mm_strings_get(&v, i, text, count);
}
int mm_rollcall_table_extra(MMRollCallTable *t, MMU32 i, void *out)
{
    if (i >= t->count || (!out && t->stride > 4))
        return 0;
    if (t->stride > 4)
        memcpy(out, t->storage.buffers[1].data + i * t->stride + 4, t->stride - 4);
    return 1;
}
int mm_rollcall_table_set_extra(MMRollCallTable *t, MMU32 i, const void *extra)
{
    if (i >= t->count || (!extra && t->stride > 4))
        return 0;
    if (t->stride > 4)
        memcpy(t->storage.buffers[1].data + i * t->stride + 4, extra, t->stride - 4);
    return 1;
}
int mm_rollcall_table_add(MMRollCallTable *t, const void *text, MMU32 count, const void *extra)
{
    MMStringTable v = view(t);
    int ok = mm_strings_add(&v, text, count, extra, NULL);
    apply(t, &v);
    return ok;
}
int mm_rollcall_table_rename(MMRollCallTable *t, MMU32 i, const void *text, MMU32 count)
{
    MMStringTable v = view(t);
    int ok = mm_strings_put(&v, i, text, count);
    apply(t, &v);
    return ok;
}
int mm_rollcall_table_delete(MMRollCallTable *t, MMU32 i)
{
    MMStringTable v = view(t);
    int ok = mm_strings_delete(&v, i);
    apply(t, &v);
    return ok;
}
static MMCollectionHeader header(const MMRollCallTable *t)
{
    MMCollectionHeader h;
    memset(&h, 0, sizeof(h));
    h.kind = MM_COLLECTION_STRINGS;
    h.width = t->stride;
    h.count = t->count;
    h.bytes = t->bytes;
    h.free_count = -1;
    h.system = t->encoding;
    return h;
}
int mm_rollcall_table_read(MMRollCallTable *t, const MMReadStream *stream, MMStringConvert convert,
                                 void *context, int *big)
{
    MMCollectionHeader h;
    MMStringTable v;
    int result, ok;
    const unsigned short one = 1;
    if (t->count || t->bytes)
        return MM_STORAGE_ARGUMENT;
    result = mm_collection_header_read(stream, MM_COLLECTION_STRINGS, 0, &h);
    if (result != MM_STORAGE_OK)
        return result;
    result = mm_storage_read(&t->storage, stream, h.header_bytes, h.first_bytes, h.second_bytes);
    if (result != MM_STORAGE_OK)
        return result;
    t->stride = h.width;
    t->count = h.count;
    t->bytes = h.bytes;
    v = view(t);
    if ((h.big_endian != 0) != (*(const unsigned char *)&one == 0))
        mm_strings_swap_offsets(&v);
    ok = h.system == t->encoding || mm_strings_translate(&v, h.system, t->encoding, convert, context);
    apply(t, &v);
    if (!ok)
        return MM_STORAGE_ARGUMENT;
    /* Different-width translation formerly compacted the host's two buffers. */
    if ((h.system == MM_TEXT_MAC || h.system == MM_TEXT_WINDOWS ? 1U : 2U) != t->unit)
        mm_storage_ensure(&t->storage, t->bytes, t->count * t->stride, 1);
    if (big)
        *big = h.big_endian;
    return MM_STORAGE_OK;
}
MMU32 mm_rollcall_table_wire_size(const MMRollCallTable *t)
{
    MMCollectionHeader h = header(t);
    if (mm_collection_layout(&h) != MM_STORAGE_OK)
        return 0;
    return h.header_bytes + h.first_bytes + h.second_bytes;
}
int mm_rollcall_table_write(MMRollCallTable *t, const MMStorageWriter *writer, int big)
{
    MMCollectionHeader h = header(t);
    MMStringTable v = view(t);
    unsigned char bytes[20];
    const unsigned short one = 1;
    int swap = (big != 0) != (*(const unsigned char *)&one == 0), result;
    h.big_endian = big != 0;
    result = mm_collection_header_write(&h, bytes, sizeof(bytes));
    if (result != MM_STORAGE_OK)
        return result;
    if (swap)
        mm_strings_swap_offsets(&v);
    result = mm_storage_write(&t->storage, writer, bytes, 20, t->bytes, t->count * t->stride);
    if (swap)
        mm_strings_swap_offsets(&v);
    return result;
}

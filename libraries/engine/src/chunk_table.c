/* Chunk index storage converted from Kauai chunk.cpp and groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_table.h"
#include "three_mm/wide.h"
#include <string.h>
ThreeMMU32 three_mm_chunk_record_flags(const void *p, int big, ThreeMMU32 mask)
{
    return big ? ((const ThreeMMChunkRecordBig *)p)->grfcrp & mask
               : ((const ThreeMMChunkRecordSmall *)p)->luGrfcrpCb & mask & 255U;
}
ThreeMMI32 three_mm_chunk_record_bytes(const void *p, int big)
{
    return big ? ((const ThreeMMChunkRecordBig *)p)->cb
               : (ThreeMMI32)(((const ThreeMMChunkRecordSmall *)p)->luGrfcrpCb >> 8);
}
ThreeMMI32 three_mm_chunk_record_name_bytes(const void *p, int big, ThreeMMI32 variable)
{
    ThreeMMU32 children =
        big ? (ThreeMMU32)((const ThreeMMChunkRecordBig *)p)->ckid : ((const ThreeMMChunkRecordSmall *)p)->ckid;
    return three_mm_signed_word((ThreeMMU32)variable - children * 12U);
}
void three_mm_chunk_record_set_bytes(void *p, int big, ThreeMMI32 bytes)
{
    if (big)
        ((ThreeMMChunkRecordBig *)p)->cb = bytes;
    else
    {
        ThreeMMChunkRecordSmall *s = (ThreeMMChunkRecordSmall *)p;
        s->luGrfcrpCb = ((ThreeMMU32)bytes << 8) | (s->luGrfcrpCb & 255U);
    }
}
void three_mm_chunk_record_set_flags(void *p, int big, ThreeMMU32 flags, ThreeMMU32 mask)
{
    ThreeMMU32 *word;
    if (big)
        word = &((ThreeMMChunkRecordBig *)p)->grfcrp;
    else
    {
        word = &((ThreeMMChunkRecordSmall *)p)->luGrfcrpCb;
        mask &= 255U;
    }
    *word = (*word & ~mask) | (flags & mask);
}
void three_mm_chunk_record_get(const void *p, int big, ThreeMMU32 variable, ThreeMMChunkEntry *e)
{
    const ThreeMMChunkRecordBig *b = (const ThreeMMChunkRecordBig *)p;
    const ThreeMMChunkRecordSmall *s = (const ThreeMMChunkRecordSmall *)p;
    e->type = big ? b->cki.ctg : s->cki.ctg;
    e->number = big ? b->cki.cno : s->cki.cno;
    e->offset = big ? (ThreeMMU32)b->fp : (ThreeMMU32)s->fp;
    e->bytes = (ThreeMMU32)three_mm_chunk_record_bytes(p, big);
    e->flags = three_mm_chunk_record_flags(p, big, 0xffffffffUL);
    e->child_count = big ? (ThreeMMU32)b->ckid : s->ckid;
    e->parent_count = big ? (ThreeMMU32)b->ccrpRef : s->ccrpRef;
    e->variable_bytes = variable;
}
void three_mm_chunk_record_set(void *p, int big, const ThreeMMChunkEntry *e)
{
    if (big)
    {
        ThreeMMChunkRecordBig *b = (ThreeMMChunkRecordBig *)p;
        b->cki.ctg = e->type;
        b->cki.cno = e->number;
        b->fp = three_mm_signed_word(e->offset);
        b->ckid = three_mm_signed_word(e->child_count);
        b->ccrpRef = three_mm_signed_word(e->parent_count);
    }
    else
    {
        ThreeMMChunkRecordSmall *s = (ThreeMMChunkRecordSmall *)p;
        s->cki.ctg = e->type;
        s->cki.cno = e->number;
        s->fp = three_mm_signed_word(e->offset);
        s->ckid = (unsigned short)e->child_count;
        s->ccrpRef = (unsigned short)e->parent_count;
    }
    three_mm_chunk_record_set_bytes(p, big, three_mm_signed_word(e->bytes));
    three_mm_chunk_record_set_flags(p, big, e->flags, 0xffffffffUL);
}
static ThreeMMGroup view(ThreeMMChunkTable *t)
{
    ThreeMMGroup g;
    g.storage = &t->storage;
    g.fixed_bytes = t->fixed_bytes;
    g.count = t->count;
    g.bytes = t->bytes;
    g.free_count = -1;
    g.trash_unused = t->trash_unused;
    return g;
}
static int apply(ThreeMMChunkTable *t, const ThreeMMGroup *g, int result)
{
    t->count = g->count;
    t->bytes = g->bytes;
    return result;
}
int three_mm_chunk_table_create(const ThreeMMStorageAllocator *allocator, ThreeMMU32 width, ThreeMMU32 reserve,
                                int trash, ThreeMMChunkTable **out)
{
    ThreeMMCollectionStorage initial;
    ThreeMMChunkTable *t;
    ThreeMMGroup g;
    int result;
    if (!out || (width != 20 && width != 32) || three_mm_storage_init(&initial, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_STORAGE_ARGUMENT;
    t = (ThreeMMChunkTable *)initial.allocator.resize(initial.allocator.context, NULL, 0, sizeof(*t),
                                                      THREE_MM_STORAGE_NORMAL);
    if (!t)
        return THREE_MM_STORAGE_MEMORY;
    memset(t, 0, sizeof(*t));
    t->storage = initial;
    t->fixed_bytes = width;
    t->trash_unused = trash;
    t->storage.min_grow[0] = width * 16;
    t->storage.min_grow[1] = 128;
    g = view(t);
    result = three_mm_group_reserve(&g, reserve, 0, 0);
    if (result != THREE_MM_STORAGE_OK)
    {
        three_mm_chunk_table_close(t);
        return result;
    }
    *out = t;
    return THREE_MM_STORAGE_OK;
}
int three_mm_chunk_table_close(ThreeMMChunkTable *t)
{
    ThreeMMStorageAllocator a;
    int result;
    if (!t)
        return THREE_MM_STORAGE_OK;
    a = t->storage.allocator;
    result = three_mm_storage_dispose(&t->storage);
    if (result == THREE_MM_STORAGE_OK)
        a.release(a.context, t, sizeof(*t));
    return result;
}
void *three_mm_chunk_table_fixed(ThreeMMChunkTable *t, ThreeMMU32 i, ThreeMMU32 *bytes)
{
    ThreeMMGroup g = view(t);
    ThreeMMGroupLocation loc;
    if (three_mm_group_location(&g, i, &loc) != THREE_MM_STORAGE_OK)
        return NULL;
    if (bytes)
        *bytes = (ThreeMMU32)loc.bytes - t->fixed_bytes;
    return t->storage.buffers[0].data + loc.offset;
}
void *three_mm_chunk_table_variable(ThreeMMChunkTable *t, ThreeMMU32 i, ThreeMMU32 *bytes)
{
    unsigned char *p = (unsigned char *)three_mm_chunk_table_fixed(t, i, bytes);
    return p ? p + t->fixed_bytes : NULL;
}
ThreeMMU32 three_mm_chunk_table_variable_bytes(ThreeMMChunkTable *t, ThreeMMU32 i)
{
    ThreeMMU32 bytes = 0;
    three_mm_chunk_table_fixed(t, i, &bytes);
    return bytes;
}
static void *range(ThreeMMChunkTable *t, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    ThreeMMU32 count;
    unsigned char *p = (unsigned char *)three_mm_chunk_table_variable(t, i, &count);
    return !p || offset > count || bytes > count - offset ? NULL : p + offset;
}
int three_mm_chunk_table_read(ThreeMMChunkTable *t, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes, void *out)
{
    void *p = range(t, i, offset, bytes);
    if (!p || (!out && bytes))
        return THREE_MM_STORAGE_ARGUMENT;
    if (bytes)
        memcpy(out, p, bytes);
    return THREE_MM_STORAGE_OK;
}
int three_mm_chunk_table_put(ThreeMMChunkTable *t, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes, const void *data)
{
    void *p = range(t, i, offset, bytes);
    if (!p || (!data && bytes))
        return THREE_MM_STORAGE_ARGUMENT;
    if (bytes)
        memcpy(p, data, bytes);
    return THREE_MM_STORAGE_OK;
}
int three_mm_chunk_table_insert(ThreeMMChunkTable *t, ThreeMMU32 i, ThreeMMU32 bytes, const void *data,
                                const void *fixed)
{
    ThreeMMGroup g = view(t);
    int result = three_mm_group_insert(&g, i, bytes, data, fixed);
    return apply(t, &g, result);
}
int three_mm_chunk_table_delete(ThreeMMChunkTable *t, ThreeMMU32 i)
{
    ThreeMMGroup g = view(t);
    int result = three_mm_group_delete(&g, i);
    return apply(t, &g, result);
}
int three_mm_chunk_table_move(ThreeMMChunkTable *t, ThreeMMU32 from, ThreeMMU32 to)
{
    ThreeMMGroup g = view(t);
    int result = three_mm_group_move(&g, from, to);
    return apply(t, &g, result);
}
int three_mm_chunk_table_insert_bytes(ThreeMMChunkTable *t, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes,
                                      const void *data)
{
    ThreeMMGroup g = view(t);
    int result = three_mm_group_insert_bytes(&g, i, offset, bytes, data);
    return apply(t, &g, result);
}
int three_mm_chunk_table_delete_bytes(ThreeMMChunkTable *t, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    ThreeMMGroup g = view(t);
    int result = three_mm_group_delete_bytes(&g, i, offset, bytes);
    return apply(t, &g, result);
}
int three_mm_chunk_table_move_bytes(ThreeMMChunkTable *t, ThreeMMU32 from, ThreeMMU32 offset, ThreeMMU32 to,
                                    ThreeMMU32 target_offset, ThreeMMU32 bytes)
{
    ThreeMMGroup g = view(t);
    int result = three_mm_group_move_bytes(&g, from, offset, to, target_offset, bytes);
    return apply(t, &g, result);
}
int three_mm_chunk_table_lock(ThreeMMChunkTable *t)
{
    return three_mm_storage_lock(&t->storage, 0);
}
int three_mm_chunk_table_unlock(ThreeMMChunkTable *t)
{
    return three_mm_storage_unlock(&t->storage, 0);
}
int three_mm_chunk_table_entry(ThreeMMChunkTable *t, ThreeMMU32 i, ThreeMMChunkEntry *entry)
{
    ThreeMMU32 variable;
    void *p = three_mm_chunk_table_fixed(t, i, &variable);
    if (!p || !entry)
        return 0;
    three_mm_chunk_record_get(p, t->fixed_bytes == 32, variable, entry);
    return 1;
}
int three_mm_chunk_table_child(ThreeMMChunkTable *t, ThreeMMU32 parent, ThreeMMU32 i, ThreeMMChunkChild *child)
{
    ThreeMMChunkEntry entry;
    if (!child || !three_mm_chunk_table_entry(t, parent, &entry) || i >= entry.child_count ||
        i >= entry.variable_bytes / 12)
        return 0;
    return three_mm_chunk_table_read(t, parent, i * 12, 12, child) == THREE_MM_STORAGE_OK;
}
static int access_entry(void *t, ThreeMMU32 i, ThreeMMChunkEntry *entry)
{
    return three_mm_chunk_table_entry((ThreeMMChunkTable *)t, i, entry);
}
static int access_child(void *t, ThreeMMU32 parent, ThreeMMU32 i, ThreeMMChunkChild *child)
{
    return three_mm_chunk_table_child((ThreeMMChunkTable *)t, parent, i, child);
}
ThreeMMChunkAccess three_mm_chunk_table_access(ThreeMMChunkTable *t)
{
    ThreeMMChunkAccess access;
    access.context = t;
    access.count = t->count;
    access.entry = access_entry;
    access.child = access_child;
    return access;
}
int three_mm_chunk_table_clear_flags(ThreeMMChunkTable *t, ThreeMMU32 mask)
{
    ThreeMMU32 i;
    void *p;
    for (i = 0; i < t->count; ++i)
    {
        p = three_mm_chunk_table_fixed(t, i, NULL);
        if (!p)
            return THREE_MM_STORAGE_ARGUMENT;
        three_mm_chunk_record_set_flags(p, t->fixed_bytes == 32, 0, mask);
    }
    return THREE_MM_STORAGE_OK;
}
static ThreeMMCollectionHeader header(const ThreeMMChunkTable *t)
{
    ThreeMMCollectionHeader h;
    memset(&h, 0, sizeof(h));
    h.kind = THREE_MM_COLLECTION_GROUP;
    h.width = t->fixed_bytes;
    h.count = t->count;
    h.bytes = t->bytes;
    h.free_count = -1;
    return h;
}
ThreeMMU32 three_mm_chunk_table_wire_size(const ThreeMMChunkTable *t)
{
    ThreeMMCollectionHeader h = header(t);
    if (three_mm_collection_layout(&h) != THREE_MM_STORAGE_OK)
        return 0;
    return h.header_bytes + h.first_bytes + h.second_bytes;
}
int three_mm_chunk_table_write(ThreeMMChunkTable *t, const ThreeMMStorageWriter *writer, int big, int system)
{
    ThreeMMCollectionHeader h = header(t);
    ThreeMMGroup g = view(t);
    unsigned char wire[20];
    const unsigned short one = 1;
    int swap = (big != 0) != (*(const unsigned char *)&one == 0), result;
    h.big_endian = big != 0;
    h.system = system;
    result = three_mm_collection_layout(&h);
    if (result != THREE_MM_STORAGE_OK)
        return result;
    result = three_mm_collection_header_write(&h, wire, sizeof(wire));
    if (result != THREE_MM_STORAGE_OK)
        return result;
    if (swap)
        three_mm_group_swap_locations(&g);
    result = three_mm_storage_write(&t->storage, writer, wire, h.header_bytes, h.first_bytes, h.second_bytes);
    if (swap)
        three_mm_group_swap_locations(&g);
    return result;
}

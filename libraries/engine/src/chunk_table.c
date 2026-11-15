/* Chunk index storage converted from Kauai chunk.cpp and groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_table.h"
#include "mm/wide.h"
#include <string.h>
MMU32 mm_chunk_record_flags(const void *p, int big, MMU32 mask)
{
    return big ? ((const MMChunkRecordBig *)p)->grfcrp & mask
               : ((const MMChunkRecordSmall *)p)->luGrfcrpCb & mask & 255U;
}
MMI32 mm_chunk_record_bytes(const void *p, int big)
{
    return big ? ((const MMChunkRecordBig *)p)->cb
               : (MMI32)(((const MMChunkRecordSmall *)p)->luGrfcrpCb >> 8);
}
MMI32 mm_chunk_record_name_bytes(const void *p, int big, MMI32 variable)
{
    MMU32 children =
        big ? (MMU32)((const MMChunkRecordBig *)p)->ckid : ((const MMChunkRecordSmall *)p)->ckid;
    return mm_signed_word((MMU32)variable - children * 12U);
}
void mm_chunk_record_set_bytes(void *p, int big, MMI32 bytes)
{
    if (big)
        ((MMChunkRecordBig *)p)->cb = bytes;
    else
    {
        MMChunkRecordSmall *s = (MMChunkRecordSmall *)p;
        s->luGrfcrpCb = ((MMU32)bytes << 8) | (s->luGrfcrpCb & 255U);
    }
}
void mm_chunk_record_set_flags(void *p, int big, MMU32 flags, MMU32 mask)
{
    MMU32 *word;
    if (big)
        word = &((MMChunkRecordBig *)p)->grfcrp;
    else
    {
        word = &((MMChunkRecordSmall *)p)->luGrfcrpCb;
        mask &= 255U;
    }
    *word = (*word & ~mask) | (flags & mask);
}
void mm_chunk_record_get(const void *p, int big, MMU32 variable, MMChunkEntry *e)
{
    const MMChunkRecordBig *b = (const MMChunkRecordBig *)p;
    const MMChunkRecordSmall *s = (const MMChunkRecordSmall *)p;
    e->type = big ? b->cki.ctg : s->cki.ctg;
    e->number = big ? b->cki.cno : s->cki.cno;
    e->offset = big ? (MMU32)b->fp : (MMU32)s->fp;
    e->bytes = (MMU32)mm_chunk_record_bytes(p, big);
    e->flags = mm_chunk_record_flags(p, big, 0xffffffffUL);
    e->child_count = big ? (MMU32)b->ckid : s->ckid;
    e->parent_count = big ? (MMU32)b->ccrpRef : s->ccrpRef;
    e->variable_bytes = variable;
}
void mm_chunk_record_set(void *p, int big, const MMChunkEntry *e)
{
    if (big)
    {
        MMChunkRecordBig *b = (MMChunkRecordBig *)p;
        b->cki.ctg = e->type;
        b->cki.cno = e->number;
        b->fp = mm_signed_word(e->offset);
        b->ckid = mm_signed_word(e->child_count);
        b->ccrpRef = mm_signed_word(e->parent_count);
    }
    else
    {
        MMChunkRecordSmall *s = (MMChunkRecordSmall *)p;
        s->cki.ctg = e->type;
        s->cki.cno = e->number;
        s->fp = mm_signed_word(e->offset);
        s->ckid = (unsigned short)e->child_count;
        s->ccrpRef = (unsigned short)e->parent_count;
    }
    mm_chunk_record_set_bytes(p, big, mm_signed_word(e->bytes));
    mm_chunk_record_set_flags(p, big, e->flags, 0xffffffffUL);
}
static MMGroup view(MMChunkTable *t)
{
    MMGroup g;
    g.storage = &t->storage;
    g.fixed_bytes = t->fixed_bytes;
    g.count = t->count;
    g.bytes = t->bytes;
    g.free_count = -1;
    g.trash_unused = t->trash_unused;
    return g;
}
static int apply(MMChunkTable *t, const MMGroup *g, int result)
{
    t->count = g->count;
    t->bytes = g->bytes;
    return result;
}
int mm_chunk_table_create(const MMStorageAllocator *allocator, MMU32 width, MMU32 reserve,
                                int trash, MMChunkTable **out)
{
    MMCollectionStorage initial;
    MMChunkTable *t;
    MMGroup g;
    int result;
    if (!out || (width != 20 && width != 32) || mm_storage_init(&initial, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    t = (MMChunkTable *)initial.allocator.resize(initial.allocator.context, NULL, 0, sizeof(*t),
                                                      MM_STORAGE_NORMAL);
    if (!t)
        return MM_STORAGE_MEMORY;
    memset(t, 0, sizeof(*t));
    t->storage = initial;
    t->fixed_bytes = width;
    t->trash_unused = trash;
    t->storage.min_grow[0] = width * 16;
    t->storage.min_grow[1] = 128;
    g = view(t);
    result = mm_group_reserve(&g, reserve, 0, 0);
    if (result != MM_STORAGE_OK)
    {
        mm_chunk_table_close(t);
        return result;
    }
    *out = t;
    return MM_STORAGE_OK;
}
int mm_chunk_table_close(MMChunkTable *t)
{
    MMStorageAllocator a;
    int result;
    if (!t)
        return MM_STORAGE_OK;
    a = t->storage.allocator;
    result = mm_storage_dispose(&t->storage);
    if (result == MM_STORAGE_OK)
        a.release(a.context, t, sizeof(*t));
    return result;
}
void *mm_chunk_table_fixed(MMChunkTable *t, MMU32 i, MMU32 *bytes)
{
    MMGroup g = view(t);
    MMGroupLocation loc;
    if (mm_group_location(&g, i, &loc) != MM_STORAGE_OK)
        return NULL;
    if (bytes)
        *bytes = (MMU32)loc.bytes - t->fixed_bytes;
    return t->storage.buffers[0].data + loc.offset;
}
void *mm_chunk_table_variable(MMChunkTable *t, MMU32 i, MMU32 *bytes)
{
    unsigned char *p = (unsigned char *)mm_chunk_table_fixed(t, i, bytes);
    return p ? p + t->fixed_bytes : NULL;
}
MMU32 mm_chunk_table_variable_bytes(MMChunkTable *t, MMU32 i)
{
    MMU32 bytes = 0;
    mm_chunk_table_fixed(t, i, &bytes);
    return bytes;
}
static void *range(MMChunkTable *t, MMU32 i, MMU32 offset, MMU32 bytes)
{
    MMU32 count;
    unsigned char *p = (unsigned char *)mm_chunk_table_variable(t, i, &count);
    return !p || offset > count || bytes > count - offset ? NULL : p + offset;
}
int mm_chunk_table_read(MMChunkTable *t, MMU32 i, MMU32 offset, MMU32 bytes, void *out)
{
    void *p = range(t, i, offset, bytes);
    if (!p || (!out && bytes))
        return MM_STORAGE_ARGUMENT;
    if (bytes)
        memcpy(out, p, bytes);
    return MM_STORAGE_OK;
}
int mm_chunk_table_put(MMChunkTable *t, MMU32 i, MMU32 offset, MMU32 bytes, const void *data)
{
    void *p = range(t, i, offset, bytes);
    if (!p || (!data && bytes))
        return MM_STORAGE_ARGUMENT;
    if (bytes)
        memcpy(p, data, bytes);
    return MM_STORAGE_OK;
}
int mm_chunk_table_insert(MMChunkTable *t, MMU32 i, MMU32 bytes, const void *data,
                                const void *fixed)
{
    MMGroup g = view(t);
    int result = mm_group_insert(&g, i, bytes, data, fixed);
    return apply(t, &g, result);
}
int mm_chunk_table_delete(MMChunkTable *t, MMU32 i)
{
    MMGroup g = view(t);
    int result = mm_group_delete(&g, i);
    return apply(t, &g, result);
}
int mm_chunk_table_move(MMChunkTable *t, MMU32 from, MMU32 to)
{
    MMGroup g = view(t);
    int result = mm_group_move(&g, from, to);
    return apply(t, &g, result);
}
int mm_chunk_table_insert_bytes(MMChunkTable *t, MMU32 i, MMU32 offset, MMU32 bytes,
                                      const void *data)
{
    MMGroup g = view(t);
    int result = mm_group_insert_bytes(&g, i, offset, bytes, data);
    return apply(t, &g, result);
}
int mm_chunk_table_delete_bytes(MMChunkTable *t, MMU32 i, MMU32 offset, MMU32 bytes)
{
    MMGroup g = view(t);
    int result = mm_group_delete_bytes(&g, i, offset, bytes);
    return apply(t, &g, result);
}
int mm_chunk_table_move_bytes(MMChunkTable *t, MMU32 from, MMU32 offset, MMU32 to,
                                    MMU32 target_offset, MMU32 bytes)
{
    MMGroup g = view(t);
    int result = mm_group_move_bytes(&g, from, offset, to, target_offset, bytes);
    return apply(t, &g, result);
}
int mm_chunk_table_lock(MMChunkTable *t)
{
    return mm_storage_lock(&t->storage, 0);
}
int mm_chunk_table_unlock(MMChunkTable *t)
{
    return mm_storage_unlock(&t->storage, 0);
}
int mm_chunk_table_entry(MMChunkTable *t, MMU32 i, MMChunkEntry *entry)
{
    MMU32 variable;
    void *p = mm_chunk_table_fixed(t, i, &variable);
    if (!p || !entry)
        return 0;
    mm_chunk_record_get(p, t->fixed_bytes == 32, variable, entry);
    return 1;
}
int mm_chunk_table_child(MMChunkTable *t, MMU32 parent, MMU32 i, MMChunkChild *child)
{
    MMChunkEntry entry;
    if (!child || !mm_chunk_table_entry(t, parent, &entry) || i >= entry.child_count ||
        i >= entry.variable_bytes / 12)
        return 0;
    return mm_chunk_table_read(t, parent, i * 12, 12, child) == MM_STORAGE_OK;
}
static int access_entry(void *t, MMU32 i, MMChunkEntry *entry)
{
    return mm_chunk_table_entry((MMChunkTable *)t, i, entry);
}
static int access_child(void *t, MMU32 parent, MMU32 i, MMChunkChild *child)
{
    return mm_chunk_table_child((MMChunkTable *)t, parent, i, child);
}
MMChunkAccess mm_chunk_table_access(MMChunkTable *t)
{
    MMChunkAccess access;
    access.context = t;
    access.count = t->count;
    access.entry = access_entry;
    access.child = access_child;
    return access;
}
int mm_chunk_table_clear_flags(MMChunkTable *t, MMU32 mask)
{
    MMU32 i;
    void *p;
    for (i = 0; i < t->count; ++i)
    {
        p = mm_chunk_table_fixed(t, i, NULL);
        if (!p)
            return MM_STORAGE_ARGUMENT;
        mm_chunk_record_set_flags(p, t->fixed_bytes == 32, 0, mask);
    }
    return MM_STORAGE_OK;
}
static MMCollectionHeader header(const MMChunkTable *t)
{
    MMCollectionHeader h;
    memset(&h, 0, sizeof(h));
    h.kind = MM_COLLECTION_GROUP;
    h.width = t->fixed_bytes;
    h.count = t->count;
    h.bytes = t->bytes;
    h.free_count = -1;
    return h;
}
MMU32 mm_chunk_table_wire_size(const MMChunkTable *t)
{
    MMCollectionHeader h = header(t);
    if (mm_collection_layout(&h) != MM_STORAGE_OK)
        return 0;
    return h.header_bytes + h.first_bytes + h.second_bytes;
}
int mm_chunk_table_write(MMChunkTable *t, const MMStorageWriter *writer, int big, int system)
{
    MMCollectionHeader h = header(t);
    MMGroup g = view(t);
    unsigned char wire[20];
    const unsigned short one = 1;
    int swap = (big != 0) != (*(const unsigned char *)&one == 0), result;
    h.big_endian = big != 0;
    h.system = system;
    result = mm_collection_layout(&h);
    if (result != MM_STORAGE_OK)
        return result;
    result = mm_collection_header_write(&h, wire, sizeof(wire));
    if (result != MM_STORAGE_OK)
        return result;
    if (swap)
        mm_group_swap_locations(&g);
    result = mm_storage_write(&t->storage, writer, wire, h.header_bytes, h.first_bytes, h.second_bytes);
    if (swap)
        mm_group_swap_locations(&g);
    return result;
}

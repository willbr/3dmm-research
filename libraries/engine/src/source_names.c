/* Source-name policy converted from engine tagman.cpp and Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/source_names.h"
#include "mm/movie_session.h"
#include "mm/wide.h"
#include <string.h>
static int reserve(void *context, MMStringTable *view, MMU32 first, MMU32 second)
{
    MMSourceNames *n = (MMSourceNames *)context;
    int result = mm_storage_ensure(&n->storage, first, second, 0);
    view->strings = n->storage.buffers[0].data;
    view->entries = n->storage.buffers[1].data;
    view->string_capacity = n->storage.buffers[0].capacity;
    view->entry_capacity = n->storage.buffers[1].capacity;
    return result == MM_STORAGE_OK;
}
static MMStringTable view(MMSourceNames *n)
{
    MMStringTable t;
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
int mm_source_names_init(MMSourceNames *n, unsigned unit, int encoding, int trash,
                               const MMStorageAllocator *allocator)
{
    if (!n || (unit != 1 && unit != 2))
        return MM_STORAGE_ARGUMENT;
    memset(n, 0, sizeof(*n));
    if (mm_storage_init(&n->storage, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    n->unit = unit;
    n->encoding = encoding;
    n->trash_unused = trash;
    n->storage.min_grow[0] = 120;
    n->storage.min_grow[1] = 96;
    return MM_STORAGE_OK;
}
int mm_source_names_dispose(MMSourceNames *n)
{
    int result = mm_storage_dispose(&n->storage);
    if (result == MM_STORAGE_OK)
        n->count = n->bytes = 0;
    return result;
}
int mm_source_names_at(MMSourceNames *n, MMU32 i, const void **text, MMU32 *count, MMI32 *sid)
{
    MMStringTable t = view(n);
    if (!sid || !mm_strings_get(&t, i, text, count))
        return 0;
    memcpy(sid, t.entries + i * 8 + 4, 4);
    return 1;
}
int mm_source_names_find_id(MMSourceNames *n, MMI32 sid, MMU32 *position)
{
    MMU32 i;
    MMI32 value;
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
int mm_source_names_add(MMSourceNames *n, MMI32 sid, const void *text, MMU32 count)
{
    MMStringTable t;
    int ok;
    if (mm_source_names_find_id(n, sid, NULL))
        return 1;
    t = view(n);
    ok = mm_strings_add(&t, text, count, &sid, NULL);
    n->count = t.count;
    n->bytes = t.bytes;
    return ok;
}
int mm_source_names_merge(MMSourceNames *n, const MMSourceNameInput *input, int swap)
{
    MMU32 i, count, value;
    MMI32 sid;
    const void *text;
    for (i = 0; i < input->count; ++i)
    {
        input->get(input->context, i, &text, &count, &sid);
        if (swap)
        {
            value = (MMU32)sid;
            value = (value >> 24) | ((value >> 8) & 0xff00U) | ((value << 8) & 0xff0000UL) | (value << 24);
            sid = mm_signed_word(value);
        }
        if (sid < 0 && input->invalid_sid)
            input->invalid_sid(input->context, sid);
        if (!mm_source_names_add(n, sid, text, count))
            return 0;
    }
    return 1;
}

typedef struct MovieSourceMerge
{
    MMSourceNames *names;
    MMStringConvert convert;
    void *context;
    void (*invalid_sid)(void *, MMI32);
} MovieSourceMerge;

static int merge_movie_payload(void *p, const MMReadStream *stream)
{
    const MovieSourceMerge *merge = p;
    MMSourceNames *names = merge->names;
    MMMovieSourceTable table;
    MMMovieSourceName row;
    unsigned short text[255];
    MMU32 i, count;
    int status = mm_movie_source_table_open(stream, stream->length, &table);
    for (i = 0; status == MM_MOVIE_OK && i < table.count; ++i)
    {
        status = mm_movie_source_table_at(&table, i, &row);
        if (status != MM_MOVIE_OK)
            break;
        if (row.id < 0 && merge->invalid_sid)
            merge->invalid_sid(merge->context, row.id);
        count = mm_movie_source_name_text(&row, names->encoding, names->unit, merge->convert, merge->context,
                                           text, sizeof(text) / sizeof(*text));
        if (!mm_source_names_add(names, row.id, text, count))
            return MM_MOVIE_MEMORY;
    }
    return status;
}

int mm_source_names_merge_movie(MMSourceNames *names, const MMMovieSessionGraph *graph, MMU32 root,
                                MMStringConvert convert, void *context, void (*invalid_sid)(void *, MMI32))
{
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMU32 position;
    MovieSourceMerge merge;
    int status;
    graph->access(graph->context, &chunks);
    status = mm_movie_view_source_table(&chunks, root, &entry, &position);
    if (status == MM_MOVIE_ASSET_NOT_FOUND)
        return 1;
    if (status != MM_MOVIE_OK)
        return 0;
    merge.names = names;
    merge.convert = convert;
    merge.context = context;
    merge.invalid_sid = invalid_sid;
    return graph->payload(graph->context, &entry, merge_movie_payload, &merge) == MM_MOVIE_OK;
}
static unsigned character(const void *text, MMU32 i, unsigned unit)
{
    unsigned short value;
    if (unit == 1)
        return ((const unsigned char *)text)[i];
    memcpy(&value, (const unsigned char *)text + i * 2, 2);
    return value;
}
MMSourceNameParts mm_source_name_split(const void *text, MMU32 count, unsigned unit)
{
    MMSourceNameParts p;
    MMU32 i, end;
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
int mm_source_names_find_name(MMSourceNames *n, const void *text, MMU32 count,
                                    MMSourceNameEqual equal, void *context, MMI32 *sid)
{
    MMU32 i, length;
    MMI32 value;
    const void *name;
    MMSourceNameParts parts;
    for (i = 0; i < n->count; ++i)
    {
        if (!mm_source_names_at(n, i, &name, &length, &value))
            return 0;
        if (equal(context, name, length, text, count))
        {
            *sid = value;
            return 1;
        }
        parts = mm_source_name_split(name, length, n->unit);
        if (equal(context, name, parts.long_count, text, count) ||
            equal(context, (const unsigned char *)name + parts.short_offset * n->unit, parts.short_count, text, count))
        {
            *sid = value;
            return 1;
        }
    }
    return 0;
}
int mm_source_names_merged(MMSourceNames *n, MMI32 sid, const void **text, MMU32 *count)
{
    MMU32 i;
    MMI32 value;
    for (i = 0; i < n->count; ++i)
    {
        if (!mm_source_names_at(n, i, text, count, &value))
            return 0;
        if (value == sid)
            return 1;
    }
    return 0;
}
int mm_source_names_resolve(MMSourceNames *n, MMI32 sid, void *path, MMSourceNameProvider provider,
                                  void *context)
{
    const void *text;
    MMU32 count;
    unsigned short scratch[256];
    MMSourceNameParts p;
    if (!provider || !mm_source_names_merged(n, sid, &text, &count))
        return 0;
    memcpy(scratch, text, count * n->unit);
    text = scratch;
    p = mm_source_name_split(text, count, n->unit);
    return provider(context, text, p.long_count, (const unsigned char *)text + p.short_offset * n->unit, p.short_count,
                    path);
}
static MMCollectionHeader header(const MMSourceNames *n)
{
    MMCollectionHeader h;
    memset(&h, 0, sizeof(h));
    h.kind = MM_COLLECTION_STRINGS;
    h.width = 8;
    h.count = n->count;
    h.bytes = n->bytes;
    h.free_count = -1;
    h.system = n->encoding;
    return h;
}
MMU32 mm_source_names_wire_size(const MMSourceNames *n)
{
    MMCollectionHeader h = header(n);
    if (mm_collection_layout(&h) != MM_STORAGE_OK)
        return 0;
    return h.header_bytes + h.first_bytes + h.second_bytes;
}
int mm_source_names_write(MMSourceNames *n, const MMStorageWriter *writer, int big)
{
    MMCollectionHeader h = header(n);
    MMStringTable t = view(n);
    unsigned char bytes[20];
    const unsigned short one = 1;
    int swap = (big != 0) != (*(const unsigned char *)&one == 0), result;
    h.big_endian = big != 0;
    result = mm_collection_layout(&h);
    if (result != MM_STORAGE_OK)
        return result;
    result = mm_collection_header_write(&h, bytes, sizeof(bytes));
    if (result != MM_STORAGE_OK)
        return result;
    if (swap)
        mm_strings_swap_offsets(&t);
    result = mm_storage_write(&n->storage, writer, bytes, h.header_bytes, h.first_bytes, h.second_bytes);
    if (swap)
        mm_strings_swap_offsets(&t);
    return result;
}

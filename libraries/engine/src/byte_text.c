/* Byte-stream line formatting converted from Kauai textdoc.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/byte_text.h"
#include <string.h>

static MMI32 minimum(MMI32 a, MMI32 b)
{
    return a < b ? a : b;
}
static MMI32 maximum(MMI32 a, MMI32 b)
{
    return a > b ? a : b;
}
static int in(MMI32 at, MMI32 first, MMI32 end)
{
    return at >= first && at < end;
}
static void store_unit(void *output, MMI32 index, unsigned unit, unsigned char ch)
{
    unsigned char *p = (unsigned char *)output + index * unit;
    unsigned short wide = ch;
    if (unit == 1)
        *p = ch;
    else
        memcpy(p, &wide, 2);
}
static MMI32 *rows(const MMByteTextView *v)
{
    return (MMI32 *)v->storage.buffers[0].data;
}
int mm_byte_text_init(MMByteTextView *v, const MMStorageAllocator *allocator, int trash)
{
    MMI32 first = 0;
    memset(v, 0, sizeof(*v));
    if (mm_storage_init(&v->storage, allocator) != MM_STORAGE_OK)
        return 0;
    v->lines.storage = &v->storage;
    v->lines.width = sizeof(first);
    v->lines.free_count = -1;
    v->lines.trash_unused = trash;
    v->storage.min_grow[0] = 20 * sizeof(first);
    return mm_array_add(&v->lines, &first, NULL) == MM_STORAGE_OK;
}
int mm_byte_text_dispose(MMByteTextView *v)
{
    if (!v->lines.storage)
        return 1;
    if (mm_storage_dispose(&v->storage) != MM_STORAGE_OK)
        return 0;
    v->lines.storage = NULL;
    v->lines.count = 0;
    return 1;
}
int mm_byte_text_fetch(MMByteTextView *v, MMPieceTable *pieces, MMI32 at, unsigned char *out)
{
    MMI32 first, end, size = MM_BYTE_TEXT_CACHE_BYTES, length = pieces->length;
    if (!in(at, v->cache_first, v->cache_end))
    {
        if (!in(at, 0, length))
            return 0;
        first = maximum(0, minimum(at - size / 2, length - size));
        end = minimum(length, first + size);
        if (v->cache_first < v->cache_end && in(v->cache_first, at, end))
        {
            if (end > v->cache_end)
            {
                end = v->cache_end;
                first = maximum(0, end - size);
            }
            memmove(v->cache + (v->cache_first - first), v->cache, end - v->cache_first);
            mm_piece_table_read(pieces, first, v->cache, v->cache_first - first);
        }
        else if (v->cache_first < v->cache_end && in(v->cache_end, first, at + 1))
        {
            if (first < v->cache_first)
            {
                first = v->cache_first;
                end = minimum(length, first + size);
            }
            memmove(v->cache, v->cache + (first - v->cache_first), v->cache_end - first);
            mm_piece_table_read(pieces, v->cache_end, v->cache + (v->cache_end - first), end - v->cache_end);
        }
        else
            mm_piece_table_read(pieces, first, v->cache, end - first);
        v->cache_first = first;
        v->cache_end = end;
    }
    *out = v->cache[at - v->cache_first];
    return 1;
}
void mm_byte_text_invalidate(MMByteTextView *v, MMI32 at, MMI32 inserted, MMI32 deleted)
{
    MMI32 end = at + deleted;
    if (v->cache_end <= v->cache_first)
        return;
    if ((v->cache_end <= at) != (v->cache_first <= at) || (v->cache_end >= end) != (v->cache_first >= end) ||
        in(v->cache_first, at + 1, end))
        v->cache_first = v->cache_end = 0;
    else if (v->cache_first > at)
    {
        v->cache_first += inserted - deleted;
        v->cache_end += inserted - deleted;
    }
}
int mm_byte_text_line_start(MMByteTextView *v, MMPieceTable *pieces, MMI32 at, MMI32 *out)
{
    unsigned char ch;
    MMI32 original = at, lf = 0, first;
    if (at < 0 || (MMU32)at > pieces->length)
        return 0;
    if (at > 0 && mm_byte_text_fetch(v, pieces, at, &ch) && ch == 10)
        ++lf;
    while (at > 0)
    {
        mm_byte_text_fetch(v, pieces, --at, &ch);
        if (ch == 10)
            ++lf;
        else
        {
            first = at + 1 + lf;
            if (ch == 13 && first <= original)
            {
                *out = first;
                return 1;
            }
            lf = 0;
        }
    }
    *out = 0;
    return 1;
}
int mm_byte_text_next_line(MMByteTextView *v, MMPieceTable *pieces, MMI32 at, MMI32 *out, void *text, MMI32 capacity,
                           unsigned unit)
{
    unsigned char ch;
    MMI32 copied = 0;
    int cr = 0;
    if (!text)
        capacity = 0;
    while (mm_byte_text_fetch(v, pieces, at, &ch))
    {
        if (ch != 10)
        {
            if (cr)
                break;
            cr = ch == 13;
        }
        if (copied < capacity)
            store_unit(text, copied++, unit, ch);
        ++at;
    }
    if (cr)
        *out = at;
    return cr;
}
int mm_byte_text_read(MMPieceTable *pieces, MMI32 first, MMI32 count, void *output, unsigned unit)
{
    unsigned char *data = (unsigned char *)output;
    int result = mm_piece_table_read(pieces, first, output, count);
    if (unit == 2 && (result == MM_DATA_OK || result == MM_DATA_IO))
        while (count > 0)
        {
            --count;
            store_unit(output, count, unit, data[count]);
        }
    return result;
}
MMI32 mm_byte_text_line_at(const MMByteTextView *v, MMI32 line, MMI32 end)
{
    if (line < 0)
        return 0;
    return (MMU32)line < v->lines.count ? rows(v)[line] : end;
}
MMI32 mm_byte_text_line_index(const MMByteTextView *v, MMI32 at, MMI32 visible)
{
    MMI32 first = 0, end = minimum(visible + 1, v->lines.count), mid, offset;
    while (first < end)
    {
        mid = (first + end) / 2;
        offset = rows(v)[mid];
        if (offset < at)
            first = mid + 1;
        else if (offset > at)
            end = mid;
        else
            return mid;
    }
    return first - 1;
}
void mm_byte_text_set_first_line(MMByteTextView *v, MMI32 at)
{
    rows(v)[0] = at;
}
static int reformat(MMByteTextView *v, MMPieceTable *pieces, MMI32 first, MMI32 visible, MMI32 *out_added)
{
    MMI32 at, old, line, added = 0;
    int ok = 1;
    first = minimum(first, (MMI32)v->lines.count - 1);
    at = rows(v)[first];
    for (line = first + 1; line <= visible; ++line)
    {
        if (!mm_byte_text_next_line(v, pieces, at, &at, NULL, 0, 1))
        {
            mm_array_set_count(&v->lines, line);
            goto publish;
        }
        while ((MMU32)line < v->lines.count)
        {
            old = rows(v)[line];
            if (old == at)
                goto done;
            if (old > at)
                break;
            mm_array_delete(&v->lines, line, 1);
        }
        if (mm_array_insert(&v->lines, line, &at, 1) != MM_STORAGE_OK)
        {
            if ((MMU32)line >= v->lines.count)
            {
                ok = 0;
                goto publish;
            }
            rows(v)[line] = at;
        }
        ++added;
    }
done:
    if (v->lines.count > (MMU32)visible + 1)
        mm_array_set_count(&v->lines, visible + 1);
publish:
    if (out_added)
        *out_added = added;
    return ok;
}
int mm_byte_text_reformat(MMByteTextView *v, MMPieceTable *pieces, MMI32 first, MMI32 visible)
{
    return reformat(v, pieces, first, visible, NULL);
}
int mm_byte_text_reformat_edit(MMByteTextView *v, MMPieceTable *pieces, MMI32 visible, MMI32 first, MMI32 inserted,
                              MMI32 deleted, MMI32 *changed_line, MMI32 *added, MMI32 *removed)
{
    MMI32 at, end = first + deleted, line, i;
    int ok;
    if (in(rows(v)[0] - 1, first, end) && mm_byte_text_line_start(v, pieces, first, &at))
        rows(v)[0] = at;
    for (line = 0; (MMU32)line < v->lines.count && rows(v)[line] <= first; ++line)
    {
    }
    *removed = 0;
    while (deleted > 0 && (MMU32)line < v->lines.count && rows(v)[line] <= end)
    {
        mm_array_delete(&v->lines, line, 1);
        ++*removed;
    }
    if (inserted != deleted)
        for (i = line; (MMU32)i < v->lines.count; ++i)
            rows(v)[i] += inserted - deleted;
    ok = reformat(v, pieces, maximum(0, line - 1), visible, added);
    *changed_line = line;
    return reformat(v, pieces, visible, visible, NULL) && ok;
}

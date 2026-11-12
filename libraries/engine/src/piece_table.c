/* Piece-table policy converted from Kauai stream.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/piece_table.h"
#include <string.h>
#define OK THREE_MM_DATA_OK
#define BAD THREE_MM_DATA_ARGUMENT
#define FAIL THREE_MM_DATA_MEMORY
#define LIMIT THREE_MM_STORAGE_LIMIT
int three_mm_piece_table_init(ThreeMMPieceTable *t, const ThreeMMStorageAllocator *a, const ThreeMMPieceFiles *files,
                              int trash)
{
    if (!t || !files || !files->retain || !files->release)
        return BAD;
    memset(t, 0, sizeof(*t));
    if (three_mm_storage_init(&t->storage, a) != THREE_MM_STORAGE_OK)
        return BAD;
    t->files = *files;
    t->group.storage = &t->storage;
    t->group.fixed_bytes = sizeof(ThreeMMFilePiece);
    t->group.free_count = -1;
    t->group.trash_unused = trash;
    t->storage.min_grow[0] = 16 * sizeof(ThreeMMFilePiece);
    if (t->storage.min_grow[0] > 1024)
        t->storage.min_grow[0] = 1024;
    t->storage.min_grow[1] = 128;
    return OK;
}
int three_mm_piece_table_get(const ThreeMMPieceTable *t, ThreeMMU32 i, ThreeMMFilePiece *piece,
                             const unsigned char **memory)
{
    ThreeMMGroupLocation loc;
    const unsigned char *p;
    if (!t || !piece || three_mm_group_location(&t->group, i, &loc) != THREE_MM_STORAGE_OK)
        return BAD;
    p = t->storage.buffers[0].data + loc.offset;
    memcpy(piece, p, sizeof(*piece));
    if (piece->length >= LIMIT || piece->offset >= LIMIT ||
        (piece->file ? (ThreeMMU32)loc.bytes != sizeof(*piece)
                     : (ThreeMMU32)loc.bytes != sizeof(*piece) + piece->length))
        return BAD;
    if (memory)
        *memory = piece->file ? NULL : p + sizeof(*piece);
    return OK;
}
static void set(ThreeMMPieceTable *t, ThreeMMU32 i, const ThreeMMFilePiece *piece)
{
    ThreeMMGroupLocation loc;
    three_mm_group_location(&t->group, i, &loc);
    memcpy(t->storage.buffers[0].data + loc.offset, piece, sizeof(*piece));
}
int three_mm_piece_table_dispose(ThreeMMPieceTable *t)
{
    ThreeMMFilePiece piece;
    ThreeMMU32 i;
    if (!t || t->storage.buffers[0].locks || t->storage.buffers[1].locks)
        return BAD;
    for (i = 0; i < t->group.count; ++i)
    {
        if (three_mm_piece_table_get(t, i, &piece, NULL) != OK)
            return BAD;
        if (piece.file)
            t->files.release(t->files.context, piece.file);
    }
    if (three_mm_storage_dispose(&t->storage) != THREE_MM_STORAGE_OK)
        return BAD;
    t->group.count = t->group.bytes = t->length = 0;
    return OK;
}
static int piece_size(void *context, ThreeMMU32 i, ThreeMMU32 *n)
{
    ThreeMMFilePiece piece;
    if (three_mm_piece_table_get((ThreeMMPieceTable *)context, i, &piece, NULL) != OK)
        return 0;
    *n = piece.length;
    return 1;
}
static int read_piece(void *context, ThreeMMU32 i, ThreeMMU32 offset, void *output, ThreeMMU32 n)
{
    ThreeMMPieceTable *t = (ThreeMMPieceTable *)context;
    ThreeMMFilePiece piece;
    const unsigned char *memory;
    if (three_mm_piece_table_get(t, i, &piece, &memory) != OK)
        return 0;
    if (!piece.file)
    {
        memcpy(output, memory + offset, n);
        return 1;
    }
    piece.offset += offset;
    piece.length = n;
    return t->files.read && t->files.read(t->files.context, &piece, output);
}
static ThreeMMPieceReader reader(ThreeMMPieceTable *t)
{
    ThreeMMPieceReader r;
    r.context = t;
    r.count = t->group.count;
    r.length = t->length;
    r.piece_size = piece_size;
    r.read_piece = read_piece;
    return r;
}
int three_mm_piece_table_read(ThreeMMPieceTable *t, ThreeMMU32 offset, void *output, ThreeMMU32 n)
{
    ThreeMMPieceReader r;
    if (!t)
        return BAD;
    r = reader(t);
    return three_mm_piece_read(&r, offset, output, n);
}
static int find(ThreeMMPieceTable *t, ThreeMMU32 offset, ThreeMMU32 *i, ThreeMMU32 *start, ThreeMMU32 *n)
{
    ThreeMMPieceReader r = reader(t);
    return three_mm_piece_find(&r, offset, i, start, n);
}
static int split(ThreeMMPieceTable *t, ThreeMMU32 offset, ThreeMMU32 *index)
{
    ThreeMMU32 i, start, n, front;
    ThreeMMFilePiece piece;
    if (find(t, offset, &i, &start, &n) != OK)
        return BAD;
    if (offset == start)
    {
        if (index)
            *index = i;
        return OK;
    }
    if (three_mm_piece_table_get(t, i, &piece, NULL) != OK)
        return BAD;
    front = offset - start;
    piece.length -= front;
    if (piece.file)
        piece.offset += front;
    if (three_mm_group_insert(&t->group, i + 1, 0, NULL, &piece) != THREE_MM_STORAGE_OK)
        return FAIL;
    if (!piece.file && three_mm_group_move_bytes(&t->group, i, front, i + 1, 0, piece.length) != THREE_MM_STORAGE_OK)
    {
        three_mm_group_delete(&t->group, i + 1);
        return FAIL;
    }
    piece.length = front;
    if (piece.file)
    {
        piece.offset -= front;
        t->files.retain(t->files.context, piece.file);
    }
    set(t, i, &piece);
    if (index)
        *index = i + 1;
    return OK;
}
static void merge(ThreeMMPieceTable *t, ThreeMMU32 from, ThreeMMU32 to)
{
    ThreeMMU32 i, start;
    ThreeMMFilePiece a, b;
    if (!t->group.count || find(t, from ? from - 1 : 0, &i, &start, NULL) != OK)
        return;
    while (start < to && i + 1 < t->group.count)
    {
        three_mm_piece_table_get(t, i, &a, NULL);
        three_mm_piece_table_get(t, i + 1, &b, NULL);
        if (a.file != b.file || (a.file && a.offset + a.length != b.offset))
        {
            ++i;
            start += a.length;
            continue;
        }
        if (!a.file)
            three_mm_group_merge(&t->group, i + 1, i);
        else
        {
            three_mm_group_delete(&t->group, i + 1);
            t->files.release(t->files.context, b.file);
        }
        a.length += b.length;
        set(t, i, &a);
    }
}
static int range(ThreeMMPieceTable *t, ThreeMMU32 offset, ThreeMMU32 remove, ThreeMMU32 n)
{
    /* As with the original group, a lock prevents relocation, not edits that
     * fit existing storage. Partial same-stream copies rely on that rule. */
    return t && three_mm_range_contains(t->length, offset, remove) && n < LIMIT && n < LIMIT - (t->length - remove);
}
int three_mm_piece_table_replace(ThreeMMPieceTable *t, ThreeMMU32 offset, ThreeMMU32 remove, const void *input,
                                 ThreeMMU32 n)
{
    ThreeMMU32 i, start, inside, amount;
    ThreeMMFilePiece piece;
    if (!range(t, offset, remove, n) || (n && !input))
        return BAD;
    if (!remove && !n)
        return OK;
    find(t, offset, &i, &start, NULL);
    inside = offset - start;
    if (inside)
    {
        three_mm_piece_table_get(t, i, &piece, NULL);
        if (piece.file)
        {
            if (split(t, offset, &i) != OK)
                goto fail;
            inside = 0;
        }
    }
    if (n && three_mm_group_reserve(&t->group, 1, n, 0) != THREE_MM_STORAGE_OK)
        goto fail;
    if (inside && remove)
    {
        three_mm_piece_table_get(t, i, &piece, NULL);
        amount = piece.length - inside;
        if (remove < amount)
            amount = remove;
        three_mm_group_delete_bytes(&t->group, i, inside, amount);
        piece.length -= amount;
        set(t, i, &piece);
        t->length -= amount;
        remove -= amount;
        if (remove || inside == piece.length)
        {
            ++i;
            inside = 0;
        }
    }
    while (remove && i < t->group.count)
    {
        three_mm_piece_table_get(t, i, &piece, NULL);
        if (remove < piece.length)
        {
            if (piece.file)
                piece.offset += remove;
            else
                three_mm_group_delete_bytes(&t->group, i, 0, remove);
            piece.length -= remove;
            t->length -= remove;
            remove = 0;
            set(t, i, &piece);
        }
        else
        {
            if (piece.file)
                t->files.release(t->files.context, piece.file);
            three_mm_group_delete(&t->group, i);
            remove -= piece.length;
            t->length -= piece.length;
        }
    }
    if (n)
    {
        if (i < t->group.count)
        {
            three_mm_piece_table_get(t, i, &piece, NULL);
            if (!piece.file)
                goto insert_memory;
        }
        if (i)
        {
            three_mm_piece_table_get(t, i - 1, &piece, NULL);
            if (!piece.file)
            {
                --i;
                inside = piece.length;
                goto insert_memory;
            }
        }
        piece.file = NULL;
        piece.offset = 0;
        piece.length = n;
        if (three_mm_group_insert(&t->group, i, n, input, &piece) != THREE_MM_STORAGE_OK)
            return FAIL;
        t->length += n;
        goto done;
    insert_memory:
        if (three_mm_group_insert_bytes(&t->group, i, inside, n, input) != THREE_MM_STORAGE_OK)
            return FAIL;
        piece.length += n;
        set(t, i, &piece);
        t->length += n;
    }
done:
    merge(t, offset, offset + n);
    return OK;
fail:
    merge(t, offset, offset);
    return FAIL;
}
int three_mm_piece_table_replace_file(ThreeMMPieceTable *t, ThreeMMU32 offset, ThreeMMU32 remove,
                                      const ThreeMMFilePiece *source, int copy)
{
    ThreeMMFilePiece piece;
    ThreeMMCollectionStorage scratch;
    ThreeMMU32 i;
    int result;
    if (!source || !range(t, offset, remove, source->length) || !source->file || source->offset >= LIMIT ||
        source->length > LIMIT - 1 - source->offset)
        return BAD;
    if (!source->length)
        return three_mm_piece_table_replace(t, offset, remove, NULL, 0);
    if (source->length < 2048)
    {
        if (!t->files.read)
            return BAD;
        three_mm_storage_init(&scratch, &t->storage.allocator);
        if (three_mm_storage_resize_exact(&scratch, 0, source->length, THREE_MM_STORAGE_NORMAL) != THREE_MM_STORAGE_OK)
            return FAIL;
        result = t->files.read(t->files.context, source, scratch.buffers[0].data)
                     ? three_mm_piece_table_replace(t, offset, remove, scratch.buffers[0].data, source->length)
                     : THREE_MM_DATA_IO;
        three_mm_storage_dispose(&scratch);
        return result;
    }
    /* File/stream replacement inserts before deletion. Bound that intermediate
     * length as well as the final length before publishing file references. */
    if (source->length >= LIMIT - t->length)
        return BAD;
    piece = *source;
    if (copy)
    {
        if (!t->files.create_temp || !t->files.copy)
            return BAD;
        piece.file = t->files.create_temp(t->files.context);
        piece.offset = 0;
        if (!piece.file)
            return FAIL;
        if (!t->files.copy(t->files.context, source, &piece))
        {
            t->files.release(t->files.context, piece.file);
            return THREE_MM_DATA_IO;
        }
    }
    else
        t->files.retain(t->files.context, piece.file);
    if (split(t, offset, &i) != OK || three_mm_group_insert(&t->group, i, 0, NULL, &piece) != THREE_MM_STORAGE_OK)
    {
        t->files.release(t->files.context, piece.file);
        merge(t, offset, offset);
        return FAIL;
    }
    t->length += piece.length;
    if (remove && three_mm_piece_table_replace(t, offset + piece.length, remove, NULL, 0) != OK)
        return BAD;
    merge(t, offset, offset + piece.length);
    return OK;
}
static int insert_slice(ThreeMMPieceTable *t, ThreeMMPieceTable *src, ThreeMMU32 index, ThreeMMU32 inside, ThreeMMU32 n,
                        ThreeMMU32 offset, ThreeMMU32 remove)
{
    ThreeMMFilePiece piece;
    const unsigned char *memory;
    int result;
    if (!n)
        return three_mm_piece_table_replace(t, offset, remove, NULL, 0);
    if (three_mm_piece_table_get(src, index, &piece, &memory) != OK)
        return BAD;
    if (piece.file)
    {
        piece.offset += inside;
        piece.length = n;
        return three_mm_piece_table_replace_file(t, offset, remove, &piece, 0);
    }
    if (three_mm_storage_lock(&src->storage, 0) != THREE_MM_STORAGE_OK)
        return BAD;
    result = three_mm_piece_table_replace(t, offset, remove, memory + inside, n);
    three_mm_storage_unlock(&src->storage, 0);
    return result;
}
int three_mm_piece_table_replace_table(ThreeMMPieceTable *t, ThreeMMU32 offset, ThreeMMU32 remove,
                                       ThreeMMPieceTable *src, ThreeMMU32 source_offset, ThreeMMU32 n)
{
    ThreeMMU32 first, last, whole, target, i, start, end, whole_start, first_length, last_length, inserted = 0, amount,
                                                                                                  at;
    ThreeMMFilePiece piece;
    int result;
    if (!src || !range(t, offset, remove, n) || !three_mm_range_contains(src->length, source_offset, n) ||
        n >= LIMIT - t->length)
        return BAD;
    if (!n && !remove)
        return OK;
    if (split(t, offset, &target) != OK)
        return FAIL;
    find(src, source_offset, &first, &start, &first_length);
    find(src, source_offset + n, &last, &end, &last_length);
    if (first < last)
    {
        whole = first + (source_offset > start);
        whole_start = source_offset > start ? start + first_length : start;
        if (whole < last)
        {
            if (three_mm_group_copy_entries(&t->group, &src->group, whole, target, last - whole) != THREE_MM_STORAGE_OK)
                goto fail;
            inserted = end - whole_start;
            t->length += inserted;
            for (i = target + last - whole; i-- > target;)
            {
                three_mm_piece_table_get(t, i, &piece, NULL);
                if (piece.file)
                    t->files.retain(t->files.context, piece.file);
            }
        }
        if (first < whole)
        {
            amount = first_length - source_offset + start;
            result = insert_slice(t, src, first, source_offset - start, amount, offset, 0);
            if (result != OK)
                goto fail;
            inserted += amount;
        }
        else
            merge(t, offset, offset);
    }
    if (end < source_offset + n || remove)
    {
        at = source_offset > end ? source_offset : end;
        amount = source_offset + n - at;
        result = insert_slice(t, src, last, at - end, amount, offset + inserted, remove);
        if (result != OK)
            goto fail;
    }
    else
        merge(t, offset + n, offset + n);
    return OK;
fail:
    if (inserted)
        three_mm_piece_table_replace(t, offset, inserted, NULL, 0);
    return FAIL;
}
int three_mm_piece_table_compact(ThreeMMPieceTable *t)
{
    ThreeMMFilePiece piece, dst, part;
    ThreeMMU32 i, offset = 0;
    const unsigned char *memory;
    int result = OK;
    if (!t || t->storage.buffers[0].locks || t->storage.buffers[1].locks)
        return BAD;
    if (!t->length)
        goto shrink;
    if (t->group.count == 1)
    {
        three_mm_piece_table_get(t, 0, &piece, NULL);
        if (piece.file)
            goto shrink;
    }
    if (!t->files.create_temp || !t->files.copy || !t->files.write)
        return BAD;
    dst.file = t->files.create_temp(t->files.context);
    dst.offset = 0;
    dst.length = t->length;
    if (!dst.file)
    {
        result = FAIL;
        goto shrink;
    }
    three_mm_storage_lock(&t->storage, 0);
    for (i = 0; i < t->group.count; ++i)
    {
        three_mm_piece_table_get(t, i, &piece, &memory);
        part = dst;
        part.offset = offset;
        part.length = piece.length;
        if (piece.file ? !t->files.copy(t->files.context, &piece, &part)
                       : !t->files.write(t->files.context, &part, memory))
        {
            result = THREE_MM_DATA_IO;
            break;
        }
        offset += piece.length;
    }
    three_mm_storage_unlock(&t->storage, 0);
    if (result == OK)
        result = three_mm_piece_table_replace_file(t, 0, t->length, &dst, 0);
    t->files.release(t->files.context, dst.file);
shrink:
    three_mm_group_reserve(&t->group, 0, 0, 1);
    return result;
}

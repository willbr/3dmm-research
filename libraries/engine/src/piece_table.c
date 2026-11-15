/* Piece-table policy converted from Kauai stream.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/piece_table.h"
#include <string.h>
#define OK MM_DATA_OK
#define BAD MM_DATA_ARGUMENT
#define FAIL MM_DATA_MEMORY
#define LIMIT MM_STORAGE_LIMIT
int mm_piece_table_init(MMPieceTable *t, const MMStorageAllocator *a, const MMPieceFiles *files,
                              int trash)
{
    if (!t || !files || !files->retain || !files->release)
        return BAD;
    memset(t, 0, sizeof(*t));
    if (mm_storage_init(&t->storage, a) != MM_STORAGE_OK)
        return BAD;
    t->files = *files;
    t->group.storage = &t->storage;
    t->group.fixed_bytes = sizeof(MMFilePiece);
    t->group.free_count = -1;
    t->group.trash_unused = trash;
    t->storage.min_grow[0] = 16 * sizeof(MMFilePiece);
    if (t->storage.min_grow[0] > 1024)
        t->storage.min_grow[0] = 1024;
    t->storage.min_grow[1] = 128;
    return OK;
}
int mm_piece_table_get(const MMPieceTable *t, MMU32 i, MMFilePiece *piece,
                             const unsigned char **memory)
{
    MMGroupLocation loc;
    const unsigned char *p;
    if (!t || !piece || mm_group_location(&t->group, i, &loc) != MM_STORAGE_OK)
        return BAD;
    p = t->storage.buffers[0].data + loc.offset;
    memcpy(piece, p, sizeof(*piece));
    if (piece->length >= LIMIT || piece->offset >= LIMIT ||
        (piece->file ? (MMU32)loc.bytes != sizeof(*piece)
                     : (MMU32)loc.bytes != sizeof(*piece) + piece->length))
        return BAD;
    if (memory)
        *memory = piece->file ? NULL : p + sizeof(*piece);
    return OK;
}
static void set(MMPieceTable *t, MMU32 i, const MMFilePiece *piece)
{
    MMGroupLocation loc;
    mm_group_location(&t->group, i, &loc);
    memcpy(t->storage.buffers[0].data + loc.offset, piece, sizeof(*piece));
}
int mm_piece_table_dispose(MMPieceTable *t)
{
    MMFilePiece piece;
    MMU32 i;
    if (!t || t->storage.buffers[0].locks || t->storage.buffers[1].locks)
        return BAD;
    for (i = 0; i < t->group.count; ++i)
    {
        if (mm_piece_table_get(t, i, &piece, NULL) != OK)
            return BAD;
        if (piece.file)
            t->files.release(t->files.context, piece.file);
    }
    if (mm_storage_dispose(&t->storage) != MM_STORAGE_OK)
        return BAD;
    t->group.count = t->group.bytes = t->length = 0;
    return OK;
}
static int piece_size(void *context, MMU32 i, MMU32 *n)
{
    MMFilePiece piece;
    if (mm_piece_table_get((MMPieceTable *)context, i, &piece, NULL) != OK)
        return 0;
    *n = piece.length;
    return 1;
}
static int read_piece(void *context, MMU32 i, MMU32 offset, void *output, MMU32 n)
{
    MMPieceTable *t = (MMPieceTable *)context;
    MMFilePiece piece;
    const unsigned char *memory;
    if (mm_piece_table_get(t, i, &piece, &memory) != OK)
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
static MMPieceReader reader(MMPieceTable *t)
{
    MMPieceReader r;
    r.context = t;
    r.count = t->group.count;
    r.length = t->length;
    r.piece_size = piece_size;
    r.read_piece = read_piece;
    return r;
}
int mm_piece_table_read(MMPieceTable *t, MMU32 offset, void *output, MMU32 n)
{
    MMPieceReader r;
    if (!t)
        return BAD;
    r = reader(t);
    return mm_piece_read(&r, offset, output, n);
}
static int find(MMPieceTable *t, MMU32 offset, MMU32 *i, MMU32 *start, MMU32 *n)
{
    MMPieceReader r = reader(t);
    return mm_piece_find(&r, offset, i, start, n);
}
static int split(MMPieceTable *t, MMU32 offset, MMU32 *index)
{
    MMU32 i, start, n, front;
    MMFilePiece piece;
    if (find(t, offset, &i, &start, &n) != OK)
        return BAD;
    if (offset == start)
    {
        if (index)
            *index = i;
        return OK;
    }
    if (mm_piece_table_get(t, i, &piece, NULL) != OK)
        return BAD;
    front = offset - start;
    piece.length -= front;
    if (piece.file)
        piece.offset += front;
    if (mm_group_insert(&t->group, i + 1, 0, NULL, &piece) != MM_STORAGE_OK)
        return FAIL;
    if (!piece.file && mm_group_move_bytes(&t->group, i, front, i + 1, 0, piece.length) != MM_STORAGE_OK)
    {
        mm_group_delete(&t->group, i + 1);
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
static void merge(MMPieceTable *t, MMU32 from, MMU32 to)
{
    MMU32 i, start;
    MMFilePiece a, b;
    if (!t->group.count || find(t, from ? from - 1 : 0, &i, &start, NULL) != OK)
        return;
    while (start < to && i + 1 < t->group.count)
    {
        mm_piece_table_get(t, i, &a, NULL);
        mm_piece_table_get(t, i + 1, &b, NULL);
        if (a.file != b.file || (a.file && a.offset + a.length != b.offset))
        {
            ++i;
            start += a.length;
            continue;
        }
        if (!a.file)
            mm_group_merge(&t->group, i + 1, i);
        else
        {
            mm_group_delete(&t->group, i + 1);
            t->files.release(t->files.context, b.file);
        }
        a.length += b.length;
        set(t, i, &a);
    }
}
static int range(MMPieceTable *t, MMU32 offset, MMU32 remove, MMU32 n)
{
    /* As with the original group, a lock prevents relocation, not edits that
     * fit existing storage. Partial same-stream copies rely on that rule. */
    return t && mm_range_contains(t->length, offset, remove) && n < LIMIT && n < LIMIT - (t->length - remove);
}
int mm_piece_table_replace(MMPieceTable *t, MMU32 offset, MMU32 remove, const void *input,
                                 MMU32 n)
{
    MMU32 i, start, inside, amount;
    MMFilePiece piece;
    if (!range(t, offset, remove, n) || (n && !input))
        return BAD;
    if (!remove && !n)
        return OK;
    find(t, offset, &i, &start, NULL);
    inside = offset - start;
    if (inside)
    {
        mm_piece_table_get(t, i, &piece, NULL);
        if (piece.file)
        {
            if (split(t, offset, &i) != OK)
                goto fail;
            inside = 0;
        }
    }
    if (n && mm_group_reserve(&t->group, 1, n, 0) != MM_STORAGE_OK)
        goto fail;
    if (inside && remove)
    {
        mm_piece_table_get(t, i, &piece, NULL);
        amount = piece.length - inside;
        if (remove < amount)
            amount = remove;
        mm_group_delete_bytes(&t->group, i, inside, amount);
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
        mm_piece_table_get(t, i, &piece, NULL);
        if (remove < piece.length)
        {
            if (piece.file)
                piece.offset += remove;
            else
                mm_group_delete_bytes(&t->group, i, 0, remove);
            piece.length -= remove;
            t->length -= remove;
            remove = 0;
            set(t, i, &piece);
        }
        else
        {
            if (piece.file)
                t->files.release(t->files.context, piece.file);
            mm_group_delete(&t->group, i);
            remove -= piece.length;
            t->length -= piece.length;
        }
    }
    if (n)
    {
        if (i < t->group.count)
        {
            mm_piece_table_get(t, i, &piece, NULL);
            if (!piece.file)
                goto insert_memory;
        }
        if (i)
        {
            mm_piece_table_get(t, i - 1, &piece, NULL);
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
        if (mm_group_insert(&t->group, i, n, input, &piece) != MM_STORAGE_OK)
            return FAIL;
        t->length += n;
        goto done;
    insert_memory:
        if (mm_group_insert_bytes(&t->group, i, inside, n, input) != MM_STORAGE_OK)
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
int mm_piece_table_replace_file(MMPieceTable *t, MMU32 offset, MMU32 remove,
                                      const MMFilePiece *source, int copy)
{
    MMFilePiece piece;
    MMCollectionStorage scratch;
    MMU32 i;
    int result;
    if (!source || !range(t, offset, remove, source->length) || !source->file || source->offset >= LIMIT ||
        source->length > LIMIT - 1 - source->offset)
        return BAD;
    if (!source->length)
        return mm_piece_table_replace(t, offset, remove, NULL, 0);
    if (source->length < 2048)
    {
        if (!t->files.read)
            return BAD;
        mm_storage_init(&scratch, &t->storage.allocator);
        if (mm_storage_resize_exact(&scratch, 0, source->length, MM_STORAGE_NORMAL) != MM_STORAGE_OK)
            return FAIL;
        result = t->files.read(t->files.context, source, scratch.buffers[0].data)
                     ? mm_piece_table_replace(t, offset, remove, scratch.buffers[0].data, source->length)
                     : MM_DATA_IO;
        mm_storage_dispose(&scratch);
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
            return MM_DATA_IO;
        }
    }
    else
        t->files.retain(t->files.context, piece.file);
    if (split(t, offset, &i) != OK || mm_group_insert(&t->group, i, 0, NULL, &piece) != MM_STORAGE_OK)
    {
        t->files.release(t->files.context, piece.file);
        merge(t, offset, offset);
        return FAIL;
    }
    t->length += piece.length;
    if (remove && mm_piece_table_replace(t, offset + piece.length, remove, NULL, 0) != OK)
        return BAD;
    merge(t, offset, offset + piece.length);
    return OK;
}
static int insert_slice(MMPieceTable *t, MMPieceTable *src, MMU32 index, MMU32 inside, MMU32 n,
                        MMU32 offset, MMU32 remove)
{
    MMFilePiece piece;
    const unsigned char *memory;
    int result;
    if (!n)
        return mm_piece_table_replace(t, offset, remove, NULL, 0);
    if (mm_piece_table_get(src, index, &piece, &memory) != OK)
        return BAD;
    if (piece.file)
    {
        piece.offset += inside;
        piece.length = n;
        return mm_piece_table_replace_file(t, offset, remove, &piece, 0);
    }
    if (mm_storage_lock(&src->storage, 0) != MM_STORAGE_OK)
        return BAD;
    result = mm_piece_table_replace(t, offset, remove, memory + inside, n);
    mm_storage_unlock(&src->storage, 0);
    return result;
}
int mm_piece_table_replace_table(MMPieceTable *t, MMU32 offset, MMU32 remove,
                                       MMPieceTable *src, MMU32 source_offset, MMU32 n)
{
    MMU32 first, last, whole, target, i, start, end, whole_start, first_length, last_length, inserted = 0, amount,
                                                                                                  at;
    MMFilePiece piece;
    int result;
    if (!src || !range(t, offset, remove, n) || !mm_range_contains(src->length, source_offset, n) ||
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
            if (mm_group_copy_entries(&t->group, &src->group, whole, target, last - whole) != MM_STORAGE_OK)
                goto fail;
            inserted = end - whole_start;
            t->length += inserted;
            for (i = target + last - whole; i-- > target;)
            {
                mm_piece_table_get(t, i, &piece, NULL);
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
        mm_piece_table_replace(t, offset, inserted, NULL, 0);
    return FAIL;
}
int mm_piece_table_compact(MMPieceTable *t)
{
    MMFilePiece piece, dst, part;
    MMU32 i, offset = 0;
    const unsigned char *memory;
    int result = OK;
    if (!t || t->storage.buffers[0].locks || t->storage.buffers[1].locks)
        return BAD;
    if (!t->length)
        goto shrink;
    if (t->group.count == 1)
    {
        mm_piece_table_get(t, 0, &piece, NULL);
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
    mm_storage_lock(&t->storage, 0);
    for (i = 0; i < t->group.count; ++i)
    {
        mm_piece_table_get(t, i, &piece, &memory);
        part = dst;
        part.offset = offset;
        part.length = piece.length;
        if (piece.file ? !t->files.copy(t->files.context, &piece, &part)
                       : !t->files.write(t->files.context, &part, memory))
        {
            result = MM_DATA_IO;
            break;
        }
        offset += piece.length;
    }
    mm_storage_unlock(&t->storage, 0);
    if (result == OK)
        result = mm_piece_table_replace_file(t, 0, t->length, &dst, 0);
    t->files.release(t->files.context, dst.file);
shrink:
    mm_group_reserve(&t->group, 0, 0, 1);
    return result;
}

typedef struct PieceCopy
{
    MMPieceTable *table;
    MMFilePiece piece;
} PieceCopy;
static int read_copy_piece(void *context, MMU32 offset, void *output, size_t bytes)
{
    PieceCopy *copy = (PieceCopy *)context;
    MMFilePiece part = copy->piece;
    part.offset += offset;
    part.length = (MMU32)bytes;
    return copy->table->files.read && copy->table->files.read(copy->table->files.context, &part, output);
}
int mm_piece_table_write(MMPieceTable *t, MMU32 offset, MMOwnedBlock *destination,
                               const MMStorageAllocator *allocator)
{
    MMU32 i, start, inside, count, total, written, index;
    MMFilePiece piece;
    MMBlockView saved;
    MMBlockIO input, output;
    const unsigned char *memory;
    PieceCopy copy;
    int result;
    if (!t || mm_owned_block_check(destination) != OK)
        return BAD;
    if (destination->view.packed)
        return MM_DATA_PACKED;
    total = destination->view.length;
    if (!mm_range_contains(t->length, offset, total))
        return BAD;
    if (!total)
        return OK;
    if (find(t, offset, &index, &start, NULL) != OK)
        return BAD;
    inside = offset - start;
    /* Retain the legacy count selection for supported windows. Its original
     * total-versus-remaining mistake could expand a final window past the
     * caller's range; reject that case before touching storage or I/O. */
    for (i = index, written = 0; written < total && i < t->group.count; ++i)
    {
        if (mm_piece_table_get(t, i, &piece, NULL) != OK || inside >= piece.length)
            return BAD;
        count = piece.length - inside;
        if (count > total)
            count = total;
        if (count > total - written)
            return BAD;
        written += count;
        inside = 0;
    }
    if (written != total)
        return BAD;
    if (mm_storage_lock(&t->storage, 0) != MM_STORAGE_OK)
        return BAD;
    if (mm_storage_lock(&t->storage, 1) != MM_STORAGE_OK)
    {
        mm_storage_unlock(&t->storage, 0);
        return BAD;
    }
    saved = destination->view;
    inside = offset - start;
    written = 0;
    result = mm_block_move_lim(&destination->view, -(MMI32)total);
    for (i = index; result == OK && written < total && i < t->group.count; ++i)
    {
        if (mm_piece_table_get(t, i, &piece, &memory) != OK)
        {
            result = BAD;
            break;
        }
        count = piece.length - inside;
        if (count > total)
            count = total;
        result = mm_block_move_lim(&destination->view, (MMI32)count);
        if (result != OK)
            break;
        result = mm_owned_block_io(destination, &output);
        if (result != OK)
            break;
        if (piece.file)
        {
            memset(&input, 0, sizeof(input));
            input.view.kind = MM_BLOCK_FILE;
            input.view.length = count;
            copy.table = t;
            copy.piece = piece;
            copy.piece.offset += inside;
            copy.piece.length = count;
            input.context = &copy;
            input.read_file = read_copy_piece;
            result = mm_block_copy(&input, &output, 1, allocator);
        }
        else
            result = mm_block_write(&output, 0, memory + inside, count, 0);
        inside = 0;
        written += count;
        /* Failed writes still consumed their attempted window in the native
         * protocol. Restoring saved below covers that path as well. */
        if (mm_block_move_min(&destination->view, (MMI32)count) != OK && result == OK)
            result = BAD;
    }
    destination->view = saved;
    mm_storage_unlock(&t->storage, 1);
    mm_storage_unlock(&t->storage, 0);
    return result;
}

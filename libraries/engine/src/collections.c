/* Collection mutation policy converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/collections.h"
#include <string.h>
#define LIMIT MM_STORAGE_LIMIT
#define OK MM_STORAGE_OK
#define BAD MM_STORAGE_ARGUMENT
static void trash(int enabled, void *p, MMU32 n)
{
    if (enabled && n)
        memset(p, 0xa3, n);
}
static void reverse(unsigned char *p, MMU32 n)
{
    MMU32 i;
    unsigned char x;
    for (i = 0; i < n / 2; ++i)
    {
        x = p[i];
        p[i] = p[n - 1 - i];
        p[n - 1 - i] = x;
    }
}
static void blocks(unsigned char *p, MMU32 a, MMU32 b)
{
    if (!a || !b)
        return;
    reverse(p, a);
    reverse(p + a, b);
    reverse(p, a + b);
}
static void move(unsigned char *p, MMU32 width, MMU32 source, MMU32 target)
{
    if (target > source)
        --target;
    if (source < target)
        blocks(p + source * width, width, (target - source) * width);
    else if (target < source)
        blocks(p + target * width, (source - target) * width, width);
}
static MMU32 bitmap(MMU32 count)
{
    return (count + 7) / 8;
}
static int array_shape(const MMArray *a)
{
    return a && a->storage && a->width && a->width <= 0x7fffffffUL && a->count <= (LIMIT - 1) / a->width &&
           a->count * a->width <= a->storage->buffers[0].capacity &&
           (!a->allocated || bitmap(a->count) <= a->storage->buffers[1].capacity);
}
int mm_array_rotate_left(MMArray *a, MMU32 split)
{
    if (!array_shape(a) || a->allocated || split > a->count)
        return BAD;
    if (split && split < a->count)
        blocks(a->storage->buffers[0].data, split * a->width, (a->count - split) * a->width);
    return OK;
}
int mm_array_reserve(MMArray *a, MMU32 additional, int shrink)
{
    MMU32 needed = additional, free_count;
    if (!array_shape(a))
        return BAD;
    if (a->allocated)
    {
        if (a->free_count >= 0)
        {
            free_count = (MMU32)a->free_count;
            needed = additional > free_count ? additional - free_count : 0;
        }
        else
        {
            free_count = 0 - (MMU32)a->free_count;
            if (additional > 0xffffffffUL - free_count)
                return BAD;
            needed += free_count;
        }
    }
    if (needed > (LIMIT - 1) / a->width - a->count)
        return BAD;
    needed += a->count;
    return mm_storage_ensure(a->storage, needed * a->width, a->allocated ? bitmap(needed) : 0, shrink);
}
int mm_array_insert(MMArray *a, MMU32 index, const void *data, MMU32 count)
{
    MMU32 bytes, position;
    int result;
    if (!array_shape(a) || a->allocated || index > a->count || count > (LIMIT - 1) / a->width - a->count)
        return BAD;
    if (!count)
        return OK;
    result = mm_array_reserve(a, count, 0);
    if (result != OK)
        return result;
    bytes = count * a->width;
    position = index * a->width;
    memmove(a->storage->buffers[0].data + position + bytes, a->storage->buffers[0].data + position,
            (a->count - index) * a->width);
    /* Null insertion intentionally leaves those bytes untouched, even in debug. */
    if (data)
        memcpy(a->storage->buffers[0].data + position, data, bytes);
    a->count += count;
    return OK;
}
int mm_array_delete(MMArray *a, MMU32 index, MMU32 count)
{
    unsigned char *p;
    MMU32 old;
    if (!array_shape(a) || index >= a->count || count > a->count - index || (a->allocated && count != 1))
        return BAD;
    p = a->storage->buffers[0].data;
    if (!a->allocated)
    {
        a->count -= count;
        memmove(p + index * a->width, p + (index + count) * a->width, (a->count - index) * a->width);
        trash(a->trash_unused, p + a->count * a->width, count * a->width);
    }
    else
    {
        unsigned char *bits = a->storage->buffers[1].data;
        if (!(bits[index / 8] & (1U << (index % 8))))
            return BAD;
        trash(a->trash_unused, p + index * a->width, a->width);
        bits[index / 8] &= (unsigned char)~(1U << (index % 8));
        ++a->free_count;
        old = a->count;
        if (index == a->count - 1)
        {
            while (a->count && !(bits[(a->count - 1) / 8] & (1U << ((a->count - 1) % 8))))
                --a->count;
            a->free_count -= (MMI32)(old - a->count);
        }
    }
    return OK;
}
int mm_array_set_count(MMArray *a, MMU32 count)
{
    int result;
    MMU32 lo, hi;
    if (!array_shape(a) || a->allocated || count > (LIMIT - 1) / a->width)
        return BAD;
    if (count > a->count)
    {
        result = mm_array_reserve(a, count - a->count, 0);
        if (result != OK)
            return result;
    }
    lo = count < a->count ? count : a->count;
    hi = count > a->count ? count : a->count;
    if (hi > lo)
        trash(a->trash_unused, a->storage->buffers[0].data + lo * a->width, (hi - lo) * a->width);
    a->count = count;
    return OK;
}
int mm_array_move(MMArray *a, MMU32 source, MMU32 target)
{
    if (!array_shape(a) || a->allocated || source >= a->count || target > a->count)
        return BAD;
    move(a->storage->buffers[0].data, a->width, source, target);
    return OK;
}
int mm_array_add(MMArray *a, const void *data, MMU32 *out)
{
    MMU32 i;
    int result;
    unsigned char *bits;
    if (!array_shape(a))
        return BAD;
    i = a->count;
    if (!a->allocated)
        result = mm_array_insert(a, i, data, 1);
    else
    {
        if (!data)
            return BAD;
        bits = a->storage->buffers[1].data;
        if (a->free_count > 0)
        {
            for (i = 0; i < a->count && (bits[i / 8] & (1U << (i % 8))); ++i)
            {
            }
            if (i == a->count)
                return BAD;
            --a->free_count;
        }
        else
        {
            result = mm_array_reserve(a, 1, 0);
            if (result != OK)
                return result;
            ++a->count;
            bits = a->storage->buffers[1].data;
        }
        bits[i / 8] |= (unsigned char)(1U << (i % 8));
        memcpy(a->storage->buffers[0].data + i * a->width, data, a->width);
        result = OK;
    }
    if (result == OK && out)
        *out = i;
    return result;
}
int mm_array_pop(MMArray *a, void *out)
{
    if (!array_shape(a) || a->allocated)
        return BAD;
    if (!a->count)
    {
        trash(a->trash_unused, out, out ? a->width : 0);
        return BAD;
    }
    if (out)
        memcpy(out, a->storage->buffers[0].data + (a->count - 1) * a->width, a->width);
    return mm_array_delete(a, a->count - 1, 1);
}
static MMU32 round4(MMU32 n)
{
    return (n + 3) & ~(MMU32)3;
}
static int group_shape(const MMGroup *g)
{
    return g && g->storage && g->fixed_bytes < LIMIT && g->bytes < LIMIT && g->count <= (LIMIT - 1) / 8 &&
           g->bytes <= g->storage->buffers[0].capacity && g->count * 8 <= g->storage->buffers[1].capacity;
}
static MMGroupLocation location(const MMGroup *g, MMU32 i)
{
    MMGroupLocation loc;
    memcpy(&loc, g->storage->buffers[1].data + i * 8, 8);
    return loc;
}
static void set_location(MMGroup *g, MMU32 i, MMGroupLocation loc)
{
    memcpy(g->storage->buffers[1].data + i * 8, &loc, 8);
}
int mm_group_read_slot(const MMGroup *g, MMU32 index, MMGroupLocation *out)
{
    MMGroupLocation loc;
    if (!group_shape(g) || !out)
        return MM_GROUP_SLOT_INVALID;
    if (index >= g->count)
        return MM_GROUP_SLOT_FREE;
    loc = location(g, index);
    if (loc.offset == -1)
        return MM_GROUP_SLOT_FREE;
    if (loc.offset < 0 || loc.bytes < 0 || (MMU32)loc.bytes < g->fixed_bytes ||
        (MMU32)loc.offset > g->bytes || (MMU32)loc.bytes > g->bytes - (MMU32)loc.offset)
        return MM_GROUP_SLOT_INVALID;
    *out = loc;
    return MM_GROUP_SLOT_PRESENT;
}
int mm_group_location(const MMGroup *g, MMU32 index, MMGroupLocation *out)
{
    MMGroupLocation loc;
    if (!out || mm_group_read_slot(g, index, &loc) != MM_GROUP_SLOT_PRESENT ||
        round4((MMU32)loc.bytes) > g->bytes - (MMU32)loc.offset)
        return BAD;
    *out = loc;
    return OK;
}
int mm_group_reserve(MMGroup *g, MMU32 additional, MMU32 variable, int shrink)
{
    MMU32 slots, per;
    if (!group_shape(g) || variable >= LIMIT)
        return BAD;
    slots = g->free_count == -1                      ? additional
            : additional > (MMU32)g->free_count ? additional - (MMU32)g->free_count
                                                     : 0;
    per = g->fixed_bytes + 3;
    /* Retain the legacy reserve budget, including its conservative bytes term. */
    if (slots > (LIMIT - 1) / 8 - g->count || g->bytes > LIMIT / per || additional > LIMIT / per - g->bytes ||
        additional > (LIMIT - 1 - g->bytes) / per || variable > LIMIT - 1 - g->bytes - additional * per)
        return BAD;
    return mm_storage_ensure(g->storage, g->bytes + variable + additional * per, (g->count + slots) * 8, shrink);
}
static void adjust(MMGroup *g, MMU32 lo, MMU32 hi, MMI32 delta)
{
    MMU32 i;
    MMGroupLocation loc;
    if (g->bytes >= lo && g->bytes < hi)
        g->bytes = (MMU32)((MMI32)g->bytes + delta);
    for (i = 0; i < g->count; ++i)
    {
        loc = location(g, i);
        if (loc.offset >= 0 && (MMU32)loc.offset >= lo && (MMU32)loc.offset < hi)
        {
            loc.offset += delta;
            set_location(g, i, loc);
        }
    }
}
static void remove_bytes(MMGroup *g, MMU32 offset, MMU32 bytes)
{
    unsigned char *p = g->storage->buffers[0].data;
    if (offset + bytes < g->bytes)
    {
        memmove(p + offset, p + offset + bytes, g->bytes - offset - bytes);
        adjust(g, offset + 1, g->bytes + 1, -(MMI32)bytes);
    }
    else
        g->bytes -= bytes;
    trash(g->trash_unused, p + g->bytes, bytes);
}
static int insert_entry(MMGroup *g, MMU32 i, MMU32 bytes, const void *data, const void *fixed,
                        int allocated)
{
    MMGroupLocation loc;
    MMU32 total, padded, new_count;
    int result;
    unsigned char *p, *entries;
    if (!group_shape(g) || bytes >= LIMIT || bytes > LIMIT - 1 - g->fixed_bytes || i > g->count)
        return BAD;
    total = bytes + g->fixed_bytes;
    padded = round4(total);
    new_count = g->count + (allocated ? i == g->count : 1);
    if (padded > LIMIT - 1 - g->bytes || new_count > (LIMIT - 1) / 8)
        return BAD;
    result = mm_storage_ensure(g->storage, g->bytes + padded, new_count * 8, 0);
    if (result != OK)
        return result;
    loc.offset = total ? (MMI32)g->bytes : 0;
    loc.bytes = (MMI32)total;
    entries = g->storage->buffers[1].data;
    if (!allocated)
        memmove(entries + (i + 1) * 8, entries + i * 8, (g->count - i) * 8);
    set_location(g, i, loc);
    p = g->storage->buffers[0].data;
    if (padded)
    {
        p += loc.offset;
        if (data)
        {
            trash(g->trash_unused, p, g->fixed_bytes);
            if (bytes)
                memcpy(p + g->fixed_bytes, data, bytes);
            trash(g->trash_unused, p + total, padded - total);
        }
        else
            trash(g->trash_unused, p, padded);
        if (fixed && g->fixed_bytes)
            memcpy(p, fixed, g->fixed_bytes);
    }
    g->bytes += padded;
    g->count = new_count;
    return OK;
}
int mm_group_insert(MMGroup *g, MMU32 i, MMU32 bytes, const void *data, const void *fixed)
{
    if (!g || g->free_count != -1)
        return BAD;
    return insert_entry(g, i, bytes, data, fixed, 0);
}
int mm_group_add(MMGroup *g, MMU32 bytes, const void *data, const void *fixed, MMU32 *out)
{
    MMU32 i;
    int result;
    if (!group_shape(g))
        return BAD;
    i = g->count;
    if (g->free_count > 0)
    {
        for (i = 0; i < g->count && location(g, i).offset != -1; ++i)
        {
        }
        if (i == g->count)
            return BAD;
    }
    result = insert_entry(g, i, bytes, data, fixed, g->free_count != -1);
    if (result == OK)
    {
        if (g->free_count > 0)
            --g->free_count;
        if (out)
            *out = i;
    }
    return result;
}
int mm_group_delete(MMGroup *g, MMU32 i)
{
    MMGroupLocation loc, free_loc;
    MMU32 old;
    if (mm_group_location(g, i, &loc) != OK)
        return BAD;
    old = g->count;
    if (g->free_count == -1)
    {
        --g->count;
        memmove(g->storage->buffers[1].data + i * 8, g->storage->buffers[1].data + (i + 1) * 8, (g->count - i) * 8);
        trash(g->trash_unused, g->storage->buffers[1].data + g->count * 8, 8);
    }
    else if (i == g->count - 1)
    {
        --g->count;
        while (g->count && location(g, g->count - 1).offset == -1)
        {
            --g->count;
            --g->free_count;
        }
        trash(g->trash_unused, g->storage->buffers[1].data + g->count * 8, (old - g->count) * 8);
    }
    else
    {
        free_loc.offset = -1;
        free_loc.bytes = 0;
        set_location(g, i, free_loc);
        ++g->free_count;
    }
    if (loc.bytes)
        remove_bytes(g, (MMU32)loc.offset, round4((MMU32)loc.bytes));
    return OK;
}
int mm_group_move(MMGroup *g, MMU32 source, MMU32 target)
{
    if (!group_shape(g) || g->free_count != -1 || source >= g->count || target > g->count)
        return BAD;
    move(g->storage->buffers[1].data, 8, source, target);
    return OK;
}
int mm_group_swap(MMGroup *g, MMU32 a, MMU32 b)
{
    MMGroupLocation first, second;
    if (!group_shape(g) || g->free_count != -1 || a >= g->count || b >= g->count)
        return BAD;
    first = location(g, a);
    second = location(g, b);
    set_location(g, a, second);
    set_location(g, b, first);
    return OK;
}
int mm_group_insert_bytes(MMGroup *g, MMU32 i, MMU32 offset, MMU32 bytes, const void *data)
{
    MMGroupLocation loc;
    MMU32 extra, end;
    unsigned char *p;
    int result;
    if (mm_group_location(g, i, &loc) != OK || offset > (MMU32)loc.bytes - g->fixed_bytes ||
        bytes > LIMIT - 1 - (MMU32)loc.bytes)
        return BAD;
    if (!bytes)
        return OK;
    offset += g->fixed_bytes;
    if (!loc.bytes)
        loc.offset = (MMI32)g->bytes;
    extra = round4((MMU32)loc.bytes + bytes) - round4((MMU32)loc.bytes);
    if (extra > LIMIT - 1 - g->bytes)
        return BAD;
    if (extra)
    {
        result = mm_storage_ensure(g->storage, g->bytes + extra, g->count * 8, 0);
        if (result != OK)
            return result;
        end = (MMU32)loc.offset + round4((MMU32)loc.bytes);
        if (end < g->bytes)
        {
            p = g->storage->buffers[0].data;
            memmove(p + end + extra, p + end, g->bytes - end);
            adjust(g, (MMU32)loc.offset + 1, g->bytes + 1, (MMI32)extra);
        }
        else
            g->bytes += extra;
    }
    p = g->storage->buffers[0].data + (MMU32)loc.offset + offset;
    if (offset < (MMU32)loc.bytes)
        memmove(p + bytes, p, (MMU32)loc.bytes - offset);
    if (data)
        memcpy(p, data, bytes);
    else
        trash(g->trash_unused, p, bytes);
    loc.bytes += (MMI32)bytes;
    set_location(g, i, loc);
    return OK;
}
int mm_group_delete_bytes(MMGroup *g, MMU32 i, MMU32 offset, MMU32 bytes)
{
    MMGroupLocation loc;
    MMU32 remove;
    unsigned char *p;
    if (mm_group_location(g, i, &loc) != OK || offset > (MMU32)loc.bytes - g->fixed_bytes ||
        bytes > (MMU32)loc.bytes - g->fixed_bytes - offset)
        return BAD;
    if (!bytes)
        return OK;
    offset += g->fixed_bytes;
    p = g->storage->buffers[0].data + (MMU32)loc.offset + offset;
    if (offset + bytes < (MMU32)loc.bytes)
        memmove(p, p + bytes, (MMU32)loc.bytes - offset - bytes);
    remove = round4((MMU32)loc.bytes) - round4((MMU32)loc.bytes - bytes);
    if (remove)
        remove_bytes(g, (MMU32)loc.offset + (MMU32)loc.bytes - bytes, remove);
    loc = location(g, i);
    loc.bytes -= (MMI32)bytes;
    if (!loc.bytes)
        loc.offset = 0;
    set_location(g, i, loc);
    return OK;
}
int mm_group_put(MMGroup *g, MMU32 i, MMU32 bytes, const void *data)
{
    MMGroupLocation loc;
    MMU32 old;
    int result = OK;
    if (mm_group_location(g, i, &loc) != OK)
        return BAD;
    old = (MMU32)loc.bytes - g->fixed_bytes;
    if (bytes > old)
        result = mm_group_insert_bytes(g, i, old, bytes - old, NULL);
    else if (bytes < old)
        result = mm_group_delete_bytes(g, i, bytes, old - bytes);
    if (result != OK)
        return result;
    if (data && bytes)
    {
        loc = location(g, i);
        memcpy(g->storage->buffers[0].data + (MMU32)loc.offset + g->fixed_bytes, data, bytes);
    }
    return OK;
}
int mm_group_move_bytes(MMGroup *g, MMU32 source, MMU32 from, MMU32 target, MMU32 to,
                              MMU32 bytes)
{
    MMGroupLocation src, dst, loc;
    MMI32 delta;
    MMU32 main, a, b;
    int result;
    unsigned char tail[4], *p;
    if (mm_group_location(g, source, &src) != OK || mm_group_location(g, target, &dst) != OK ||
        from > (MMU32)src.bytes - g->fixed_bytes || bytes > (MMU32)src.bytes - g->fixed_bytes - from ||
        to > (MMU32)dst.bytes - g->fixed_bytes || bytes > LIMIT - 1 - (MMU32)dst.bytes)
        return BAD;
    main = bytes & ~(MMU32)3;
    a = (MMU32)src.offset + from + g->fixed_bytes;
    b = (MMU32)dst.offset + to + g->fixed_bytes;
    if (source == target && a < b && b - a < main)
        return BAD;
    delta = (MMI32)(round4((MMU32)dst.bytes + bytes) - round4((MMU32)dst.bytes)) -
            (MMI32)(round4((MMU32)src.bytes) - round4((MMU32)src.bytes - bytes));
    if (delta > 0)
    {
        if ((MMU32)delta > LIMIT - 1 - g->bytes)
            return BAD;
        result = mm_storage_ensure(g->storage, g->bytes + (MMU32)delta, g->count * 8, 0);
        if (result != OK)
            return result;
    }
    if (main)
    {
        loc = location(g, source);
        loc.bytes -= (MMI32)main;
        if (!loc.bytes)
            loc.offset = 0;
        set_location(g, source, loc);
        loc = location(g, target);
        if (!loc.bytes)
        {
            loc.offset = (MMI32)g->bytes;
            b = g->bytes;
        }
        loc.bytes += (MMI32)main;
        set_location(g, target, loc);
        p = g->storage->buffers[0].data;
        if (a < b)
        {
            blocks(p + a, main, b - a - main);
            adjust(g, (MMU32)src.offset + 1, (MMU32)dst.offset + 1, -(MMI32)main);
        }
        else
        {
            blocks(p + b, a - b, main);
            adjust(g, (MMU32)dst.offset + 1, (MMU32)src.offset + 1, (MMI32)main);
        }
    }
    if (bytes > main)
    {
        loc = location(g, source);
        memcpy(tail, g->storage->buffers[0].data + (MMU32)loc.offset + g->fixed_bytes + from, bytes - main);
        result = mm_group_delete_bytes(g, source, from, bytes - main);
        if (result != OK)
            return result;
        return mm_group_insert_bytes(g, target, to + main, bytes - main, tail);
    }
    return OK;
}
int mm_group_merge(MMGroup *g, MMU32 source, MMU32 target)
{
    MMGroupLocation src, dst;
    MMU32 bytes, main, end;
    unsigned char tail[4];
    int result;
    if (source == target || mm_group_location(g, source, &src) != OK ||
        mm_group_location(g, target, &dst) != OK)
        return BAD;
    bytes = (MMU32)src.bytes - g->fixed_bytes;
    main = bytes & ~(MMU32)3;
    end = (MMU32)dst.bytes - g->fixed_bytes;
    if (bytes > main)
        memcpy(tail, g->storage->buffers[0].data + (MMU32)src.offset + g->fixed_bytes + main, bytes - main);
    if (main)
    {
        result = mm_group_move_bytes(g, source, 0, target, end, main);
        if (result != OK)
            return result;
    }
    result = mm_group_delete(g, source);
    if (result != OK)
        return result;
    if (source < target)
        --target; /* Preserve VirtualGroup's legacy dense-index adjustment. */
    return bytes > main ? mm_group_insert_bytes(g, target, end + main, bytes - main, tail) : OK;
}
int mm_group_copy_entries(MMGroup *dst, MMGroup *src, MMU32 from, MMU32 to, MMU32 count)
{
    MMU32 i, bytes = 0;
    MMGroupLocation loc;
    const unsigned char *p;
    int result;
    if (!group_shape(src) || !group_shape(dst) || src->storage == dst->storage || src->free_count != -1 ||
        dst->free_count != -1 || src->fixed_bytes != dst->fixed_bytes || from > src->count ||
        count > src->count - from || to > dst->count)
        return BAD;
    for (i = 0; i < count; ++i)
    {
        if (mm_group_location(src, from + i, &loc) != OK ||
            (MMU32)loc.bytes - src->fixed_bytes > LIMIT - 1 - bytes)
            return BAD;
        bytes += (MMU32)loc.bytes - src->fixed_bytes;
    }
    result = mm_group_reserve(dst, count, bytes, 0);
    if (result != OK)
        return result;
    result = mm_storage_lock(src->storage, 0);
    if (result != OK)
        return result;
    for (i = 0; i < count; ++i)
    {
        loc = location(src, from + i);
        p = loc.bytes ? src->storage->buffers[0].data + (MMU32)loc.offset : NULL;
        result = mm_group_insert(dst, to + i, (MMU32)loc.bytes - src->fixed_bytes,
                                       p ? p + src->fixed_bytes : NULL, src->fixed_bytes ? p : NULL);
        if (result != OK)
            break;
    }
    mm_storage_unlock(src->storage, 0);
    return result;
}
void mm_group_swap_locations(MMGroup *g)
{
    MMU32 i;
    unsigned j;
    unsigned char *p, x;
    for (i = 0; i < g->count * 2; ++i)
    {
        p = g->storage->buffers[1].data + i * 4;
        for (j = 0; j < 2; ++j)
        {
            x = p[j];
            p[j] = p[3 - j];
            p[3 - j] = x;
        }
    }
}

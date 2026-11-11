/* Collection mutation policy converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/collections.h"
#include <string.h>
#define LIMIT THREE_MM_STORAGE_LIMIT
#define OK THREE_MM_STORAGE_OK
#define BAD THREE_MM_STORAGE_ARGUMENT
static void trash(int enabled, void *p, ThreeMMU32 n)
{
    if (enabled && n)
        memset(p, 0xa3, n);
}
static void reverse(unsigned char *p, ThreeMMU32 n)
{
    ThreeMMU32 i;
    unsigned char x;
    for (i = 0; i < n / 2; ++i)
    {
        x = p[i];
        p[i] = p[n - 1 - i];
        p[n - 1 - i] = x;
    }
}
static void blocks(unsigned char *p, ThreeMMU32 a, ThreeMMU32 b)
{
    if (!a || !b)
        return;
    reverse(p, a);
    reverse(p + a, b);
    reverse(p, a + b);
}
static void move(unsigned char *p, ThreeMMU32 width, ThreeMMU32 source, ThreeMMU32 target)
{
    if (target > source)
        --target;
    if (source < target)
        blocks(p + source * width, width, (target - source) * width);
    else if (target < source)
        blocks(p + target * width, (source - target) * width, width);
}
static ThreeMMU32 bitmap(ThreeMMU32 count)
{
    return (count + 7) / 8;
}
static int array_shape(const ThreeMMArray *a)
{
    return a && a->storage && a->width && a->width <= 0x7fffffffUL && a->count <= (LIMIT - 1) / a->width &&
           a->count * a->width <= a->storage->buffers[0].capacity &&
           (!a->allocated || bitmap(a->count) <= a->storage->buffers[1].capacity);
}
int three_mm_array_reserve(ThreeMMArray *a, ThreeMMU32 additional, int shrink)
{
    ThreeMMU32 needed = additional, free_count;
    if (!array_shape(a))
        return BAD;
    if (a->allocated)
    {
        if (a->free_count >= 0)
        {
            free_count = (ThreeMMU32)a->free_count;
            needed = additional > free_count ? additional - free_count : 0;
        }
        else
        {
            free_count = 0 - (ThreeMMU32)a->free_count;
            if (additional > 0xffffffffUL - free_count)
                return BAD;
            needed += free_count;
        }
    }
    if (needed > (LIMIT - 1) / a->width - a->count)
        return BAD;
    needed += a->count;
    return three_mm_storage_ensure(a->storage, needed * a->width, a->allocated ? bitmap(needed) : 0, shrink);
}
int three_mm_array_insert(ThreeMMArray *a, ThreeMMU32 index, const void *data, ThreeMMU32 count)
{
    ThreeMMU32 bytes, position;
    int result;
    if (!array_shape(a) || a->allocated || index > a->count || count > (LIMIT - 1) / a->width - a->count)
        return BAD;
    if (!count)
        return OK;
    result = three_mm_array_reserve(a, count, 0);
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
int three_mm_array_delete(ThreeMMArray *a, ThreeMMU32 index, ThreeMMU32 count)
{
    unsigned char *p;
    ThreeMMU32 old;
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
            a->free_count -= (ThreeMMI32)(old - a->count);
        }
    }
    return OK;
}
int three_mm_array_set_count(ThreeMMArray *a, ThreeMMU32 count)
{
    int result;
    ThreeMMU32 lo, hi;
    if (!array_shape(a) || a->allocated || count > (LIMIT - 1) / a->width)
        return BAD;
    if (count > a->count)
    {
        result = three_mm_array_reserve(a, count - a->count, 0);
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
int three_mm_array_move(ThreeMMArray *a, ThreeMMU32 source, ThreeMMU32 target)
{
    if (!array_shape(a) || a->allocated || source >= a->count || target > a->count)
        return BAD;
    move(a->storage->buffers[0].data, a->width, source, target);
    return OK;
}
int three_mm_array_add(ThreeMMArray *a, const void *data, ThreeMMU32 *out)
{
    ThreeMMU32 i;
    int result;
    unsigned char *bits;
    if (!array_shape(a))
        return BAD;
    i = a->count;
    if (!a->allocated)
        result = three_mm_array_insert(a, i, data, 1);
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
            result = three_mm_array_reserve(a, 1, 0);
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
int three_mm_array_pop(ThreeMMArray *a, void *out)
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
    return three_mm_array_delete(a, a->count - 1, 1);
}
static ThreeMMU32 round4(ThreeMMU32 n)
{
    return (n + 3) & ~(ThreeMMU32)3;
}
static int group_shape(const ThreeMMGroup *g)
{
    return g && g->storage && g->fixed_bytes < LIMIT && g->bytes < LIMIT && g->count <= (LIMIT - 1) / 8 &&
           g->bytes <= g->storage->buffers[0].capacity && g->count * 8 <= g->storage->buffers[1].capacity;
}
static ThreeMMGroupLocation location(const ThreeMMGroup *g, ThreeMMU32 i)
{
    ThreeMMGroupLocation loc;
    memcpy(&loc, g->storage->buffers[1].data + i * 8, 8);
    return loc;
}
static void set_location(ThreeMMGroup *g, ThreeMMU32 i, ThreeMMGroupLocation loc)
{
    memcpy(g->storage->buffers[1].data + i * 8, &loc, 8);
}
int three_mm_group_location(const ThreeMMGroup *g, ThreeMMU32 index, ThreeMMGroupLocation *out)
{
    ThreeMMGroupLocation loc;
    if (!group_shape(g) || index >= g->count || !out)
        return BAD;
    loc = location(g, index);
    if (loc.offset < 0 || loc.bytes < 0 || (ThreeMMU32)loc.bytes < g->fixed_bytes ||
        (ThreeMMU32)loc.offset > g->bytes || round4((ThreeMMU32)loc.bytes) > g->bytes - (ThreeMMU32)loc.offset)
        return BAD;
    *out = loc;
    return OK;
}
int three_mm_group_reserve(ThreeMMGroup *g, ThreeMMU32 additional, ThreeMMU32 variable, int shrink)
{
    ThreeMMU32 slots, per;
    if (!group_shape(g) || variable >= LIMIT)
        return BAD;
    slots = g->free_count == -1                      ? additional
            : additional > (ThreeMMU32)g->free_count ? additional - (ThreeMMU32)g->free_count
                                                     : 0;
    per = g->fixed_bytes + 3;
    /* Retain the legacy reserve budget, including its conservative bytes term. */
    if (slots > (LIMIT - 1) / 8 - g->count || g->bytes > LIMIT / per || additional > LIMIT / per - g->bytes ||
        additional > (LIMIT - 1 - g->bytes) / per || variable > LIMIT - 1 - g->bytes - additional * per)
        return BAD;
    return three_mm_storage_ensure(g->storage, g->bytes + variable + additional * per, (g->count + slots) * 8, shrink);
}
static void adjust(ThreeMMGroup *g, ThreeMMU32 lo, ThreeMMU32 hi, ThreeMMI32 delta)
{
    ThreeMMU32 i;
    ThreeMMGroupLocation loc;
    if (g->bytes >= lo && g->bytes < hi)
        g->bytes = (ThreeMMU32)((ThreeMMI32)g->bytes + delta);
    for (i = 0; i < g->count; ++i)
    {
        loc = location(g, i);
        if (loc.offset >= 0 && (ThreeMMU32)loc.offset >= lo && (ThreeMMU32)loc.offset < hi)
        {
            loc.offset += delta;
            set_location(g, i, loc);
        }
    }
}
static void remove_bytes(ThreeMMGroup *g, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    unsigned char *p = g->storage->buffers[0].data;
    if (offset + bytes < g->bytes)
    {
        memmove(p + offset, p + offset + bytes, g->bytes - offset - bytes);
        adjust(g, offset + 1, g->bytes + 1, -(ThreeMMI32)bytes);
    }
    else
        g->bytes -= bytes;
    trash(g->trash_unused, p + g->bytes, bytes);
}
static int insert_entry(ThreeMMGroup *g, ThreeMMU32 i, ThreeMMU32 bytes, const void *data, const void *fixed,
                        int allocated)
{
    ThreeMMGroupLocation loc;
    ThreeMMU32 total, padded, new_count;
    int result;
    unsigned char *p, *entries;
    if (!group_shape(g) || bytes >= LIMIT || bytes > LIMIT - 1 - g->fixed_bytes || i > g->count)
        return BAD;
    total = bytes + g->fixed_bytes;
    padded = round4(total);
    new_count = g->count + (allocated ? i == g->count : 1);
    if (padded > LIMIT - 1 - g->bytes || new_count > (LIMIT - 1) / 8)
        return BAD;
    result = three_mm_storage_ensure(g->storage, g->bytes + padded, new_count * 8, 0);
    if (result != OK)
        return result;
    loc.offset = total ? (ThreeMMI32)g->bytes : 0;
    loc.bytes = (ThreeMMI32)total;
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
int three_mm_group_insert(ThreeMMGroup *g, ThreeMMU32 i, ThreeMMU32 bytes, const void *data, const void *fixed)
{
    if (!g || g->free_count != -1)
        return BAD;
    return insert_entry(g, i, bytes, data, fixed, 0);
}
int three_mm_group_add(ThreeMMGroup *g, ThreeMMU32 bytes, const void *data, const void *fixed, ThreeMMU32 *out)
{
    ThreeMMU32 i;
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
int three_mm_group_delete(ThreeMMGroup *g, ThreeMMU32 i)
{
    ThreeMMGroupLocation loc, free_loc;
    ThreeMMU32 old;
    if (three_mm_group_location(g, i, &loc) != OK)
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
        remove_bytes(g, (ThreeMMU32)loc.offset, round4((ThreeMMU32)loc.bytes));
    return OK;
}
int three_mm_group_move(ThreeMMGroup *g, ThreeMMU32 source, ThreeMMU32 target)
{
    if (!group_shape(g) || g->free_count != -1 || source >= g->count || target > g->count)
        return BAD;
    move(g->storage->buffers[1].data, 8, source, target);
    return OK;
}
int three_mm_group_swap(ThreeMMGroup *g, ThreeMMU32 a, ThreeMMU32 b)
{
    ThreeMMGroupLocation first, second;
    if (!group_shape(g) || g->free_count != -1 || a >= g->count || b >= g->count)
        return BAD;
    first = location(g, a);
    second = location(g, b);
    set_location(g, a, second);
    set_location(g, b, first);
    return OK;
}
int three_mm_group_insert_bytes(ThreeMMGroup *g, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes, const void *data)
{
    ThreeMMGroupLocation loc;
    ThreeMMU32 extra, end;
    unsigned char *p;
    int result;
    if (three_mm_group_location(g, i, &loc) != OK || offset > (ThreeMMU32)loc.bytes - g->fixed_bytes ||
        bytes > LIMIT - 1 - (ThreeMMU32)loc.bytes)
        return BAD;
    if (!bytes)
        return OK;
    offset += g->fixed_bytes;
    if (!loc.bytes)
        loc.offset = (ThreeMMI32)g->bytes;
    extra = round4((ThreeMMU32)loc.bytes + bytes) - round4((ThreeMMU32)loc.bytes);
    if (extra > LIMIT - 1 - g->bytes)
        return BAD;
    if (extra)
    {
        result = three_mm_storage_ensure(g->storage, g->bytes + extra, g->count * 8, 0);
        if (result != OK)
            return result;
        end = (ThreeMMU32)loc.offset + round4((ThreeMMU32)loc.bytes);
        if (end < g->bytes)
        {
            p = g->storage->buffers[0].data;
            memmove(p + end + extra, p + end, g->bytes - end);
            adjust(g, (ThreeMMU32)loc.offset + 1, g->bytes + 1, (ThreeMMI32)extra);
        }
        else
            g->bytes += extra;
    }
    p = g->storage->buffers[0].data + (ThreeMMU32)loc.offset + offset;
    if (offset < (ThreeMMU32)loc.bytes)
        memmove(p + bytes, p, (ThreeMMU32)loc.bytes - offset);
    if (data)
        memcpy(p, data, bytes);
    else
        trash(g->trash_unused, p, bytes);
    loc.bytes += (ThreeMMI32)bytes;
    set_location(g, i, loc);
    return OK;
}
int three_mm_group_delete_bytes(ThreeMMGroup *g, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    ThreeMMGroupLocation loc;
    ThreeMMU32 remove;
    unsigned char *p;
    if (three_mm_group_location(g, i, &loc) != OK || offset > (ThreeMMU32)loc.bytes - g->fixed_bytes ||
        bytes > (ThreeMMU32)loc.bytes - g->fixed_bytes - offset)
        return BAD;
    if (!bytes)
        return OK;
    offset += g->fixed_bytes;
    p = g->storage->buffers[0].data + (ThreeMMU32)loc.offset + offset;
    if (offset + bytes < (ThreeMMU32)loc.bytes)
        memmove(p, p + bytes, (ThreeMMU32)loc.bytes - offset - bytes);
    remove = round4((ThreeMMU32)loc.bytes) - round4((ThreeMMU32)loc.bytes - bytes);
    if (remove)
        remove_bytes(g, (ThreeMMU32)loc.offset + (ThreeMMU32)loc.bytes - bytes, remove);
    loc = location(g, i);
    loc.bytes -= (ThreeMMI32)bytes;
    if (!loc.bytes)
        loc.offset = 0;
    set_location(g, i, loc);
    return OK;
}
int three_mm_group_put(ThreeMMGroup *g, ThreeMMU32 i, ThreeMMU32 bytes, const void *data)
{
    ThreeMMGroupLocation loc;
    ThreeMMU32 old;
    int result = OK;
    if (three_mm_group_location(g, i, &loc) != OK)
        return BAD;
    old = (ThreeMMU32)loc.bytes - g->fixed_bytes;
    if (bytes > old)
        result = three_mm_group_insert_bytes(g, i, old, bytes - old, NULL);
    else if (bytes < old)
        result = three_mm_group_delete_bytes(g, i, bytes, old - bytes);
    if (result != OK)
        return result;
    if (data && bytes)
    {
        loc = location(g, i);
        memcpy(g->storage->buffers[0].data + (ThreeMMU32)loc.offset + g->fixed_bytes, data, bytes);
    }
    return OK;
}
int three_mm_group_move_bytes(ThreeMMGroup *g, ThreeMMU32 source, ThreeMMU32 from, ThreeMMU32 target, ThreeMMU32 to,
                              ThreeMMU32 bytes)
{
    ThreeMMGroupLocation src, dst, loc;
    ThreeMMI32 delta;
    ThreeMMU32 main, a, b;
    int result;
    unsigned char tail[4], *p;
    if (three_mm_group_location(g, source, &src) != OK || three_mm_group_location(g, target, &dst) != OK ||
        from > (ThreeMMU32)src.bytes - g->fixed_bytes || bytes > (ThreeMMU32)src.bytes - g->fixed_bytes - from ||
        to > (ThreeMMU32)dst.bytes - g->fixed_bytes || bytes > LIMIT - 1 - (ThreeMMU32)dst.bytes)
        return BAD;
    main = bytes & ~(ThreeMMU32)3;
    a = (ThreeMMU32)src.offset + from + g->fixed_bytes;
    b = (ThreeMMU32)dst.offset + to + g->fixed_bytes;
    if (source == target && a < b && b - a < main)
        return BAD;
    delta = (ThreeMMI32)(round4((ThreeMMU32)dst.bytes + bytes) - round4((ThreeMMU32)dst.bytes)) -
            (ThreeMMI32)(round4((ThreeMMU32)src.bytes) - round4((ThreeMMU32)src.bytes - bytes));
    if (delta > 0)
    {
        if ((ThreeMMU32)delta > LIMIT - 1 - g->bytes)
            return BAD;
        result = three_mm_storage_ensure(g->storage, g->bytes + (ThreeMMU32)delta, g->count * 8, 0);
        if (result != OK)
            return result;
    }
    if (main)
    {
        loc = location(g, source);
        loc.bytes -= (ThreeMMI32)main;
        if (!loc.bytes)
            loc.offset = 0;
        set_location(g, source, loc);
        loc = location(g, target);
        if (!loc.bytes)
        {
            loc.offset = (ThreeMMI32)g->bytes;
            b = g->bytes;
        }
        loc.bytes += (ThreeMMI32)main;
        set_location(g, target, loc);
        p = g->storage->buffers[0].data;
        if (a < b)
        {
            blocks(p + a, main, b - a - main);
            adjust(g, (ThreeMMU32)src.offset + 1, (ThreeMMU32)dst.offset + 1, -(ThreeMMI32)main);
        }
        else
        {
            blocks(p + b, a - b, main);
            adjust(g, (ThreeMMU32)dst.offset + 1, (ThreeMMU32)src.offset + 1, (ThreeMMI32)main);
        }
    }
    if (bytes > main)
    {
        loc = location(g, source);
        memcpy(tail, g->storage->buffers[0].data + (ThreeMMU32)loc.offset + g->fixed_bytes + from, bytes - main);
        result = three_mm_group_delete_bytes(g, source, from, bytes - main);
        if (result != OK)
            return result;
        return three_mm_group_insert_bytes(g, target, to + main, bytes - main, tail);
    }
    return OK;
}
int three_mm_group_merge(ThreeMMGroup *g, ThreeMMU32 source, ThreeMMU32 target)
{
    ThreeMMGroupLocation src, dst;
    ThreeMMU32 bytes, main, end;
    unsigned char tail[4];
    int result;
    if (source == target || three_mm_group_location(g, source, &src) != OK ||
        three_mm_group_location(g, target, &dst) != OK)
        return BAD;
    bytes = (ThreeMMU32)src.bytes - g->fixed_bytes;
    main = bytes & ~(ThreeMMU32)3;
    end = (ThreeMMU32)dst.bytes - g->fixed_bytes;
    if (bytes > main)
        memcpy(tail, g->storage->buffers[0].data + (ThreeMMU32)src.offset + g->fixed_bytes + main, bytes - main);
    if (main)
    {
        result = three_mm_group_move_bytes(g, source, 0, target, end, main);
        if (result != OK)
            return result;
    }
    result = three_mm_group_delete(g, source);
    if (result != OK)
        return result;
    if (source < target)
        --target; /* Preserve VirtualGroup's legacy dense-index adjustment. */
    return bytes > main ? three_mm_group_insert_bytes(g, target, end + main, bytes - main, tail) : OK;
}
int three_mm_group_copy_entries(ThreeMMGroup *dst, ThreeMMGroup *src, ThreeMMU32 from, ThreeMMU32 to, ThreeMMU32 count)
{
    ThreeMMU32 i, bytes = 0;
    ThreeMMGroupLocation loc;
    const unsigned char *p;
    int result;
    if (!group_shape(src) || !group_shape(dst) || src->storage == dst->storage || src->free_count != -1 ||
        dst->free_count != -1 || src->fixed_bytes != dst->fixed_bytes || from > src->count ||
        count > src->count - from || to > dst->count)
        return BAD;
    for (i = 0; i < count; ++i)
    {
        if (three_mm_group_location(src, from + i, &loc) != OK ||
            (ThreeMMU32)loc.bytes - src->fixed_bytes > LIMIT - 1 - bytes)
            return BAD;
        bytes += (ThreeMMU32)loc.bytes - src->fixed_bytes;
    }
    result = three_mm_group_reserve(dst, count, bytes, 0);
    if (result != OK)
        return result;
    result = three_mm_storage_lock(src->storage, 0);
    if (result != OK)
        return result;
    for (i = 0; i < count; ++i)
    {
        loc = location(src, from + i);
        p = loc.bytes ? src->storage->buffers[0].data + (ThreeMMU32)loc.offset : NULL;
        result = three_mm_group_insert(dst, to + i, (ThreeMMU32)loc.bytes - src->fixed_bytes,
                                       p ? p + src->fixed_bytes : NULL, src->fixed_bytes ? p : NULL);
        if (result != OK)
            break;
    }
    three_mm_storage_unlock(src->storage, 0);
    return result;
}
void three_mm_group_swap_locations(ThreeMMGroup *g)
{
    ThreeMMU32 i;
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

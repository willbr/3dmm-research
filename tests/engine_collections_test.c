#include "three_mm/collections.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "collection check %d: %s\n", __LINE__, #x);                                                \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
#define OK THREE_MM_STORAGE_OK
typedef struct Heap
{
    int fail, live;
} Heap;
static void *resize_test(void *context, void *data, ThreeMMU32 old_bytes, ThreeMMU32 new_bytes, int priority)
{
    Heap *h = (Heap *)context;
    unsigned char *p;
    (void)priority;
    if (h->fail)
        return NULL;
    p = (unsigned char *)malloc(new_bytes);
    CHECK(p);
    memset(p, 0xa3, new_bytes);
    if (data)
    {
        memcpy(p, data, old_bytes < new_bytes ? old_bytes : new_bytes);
        free(data);
    }
    else
        ++h->live;
    return p;
}
static void release_test(void *context, void *data, ThreeMMU32 bytes)
{
    Heap *h = (Heap *)context;
    (void)bytes;
    --h->live;
    free(data);
}
static void init(ThreeMMCollectionStorage *s, Heap *h)
{
    ThreeMMStorageAllocator allocator;
    memset(h, 0, sizeof(*h));
    allocator.context = h;
    allocator.resize = resize_test;
    allocator.release = release_test;
    CHECK(three_mm_storage_init(s, &allocator) == OK);
}
static void word(unsigned char *p, ThreeMMU32 value, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(value >> (8 * i));
}
static void headers(void)
{
    int kind, big;
    unsigned char wire[96], written[20], saved[sizeof(ThreeMMCollectionHeader)];
    ThreeMMMemoryStream stream;
    ThreeMMCollectionHeader h;
    ThreeMMU32 n, first, second;
    for (kind = 0; kind < 3; ++kind)
        for (big = 0; big < 2; ++big)
        {
            memset(wire, 0xcc, sizeof(wire));
            n = kind == 2 ? 20 : kind == 0 ? 12 : 16;
            wire[0] = (unsigned char)(big ? 0 : 1);
            wire[1] = (unsigned char)(big ? 1 : 0);
            wire[2] = (unsigned char)(big ? 0x12 : 0x34);
            wire[3] = (unsigned char)(big ? 0x34 : 0x12);
            if (kind == 2)
            {
                word(wire + 4, 2, big);
                word(wire + 8, 16, big);
                word(wire + 12, 0xffffffffUL, big);
                word(wire + 16, 1, big);
                first = 16;
                second = 16;
            }
            else
            {
                word(wire + 4, 3, big);
                word(wire + 8, 9, big);
                if (kind == 1)
                    word(wire + 12, 3, big);
                first = 27;
                second = kind == 1 ? 2 : 0;
            }
            CHECK(three_mm_memory_stream_init(&stream, wire, n + first + second));
            CHECK(three_mm_collection_header_read(&stream.stream, kind, 0, &h) == OK);
            CHECK(h.kind == kind && h.big_endian == big && h.system == 0x1234 && h.first_bytes == first &&
                  h.second_bytes == second);
            CHECK(three_mm_collection_header_write(&h, written, sizeof(written)) == OK && !memcmp(wire, written, n));
            memcpy(saved, &h, sizeof(h));
            wire[0] = wire[1] = 0x99;
            CHECK(three_mm_collection_header_read(&stream.stream, kind, 0, &h) != OK && !memcmp(saved, &h, sizeof(h)));
        }
    memset(wire, 0, sizeof(wire));
    wire[0] = 1;
    wire[2] = wire[3] = 3;
    word(wire + 4, 1, 0);
    word(wire + 8, 1, 0);
    word(wire + 12, 0xfffffff9UL, 0);
    CHECK(three_mm_memory_stream_init(&stream, wire, 18));
    CHECK(three_mm_collection_header_read(&stream.stream, THREE_MM_COLLECTION_ALLOCATED_ARRAY, 0, &h) == OK &&
          h.free_count == -7);
    word(wire + 4, 0x7fffffffUL, 0);
    word(wire + 8, 0x40000000UL, 0);
    CHECK(three_mm_collection_header_read(&stream.stream, THREE_MM_COLLECTION_ALLOCATED_ARRAY, 0, &h) != OK);
    word(wire + 8, 0, 0);
    CHECK(three_mm_memory_stream_init(&stream, wire, 12));
    CHECK(three_mm_collection_header_read(&stream.stream, THREE_MM_COLLECTION_ARRAY, 0, &h) == OK &&
          h.width == 0x7fffffffUL);
    memset(wire + 4, 0, 16);
    word(wire + 12, 0xffffffffUL, 0);
    CHECK(three_mm_memory_stream_init(&stream, wire, 20));
    CHECK(three_mm_collection_header_read(&stream.stream, THREE_MM_COLLECTION_GROUP, 0, &h) == OK);
    word(wire + 12, 0, 0);
    CHECK(three_mm_collection_header_read(&stream.stream, THREE_MM_COLLECTION_GROUP, 1, &h) != OK);
    /* Empty allocated groups have always been writable but rejected by reading. */
    h.free_count = 0;
    CHECK(three_mm_collection_header_write(&h, written, sizeof(written)) == OK);
}
static void arrays(void)
{
    ThreeMMCollectionStorage s;
    ThreeMMArray a;
    Heap heap;
    ThreeMMU32 i, pos;
    unsigned values[] = {10, 20, 30, 40}, got;
    unsigned char before[64], bitmap_byte;
    init(&s, &heap);
    memset(&a, 0, sizeof(a));
    a.storage = &s;
    a.width = sizeof(unsigned);
    a.trash_unused = 1;
    CHECK(three_mm_array_insert(&a, 0, values, 4) == OK);
    CHECK(three_mm_array_move(&a, 0, 4) == OK);
    memcpy(&got, s.buffers[0].data + 3 * a.width, a.width);
    CHECK(got == 10);
    CHECK(three_mm_array_move(&a, 3, 0) == OK);
    CHECK(three_mm_array_delete(&a, 1, 2) == OK && a.count == 2);
    memcpy(&got, s.buffers[0].data + a.width, a.width);
    CHECK(got == 40);
    memcpy(before, s.buffers[0].data, a.count * a.width);
    heap.fail = 1;
    CHECK(three_mm_array_set_count(&a, 100) != OK && a.count == 2 &&
          !memcmp(before, s.buffers[0].data, a.count * a.width));
    heap.fail = 0;
    CHECK(three_mm_array_pop(&a, &got) == OK && got == 40);
    CHECK(three_mm_array_pop(&a, &got) == OK && got == 10);
    CHECK(three_mm_array_pop(&a, &got) != OK);
    CHECK(three_mm_storage_dispose(&s) == OK && !heap.live);
    init(&s, &heap);
    memset(&a, 0, sizeof(a));
    a.storage = &s;
    a.width = 1;
    a.allocated = 1;
    a.trash_unused = 1;
    for (i = 0; i < 70; ++i)
    {
        unsigned char b = (unsigned char)i;
        CHECK(three_mm_array_add(&a, &b, &pos) == OK && pos == i);
    }
    bitmap_byte = s.buffers[1].data[8];
    CHECK((bitmap_byte & 0x3f) == 0x3f); /* Only six of the last byte's bits are used. */
    CHECK(three_mm_array_delete(&a, 8, 1) == OK && three_mm_array_delete(&a, 63, 1) == OK);
    CHECK(a.free_count == 2);
    {
        unsigned char b = 222;
        CHECK(three_mm_array_add(&a, &b, &pos) == OK && pos == 8);
        CHECK(s.buffers[0].data[8] == 222);
    }
    CHECK(three_mm_array_delete(&a, 69, 1) == OK && a.count == 69);
    CHECK((s.buffers[1].data[8] & 0xc0) == (bitmap_byte & 0xc0));
    CHECK(three_mm_array_delete(&a, 68, 1) == OK);
    CHECK(three_mm_array_delete(&a, 67, 1) == OK);
    CHECK(three_mm_array_delete(&a, 66, 1) == OK);
    CHECK(three_mm_array_delete(&a, 65, 1) == OK);
    CHECK(three_mm_array_delete(&a, 64, 1) == OK && a.count == 63 && a.free_count == 0);
    CHECK(three_mm_storage_dispose(&s) == OK && !heap.live);
}
typedef struct Item
{
    unsigned char data[64], fixed[8];
    ThreeMMU32 length;
    int used;
} Item;
static ThreeMMU32 random_state = 0x12345678UL;
static ThreeMMU32 pick(ThreeMMU32 n)
{
    random_state = random_state * 1664525UL + 1013904223UL;
    return n ? random_state % n : 0;
}
static void check_group(const ThreeMMGroup *g, const Item *items, ThreeMMU32 count)
{
    ThreeMMU32 i, total = 0, free_count = 0;
    ThreeMMGroupLocation loc;
    CHECK(g->count == count);
    for (i = 0; i < count; ++i)
    {
        if (!items[i].used)
        {
            CHECK(three_mm_group_location(g, i, &loc) != OK);
            ++free_count;
            continue;
        }
        CHECK(three_mm_group_location(g, i, &loc) == OK);
        CHECK((ThreeMMU32)loc.bytes == items[i].length + g->fixed_bytes);
        if (g->fixed_bytes)
            CHECK(!memcmp(g->storage->buffers[0].data + loc.offset, items[i].fixed, g->fixed_bytes));
        if (items[i].length)
            CHECK(!memcmp(g->storage->buffers[0].data + loc.offset + g->fixed_bytes, items[i].data, items[i].length));
        total += ((ThreeMMU32)loc.bytes + 3) & ~(ThreeMMU32)3;
    }
    CHECK(total == g->bytes);
    CHECK(g->free_count == -1 ? !free_count : (ThreeMMU32)g->free_count == free_count);
}
static void group_model(unsigned fixed, int allocated)
{
    ThreeMMCollectionStorage s;
    ThreeMMGroup g;
    Heap heap;
    Item items[16], item, merged;
    ThreeMMU32 count = 0, iteration, i, j, op, n, offset, target, actual, k;
    int result;
    init(&s, &heap);
    memset(&g, 0, sizeof(g));
    memset(items, 0, sizeof(items));
    g.storage = &s;
    g.fixed_bytes = fixed;
    g.free_count = allocated ? 0 : -1;
    g.trash_unused = 1;
    for (iteration = 0; iteration < 800; ++iteration)
    {
        op = pick(8);
        i = pick(count);
        if (!count || (op == 0 && count < 15))
        {
            memset(&item, 0, sizeof(item));
            item.length = pick(17);
            item.used = 1;
            for (j = 0; j < item.length; ++j)
                item.data[j] = (unsigned char)pick(256);
            for (j = 0; j < fixed; ++j)
                item.fixed[j] = (unsigned char)pick(256);
            if (allocated)
            {
                for (i = 0; i < count && items[i].used; ++i)
                {
                }
                CHECK(three_mm_group_add(&g, item.length, item.data, item.fixed, &actual) == OK && actual == i);
                if (i == count)
                    ++count;
            }
            else
            {
                i = pick(count + 1);
                CHECK(three_mm_group_insert(&g, i, item.length, item.data, item.fixed) == OK);
                memmove(items + i + 1, items + i, (count - i) * sizeof(Item));
                ++count;
            }
            items[i] = item;
        }
        else if (!items[i].used)
            continue;
        else if (op == 1)
        {
            CHECK(three_mm_group_delete(&g, i) == OK);
            if (allocated)
            {
                items[i].used = 0;
                while (count && !items[count - 1].used)
                    --count;
            }
            else
            {
                --count;
                memmove(items + i, items + i + 1, (count - i) * sizeof(Item));
            }
        }
        else if (op == 2)
        {
            n = pick(25);
            for (j = 0; j < n; ++j)
                item.data[j] = (unsigned char)pick(256);
            CHECK(three_mm_group_put(&g, i, n, item.data) == OK);
            memcpy(items[i].data, item.data, n);
            items[i].length = n;
        }
        else if (op == 3 && items[i].length < 40)
        {
            n = pick(5);
            offset = pick(items[i].length + 1);
            for (j = 0; j < n; ++j)
                item.data[j] = (unsigned char)pick(256);
            CHECK(three_mm_group_insert_bytes(&g, i, offset, n, item.data) == OK);
            memmove(items[i].data + offset + n, items[i].data + offset, items[i].length - offset);
            memcpy(items[i].data + offset, item.data, n);
            items[i].length += n;
        }
        else if (op == 4)
        {
            offset = pick(items[i].length + 1);
            n = pick(items[i].length - offset + 1);
            CHECK(three_mm_group_delete_bytes(&g, i, offset, n) == OK);
            memmove(items[i].data + offset, items[i].data + offset + n, items[i].length - offset - n);
            items[i].length -= n;
        }
        else if (op == 5 && !allocated)
        {
            target = pick(count + 1);
            CHECK(three_mm_group_move(&g, i, target) == OK);
            item = items[i];
            if (target > i)
                --target;
            if (i < target)
                memmove(items + i, items + i + 1, (target - i) * sizeof(Item));
            else if (target < i)
                memmove(items + target + 1, items + target, (i - target) * sizeof(Item));
            items[target] = item;
        }
        else if (op == 6 && count > 1)
        {
            target = pick(count);
            if (target == i || !items[target].used)
                continue;
            offset = pick(items[i].length + 1);
            n = pick(items[i].length - offset + 1);
            k = pick(items[target].length + 1);
            if (items[target].length + n > 60 || (!fixed && !items[target].length && n >= 4))
                continue;
            result = three_mm_group_move_bytes(&g, i, offset, target, k, n);
            CHECK(result == OK);
            memmove(items[target].data + k + n, items[target].data + k, items[target].length - k);
            memcpy(items[target].data + k, items[i].data + offset, n);
            items[target].length += n;
            memmove(items[i].data + offset, items[i].data + offset + n, items[i].length - offset - n);
            items[i].length -= n;
        }
        else if (op == 7 && !allocated && count > 1)
        {
            target = pick(count);
            if (target == i || items[i].length + items[target].length > 60 ||
                (!fixed && !items[target].length && items[i].length >= 4))
                continue;
            merged = items[target];
            memcpy(merged.data + merged.length, items[i].data, items[i].length);
            merged.length += items[i].length;
            CHECK(three_mm_group_merge(&g, i, target) == OK);
            --count;
            memmove(items + i, items + i + 1, (count - i) * sizeof(Item));
            if (i < target)
                --target;
            items[target] = merged;
        }
        check_group(&g, items, count);
    }
    CHECK(three_mm_storage_dispose(&s) == OK && !heap.live);
}
static void group_failures(void)
{
    ThreeMMCollectionStorage s;
    ThreeMMGroup g;
    ThreeMMGroupLocation before, after;
    Heap heap;
    unsigned char fixed = 1, snapshot[16];
    ThreeMMU32 index;
    init(&s, &heap);
    memset(&g, 0, sizeof(g));
    g.storage = &s;
    g.fixed_bytes = 1;
    g.free_count = 0;
    g.trash_unused = 1;
    CHECK(three_mm_group_add(&g, 1, "a", &fixed, &index) == OK);
    CHECK(three_mm_group_add(&g, 3, "xyz", &fixed, &index) == OK);
    CHECK(three_mm_group_location(&g, 0, &before) == OK);
    memcpy(snapshot, s.buffers[0].data, g.bytes);
    heap.fail = 1;
    CHECK(three_mm_group_move_bytes(&g, 0, 0, 1, 0, 1) == THREE_MM_STORAGE_MEMORY);
    CHECK(g.bytes == 8 && g.count == 2 && !memcmp(snapshot, s.buffers[0].data, g.bytes));
    CHECK(three_mm_group_location(&g, 0, &after) == OK && !memcmp(&before, &after, sizeof(before)));
    CHECK(three_mm_group_delete(&g, 0) == OK && g.free_count == 1);
    CHECK(three_mm_group_add(&g, 20, "abcdefghijklmnopqrst", &fixed, &index) != OK && g.free_count == 1 &&
          g.count == 2);
    heap.fail = 0;
    CHECK(three_mm_group_add(&g, 1, "b", &fixed, &index) == OK && index == 0);
    CHECK(three_mm_storage_dispose(&s) == OK && !heap.live);
}
int main(void)
{
    unsigned fixed;
    headers();
    arrays();
    group_failures();
    for (fixed = 0; fixed <= 7; ++fixed)
    {
        group_model(fixed, 0);
        group_model(fixed, 1);
    }
    return 0;
}

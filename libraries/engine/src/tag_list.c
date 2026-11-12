/* Tag list converted from engine tagl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/tag.h"
#include <string.h>
static ThreeMMGroup view(ThreeMMTagList *list)
{
    ThreeMMGroup g;
    memset(&g, 0, sizeof(g));
    g.storage = &list->storage;
    g.fixed_bytes = sizeof(ThreeMMTagCached);
    g.count = list->count;
    g.bytes = list->bytes;
    g.free_count = -1;
    return g;
}
static int apply(ThreeMMTagList *list, const ThreeMMGroup *g, int result)
{
    list->count = g->count;
    list->bytes = g->bytes;
    return result;
}
int three_mm_tag_list_init(ThreeMMTagList *list, const ThreeMMStorageAllocator *allocator)
{
    if (!list)
        return THREE_MM_STORAGE_ARGUMENT;
    memset(list, 0, sizeof(*list));
    if (three_mm_storage_init(&list->storage, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_STORAGE_ARGUMENT;
    list->storage.min_grow[0] = sizeof(ThreeMMTagCached) * 16;
    list->storage.min_grow[1] = 128;
    return THREE_MM_STORAGE_OK;
}
int three_mm_tag_list_dispose(ThreeMMTagList *list)
{
    int result = three_mm_storage_dispose(&list->storage);
    if (result == THREE_MM_STORAGE_OK)
        list->count = list->bytes = 0;
    return result;
}
int three_mm_tag_list_get(ThreeMMTagList *list, ThreeMMU32 i, ThreeMMTagCached *out)
{
    ThreeMMGroup g = view(list);
    ThreeMMGroupLocation loc;
    int result = three_mm_group_location(&g, i, &loc);
    if (result != THREE_MM_STORAGE_OK || !out)
        return THREE_MM_STORAGE_ARGUMENT;
    memcpy(out, list->storage.buffers[0].data + loc.offset, sizeof(*out));
    return THREE_MM_STORAGE_OK;
}
static int find(ThreeMMTagList *list, const ThreeMMTag *tag, ThreeMMU32 *position)
{
    ThreeMMU32 low = 0, high = list->count, i;
    ThreeMMTagCached e;
    int order;
    while (low < high)
    {
        i = low + (high - low) / 2;
        three_mm_tag_list_get(list, i, &e);
        order = tag->sid != e.tag.sid         ? (tag->sid < e.tag.sid ? -1 : 1)
                : tag->type != e.tag.type     ? (tag->type < e.tag.type ? -1 : 1)
                : tag->number != e.tag.number ? (tag->number < e.tag.number ? -1 : 1)
                                              : 0;
        if (order < 0)
            high = i;
        else if (order > 0)
            low = i + 1;
        else
        {
            *position = i;
            return 1;
        }
    }
    *position = low;
    return 0;
}
int three_mm_tag_list_insert(ThreeMMTagList *list, const ThreeMMTag *tag, int children)
{
    ThreeMMGroup g = view(list);
    ThreeMMGroupLocation loc;
    ThreeMMTagCached e;
    ThreeMMU32 i;
    if (!find(list, tag, &i))
    {
        memset(&e, 0, sizeof(e));
        e.tag = *tag;
        e.children = children != 0;
        return apply(list, &g, three_mm_group_insert(&g, i, 0, NULL, &e));
    }
    three_mm_tag_list_get(list, i, &e);
    if (!e.children && children)
    {
        e.children = 1;
        three_mm_group_location(&g, i, &loc);
        memcpy(list->storage.buffers[0].data + loc.offset, &e, sizeof(e));
    }
    return THREE_MM_STORAGE_OK;
}
int three_mm_tag_list_child(ThreeMMTagList *list, const ThreeMMTag *tag, ThreeMMU32 slot, ThreeMMU32 type)
{
    ThreeMMGroup g = view(list);
    ThreeMMGroupLocation loc;
    ThreeMMTagChild child, next;
    ThreeMMU32 i, count, at;
    if (!find(list, tag, &i))
        return THREE_MM_TAG_PARENT_NOT_FOUND;
    three_mm_group_location(&g, i, &loc);
    count = ((ThreeMMU32)loc.bytes - g.fixed_bytes) / sizeof(child);
    child.slot = slot;
    child.type = type;
    for (at = 0; at < count; ++at)
    {
        memcpy(&next, list->storage.buffers[0].data + loc.offset + g.fixed_bytes + at * sizeof(child), sizeof(child));
        if (next.type > type || (next.type == type && next.slot > slot))
            break;
    }
    return apply(list, &g, three_mm_group_insert_bytes(&g, i, at * sizeof(child), sizeof(child), &child));
}
int three_mm_tag_list_cache(ThreeMMTagList *list, const ThreeMMTagListServices *s)
{
    ThreeMMGroup g;
    ThreeMMGroupLocation loc;
    ThreeMMTagCached e;
    ThreeMMTagChild child;
    ThreeMMTag tag;
    ThreeMMU32 i, at, count;
    for (i = 0; i < list->count; ++i)
    {
        three_mm_tag_list_get(list, i, &e);
        if (!s->cache(s->context, &e.tag, e.children))
            return 0;
        g = view(list);
        three_mm_group_location(&g, i, &loc);
        count = ((ThreeMMU32)loc.bytes - g.fixed_bytes) / sizeof(child);
        for (at = 0; at < count; ++at)
        {
            memcpy(&child, list->storage.buffers[0].data + loc.offset + g.fixed_bytes + at * sizeof(child),
                   sizeof(child));
            memset(&tag, 0, sizeof(tag));
            if (!s->child(s->context, &e.tag, child.slot, child.type, &tag) || !s->cache(s->context, &tag, 1))
                return 0;
        }
    }
    return 1;
}

/* Tag list converted from engine tagl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/tag.h"
#include <string.h>
static MMGroup view(MMTagList *list)
{
    MMGroup g;
    memset(&g, 0, sizeof(g));
    g.storage = &list->storage;
    g.fixed_bytes = sizeof(MMTagCached);
    g.count = list->count;
    g.bytes = list->bytes;
    g.free_count = -1;
    return g;
}
static int apply(MMTagList *list, const MMGroup *g, int result)
{
    list->count = g->count;
    list->bytes = g->bytes;
    return result;
}
int mm_tag_list_init(MMTagList *list, const MMStorageAllocator *allocator)
{
    if (!list)
        return MM_STORAGE_ARGUMENT;
    memset(list, 0, sizeof(*list));
    if (mm_storage_init(&list->storage, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    list->storage.min_grow[0] = sizeof(MMTagCached) * 16;
    list->storage.min_grow[1] = 128;
    return MM_STORAGE_OK;
}
int mm_tag_list_dispose(MMTagList *list)
{
    int result = mm_storage_dispose(&list->storage);
    if (result == MM_STORAGE_OK)
        list->count = list->bytes = 0;
    return result;
}
int mm_tag_list_get(MMTagList *list, MMU32 i, MMTagCached *out)
{
    MMGroup g = view(list);
    MMGroupLocation loc;
    int result = mm_group_location(&g, i, &loc);
    if (result != MM_STORAGE_OK || !out)
        return MM_STORAGE_ARGUMENT;
    memcpy(out, list->storage.buffers[0].data + loc.offset, sizeof(*out));
    return MM_STORAGE_OK;
}
static int find(MMTagList *list, const MMTag *tag, MMU32 *position)
{
    MMU32 low = 0, high = list->count, i;
    MMTagCached e;
    int order;
    while (low < high)
    {
        i = low + (high - low) / 2;
        mm_tag_list_get(list, i, &e);
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
int mm_tag_list_insert(MMTagList *list, const MMTag *tag, int children)
{
    MMGroup g = view(list);
    MMGroupLocation loc;
    MMTagCached e;
    MMU32 i;
    if (!find(list, tag, &i))
    {
        memset(&e, 0, sizeof(e));
        e.tag = *tag;
        e.children = children != 0;
        return apply(list, &g, mm_group_insert(&g, i, 0, NULL, &e));
    }
    mm_tag_list_get(list, i, &e);
    if (!e.children && children)
    {
        e.children = 1;
        mm_group_location(&g, i, &loc);
        memcpy(list->storage.buffers[0].data + loc.offset, &e, sizeof(e));
    }
    return MM_STORAGE_OK;
}
int mm_tag_list_child(MMTagList *list, const MMTag *tag, MMU32 slot, MMU32 type)
{
    MMGroup g = view(list);
    MMGroupLocation loc;
    MMTagChild child, next;
    MMU32 i, count, at;
    if (!find(list, tag, &i))
        return MM_TAG_PARENT_NOT_FOUND;
    mm_group_location(&g, i, &loc);
    count = ((MMU32)loc.bytes - g.fixed_bytes) / sizeof(child);
    child.slot = slot;
    child.type = type;
    for (at = 0; at < count; ++at)
    {
        memcpy(&next, list->storage.buffers[0].data + loc.offset + g.fixed_bytes + at * sizeof(child), sizeof(child));
        if (next.type > type || (next.type == type && next.slot > slot))
            break;
    }
    return apply(list, &g, mm_group_insert_bytes(&g, i, at * sizeof(child), sizeof(child), &child));
}
int mm_tag_list_cache(MMTagList *list, const MMTagListServices *s)
{
    MMGroup g;
    MMGroupLocation loc;
    MMTagCached e;
    MMTagChild child;
    MMTag tag;
    MMU32 i, at, count;
    for (i = 0; i < list->count; ++i)
    {
        mm_tag_list_get(list, i, &e);
        if (!s->cache(s->context, &e.tag, e.children))
            return 0;
        g = view(list);
        mm_group_location(&g, i, &loc);
        count = ((MMU32)loc.bytes - g.fixed_bytes) / sizeof(child);
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

/* Tag policy converted from engine tagman.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/tag.h"
static void publish(const MMTagServices *s, const MMTag *tag)
{
    if (s->publish)
        s->publish(s->context, tag);
}
int mm_tag_open(MMTag *tag, void *destination, void *source, const MMTagServices *s)
{
    MMU32 number;
    if (tag->sid)
        return 1;
    if (source && source != s->file(destination))
    {
        if (!s->copy(source, tag->type, tag->number, s->file(destination), &number))
        {
            tag->cache = NULL;
            publish(s, tag);
            return 0;
        }
        tag->number = number;
    }
    tag->cache = destination;
    publish(s, tag);
    s->retain(destination);
    return 1;
}
int mm_tag_save(MMTag *tag, void *destination, int redirect, const MMTagServices *s)
{
    MMU32 number;
    if (tag->sid)
        return 1;
    if (!s->copy_cached(tag->cache, tag->type, tag->number, destination, &number))
        return 0;
    if (redirect)
    {
        s->retain(destination);
        s->release(tag->cache);
        tag->cache = destination;
        tag->number = number;
        publish(s, tag);
    }
    return 1;
}
void mm_tag_dup(const MMTag *tag, const MMTagServices *s)
{
    if (!tag->sid)
        s->retain(tag->cache);
}
void mm_tag_close(MMTag *tag, const MMTagServices *s)
{
    if (!tag->sid)
    {
        if (tag->cache)
            s->release(tag->cache);
        tag->cache = NULL;
        publish(s, tag);
    }
}
int mm_tag_compare(const MMTag *a, const MMTag *b, const MMTagServices *s)
{
    if (a->sid != b->sid)
        return a->sid < b->sid ? -1 : 1;
    if (a->type != b->type)
        return a->type < b->type ? -1 : 1;
    if (a->number != b->number)
        return a->number < b->number ? -1 : 1;
    return a->sid ? 0 : s->compare_cache(a->cache, b->cache);
}
int mm_tag_child(const MMTag *parent, MMU32 slot, MMU32 type, MMTag *out,
                       const MMTagServices *s)
{
    MMResourceCache *cache;
    MMResourceManager *manager;
    MMChunkAccess chunks;
    MMChunkChild child;
    MMU32 position;
    if (parent->sid)
    {
        manager = s->source(s->context, parent->sid, 0, parent);
        if (!manager)
            return 0;
        cache = mm_resource_manager_find_file(manager, parent->type, parent->number, 0);
    }
    else
        cache = s->cache(parent->cache);
    if (!cache)
        return 0;
    chunks = mm_resource_cache_chunks(cache);
    if (mm_chunk_find(&chunks, parent->type, parent->number, &position) != MM_CHUNK_OK)
    {
        if (cache->file && s->missing_parent)
            s->missing_parent(s->context);
        return 0;
    }
    if (mm_chunk_child_exact(&chunks, position, slot, type, &child) != MM_CHUNK_OK)
        return 0;
    out->sid = parent->sid;
    out->type = type;
    out->number = child.number;
    if (!out->sid)
        out->cache = parent->cache;
    publish(s, out);
    if (!out->sid)
        s->retain(out->cache);
    return 1;
}
void *mm_tag_fetch(const MMTag *tag, MMResourceLoader reader, int use_media,
                         const MMTagServices *s)
{
    MMResourceCache *cache;
    MMResourceManager *manager;
    MMResourceRequest request;
    void *object = NULL;
    if (!tag->sid)
    {
        cache = s->cache(tag->cache);
        if (cache)
            object = mm_resource_cache_fetch_chunk(cache, tag->type, tag->number, reader);
    }
    else
    {
        manager = s->source(s->context, tag->sid, !use_media, tag);
        if (manager)
        {
            request.type = tag->type;
            request.number = tag->number;
            request.scope = 0;
            request.loader = reader;
            request.priority = MM_RESOURCE_NORMAL_PRIORITY;
            object = mm_resource_manager_fetch(manager, &request, NULL);
        }
    }
    if (!object && s->missing)
        s->missing(s->context, tag);
    return object;
}
int mm_tag_cache_file(const MMTag *tag, int children)
{
    (void)tag;
    (void)children;
    return 1;
}
void mm_tag_clear_cache(MMI32 sid, MMU32 flags, const MMTagCacheServices *s)
{
    MMU32 i, j, count = s->sources(s->context), caches;
    MMI32 source, budget;
    MMResourceManager *manager;
    MMResourceCache *cache;
    for (i = 0; i < count; ++i)
    {
        s->source_at(s->context, i, &source, &manager);
        if ((sid && sid != source) || !manager)
            continue;
        if (flags & 1)
        {
            manager = s->resolve(s->context, source);
            if (!manager)
                continue;
        }
        caches = manager->count;
        for (j = 0; j < caches; ++j)
        {
            cache = mm_resource_manager_at(manager, j);
            if (!cache->file)
                continue;
            if (flags & 1)
                s->reopen(cache->file);
            if (flags & 2)
            {
                budget = cache->budget;
                mm_resource_cache_budget(cache, 0);
                mm_resource_cache_budget(cache, budget);
            }
        }
    }
}

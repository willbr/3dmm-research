/* Tag policy converted from engine tagman.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/tag.h"
static void publish(const ThreeMMTagServices *s, const ThreeMMTag *tag)
{
    if (s->publish)
        s->publish(s->context, tag);
}
int three_mm_tag_open(ThreeMMTag *tag, void *destination, void *source, const ThreeMMTagServices *s)
{
    ThreeMMU32 number;
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
int three_mm_tag_save(ThreeMMTag *tag, void *destination, int redirect, const ThreeMMTagServices *s)
{
    ThreeMMU32 number;
    void *source_file, *destination_file;
    if (tag->sid)
        return 1;
    source_file = s->file(tag->cache);
    destination_file = s->file(destination);
    if (!s->copy(source_file, tag->type, tag->number, destination_file, &number))
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
void three_mm_tag_dup(const ThreeMMTag *tag, const ThreeMMTagServices *s)
{
    if (!tag->sid)
        s->retain(tag->cache);
}
void three_mm_tag_close(ThreeMMTag *tag, const ThreeMMTagServices *s)
{
    if (!tag->sid)
    {
        if (tag->cache)
            s->release(tag->cache);
        tag->cache = NULL;
        publish(s, tag);
    }
}
int three_mm_tag_compare(const ThreeMMTag *a, const ThreeMMTag *b, const ThreeMMTagServices *s)
{
    if (a->sid != b->sid)
        return a->sid < b->sid ? -1 : 1;
    if (a->type != b->type)
        return a->type < b->type ? -1 : 1;
    if (a->number != b->number)
        return a->number < b->number ? -1 : 1;
    return a->sid ? 0 : s->compare_cache(a->cache, b->cache);
}
int three_mm_tag_child(const ThreeMMTag *parent, ThreeMMU32 slot, ThreeMMU32 type, ThreeMMTag *out,
                       const ThreeMMTagServices *s)
{
    ThreeMMTag child = *out;
    void *cache = parent->cache, *manager;
    if (parent->sid)
    {
        manager = s->source(s->context, parent->sid, 0, parent);
        if (!manager)
            return 0;
        cache = s->find_cache(manager, parent->type, parent->number);
        if (!cache)
            return 0;
    }
    if (!s->child(cache, parent, slot, type, &child.number))
        return 0;
    child.sid = parent->sid;
    child.type = type;
    if (!child.sid)
        child.cache = parent->cache;
    *out = child;
    publish(s, out);
    if (!child.sid)
        s->retain(child.cache);
    return 1;
}
void *three_mm_tag_fetch(const ThreeMMTag *tag, ThreeMMResourceReader reader, int use_media,
                         const ThreeMMTagServices *s)
{
    void *cache, *object = NULL;
    if (!tag->sid)
        object = s->fetch(tag->cache, 0, tag, reader);
    else
    {
        cache = s->source(s->context, tag->sid, !use_media, tag);
        if (cache)
            object = s->fetch(cache, 1, tag, reader);
    }
    if (!object && s->missing)
        s->missing(s->context, tag);
    return object;
}
int three_mm_tag_cache_file(const ThreeMMTag *tag, int children)
{
    (void)tag;
    (void)children;
    return 1;
}
void three_mm_tag_clear_cache(ThreeMMI32 sid, ThreeMMU32 flags, const ThreeMMTagCacheServices *s)
{
    ThreeMMU32 i, j, count = s->sources(s->context), caches;
    ThreeMMI32 source, budget;
    void *manager, *cache, *file;
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
        caches = s->caches(manager);
        for (j = 0; j < caches; ++j)
        {
            cache = s->cache_at(manager, j);
            file = s->file(cache);
            if (!file)
                continue;
            if (flags & 1)
                s->reopen(file);
            if (flags & 2)
            {
                budget = s->budget(cache);
                s->set_budget(cache, 0);
                s->set_budget(cache, budget);
            }
        }
    }
}

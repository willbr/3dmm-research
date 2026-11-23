/* Source catalogue policy converted from engine tagman.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/source.h"
#include <string.h>
static MMArray view(MMSourceCatalogue *c)
{
    MMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &c->storage;
    a.width = sizeof(MMSourceRecord);
    a.count = c->count;
    a.free_count = -1;
    return a;
}
int mm_source_init(MMSourceCatalogue *c, const MMSourceOwners *owners,
                         const MMStorageAllocator *allocator)
{
    if (!c || !owners || !owners->new_paths || !owners->copy_paths || !owners->release_paths)
        return MM_STORAGE_ARGUMENT;
    memset(c, 0, sizeof(*c));
    if (mm_storage_init(&c->storage, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    c->owners = *owners;
    c->storage.min_grow[0] = 128;
    return MM_STORAGE_OK;
}
int mm_source_at(const MMSourceCatalogue *c, MMU32 i, MMSourceRecord *out)
{
    if (!c || !out || i >= c->count || c->count > c->storage.buffers[0].capacity / sizeof(*out))
        return MM_STORAGE_ARGUMENT;
    memcpy(out, c->storage.buffers[0].data + i * sizeof(*out), sizeof(*out));
    return MM_STORAGE_OK;
}
static void put(MMSourceCatalogue *c, MMU32 i, const MMSourceRecord *record)
{
    memcpy(c->storage.buffers[0].data + i * sizeof(*record), record, sizeof(*record));
}
int mm_source_dispose(MMSourceCatalogue *c)
{
    MMU32 i;
    MMSourceRecord record;
    if (!c || c->storage.buffers[0].locks || c->storage.buffers[1].locks)
        return MM_STORAGE_LOCKED;
    for (i = 0; i < c->count; ++i)
    {
        mm_source_at(c, i, &record);
        mm_resource_manager_destroy(record.manager);
    }
    for (i = 0; i < c->count; ++i)
    {
        mm_source_at(c, i, &record);
        c->owners.release_paths(record.paths);
    }
    c->count = 0;
    return mm_storage_dispose(&c->storage);
}
int mm_source_invalidate(MMSourceCatalogue *c, MMI32 sid)
{
    MMU32 i = c->count;
    MMSourceRecord record;
    MMArray a;
    if (c->storage.buffers[0].locks)
        return MM_STORAGE_LOCKED;
    while (i-- > 0)
    {
        mm_source_at(c, i, &record);
        if (record.sid != sid)
            continue;
        mm_resource_manager_destroy(record.manager);
        c->owners.release_paths(record.paths);
        a = view(c);
        mm_array_delete(&a, i, 1);
        c->count = a.count;
    }
    return MM_STORAGE_OK;
}
static int acquire(MMSourceCatalogue *c, MMI32 sid, void *paths, MMSourceRecord *work,
                   MMU32 *position)
{
    MMU32 i;
    MMSourceRecord record;
    MMArray a;
    int result;
    for (i = 0; i < c->count; ++i)
    {
        mm_source_at(c, i, &record);
        if (record.sid == sid)
            break;
    }
    if (i == c->count)
    {
        memset(&record, 0, sizeof(record));
        record.sid = sid;
        record.local_content = 2;
        record.paths = c->owners.new_paths(c->owners.context);
        if (!record.paths)
            return 0;
        a = view(c);
        result = mm_array_add(&a, &record, &i);
        c->count = a.count;
        if (result != MM_STORAGE_OK)
        {
            c->owners.release_paths(record.paths);
            return 0;
        }
    }
    *work = record;
    c->owners.copy_paths(paths, record.paths);
    work->paths = paths;
    *position = i;
    return 1;
}
static void publish(MMSourceCatalogue *c, MMU32 i, const MMSourceRecord *work)
{
    MMSourceRecord record;
    mm_source_at(c, i, &record);
    c->owners.copy_paths(record.paths, work->paths);
    record.local_content = work->local_content;
    record.manager = work->manager;
    put(c, i, &record);
}
static int local(MMSourceCatalogue *c, MMSourceRecord *r, MMU32 i, const MMSourceServices *s)
{
    int exists;
    if (r->local_content == 2)
    {
        if (s->path_empty(r->paths, 0))
        {
            if (!s->build_local(s->context, r->sid, r->paths, &exists))
                return 0;
            if (!exists)
                r->local_content = 0;
            publish(c, i, r);
        }
        if (r->local_content == 2)
        {
            if (s->archive_source(s->context, r->sid))
                exists = 1;
            else if (!s->content(s->context, r->paths, &exists))
                return 0;
            r->local_content = exists != 0;
            publish(c, i, r);
        }
    }
    return 1;
}
static MMResourceManager *build_manager(const MMSourceRecord *source, int media, const MMSourceServices *s,
                                        const MMSourceBuildServices *build)
{
    MMResourceManager *manager = mm_resource_manager_create(build->owner, build->storage);
    MMResourceCache *cache;
    MMU32 index;
    int archive, status;
    if (!manager)
        return NULL;
    archive = s->archive_source(s->context, source->sid);
    if (!archive && !build->begin(s->context, source->paths, media))
        goto fail;
    for (index = 0; build->next(s->context, source->sid, index, archive); ++index)
    {
        cache = build->open(s->context, build->budget, media);
        if (!cache)
            goto fail;
        status = mm_resource_manager_add_cache(manager, cache, NULL);
        mm_resource_cache_release(cache);
        if (status != MM_STORAGE_OK)
            goto fail;
    }
    return manager;
fail:
    mm_resource_manager_destroy(manager);
    return NULL;
}

MMResourceManager *mm_source_manager(MMSourceCatalogue *c, MMI32 sid, int local_only, void *paths,
                                       const MMSourceServices *s, const MMSourceBuildServices *build)
{
    MMSourceRecord r;
    MMU32 i;
    int changed, access = s->access(s->context, sid, NULL, 0);
    if (access == MM_SOURCE_DISABLED || access == MM_SOURCE_MISSING)
    {
        if (!local_only)
            s->retry(s->context, sid);
        return NULL;
    }
    if (!acquire(c, sid, paths, &r, &i))
        return NULL;
setup:
    if (!local(c, &r, i, s))
        return NULL;
    if (r.local_content == 1)
    {
        if (!r.manager)
        {
            r.manager = build_manager(&r, 0, s, build);
            if (!r.manager)
                return NULL;
            publish(c, i, &r);
        }
        return r.manager;
    }
    if (!local_only)
    {
        while (!s->find_media(s->context, sid, paths, &changed))
        {
            if (!s->retry(s->context, sid))
                return NULL;
            if (!r.manager)
            {
                r.local_content = 2;
                s->clear_local(paths);
                goto setup;
            }
        }
        if (changed)
        {
            if (r.manager && s->moved_open_source)
                s->moved_open_source(s->context);
            publish(c, i, &r);
        }
    }
    if (!r.manager)
    {
        if (local_only)
            return NULL;
        r.manager = build_manager(&r, 1, s, build);
        if (!r.manager)
            return NULL;
        publish(c, i, &r);
    }
    return r.manager;
}
int mm_source_local_content(MMSourceCatalogue *c, MMI32 sid, void *paths, int *exists,
                                  const MMSourceServices *s)
{
    MMSourceRecord r;
    MMU32 i;
    if (!acquire(c, sid, paths, &r, &i) || !local(c, &r, i, s))
        return 0;
    *exists = r.local_content == 1;
    return 1;
}
int mm_source_local_path(MMSourceCatalogue *c, MMI32 sid, void *paths, const MMSourceServices *s)
{
    MMSourceRecord r;
    MMU32 i;
    int exists, access = s->access(s->context, sid, paths, 1);
    if (access != MM_SOURCE_LEGACY)
        return access == MM_SOURCE_AVAILABLE;
    if (!acquire(c, sid, paths, &r, &i))
        return 0;
    if (s->path_empty(paths, 0) && (!s->build_local(s->context, sid, paths, &exists) || !exists))
        return 0;
    publish(c, i, &r);
    s->output(s->context, paths, 0);
    return 1;
}
int mm_source_media_path(MMSourceCatalogue *c, MMI32 sid, int ask, void *paths,
                               const MMSourceServices *s)
{
    MMSourceRecord r;
    MMU32 i;
    int changed, access = s->access(s->context, sid, NULL, 0);
    if (access != MM_SOURCE_LEGACY)
    {
        if (access != MM_SOURCE_AVAILABLE && ask)
            s->retry(s->context, sid);
        return access == MM_SOURCE_AVAILABLE;
    }
    if (!acquire(c, sid, paths, &r, &i))
        return 0;
    while (!s->find_media(s->context, sid, paths, &changed))
    {
        if (!ask || !s->retry(s->context, sid))
            return 0;
    }
    if (changed)
        publish(c, i, &r);
    s->output(s->context, paths, 1);
    return 1;
}
int mm_source_find_file(MMSourceCatalogue *c, MMI32 sid, int ask, void *paths,
                              const MMSourceServices *s)
{
    if (!s->prepare_file(s->context) || !mm_source_local_path(c, sid, paths, s))
        return 0;
    if (s->find_leaf(s->context))
        return 1;
    if (!mm_source_media_path(c, sid, ask, paths, s))
        return 0;
    if (s->find_leaf(s->context))
        return 1;
    s->clear_output(s->context);
    return 0;
}

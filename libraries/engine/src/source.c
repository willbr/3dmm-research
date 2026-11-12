/* Source catalogue policy converted from engine tagman.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/source.h"
#include <string.h>
static ThreeMMArray view(ThreeMMSourceCatalogue *c)
{
    ThreeMMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &c->storage;
    a.width = sizeof(ThreeMMSourceRecord);
    a.count = c->count;
    a.free_count = -1;
    return a;
}
int three_mm_source_init(ThreeMMSourceCatalogue *c, const ThreeMMSourceOwners *owners,
                         const ThreeMMStorageAllocator *allocator)
{
    if (!c || !owners || !owners->new_paths || !owners->copy_paths || !owners->release_paths ||
        !owners->release_manager)
        return THREE_MM_STORAGE_ARGUMENT;
    memset(c, 0, sizeof(*c));
    if (three_mm_storage_init(&c->storage, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_STORAGE_ARGUMENT;
    c->owners = *owners;
    c->storage.min_grow[0] = 128;
    return THREE_MM_STORAGE_OK;
}
int three_mm_source_at(const ThreeMMSourceCatalogue *c, ThreeMMU32 i, ThreeMMSourceRecord *out)
{
    if (!c || !out || i >= c->count || c->count > c->storage.buffers[0].capacity / sizeof(*out))
        return THREE_MM_STORAGE_ARGUMENT;
    memcpy(out, c->storage.buffers[0].data + i * sizeof(*out), sizeof(*out));
    return THREE_MM_STORAGE_OK;
}
static void put(ThreeMMSourceCatalogue *c, ThreeMMU32 i, const ThreeMMSourceRecord *record)
{
    memcpy(c->storage.buffers[0].data + i * sizeof(*record), record, sizeof(*record));
}
int three_mm_source_dispose(ThreeMMSourceCatalogue *c)
{
    ThreeMMU32 i;
    ThreeMMSourceRecord record;
    if (!c || c->storage.buffers[0].locks || c->storage.buffers[1].locks)
        return THREE_MM_STORAGE_LOCKED;
    for (i = 0; i < c->count; ++i)
    {
        three_mm_source_at(c, i, &record);
        if (record.manager)
            c->owners.release_manager(record.manager);
    }
    for (i = 0; i < c->count; ++i)
    {
        three_mm_source_at(c, i, &record);
        c->owners.release_paths(record.paths);
    }
    c->count = 0;
    return three_mm_storage_dispose(&c->storage);
}
int three_mm_source_invalidate(ThreeMMSourceCatalogue *c, ThreeMMI32 sid)
{
    ThreeMMU32 i = c->count;
    ThreeMMSourceRecord record;
    ThreeMMArray a;
    if (c->storage.buffers[0].locks)
        return THREE_MM_STORAGE_LOCKED;
    while (i-- > 0)
    {
        three_mm_source_at(c, i, &record);
        if (record.sid != sid)
            continue;
        if (record.manager)
            c->owners.release_manager(record.manager);
        c->owners.release_paths(record.paths);
        a = view(c);
        three_mm_array_delete(&a, i, 1);
        c->count = a.count;
    }
    return THREE_MM_STORAGE_OK;
}
static int acquire(ThreeMMSourceCatalogue *c, ThreeMMI32 sid, void *paths, ThreeMMSourceRecord *work,
                   ThreeMMU32 *position)
{
    ThreeMMU32 i;
    ThreeMMSourceRecord record;
    ThreeMMArray a;
    int result;
    for (i = 0; i < c->count; ++i)
    {
        three_mm_source_at(c, i, &record);
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
        result = three_mm_array_add(&a, &record, &i);
        c->count = a.count;
        if (result != THREE_MM_STORAGE_OK)
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
static void publish(ThreeMMSourceCatalogue *c, ThreeMMU32 i, const ThreeMMSourceRecord *work)
{
    ThreeMMSourceRecord record;
    three_mm_source_at(c, i, &record);
    c->owners.copy_paths(record.paths, work->paths);
    record.local_content = work->local_content;
    record.manager = work->manager;
    put(c, i, &record);
}
static int local(ThreeMMSourceCatalogue *c, ThreeMMSourceRecord *r, ThreeMMU32 i, const ThreeMMSourceServices *s)
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
void *three_mm_source_manager(ThreeMMSourceCatalogue *c, ThreeMMI32 sid, int local_only, void *paths,
                              const ThreeMMSourceServices *s)
{
    ThreeMMSourceRecord r;
    ThreeMMU32 i;
    int changed, access = s->access(s->context, sid, NULL, 0);
    if (access == THREE_MM_SOURCE_DISABLED || access == THREE_MM_SOURCE_MISSING)
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
            r.manager = s->new_manager(s->context, sid, paths, 0);
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
        r.manager = s->new_manager(s->context, sid, paths, 1);
        if (!r.manager)
            return NULL;
        publish(c, i, &r);
    }
    return r.manager;
}
int three_mm_source_local_content(ThreeMMSourceCatalogue *c, ThreeMMI32 sid, void *paths, int *exists,
                                  const ThreeMMSourceServices *s)
{
    ThreeMMSourceRecord r;
    ThreeMMU32 i;
    if (!acquire(c, sid, paths, &r, &i) || !local(c, &r, i, s))
        return 0;
    *exists = r.local_content == 1;
    return 1;
}
int three_mm_source_local_path(ThreeMMSourceCatalogue *c, ThreeMMI32 sid, void *paths, const ThreeMMSourceServices *s)
{
    ThreeMMSourceRecord r;
    ThreeMMU32 i;
    int exists, access = s->access(s->context, sid, paths, 1);
    if (access != THREE_MM_SOURCE_LEGACY)
        return access == THREE_MM_SOURCE_AVAILABLE;
    if (!acquire(c, sid, paths, &r, &i))
        return 0;
    if (s->path_empty(paths, 0) && (!s->build_local(s->context, sid, paths, &exists) || !exists))
        return 0;
    publish(c, i, &r);
    s->output(s->context, paths, 0);
    return 1;
}
int three_mm_source_media_path(ThreeMMSourceCatalogue *c, ThreeMMI32 sid, int ask, void *paths,
                               const ThreeMMSourceServices *s)
{
    ThreeMMSourceRecord r;
    ThreeMMU32 i;
    int changed, access = s->access(s->context, sid, NULL, 0);
    if (access != THREE_MM_SOURCE_LEGACY)
    {
        if (access != THREE_MM_SOURCE_AVAILABLE && ask)
            s->retry(s->context, sid);
        return access == THREE_MM_SOURCE_AVAILABLE;
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
int three_mm_source_find_file(ThreeMMSourceCatalogue *c, ThreeMMI32 sid, int ask, void *paths,
                              const ThreeMMSourceServices *s)
{
    if (!s->prepare_file(s->context) || !three_mm_source_local_path(c, sid, paths, s))
        return 0;
    if (s->find_leaf(s->context))
        return 1;
    if (!three_mm_source_media_path(c, sid, ask, paths, s))
        return 0;
    if (s->find_leaf(s->context))
        return 1;
    s->clear_output(s->context);
    return 0;
}
void *three_mm_source_build(ThreeMMI32 sid, ThreeMMI32 budget, const ThreeMMSourceBuildServices *s)
{
    void *manager = s->create(s->context), *file = NULL;
    ThreeMMU32 index;
    int archive;
    if (!manager)
        return NULL;
    archive = s->archive_source(s->context, sid);
    if (!archive && !s->begin(s->context))
        goto fail;
    for (index = 0; s->next(s->context, sid, index, archive); ++index)
    {
        file = s->open(s->context);
        if (!file || !s->add(manager, file, budget))
            goto fail;
        s->release_file(file);
        file = NULL;
    }
    return manager;
fail:
    if (file)
        s->release_file(file);
    s->release_manager(manager);
    return NULL;
}

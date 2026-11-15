/* Resource manager converted from Kauai crf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/resource_cache.h"
#include <string.h>
static MMArray view(MMResourceManager *m)
{
    MMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &m->storage;
    a.width = sizeof(void *);
    a.count = m->count;
    a.free_count = -1;
    return a;
}
int mm_resource_manager_init(MMResourceManager *m, const MMResourceManagerServices *s,
                                   const MMStorageAllocator *allocator)
{
    if (!m || !s || !s->create || !s->release || !s->load || !s->fetch || !s->find || !s->priority || !s->contains)
        return MM_STORAGE_ARGUMENT;
    memset(m, 0, sizeof(*m));
    if (mm_storage_init(&m->storage, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    m->services = *s;
    m->storage.min_grow[0] = 128;
    return MM_STORAGE_OK;
}
int mm_resource_manager_reserve(MMResourceManager *m, MMU32 additional)
{
    MMArray a = view(m);
    return mm_array_reserve(&a, additional, 0);
}
void *mm_resource_manager_at(const MMResourceManager *m, MMU32 i)
{
    void *p;
    if (!m || i >= m->count || m->count > m->storage.buffers[0].capacity / sizeof(p))
        return NULL;
    memcpy(&p, m->storage.buffers[0].data + i * sizeof(p), sizeof(p));
    return p;
}
int mm_resource_manager_dispose(MMResourceManager *m)
{
    void *p;
    if (!m || m->storage.buffers[0].locks || m->storage.buffers[1].locks)
        return MM_STORAGE_ARGUMENT;
    while (m->count)
    {
        p = mm_resource_manager_at(m, m->count - 1);
        --m->count;
        m->services.release(p);
    }
    return mm_storage_dispose(&m->storage);
}
int mm_resource_manager_add(MMResourceManager *m, void *file, MMI32 budget, MMU32 *position)
{
    void *p;
    MMArray a;
    MMU32 i;
    int r;
    if (!m || !file || budget < 0)
        return MM_STORAGE_ARGUMENT;
    p = m->services.create(m->services.context, file, budget);
    if (!p)
        return MM_STORAGE_IO;
    a = view(m);
    r = mm_array_add(&a, &p, &i);
    m->count = a.count;
    if (r != MM_STORAGE_OK)
    {
        m->services.release(p);
        return r;
    }
    if (position)
        *position = i;
    return MM_STORAGE_OK;
}
int mm_resource_manager_load(MMResourceManager *m, const MMResourceRequest *q)
{
    MMU32 i, count = m->count;
    int r;
    for (i = 0; i < count; ++i)
    {
        r = m->services.load(mm_resource_manager_at(m, i), q);
        if (r)
            return r;
    }
    return 0;
}
void *mm_resource_manager_fetch(MMResourceManager *m, const MMResourceRequest *q, int *error)
{
    MMU32 i, count = m->count;
    void *p = NULL;
    int failed = 0;
    for (i = 0; i < count; ++i)
    {
        p = m->services.fetch(mm_resource_manager_at(m, i), q, &failed);
        if (p || failed)
            break;
    }
    if (error)
        *error = failed;
    return p;
}
void *mm_resource_manager_find_file(MMResourceManager *m, MMU32 type, MMU32 number,
                                          MMU32 scope)
{
    MMU32 i, count = m->count;
    void *p;
    for (i = 0; i < count; ++i)
    {
        p = mm_resource_manager_at(m, i);
        if (m->services.contains(p, type, number, scope))
            return p;
    }
    return NULL;
}
void *mm_resource_manager_find(MMResourceManager *m, const MMResourceRequest *q)
{
    MMResourceRequest local = *q;
    void *p = mm_resource_manager_find_file(m, q->type, q->number, q->scope);
    if (!p)
        return NULL;
    local.scope = 0;
    return m->services.find(p, &local);
}
int mm_resource_manager_priority(MMResourceManager *m, const MMResourceRequest *q)
{
    MMU32 i, count = m->count;
    for (i = 0; i < count; ++i)
        if (m->services.priority(mm_resource_manager_at(m, i), q))
            return 1;
    return 0;
}

/* Resource manager converted from Kauai crf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/resource_cache.h"
#include <string.h>
static ThreeMMArray view(ThreeMMResourceManager *m)
{
    ThreeMMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &m->storage;
    a.width = sizeof(void *);
    a.count = m->count;
    a.free_count = -1;
    return a;
}
int three_mm_resource_manager_init(ThreeMMResourceManager *m, const ThreeMMResourceManagerServices *s,
                                   const ThreeMMStorageAllocator *allocator)
{
    if (!m || !s || !s->create || !s->release || !s->load || !s->fetch || !s->find || !s->priority || !s->contains)
        return THREE_MM_STORAGE_ARGUMENT;
    memset(m, 0, sizeof(*m));
    if (three_mm_storage_init(&m->storage, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_STORAGE_ARGUMENT;
    m->services = *s;
    m->storage.min_grow[0] = 128;
    return THREE_MM_STORAGE_OK;
}
int three_mm_resource_manager_reserve(ThreeMMResourceManager *m, ThreeMMU32 additional)
{
    ThreeMMArray a = view(m);
    return three_mm_array_reserve(&a, additional, 0);
}
void *three_mm_resource_manager_at(const ThreeMMResourceManager *m, ThreeMMU32 i)
{
    void *p;
    if (!m || i >= m->count || m->count > m->storage.buffers[0].capacity / sizeof(p))
        return NULL;
    memcpy(&p, m->storage.buffers[0].data + i * sizeof(p), sizeof(p));
    return p;
}
int three_mm_resource_manager_dispose(ThreeMMResourceManager *m)
{
    void *p;
    if (!m || m->storage.buffers[0].locks || m->storage.buffers[1].locks)
        return THREE_MM_STORAGE_ARGUMENT;
    while (m->count)
    {
        p = three_mm_resource_manager_at(m, m->count - 1);
        --m->count;
        m->services.release(p);
    }
    return three_mm_storage_dispose(&m->storage);
}
int three_mm_resource_manager_add(ThreeMMResourceManager *m, void *file, ThreeMMI32 budget, ThreeMMU32 *position)
{
    void *p;
    ThreeMMArray a;
    ThreeMMU32 i;
    int r;
    if (!m || !file || budget < 0)
        return THREE_MM_STORAGE_ARGUMENT;
    p = m->services.create(m->services.context, file, budget);
    if (!p)
        return THREE_MM_STORAGE_IO;
    a = view(m);
    r = three_mm_array_add(&a, &p, &i);
    m->count = a.count;
    if (r != THREE_MM_STORAGE_OK)
    {
        m->services.release(p);
        return r;
    }
    if (position)
        *position = i;
    return THREE_MM_STORAGE_OK;
}
int three_mm_resource_manager_load(ThreeMMResourceManager *m, const ThreeMMResourceRequest *q)
{
    ThreeMMU32 i, count = m->count;
    int r;
    for (i = 0; i < count; ++i)
    {
        r = m->services.load(three_mm_resource_manager_at(m, i), q);
        if (r)
            return r;
    }
    return 0;
}
void *three_mm_resource_manager_fetch(ThreeMMResourceManager *m, const ThreeMMResourceRequest *q, int *error)
{
    ThreeMMU32 i, count = m->count;
    void *p = NULL;
    int failed = 0;
    for (i = 0; i < count; ++i)
    {
        p = m->services.fetch(three_mm_resource_manager_at(m, i), q, &failed);
        if (p || failed)
            break;
    }
    if (error)
        *error = failed;
    return p;
}
void *three_mm_resource_manager_find_file(ThreeMMResourceManager *m, ThreeMMU32 type, ThreeMMU32 number,
                                          ThreeMMU32 scope)
{
    ThreeMMU32 i, count = m->count;
    void *p;
    for (i = 0; i < count; ++i)
    {
        p = three_mm_resource_manager_at(m, i);
        if (m->services.contains(p, type, number, scope))
            return p;
    }
    return NULL;
}
void *three_mm_resource_manager_find(ThreeMMResourceManager *m, const ThreeMMResourceRequest *q)
{
    ThreeMMResourceRequest local = *q;
    void *p = three_mm_resource_manager_find_file(m, q->type, q->number, q->scope);
    if (!p)
        return NULL;
    local.scope = 0;
    return m->services.find(p, &local);
}
int three_mm_resource_manager_priority(ThreeMMResourceManager *m, const ThreeMMResourceRequest *q)
{
    ThreeMMU32 i, count = m->count;
    for (i = 0; i < count; ++i)
        if (m->services.priority(three_mm_resource_manager_at(m, i), q))
            return 1;
    return 0;
}

/* Resource manager converted from Kauai crf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/resource_cache.h"
#include "mm/resource_lookup.h"
#include <stdlib.h>
#include <string.h>

typedef struct MMOwnedResourceManager
{
    MMResourceManager manager;
    const MMStorageAllocator *allocator;
} MMOwnedResourceManager;
static MMArray view(MMResourceManager *m)
{
    MMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &m->storage;
    a.width = sizeof(MMResourceCache *);
    a.count = m->count;
    a.free_count = -1;
    return a;
}
int mm_resource_manager_init(MMResourceManager *m, const MMResourceManagerServices *s,
                                   const MMStorageAllocator *allocator)
{
    if (!m || (s && !s->create))
        return MM_STORAGE_ARGUMENT;
    memset(m, 0, sizeof(*m));
    if (mm_storage_init(&m->storage, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    if (s)
        m->services = *s;
    m->storage.min_grow[0] = 128;
    return MM_STORAGE_OK;
}
static void free_owned_manager(MMOwnedResourceManager *owner)
{
    const MMStorageAllocator *allocator = owner->allocator;
    if (allocator)
        allocator->release(allocator->context, owner, sizeof(*owner));
    else
        free(owner);
}
MMResourceManager *mm_resource_manager_create(const MMStorageAllocator *allocator, const MMStorageAllocator *storage)
{
    MMOwnedResourceManager *owner;
    if (allocator && (!allocator->resize || !allocator->release))
        return NULL;
    owner = allocator ? allocator->resize(allocator->context, NULL, 0, sizeof(*owner), MM_STORAGE_NORMAL)
                      : malloc(sizeof(*owner));
    if (!owner)
        return NULL;
    owner->allocator = allocator;
    if (mm_resource_manager_init(&owner->manager, NULL, storage) != MM_STORAGE_OK)
    {
        free_owned_manager(owner);
        return NULL;
    }
    return &owner->manager;
}
void mm_resource_manager_destroy(MMResourceManager *m)
{
    if (m && mm_resource_manager_dispose(m) == MM_STORAGE_OK)
        free_owned_manager((MMOwnedResourceManager *)m);
}
int mm_resource_manager_reserve(MMResourceManager *m, MMU32 additional)
{
    MMArray a = view(m);
    return mm_array_reserve(&a, additional, 0);
}
MMResourceCache *mm_resource_manager_at(const MMResourceManager *m, MMU32 i)
{
    MMResourceCache *p;
    if (!m || i >= m->count || m->count > m->storage.buffers[0].capacity / sizeof(p))
        return NULL;
    memcpy(&p, m->storage.buffers[0].data + i * sizeof(p), sizeof(p));
    return p;
}
int mm_resource_manager_dispose(MMResourceManager *m)
{
    MMResourceCache *p;
    if (!m || m->storage.buffers[0].locks || m->storage.buffers[1].locks)
        return MM_STORAGE_ARGUMENT;
    while (m->count)
    {
        p = mm_resource_manager_at(m, m->count - 1);
        --m->count;
        mm_resource_cache_release(p);
    }
    return mm_storage_dispose(&m->storage);
}
int mm_resource_manager_add_cache(MMResourceManager *m, MMResourceCache *p, MMU32 *position)
{
    MMArray a;
    MMU32 i;
    int r;
    if (!m || !p || !p->owner)
        return MM_STORAGE_ARGUMENT;
    a = view(m);
    r = mm_array_add(&a, &p, &i);
    m->count = a.count;
    if (r != MM_STORAGE_OK)
        return r;
    mm_resource_cache_retain(p);
    if (position)
        *position = i;
    return MM_STORAGE_OK;
}
int mm_resource_manager_add(MMResourceManager *m, void *file, MMI32 budget, MMU32 *position)
{
    MMResourceCache *p;
    int result;
    if (!m || !m->services.create || !file || budget < 0)
        return MM_STORAGE_ARGUMENT;
    p = m->services.create(m->services.context, file, budget);
    if (!p)
        return MM_STORAGE_IO;
    result = mm_resource_manager_add_cache(m, p, position);
    mm_resource_cache_release(p);
    return result;
}
int mm_resource_manager_load_owned(MMResourceManager *m, const MMResourceRequest *q,
                                  const MMResourceObjectServices *objects)
{
    MMU32 i, count = m->count;
    int r;
    for (i = 0; i < count; ++i)
    {
        r = mm_resource_cache_load_owned(mm_resource_manager_at(m, i), q, objects);
        if (r)
            return r;
    }
    return 0;
}

int mm_resource_manager_load(MMResourceManager *m, const MMResourceRequest *q)
{
    return mm_resource_manager_load_owned(m, q, NULL);
}
typedef struct ManagerLookup
{
    MMResourceManager *manager;
    const MMResourceRequest *request;
    const MMResourceObjectServices *objects;
    MMU32 count;
    int failed;
} ManagerLookup;

static int next_cache(void *context, size_t *cursor, void **out)
{
    ManagerLookup *search = (ManagerLookup *)context;
    if (*cursor >= search->count)
        return MM_RESOURCE_LOOKUP_MISSING;
    *out = mm_resource_manager_at(search->manager, (MMU32)*cursor);
    ++*cursor;
    return MM_RESOURCE_LOOKUP_OK;
}

static int fetch_cached(void *context, void *cache, void **out)
{
    ManagerLookup *search = (ManagerLookup *)context;
    void *object = mm_resource_cache_fetch_owned((MMResourceCache *)cache, search->request, search->objects,
                                                 &search->failed);
    if (!object)
        return search->failed ? MM_STORAGE_IO : MM_RESOURCE_LOOKUP_MISSING;
    *out = object;
    return MM_RESOURCE_LOOKUP_OK;
}

static int contains_chunk(void *context, void *cache, void **out)
{
    ManagerLookup *search = (ManagerLookup *)context;
    const MMResourceRequest *q = search->request;
    if (!mm_resource_cache_contains((MMResourceCache *)cache, q->type, q->number, q->scope))
        return MM_RESOURCE_LOOKUP_MISSING;
    *out = cache;
    return MM_RESOURCE_LOOKUP_OK;
}

void *mm_resource_manager_fetch_owned(MMResourceManager *m, const MMResourceRequest *q,
                                      const MMResourceObjectServices *objects, int *error)
{
    ManagerLookup search;
    MMResourceLookup lookup;
    void *p = NULL;
    search.manager = m;
    search.request = q;
    search.objects = objects;
    search.count = m->count;
    search.failed = 0;
    lookup.context = &search;
    lookup.next = next_cache;
    lookup.probe = fetch_cached;
    mm_resource_lookup(&lookup, &p);
    if (error)
        *error = search.failed;
    return p;
}
void *mm_resource_manager_fetch(MMResourceManager *m, const MMResourceRequest *q, int *error)
{
    return mm_resource_manager_fetch_owned(m, q, NULL, error);
}
MMResourceCache *mm_resource_manager_find_file(MMResourceManager *m, MMU32 type, MMU32 number,
                                               MMU32 scope)
{
    MMResourceRequest q;
    ManagerLookup search;
    MMResourceLookup lookup;
    void *p = NULL;
    memset(&q, 0, sizeof(q));
    q.type = type;
    q.number = number;
    q.scope = scope;
    search.manager = m;
    search.request = &q;
    search.count = m->count;
    search.failed = 0;
    lookup.context = &search;
    lookup.next = next_cache;
    lookup.probe = contains_chunk;
    mm_resource_lookup(&lookup, &p);
    return (MMResourceCache *)p;
}
void *mm_resource_manager_find(MMResourceManager *m, const MMResourceRequest *q)
{
    MMResourceCache *p = mm_resource_manager_find_file(m, q->type, q->number, q->scope);
    if (!p)
        return NULL;
    return mm_resource_cache_find(p, q->type, q->number, q->reader, 0);
}
int mm_resource_manager_priority(MMResourceManager *m, const MMResourceRequest *q)
{
    MMU32 i, count = m->count;
    for (i = 0; i < count; ++i)
        if (mm_resource_cache_priority(mm_resource_manager_at(m, i), q->priority, q->type,
                                       q->number, q->reader, q->scope))
            return 1;
    return 0;
}

void *mm_resource_access_fetch(const MMResourceAccess *a, const MMResourceRequest *q,
                               const MMResourceObjectServices *objects, int *error)
{
    if (a->cache)
        return mm_resource_cache_fetch_owned(a->cache, q, objects, error);
    if (a->manager)
        return mm_resource_manager_fetch_owned(a->manager, q, objects, error);
    if (error)
        *error = 0;
    return NULL;
}

int mm_resource_access_load(const MMResourceAccess *a, const MMResourceRequest *q,
                            const MMResourceObjectServices *objects)
{
    if (a->cache)
        return mm_resource_cache_load_owned(a->cache, q, objects);
    return a->manager ? mm_resource_manager_load_owned(a->manager, q, objects) : 0;
}

void *mm_resource_access_find(const MMResourceAccess *a, const MMResourceRequest *q)
{
    if (a->cache)
        return mm_resource_cache_find(a->cache, q->type, q->number, q->reader, q->scope);
    return a->manager ? mm_resource_manager_find(a->manager, q) : NULL;
}

int mm_resource_access_priority(const MMResourceAccess *a, const MMResourceRequest *q)
{
    if (a->cache)
        return mm_resource_cache_priority(a->cache, q->priority, q->type, q->number, q->reader, q->scope);
    return a->manager ? mm_resource_manager_priority(a->manager, q) : 0;
}

MMResourceCache *mm_resource_access_find_cache(const MMResourceAccess *a, MMU32 type, MMU32 number, MMU32 scope)
{
    if (a->cache)
        return mm_resource_cache_contains(a->cache, type, number, scope) ? a->cache : NULL;
    return a->manager ? mm_resource_manager_find_file(a->manager, type, number, scope) : NULL;
}

/* Resource caching converted from Kauai crf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/resource_cache.h"
#include <string.h>
#define SCOPE_TYPE 0x52534320UL
static MMI32 priority16(MMI32 n)
{
    MMU32 low = (MMU32)n & 65535U;
    return low < 32768U ? (MMI32)low : (MMI32)low - 65536L;
}
void mm_resource_object_init(MMResourceObject *state)
{
    memset(state, 0, sizeof(*state));
}
void mm_resource_object_dispose(void *object, const MMResourceObjectServices *s)
{
    MMResourceObject *state = s->state(object);
    void *owner = state->owner;
    if (state->attached)
    {
        if (s->issue)
            s->issue(object, MM_RESOURCE_BAD_OBJECT);
        return;
    }
    state->owner = NULL;
    if (owner)
        s->release_owner(owner);
}
void mm_resource_object_release(void *object, const MMResourceObjectServices *s)
{
    MMI32 refs = s->references(object)->count;
    MMResourceObject *state;
    if (refs <= 0)
    {
        s->references(object)->count = 0;
        if (s->issue)
            s->issue(object, MM_RESOURCE_BAD_REFS);
    }
    else
        s->references(object)->count = refs - 1;
    if (s->references(object)->count == 0)
    {
        state = s->state(object);
        if (!state->attached)
            s->destroy(object);
        else
            s->released(state->owner, object);
    }
}
void mm_resource_object_detach(void *object, const MMResourceObjectServices *s)
{
    MMResourceObject *state = s->state(object);
    void *owner;
    if (state->attached)
    {
        owner = state->owner;
        s->retain_owner(owner);
        state->attached = 0;
        s->detached(owner, object);
    }
    if (s->references(object)->count <= 0)
        s->destroy(object);
}
void mm_resource_object_priority(void *object, MMI32 priority, const MMResourceObjectServices *s)
{
    s->retain(object);
    s->state(object)->priority = priority16(priority);
    s->release(object);
}
static MMArray view(MMResourceCache *c)
{
    MMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &c->storage;
    a.width = sizeof(MMResourceEntry);
    a.count = c->count;
    a.free_count = -1;
    return a;
}
static void issue(MMResourceCache *c, int kind, MMU32 type, MMU32 number)
{
    if (c->services.issue)
        c->services.issue(c->services.context, kind, type, number);
}
int mm_resource_cache_at(const MMResourceCache *c, MMU32 i, MMResourceEntry *out)
{
    if (!c || !out || i >= c->count || c->count > c->storage.buffers[0].capacity / sizeof(*out))
        return MM_STORAGE_ARGUMENT;
    memcpy(out, c->storage.buffers[0].data + i * sizeof(*out), sizeof(*out));
    return MM_STORAGE_OK;
}
static void put(MMResourceCache *c, MMU32 i, const MMResourceEntry *entry)
{
    memcpy(c->storage.buffers[0].data + i * sizeof(*entry), entry, sizeof(*entry));
}
static int insert(MMResourceCache *c, MMU32 i, const MMResourceEntry *entry)
{
    MMArray a = view(c);
    int r = mm_array_insert(&a, i, entry, 1);
    c->count = a.count;
    return r == MM_STORAGE_OK;
}
static MMI32 tick(MMResourceCache *c)
{
    MMI32 old = c->releases;
    c->releases = mm_signed_word((MMU32)c->releases + 1);
    return old;
}
int mm_resource_cache_init(MMResourceCache *c, void *owner, void *file, MMI32 budget,
                                 const MMResourceServices *s, const MMStorageAllocator *allocator,
                                 int diagnostic_refs)
{
    if (!c || !owner || !file || !s || budget < 0 || !s->compare_reader || !s->contains || !s->with_block || !s->read ||
        !s->retain_file || !s->release_file || !s->owner_references || !s->objects.state ||
        !s->objects.references || !s->objects.retain || !s->objects.release || !s->objects.detach ||
        !s->objects.priority || !s->objects.destroy || !s->objects.retain_owner || !s->objects.release_owner ||
        !s->objects.released || !s->objects.detached)
        return MM_STORAGE_ARGUMENT;
    memset(c, 0, sizeof(*c));
    if (mm_storage_init(&c->storage, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    c->services = *s;
    c->owner = owner;
    c->file = file;
    c->budget = budget;
    c->diagnostic_refs = diagnostic_refs;
    c->storage.min_grow[0] = 128;
    s->retain_file(file);
    return MM_STORAGE_OK;
}
int mm_resource_cache_reserve(MMResourceCache *c, MMU32 additional)
{
    MMArray a = view(c);
    return mm_array_reserve(&a, additional, 0);
}
static int find(MMResourceCache *c, MMU32 type, MMU32 number, MMResourceReader reader,
                MMU32 *position)
{
    MMU32 low = 0, high = c->count, middle;
    MMResourceEntry e;
    MMResourceObject *state;
    int order;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        mm_resource_cache_at(c, middle, &e);
        state = c->services.objects.state(e.object);
        order = type == state->type  ? (number == state->number  ? c->services.compare_reader(reader, e.reader)
                                        : number < state->number ? -1
                                                                 : 1)
                : type < state->type ? -1
                                     : 1;
        if (order < 0)
            high = middle;
        else if (order > 0)
            low = middle + 1;
        else
        {
            *position = middle;
            return 1;
        }
    }
    *position = low;
    return 0;
}
static int find_object(MMResourceCache *c, void *object, MMU32 *position)
{
    MMResourceObject key = *c->services.objects.state(object), *state;
    MMResourceEntry e;
    MMU32 low = 0, high = c->count, middle, i;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        mm_resource_cache_at(c, middle, &e);
        state = c->services.objects.state(e.object);
        if (key.type < state->type || (key.type == state->type && key.number < state->number))
            high = middle;
        else if (key.type > state->type || key.number > state->number)
            low = middle + 1;
        else
        {
            if (e.object == object)
            {
                *position = middle;
                return 1;
            }
            for (i = middle; i-- > 0;)
            {
                mm_resource_cache_at(c, i, &e);
                state = c->services.objects.state(e.object);
                if (state->type != key.type || state->number != key.number)
                    break;
                if (e.object == object)
                {
                    *position = i;
                    return 1;
                }
            }
            for (i = middle + 1; i < c->count; ++i)
            {
                mm_resource_cache_at(c, i, &e);
                state = c->services.objects.state(e.object);
                if (state->type != key.type || state->number != key.number)
                    break;
                if (e.object == object)
                {
                    *position = i;
                    return 1;
                }
            }
            return 0;
        }
    }
    return 0;
}
void mm_resource_cache_detached(MMResourceCache *c, void *object)
{
    MMU32 i;
    MMResourceEntry e;
    MMArray a;
    if (c->services.objects.state(object)->owner != c->owner || c->services.objects.state(object)->attached ||
        !find_object(c, object, &i))
    {
        issue(c, MM_RESOURCE_BAD_OBJECT, 0, 0);
        return;
    }
    mm_resource_cache_at(c, i, &e);
    c->resident = mm_wide_subtract(c->resident, mm_wide_i32(e.bytes));
    a = view(c);
    mm_array_delete(&a, i, 1);
    c->count = a.count;
}
void mm_resource_cache_released(MMResourceCache *c, void *object)
{
    MMResourceObject *state = c->services.objects.state(object);
    MMResourceEntry e;
    MMU32 i;
    if (state->owner != c->owner || !state->attached || c->services.objects.references(object)->count != 0 ||
        !find_object(c, object, &i))
    {
        issue(c, MM_RESOURCE_BAD_OBJECT, 0, 0);
        return;
    }
    mm_resource_cache_at(c, i, &e);
    e.released = tick(c);
    put(c, i, &e);
    if (state->priority <= 0 || mm_wide_compare(c->resident, mm_wide_i32(c->budget)) > 0)
        c->services.objects.detach(object);
}
int mm_resource_cache_purge(MMResourceCache *c, MMWide need, MMI32 last)
{
    MMResourceEntry e, best;
    MMResourceObject *state;
    MMU32 i, chosen;
    MMI32 priority, age;
    MMWide score, best_score, delta, zero = mm_wide_i32(0);
    MMI32 best_priority;
    if (last <= 0 || mm_wide_compare(need, zero) <= 0)
        return 0;
    while (c->count)
    {
        chosen = c->count;
        best_priority = last;
        best_score.high = 0x7fffffffL;
        best_score.low = 0xffffffffUL;
        for (i = 0; i < c->count; ++i)
        {
            mm_resource_cache_at(c, i, &e);
            state = c->services.objects.state(e.object);
            priority = state->priority;
            if (c->services.objects.references(e.object)->count > 0 || priority > best_priority)
                continue;
            age = mm_signed_word((MMU32)c->releases - (MMU32)e.released);
            if (age > MM_STORAGE_LIMIT / 256)
                age = MM_STORAGE_LIMIT / 256;
            delta = mm_wide_subtract(mm_wide_i32(e.bytes), need);
            if (mm_wide_compare(delta, zero) < 0)
            {
                delta = mm_wide_subtract(zero, delta);
                score = mm_wide_add(delta, mm_wide_add(delta, delta));
            }
            else
                score = delta;
            score = mm_wide_add(score, mm_wide_i32(mm_signed_word(0 - (MMU32)age * 256U)));
            if (priority < best_priority || mm_wide_compare(score, best_score) < 0)
            {
                chosen = i;
                best = e;
                best_priority = priority;
                best_score = score;
            }
        }
        if (chosen == c->count)
            return 0;
        c->services.objects.detach(best.object);
        need = mm_wide_subtract(need, mm_wide_i32(best.bytes));
        if (mm_wide_compare(need, zero) <= 0)
            return 1;
    }
    return 0;
}
void mm_resource_cache_budget(MMResourceCache *c, MMI32 budget)
{
    MMU32 i;
    MMResourceEntry e;
    if (budget < 0)
        return;
    if (!budget)
    {
        i = c->count;
        while (i-- > 0)
        {
            mm_resource_cache_at(c, i, &e);
            if (c->services.objects.references(e.object)->count == 0)
            {
                c->services.objects.detach(e.object);
                i = c->count;
            }
        }
    }
    else if (mm_wide_compare(c->resident, mm_wide_i32(budget)) > 0)
        mm_resource_cache_purge(c, mm_wide_subtract(c->resident, mm_wide_i32(budget)), 0x7fffffffL);
    c->budget = budget;
}
static int scope(MMResourceCache *c, MMU32 rsc)
{
    return !rsc || c->services.contains(c->file, SCOPE_TYPE, rsc);
}
int mm_resource_cache_contains(MMResourceCache *c, MMU32 type, MMU32 number, MMU32 rsc)
{
    return c->services.contains(c->file, type, number) && scope(c, rsc);
}
static int charge(MMResourceCache *c, MMI32 bytes, void *object, MMU32 type, MMU32 number)
{
    if (bytes >= 0)
        return 1;
    issue(c, MM_RESOURCE_BAD_CHARGE, type, number);
    if (object)
        c->services.objects.release(object);
    return 0;
}
static void bind(MMResourceCache *c, void *object, MMU32 type, MMU32 number, MMI32 priority)
{
    MMResourceObject *s = c->services.objects.state(object);
    s->owner = c->owner;
    s->type = type;
    s->number = number;
    s->priority = priority16(priority);
    c->services.objects.retain_owner(c->owner);
    s->attached = 0;
}
typedef struct ResourceMiss
{
    MMResourceCache *cache;
    const MMResourceRequest *request;
    void *object;
} ResourceMiss;

static int load_block(void *context, void *block)
{
    ResourceMiss *miss = (ResourceMiss *)context;
    MMResourceCache *c = miss->cache;
    const MMResourceRequest *q = miss->request;
    MMU32 type = q->type, number = q->number;
    MMResourceReader reader = q->reader;
    MMI32 priority = q->priority;
    MMResourceEntry e;
    MMU32 i;
    MMWide excess;
    memset(&e, 0, sizeof(e));
    if (!c->services.read(c->services.context, reader, type, number, block, NULL, &e.bytes) ||
        !charge(c, e.bytes, NULL, type, number))
        return 2;
    excess = mm_wide_subtract(mm_wide_add(c->resident, mm_wide_i32(e.bytes)),
                                    mm_wide_i32(c->budget));
    if (mm_wide_compare(excess, mm_wide_i32(0)) > 0 &&
        !mm_resource_cache_purge(c, excess, priority - 1))
        return 2;
    if (!c->services.read(c->services.context, reader, type, number, block, &e.object, &e.bytes))
        return 2;
    if (!charge(c, e.bytes, e.object, type, number))
        return 2;
    excess = mm_wide_subtract(mm_wide_add(c->resident, mm_wide_i32(e.bytes)),
                                    mm_wide_i32(c->budget));
    if (mm_wide_compare(excess, mm_wide_i32(0)) > 0 &&
        !mm_resource_cache_purge(c, excess, priority - 1))
    {
        c->services.objects.release(e.object);
        return 2;
    }
    bind(c, e.object, type, number, priority);
    e.reader = reader;
    e.released = tick(c);
    if (find(c, type, number, reader, &i))
        issue(c, MM_RESOURCE_DUPLICATE, type, number);
    if (!insert(c, i, &e))
    {
        c->services.objects.release(e.object);
        return 2;
    }
    c->resident = mm_wide_add(c->resident, mm_wide_i32(e.bytes));
    c->services.objects.state(e.object)->attached = 1;
    c->services.objects.release(e.object);
    c->services.objects.release_owner(c->owner);
    return 1;
}
int mm_resource_cache_load(MMResourceCache *c, const MMResourceRequest *q)
{
    ResourceMiss miss;
    MMResourceEntry e;
    MMU32 i;
    MMI32 current_priority;
    if (!q->reader || q->priority <= 0)
        return 2;
    if (!scope(c, q->scope))
        return 0;
    if (find(c, q->type, q->number, q->reader, &i))
    {
        mm_resource_cache_at(c, i, &e);
        current_priority = c->services.objects.state(e.object)->priority;
        c->services.objects.priority(e.object, current_priority > q->priority ? current_priority : q->priority);
        if (find(c, q->type, q->number, q->reader, &i))
        {
            mm_resource_cache_at(c, i, &e);
            e.released = tick(c);
            put(c, i, &e);
        }
        return 1;
    }
    miss.cache = c;
    miss.request = q;
    return c->services.with_block(c->file, q->type, q->number, load_block, &miss);
}

static int fetch_block(void *context, void *block)
{
    ResourceMiss *miss = (ResourceMiss *)context;
    MMResourceCache *c = miss->cache;
    const MMResourceRequest *q = miss->request;
    MMU32 type = q->type, number = q->number;
    MMResourceReader reader = q->reader;
    MMResourceEntry e;
    MMU32 i;
    memset(&e, 0, sizeof(e));
    if (!c->services.read(c->services.context, reader, type, number, block, &e.object, &e.bytes))
    {
        issue(c, MM_RESOURCE_LOAD_FAILED, type, number);
        return 1;
    }
    if (!charge(c, e.bytes, e.object, type, number))
        return 1;
    miss->object = e.object;
    bind(c, e.object, type, number, 2);
    e.reader = reader;
    if (find(c, type, number, reader, &i))
        issue(c, MM_RESOURCE_DUPLICATE, type, number);
    if (!insert(c, i, &e))
        return 1;
    c->resident = mm_wide_add(c->resident, mm_wide_i32(e.bytes));
    c->services.objects.state(e.object)->attached = 1;
    c->services.objects.release_owner(c->owner);
    if (mm_wide_compare(c->resident, mm_wide_i32(c->budget)) > 0)
        mm_resource_cache_purge(c, mm_wide_subtract(c->resident, mm_wide_i32(c->budget)),
                                      0x7fffffffL);
    return 0;
}

void *mm_resource_cache_fetch(MMResourceCache *c, const MMResourceRequest *q, int *error)
{
    ResourceMiss miss;
    MMResourceEntry e;
    MMU32 i;
    int failed;
    if (error)
        *error = 0;
    if (!q->reader || !scope(c, q->scope))
        return NULL;
    if (find(c, q->type, q->number, q->reader, &i))
    {
        mm_resource_cache_at(c, i, &e);
        c->services.objects.retain(e.object);
        return e.object;
    }
    miss.cache = c;
    miss.request = q;
    miss.object = NULL;
    failed = c->services.with_block(c->file, q->type, q->number, fetch_block, &miss);
    if (error)
        *error = failed;
    return miss.object;
}
void *mm_resource_cache_find(MMResourceCache *c, MMU32 type, MMU32 number,
                                   MMResourceReader reader, MMU32 rsc)
{
    MMResourceEntry e;
    MMU32 i;
    if (!reader || !find(c, type, number, reader, &i) || !scope(c, rsc))
        return NULL;
    mm_resource_cache_at(c, i, &e);
    c->services.objects.retain(e.object);
    return e.object;
}
int mm_resource_cache_priority(MMResourceCache *c, MMI32 priority, MMU32 type, MMU32 number,
                                     MMResourceReader reader, MMU32 rsc)
{
    MMResourceEntry e;
    MMU32 i;
    if (!reader || !find(c, type, number, reader, &i) || !scope(c, rsc))
        return 0;
    mm_resource_cache_at(c, i, &e);
    c->services.objects.priority(e.object, priority);
    return 1;
}
int mm_resource_cache_dispose(MMResourceCache *c)
{
    MMResourceEntry e;
    MMResourceObject *state;
    void *file;
    int result;
    if (!c || c->storage.buffers[0].locks || c->storage.buffers[1].locks)
        return MM_STORAGE_ARGUMENT;
    mm_reference_add(c->services.owner_references(c->services.context));
    while (c->count)
    {
        mm_resource_cache_at(c, 0, &e);
        c->services.objects.retain(e.object);
        c->services.objects.detach(e.object);
        state = c->services.objects.state(e.object);
        state->owner = NULL;
        if (c->diagnostic_refs)
            mm_reference_drop(c->services.owner_references(c->services.context));
        c->services.objects.release(e.object);
    }
    if (c->diagnostic_refs && c->services.owner_references(c->services.context)->count != 1)
        issue(c, MM_RESOURCE_BAD_OWNER, 0, 0);
    result = mm_storage_dispose(&c->storage);
    file = c->file;
    c->file = NULL;
    if (file)
        c->services.release_file(file);
    return result;
}

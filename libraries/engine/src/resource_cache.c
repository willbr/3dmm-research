/* Resource caching converted from Kauai crf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/resource_cache.h"
#include <string.h>
#define SCOPE_TYPE 0x52534320UL
static ThreeMMI32 priority16(ThreeMMI32 n)
{
    ThreeMMU32 low = (ThreeMMU32)n & 65535U;
    return low < 32768U ? (ThreeMMI32)low : (ThreeMMI32)low - 65536L;
}
void three_mm_resource_object_init(ThreeMMResourceObject *state)
{
    memset(state, 0, sizeof(*state));
}
void three_mm_resource_object_dispose(void *object, const ThreeMMResourceObjectServices *s)
{
    ThreeMMResourceObject *state = s->state(object);
    void *owner = state->owner;
    if (state->attached)
    {
        if (s->issue)
            s->issue(object, THREE_MM_RESOURCE_BAD_OBJECT);
        return;
    }
    state->owner = NULL;
    if (owner)
        s->release_owner(owner);
}
void three_mm_resource_object_release(void *object, const ThreeMMResourceObjectServices *s)
{
    ThreeMMI32 refs = s->refs(object);
    ThreeMMResourceObject *state;
    if (refs <= 0)
    {
        s->set_refs(object, 0);
        if (s->issue)
            s->issue(object, THREE_MM_RESOURCE_BAD_REFS);
    }
    else
        s->set_refs(object, refs - 1);
    if (s->refs(object) == 0)
    {
        state = s->state(object);
        if (!state->attached)
            s->destroy(object);
        else
            s->released(state->owner, object);
    }
}
void three_mm_resource_object_detach(void *object, const ThreeMMResourceObjectServices *s)
{
    ThreeMMResourceObject *state = s->state(object);
    void *owner;
    if (state->attached)
    {
        owner = state->owner;
        s->retain_owner(owner);
        state->attached = 0;
        s->detached(owner, object);
    }
    if (s->refs(object) <= 0)
        s->destroy(object);
}
void three_mm_resource_object_priority(void *object, ThreeMMI32 priority, const ThreeMMResourceObjectServices *s)
{
    s->retain(object);
    s->state(object)->priority = priority16(priority);
    s->release(object);
}
static ThreeMMArray view(ThreeMMResourceCache *c)
{
    ThreeMMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &c->storage;
    a.width = sizeof(ThreeMMResourceEntry);
    a.count = c->count;
    a.free_count = -1;
    return a;
}
static void issue(ThreeMMResourceCache *c, int kind, ThreeMMU32 type, ThreeMMU32 number)
{
    if (c->services.issue)
        c->services.issue(c->services.context, kind, type, number);
}
int three_mm_resource_cache_at(const ThreeMMResourceCache *c, ThreeMMU32 i, ThreeMMResourceEntry *out)
{
    if (!c || !out || i >= c->count || c->count > c->storage.buffers[0].capacity / sizeof(*out))
        return THREE_MM_STORAGE_ARGUMENT;
    memcpy(out, c->storage.buffers[0].data + i * sizeof(*out), sizeof(*out));
    return THREE_MM_STORAGE_OK;
}
static void put(ThreeMMResourceCache *c, ThreeMMU32 i, const ThreeMMResourceEntry *entry)
{
    memcpy(c->storage.buffers[0].data + i * sizeof(*entry), entry, sizeof(*entry));
}
static int insert(ThreeMMResourceCache *c, ThreeMMU32 i, const ThreeMMResourceEntry *entry)
{
    ThreeMMArray a = view(c);
    int r = three_mm_array_insert(&a, i, entry, 1);
    c->count = a.count;
    return r == THREE_MM_STORAGE_OK;
}
static ThreeMMI32 tick(ThreeMMResourceCache *c)
{
    ThreeMMI32 old = c->releases;
    c->releases = three_mm_signed_word((ThreeMMU32)c->releases + 1);
    return old;
}
int three_mm_resource_cache_init(ThreeMMResourceCache *c, void *owner, void *file, ThreeMMI32 budget,
                                 const ThreeMMResourceServices *s, const ThreeMMStorageAllocator *allocator,
                                 int diagnostic_refs)
{
    if (!c || !owner || !file || !s || budget < 0 || !s->compare_reader || !s->contains || !s->find_block || !s->read ||
        !s->retain_file || !s->release_file || !s->owner_refs || !s->set_owner_refs || !s->objects.state ||
        !s->objects.refs || !s->objects.set_refs || !s->objects.retain || !s->objects.release || !s->objects.detach ||
        !s->objects.priority || !s->objects.destroy || !s->objects.retain_owner || !s->objects.release_owner ||
        !s->objects.released || !s->objects.detached)
        return THREE_MM_STORAGE_ARGUMENT;
    memset(c, 0, sizeof(*c));
    if (three_mm_storage_init(&c->storage, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_STORAGE_ARGUMENT;
    c->services = *s;
    c->owner = owner;
    c->file = file;
    c->budget = budget;
    c->diagnostic_refs = diagnostic_refs;
    c->storage.min_grow[0] = 128;
    s->retain_file(file);
    return THREE_MM_STORAGE_OK;
}
int three_mm_resource_cache_reserve(ThreeMMResourceCache *c, ThreeMMU32 additional)
{
    ThreeMMArray a = view(c);
    return three_mm_array_reserve(&a, additional, 0);
}
static int find(ThreeMMResourceCache *c, ThreeMMU32 type, ThreeMMU32 number, ThreeMMResourceReader reader,
                ThreeMMU32 *position)
{
    ThreeMMU32 low = 0, high = c->count, middle;
    ThreeMMResourceEntry e;
    ThreeMMResourceObject *state;
    int order;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        three_mm_resource_cache_at(c, middle, &e);
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
static int find_object(ThreeMMResourceCache *c, void *object, ThreeMMU32 *position)
{
    ThreeMMResourceObject key = *c->services.objects.state(object), *state;
    ThreeMMResourceEntry e;
    ThreeMMU32 low = 0, high = c->count, middle, i;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        three_mm_resource_cache_at(c, middle, &e);
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
                three_mm_resource_cache_at(c, i, &e);
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
                three_mm_resource_cache_at(c, i, &e);
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
void three_mm_resource_cache_detached(ThreeMMResourceCache *c, void *object)
{
    ThreeMMU32 i;
    ThreeMMResourceEntry e;
    ThreeMMArray a;
    if (c->services.objects.state(object)->owner != c->owner || c->services.objects.state(object)->attached ||
        !find_object(c, object, &i))
    {
        issue(c, THREE_MM_RESOURCE_BAD_OBJECT, 0, 0);
        return;
    }
    three_mm_resource_cache_at(c, i, &e);
    c->resident = three_mm_wide_subtract(c->resident, three_mm_wide_i32(e.bytes));
    a = view(c);
    three_mm_array_delete(&a, i, 1);
    c->count = a.count;
}
void three_mm_resource_cache_released(ThreeMMResourceCache *c, void *object)
{
    ThreeMMResourceObject *state = c->services.objects.state(object);
    ThreeMMResourceEntry e;
    ThreeMMU32 i;
    if (state->owner != c->owner || !state->attached || c->services.objects.refs(object) != 0 ||
        !find_object(c, object, &i))
    {
        issue(c, THREE_MM_RESOURCE_BAD_OBJECT, 0, 0);
        return;
    }
    three_mm_resource_cache_at(c, i, &e);
    e.released = tick(c);
    put(c, i, &e);
    if (state->priority <= 0 || three_mm_wide_compare(c->resident, three_mm_wide_i32(c->budget)) > 0)
        c->services.objects.detach(object);
}
int three_mm_resource_cache_purge(ThreeMMResourceCache *c, ThreeMMWide need, ThreeMMI32 last)
{
    ThreeMMResourceEntry e, best;
    ThreeMMResourceObject *state;
    ThreeMMU32 i, chosen;
    ThreeMMI32 priority, age;
    ThreeMMWide score, best_score, delta, zero = three_mm_wide_i32(0);
    ThreeMMI32 best_priority;
    if (last <= 0 || three_mm_wide_compare(need, zero) <= 0)
        return 0;
    while (c->count)
    {
        chosen = c->count;
        best_priority = last;
        best_score.high = 0x7fffffffL;
        best_score.low = 0xffffffffUL;
        for (i = 0; i < c->count; ++i)
        {
            three_mm_resource_cache_at(c, i, &e);
            state = c->services.objects.state(e.object);
            priority = state->priority;
            if (c->services.objects.refs(e.object) > 0 || priority > best_priority)
                continue;
            age = three_mm_signed_word((ThreeMMU32)c->releases - (ThreeMMU32)e.released);
            if (age > THREE_MM_STORAGE_LIMIT / 256)
                age = THREE_MM_STORAGE_LIMIT / 256;
            delta = three_mm_wide_subtract(three_mm_wide_i32(e.bytes), need);
            if (three_mm_wide_compare(delta, zero) < 0)
            {
                delta = three_mm_wide_subtract(zero, delta);
                score = three_mm_wide_add(delta, three_mm_wide_add(delta, delta));
            }
            else
                score = delta;
            score = three_mm_wide_add(score, three_mm_wide_i32(three_mm_signed_word(0 - (ThreeMMU32)age * 256U)));
            if (priority < best_priority || three_mm_wide_compare(score, best_score) < 0)
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
        need = three_mm_wide_subtract(need, three_mm_wide_i32(best.bytes));
        if (three_mm_wide_compare(need, zero) <= 0)
            return 1;
    }
    return 0;
}
void three_mm_resource_cache_budget(ThreeMMResourceCache *c, ThreeMMI32 budget)
{
    ThreeMMU32 i;
    ThreeMMResourceEntry e;
    if (budget < 0)
        return;
    if (!budget)
    {
        i = c->count;
        while (i-- > 0)
        {
            three_mm_resource_cache_at(c, i, &e);
            if (c->services.objects.refs(e.object) == 0)
            {
                c->services.objects.detach(e.object);
                i = c->count;
            }
        }
    }
    else if (three_mm_wide_compare(c->resident, three_mm_wide_i32(budget)) > 0)
        three_mm_resource_cache_purge(c, three_mm_wide_subtract(c->resident, three_mm_wide_i32(budget)), 0x7fffffffL);
    c->budget = budget;
}
static int scope(ThreeMMResourceCache *c, ThreeMMU32 rsc)
{
    return !rsc || c->services.contains(c->services.context, SCOPE_TYPE, rsc);
}
int three_mm_resource_cache_contains(ThreeMMResourceCache *c, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 rsc)
{
    return c->services.contains(c->services.context, type, number) && scope(c, rsc);
}
static int charge(ThreeMMResourceCache *c, ThreeMMI32 bytes, void *object, ThreeMMU32 type, ThreeMMU32 number)
{
    if (bytes >= 0)
        return 1;
    issue(c, THREE_MM_RESOURCE_BAD_CHARGE, type, number);
    if (object)
        c->services.objects.release(object);
    return 0;
}
static void bind(ThreeMMResourceCache *c, void *object, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 priority)
{
    ThreeMMResourceObject *s = c->services.objects.state(object);
    s->owner = c->owner;
    s->type = type;
    s->number = number;
    s->priority = priority16(priority);
    c->services.objects.retain_owner(c->owner);
    s->attached = 0;
}
int three_mm_resource_cache_load(ThreeMMResourceCache *c, ThreeMMU32 type, ThreeMMU32 number,
                                 ThreeMMResourceReader reader, ThreeMMU32 rsc, ThreeMMI32 priority, void *block)
{
    ThreeMMResourceEntry e;
    ThreeMMU32 i;
    ThreeMMI32 current_priority;
    ThreeMMWide excess;
    if (!reader || priority <= 0)
        return 2;
    if (!scope(c, rsc))
        return 0;
    if (find(c, type, number, reader, &i))
    {
        three_mm_resource_cache_at(c, i, &e);
        current_priority = c->services.objects.state(e.object)->priority;
        c->services.objects.priority(e.object, current_priority > priority ? current_priority : priority);
        if (find(c, type, number, reader, &i))
        {
            three_mm_resource_cache_at(c, i, &e);
            e.released = tick(c);
            put(c, i, &e);
        }
        return 1;
    }
    if (!c->services.find_block(c->services.context, type, number, block))
        return 0;
    memset(&e, 0, sizeof(e));
    if (!c->services.read(c->services.context, reader, type, number, block, NULL, &e.bytes) ||
        !charge(c, e.bytes, NULL, type, number))
        return 2;
    excess = three_mm_wide_subtract(three_mm_wide_add(c->resident, three_mm_wide_i32(e.bytes)),
                                    three_mm_wide_i32(c->budget));
    if (three_mm_wide_compare(excess, three_mm_wide_i32(0)) > 0 &&
        !three_mm_resource_cache_purge(c, excess, priority - 1))
        return 2;
    if (!c->services.read(c->services.context, reader, type, number, block, &e.object, &e.bytes))
        return 2;
    if (!charge(c, e.bytes, e.object, type, number))
        return 2;
    excess = three_mm_wide_subtract(three_mm_wide_add(c->resident, three_mm_wide_i32(e.bytes)),
                                    three_mm_wide_i32(c->budget));
    if (three_mm_wide_compare(excess, three_mm_wide_i32(0)) > 0 &&
        !three_mm_resource_cache_purge(c, excess, priority - 1))
    {
        c->services.objects.release(e.object);
        return 2;
    }
    bind(c, e.object, type, number, priority);
    e.reader = reader;
    e.released = tick(c);
    if (find(c, type, number, reader, &i))
        issue(c, THREE_MM_RESOURCE_DUPLICATE, type, number);
    if (!insert(c, i, &e))
    {
        c->services.objects.release(e.object);
        return 2;
    }
    c->resident = three_mm_wide_add(c->resident, three_mm_wide_i32(e.bytes));
    c->services.objects.state(e.object)->attached = 1;
    c->services.objects.release(e.object);
    c->services.objects.release_owner(c->owner);
    return 1;
}
void *three_mm_resource_cache_fetch(ThreeMMResourceCache *c, ThreeMMU32 type, ThreeMMU32 number,
                                    ThreeMMResourceReader reader, ThreeMMU32 rsc, void *block, int *error)
{
    ThreeMMResourceEntry e;
    ThreeMMU32 i;
    if (error)
        *error = 0;
    if (!reader || !scope(c, rsc))
        return NULL;
    if (find(c, type, number, reader, &i))
    {
        three_mm_resource_cache_at(c, i, &e);
        c->services.objects.retain(e.object);
        return e.object;
    }
    if (!c->services.find_block(c->services.context, type, number, block))
        return NULL;
    memset(&e, 0, sizeof(e));
    if (!c->services.read(c->services.context, reader, type, number, block, &e.object, &e.bytes))
    {
        if (error)
            *error = 1;
        issue(c, THREE_MM_RESOURCE_LOAD_FAILED, type, number);
        return NULL;
    }
    if (!charge(c, e.bytes, e.object, type, number))
    {
        if (error)
            *error = 1;
        return NULL;
    }
    bind(c, e.object, type, number, 2);
    e.reader = reader;
    if (find(c, type, number, reader, &i))
        issue(c, THREE_MM_RESOURCE_DUPLICATE, type, number);
    if (!insert(c, i, &e))
    {
        if (error)
            *error = 1;
        return e.object;
    }
    c->resident = three_mm_wide_add(c->resident, three_mm_wide_i32(e.bytes));
    c->services.objects.state(e.object)->attached = 1;
    c->services.objects.release_owner(c->owner);
    if (three_mm_wide_compare(c->resident, three_mm_wide_i32(c->budget)) > 0)
        three_mm_resource_cache_purge(c, three_mm_wide_subtract(c->resident, three_mm_wide_i32(c->budget)),
                                      0x7fffffffL);
    return e.object;
}
void *three_mm_resource_cache_find(ThreeMMResourceCache *c, ThreeMMU32 type, ThreeMMU32 number,
                                   ThreeMMResourceReader reader, ThreeMMU32 rsc)
{
    ThreeMMResourceEntry e;
    ThreeMMU32 i;
    if (!reader || !find(c, type, number, reader, &i) || !scope(c, rsc))
        return NULL;
    three_mm_resource_cache_at(c, i, &e);
    c->services.objects.retain(e.object);
    return e.object;
}
int three_mm_resource_cache_priority(ThreeMMResourceCache *c, ThreeMMI32 priority, ThreeMMU32 type, ThreeMMU32 number,
                                     ThreeMMResourceReader reader, ThreeMMU32 rsc)
{
    ThreeMMResourceEntry e;
    ThreeMMU32 i;
    if (!reader || !find(c, type, number, reader, &i) || !scope(c, rsc))
        return 0;
    three_mm_resource_cache_at(c, i, &e);
    c->services.objects.priority(e.object, priority);
    return 1;
}
int three_mm_resource_cache_dispose(ThreeMMResourceCache *c)
{
    ThreeMMResourceEntry e;
    ThreeMMResourceObject *state;
    void *file;
    int result;
    if (!c || c->storage.buffers[0].locks || c->storage.buffers[1].locks)
        return THREE_MM_STORAGE_ARGUMENT;
    c->services.set_owner_refs(c->services.context, c->services.owner_refs(c->services.context) + 1);
    while (c->count)
    {
        three_mm_resource_cache_at(c, 0, &e);
        c->services.objects.retain(e.object);
        c->services.objects.detach(e.object);
        state = c->services.objects.state(e.object);
        state->owner = NULL;
        if (c->diagnostic_refs)
            c->services.set_owner_refs(c->services.context, c->services.owner_refs(c->services.context) - 1);
        c->services.objects.release(e.object);
    }
    if (c->diagnostic_refs && c->services.owner_refs(c->services.context) != 1)
        issue(c, THREE_MM_RESOURCE_BAD_OWNER, 0, 0);
    result = three_mm_storage_dispose(&c->storage);
    file = c->file;
    c->file = NULL;
    if (file)
        c->services.release_file(file);
    return result;
}

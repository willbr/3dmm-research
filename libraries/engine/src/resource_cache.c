/* Resource caching converted from Kauai crf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/resource_cache.h"
#include "mm/chunk_storage.h"
#include <string.h>
#define SCOPE_TYPE 0x52534320UL

static void cache_released(MMResourceCache *, void *object, const MMResourceObject *);
static void cache_detached(MMResourceCache *, void *object, const MMResourceObject *);

static MMI32 priority16(MMI32 n)
{
    MMU32 low = (MMU32)n & 65535U;
    return low < 32768U ? (MMI32)low : (MMI32)low - 65536L;
}
static void retain_object(void *object, const MMResourceObjectServices *s)
{
    if (mm_reference_add(s->references(object)) <= 0 && s->issue)
        s->issue(object, MM_RESOURCE_BAD_RETAIN);
}
void mm_resource_cache_retain(MMResourceCache *c)
{
    if (mm_reference_add(c->references) <= 0 && c->services.issue)
        c->services.issue(c->services.context, MM_RESOURCE_BAD_RETAIN, 0, 0);
}
void mm_resource_cache_release(MMResourceCache *c)
{
    mm_reference_release(c->references, c->owner, c->services.destroy_owner);
}
void mm_resource_object_init(MMResourceObject *state)
{
    memset(state, 0, sizeof(*state));
}
void mm_resource_object_dispose(void *object, const MMResourceObjectServices *s)
{
    MMResourceObject *state = s->state(object);
    MMResourceCache *cache = state->cache;
    if (state->attached)
    {
        if (s->issue)
            s->issue(object, MM_RESOURCE_BAD_OBJECT);
        return;
    }
    state->cache = NULL;
    if (cache)
        mm_resource_cache_release(cache);
}
void mm_resource_object_release(void *object, const MMResourceObjectServices *s)
{
    MMReference *refs = s->references(object);
    MMResourceObject *state;
    if (refs->count <= 0)
    {
        refs->count = 0;
        if (s->issue)
            s->issue(object, MM_RESOURCE_BAD_REFS);
    }
    else
        mm_reference_drop(refs);
    if (refs->count == 0)
    {
        state = s->state(object);
        if (!state->attached)
            s->destroy(object);
        else
            cache_released(state->cache, object, state);
    }
}
void mm_resource_object_detach(void *object, const MMResourceObjectServices *s)
{
    MMResourceObject *state = s->state(object);
    if (state->attached)
    {
        mm_resource_cache_retain(state->cache);
        state->attached = 0;
        cache_detached(state->cache, object, state);
    }
    if (s->references(object)->count <= 0)
        s->destroy(object);
}
void mm_resource_object_priority(void *object, MMI32 priority, const MMResourceObjectServices *s)
{
    retain_object(object, s);
    s->state(object)->priority = priority16(priority);
    mm_resource_object_release(object, s);
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
static const MMResourceObjectServices *object_services(const MMResourceCache *c, const MMResourceEntry *entry)
{
    return entry->objects ? entry->objects : &c->services.objects;
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
int mm_resource_cache_init(MMResourceCache *c, void *owner, MMReference *references,
                           const MMResourceSource *source, MMI32 budget, const MMResourceServices *s,
                           const MMStorageAllocator *allocator, int diagnostic_refs)
{
    MMOwnedBlock block;
    if (!c || !owner || !references || !source || !s || budget < 0 || !s->compare_reader ||
        !s->destroy_owner || !s->objects.state || !s->objects.references || !s->objects.destroy ||
        mm_owned_block_init(&block, s->handles, s->block_files) != MM_DATA_OK)
        return MM_STORAGE_ARGUMENT;
    if (source->live ?
            source->index || source->backing || !s->contains || !s->locate || !s->retain_file || !s->release_file :
            !source->index || !source->backing)
        return MM_STORAGE_ARGUMENT;
    memset(c, 0, sizeof(*c));
    if (mm_storage_init(&c->storage, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    if (source->index)
    {
        c->wire_index = mm_chunk_index_retain(source->index);
        if (!c->wire_index)
        {
            mm_storage_dispose(&c->storage);
            return MM_STORAGE_MEMORY;
        }
        c->wire_file = source->backing;
        s->block_files->retain(s->block_files->context, c->wire_file);
    }
    else
        s->retain_file(source->live);
    c->services = *s;
    c->owner = owner;
    c->file = source->live;
    c->references = references;
    c->budget = budget;
    c->diagnostic_refs = diagnostic_refs;
    c->storage.min_grow[0] = 128;
    return MM_STORAGE_OK;
}
int mm_resource_cache_reserve(MMResourceCache *c, MMU32 additional)
{
    MMArray a = view(c);
    return mm_array_reserve(&a, additional, 0);
}
static int find(MMResourceCache *c, MMU32 type, MMU32 number, MMResourceBlockReader reader,
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
        state = object_services(c, &e)->state(e.object);
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
static int find_object(MMResourceCache *c, void *object, const MMResourceObject *key,
                       MMU32 *position)
{
    const MMResourceObject *state;
    MMResourceEntry e;
    MMU32 low = 0, high = c->count, middle, i;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        mm_resource_cache_at(c, middle, &e);
        state = object_services(c, &e)->state(e.object);
        if (key->type < state->type || (key->type == state->type && key->number < state->number))
            high = middle;
        else if (key->type > state->type || key->number > state->number)
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
                state = object_services(c, &e)->state(e.object);
                if (state->type != key->type || state->number != key->number)
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
                state = object_services(c, &e)->state(e.object);
                if (state->type != key->type || state->number != key->number)
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
static void cache_detached(MMResourceCache *c, void *object, const MMResourceObject *state)
{
    MMU32 i;
    MMResourceEntry e;
    MMArray a;
    if (!find_object(c, object, state, &i))
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
static void cache_released(MMResourceCache *c, void *object, const MMResourceObject *state)
{
    MMResourceEntry e;
    MMU32 i;
    if (!find_object(c, object, state, &i))
    {
        issue(c, MM_RESOURCE_BAD_OBJECT, 0, 0);
        return;
    }
    mm_resource_cache_at(c, i, &e);
    e.released = tick(c);
    put(c, i, &e);
    if (state->priority <= MM_RESOURCE_TOSS_PRIORITY || mm_wide_compare(c->resident, mm_wide_i32(c->budget)) > 0)
        mm_resource_object_detach(object, object_services(c, &e));
}
int mm_resource_cache_purge(MMResourceCache *c, MMWide need, MMI32 last)
{
    MMResourceEntry e, best;
    MMResourceObject *state;
    const MMResourceObjectServices *objects;
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
            objects = object_services(c, &e);
            state = objects->state(e.object);
            priority = state->priority;
            if (objects->references(e.object)->count > 0 || priority > best_priority)
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
        mm_resource_object_detach(best.object, object_services(c, &best));
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
    const MMResourceObjectServices *objects;
    if (budget < 0)
        return;
    if (!budget)
    {
        i = c->count;
        while (i-- > 0)
        {
            mm_resource_cache_at(c, i, &e);
            objects = object_services(c, &e);
            if (objects->references(e.object)->count == 0)
            {
                mm_resource_object_detach(e.object, objects);
                i = c->count;
            }
        }
    }
    else if (mm_wide_compare(c->resident, mm_wide_i32(budget)) > 0)
        mm_resource_cache_purge(c, mm_wide_subtract(c->resident, mm_wide_i32(budget)), 0x7fffffffL);
    c->budget = budget;
}
static int wire_find(MMResourceCache *c, MMU32 type, MMU32 number, MMBlockFile *file, int *packed)
{
    MMChunkAccess access = mm_chunk_index_access(c->wire_index);
    MMChunkEntry entry;
    MMU32 position;
    if (mm_chunk_find(&access, type, number, &position) != MM_CHUNK_OK)
        return 0;
    if (file)
    {
        if (mm_chunk_at(&access, position, &entry) != MM_CHUNK_OK ||
            !mm_chunk_stored_extent(mm_chunk_index_header(c->wire_index), &entry))
            return 0;
        file->file = c->wire_file;
        file->offset = entry.offset;
        file->length = entry.bytes;
        *packed = (entry.flags & MM_CHUNK_PACKED) != 0;
    }
    return 1;
}
static int contains(MMResourceCache *c, MMU32 type, MMU32 number)
{
    if (c->wire_index)
        return wire_find(c, type, number, NULL, NULL);
    return c->services.contains(c->file, type, number);
}
static int scope(MMResourceCache *c, MMU32 rsc)
{
    return !rsc || contains(c, SCOPE_TYPE, rsc);
}
int mm_resource_cache_contains(MMResourceCache *c, MMU32 type, MMU32 number, MMU32 rsc)
{
    return contains(c, type, number) && scope(c, rsc);
}
static int charge(MMResourceCache *c, const MMResourceEntry *entry, MMU32 type, MMU32 number)
{
    if (entry->bytes >= 0)
        return 1;
    issue(c, MM_RESOURCE_BAD_CHARGE, type, number);
    if (entry->object)
        mm_resource_object_release(entry->object, object_services(c, entry));
    return 0;
}
static void bind(MMResourceCache *c, const MMResourceEntry *entry, MMU32 type, MMU32 number, MMI32 priority)
{
    MMResourceObject *s = object_services(c, entry)->state(entry->object);
    s->cache = c;
    s->type = type;
    s->number = number;
    s->priority = priority16(priority);
    mm_resource_cache_retain(c);
    s->attached = 0;
}
MMChunkAccess mm_resource_cache_chunks(const MMResourceCache *c)
{
    MMChunkAccess empty = {0};
    if (c && c->wire_index)
        return mm_chunk_index_access(c->wire_index);
    if (c && c->file && c->services.live_chunks)
        return c->services.live_chunks(c->file);
    return empty;
}

static int locate_payload(void *context, MMU32 type, MMU32 number, MMBlockFile *file, int *packed)
{
    MMResourceCache *c = (MMResourceCache *)context;
    return c->wire_index ? wire_find(c, type, number, file, packed) :
                           c->services.locate(c->file, type, number, file, packed);
}

int mm_resource_cache_read_block(MMResourceCache *c, MMU32 type, MMU32 number, MMOwnedBlock *out)
{
    MMBlockFile file;
    int packed;
    if (!locate_payload(c, type, number, &file, &packed))
    {
        mm_owned_block_clear(out);
        return MM_CHUNK_NOT_FOUND;
    }
    if (out && out->files != c->services.block_files && mm_owned_block_clear(out) == MM_DATA_OK)
        out->files = c->services.block_files;
    if (out && out->files == c->services.block_files && mm_owned_block_set_file(out, &file, packed) == MM_DATA_OK)
        return MM_CHUNK_OK;
    mm_owned_block_clear(out);
    issue(c, MM_RESOURCE_BAD_BLOCK, type, number);
    return MM_CHUNK_BAD_FILE;
}

int mm_resource_cache_visit_payload(MMResourceCache *c, MMU32 type, MMU32 number,
                                    MMBlockTransform decode, void *decode_context,
                                    MMReadReceiver receive, void *context, int *result)
{
    MMChunkPayloadServices services;
    if (!c)
        return MM_CHUNK_BAD_FILE;
    services.handles = c->services.handles;
    services.files = c->services.block_files;
    services.locate = locate_payload;
    services.decode = decode;
    services.decode_context = decode_context;
    return mm_chunk_read_payload(&services, c, type, number, receive, context, result);
}

static int open_block(MMResourceCache *c, const MMResourceRequest *q, MMOwnedBlock *out)
{
    if (mm_owned_block_init(out, c->services.handles, c->services.block_files) != MM_DATA_OK)
    {
        issue(c, MM_RESOURCE_BAD_BLOCK, q->type, q->number);
        return 0;
    }
    /* A found record with an unusable extent leaves the fresh block empty. */
    return mm_resource_cache_read_block(c, q->type, q->number, out) != MM_CHUNK_NOT_FOUND;
}

static void clear_block(MMResourceCache *c, const MMResourceRequest *q, MMOwnedBlock *block)
{
    if (mm_owned_block_clear(block) != MM_DATA_OK)
        issue(c, MM_RESOURCE_BAD_BLOCK, q->type, q->number);
}

static int load_block(MMResourceCache *c, const MMResourceRequest *q, MMOwnedBlock *block)
{
    MMU32 type = q->type, number = q->number;
    MMResourceBlockReader reader = q->reader;
    MMI32 priority = q->priority;
    MMResourceEntry e;
    MMU32 i;
    MMWide excess;
    memset(&e, 0, sizeof(e));
    if (!reader(c, type, number, block, NULL, &e.bytes) ||
        !charge(c, &e, type, number))
        return 2;
    excess = mm_wide_subtract(mm_wide_add(c->resident, mm_wide_i32(e.bytes)),
                                    mm_wide_i32(c->budget));
    if (mm_wide_compare(excess, mm_wide_i32(0)) > 0 &&
        !mm_resource_cache_purge(c, excess, priority - 1))
        return 2;
    if (!reader(c, type, number, block, &e.object, &e.bytes))
        return 2;
    if (!charge(c, &e, type, number))
        return 2;
    excess = mm_wide_subtract(mm_wide_add(c->resident, mm_wide_i32(e.bytes)),
                                    mm_wide_i32(c->budget));
    if (mm_wide_compare(excess, mm_wide_i32(0)) > 0 &&
        !mm_resource_cache_purge(c, excess, priority - 1))
    {
        mm_resource_object_release(e.object, &c->services.objects);
        return 2;
    }
    bind(c, &e, type, number, priority);
    e.reader = reader;
    e.released = tick(c);
    if (find(c, type, number, reader, &i))
        issue(c, MM_RESOURCE_DUPLICATE, type, number);
    if (!insert(c, i, &e))
    {
        mm_resource_object_release(e.object, &c->services.objects);
        return 2;
    }
    c->resident = mm_wide_add(c->resident, mm_wide_i32(e.bytes));
    c->services.objects.state(e.object)->attached = 1;
    mm_resource_object_release(e.object, &c->services.objects);
    mm_resource_cache_release(c);
    return 1;
}
int mm_resource_cache_load(MMResourceCache *c, const MMResourceRequest *q)
{
    MMOwnedBlock block;
    MMResourceEntry e;
    const MMResourceObjectServices *objects;
    MMU32 i;
    MMI32 current_priority;
    int result;
    if (!q->reader || q->priority <= 0)
        return 2;
    if (!scope(c, q->scope))
        return 0;
    if (find(c, q->type, q->number, q->reader, &i))
    {
        mm_resource_cache_at(c, i, &e);
        objects = object_services(c, &e);
        current_priority = objects->state(e.object)->priority;
        mm_resource_object_priority(e.object, current_priority > q->priority ? current_priority : q->priority,
                                    objects);
        if (find(c, q->type, q->number, q->reader, &i))
        {
            mm_resource_cache_at(c, i, &e);
            e.released = tick(c);
            put(c, i, &e);
        }
        return 1;
    }
    if (!open_block(c, q, &block))
        return 0;
    result = load_block(c, q, &block);
    clear_block(c, q, &block);
    return result;
}

static void *fetch_block(MMResourceCache *c, const MMResourceRequest *q, MMOwnedBlock *block,
                         const MMResourceObjectServices *objects, int *error)
{
    MMU32 type = q->type, number = q->number;
    MMResourceBlockReader reader = q->reader;
    MMResourceEntry e;
    MMU32 i;
    memset(&e, 0, sizeof(e));
    e.objects = objects;
    if (!reader(c, type, number, block, &e.object, &e.bytes))
    {
        *error = 1;
        issue(c, MM_RESOURCE_LOAD_FAILED, type, number);
        return NULL;
    }
    if (!charge(c, &e, type, number))
    {
        *error = 1;
        return NULL;
    }
    bind(c, &e, type, number, MM_RESOURCE_NORMAL_PRIORITY);
    e.reader = reader;
    if (find(c, type, number, reader, &i))
        issue(c, MM_RESOURCE_DUPLICATE, type, number);
    if (!insert(c, i, &e))
    {
        *error = 1;
        return e.object;
    }
    c->resident = mm_wide_add(c->resident, mm_wide_i32(e.bytes));
    object_services(c, &e)->state(e.object)->attached = 1;
    mm_resource_cache_release(c);
    if (mm_wide_compare(c->resident, mm_wide_i32(c->budget)) > 0)
        mm_resource_cache_purge(c, mm_wide_subtract(c->resident, mm_wide_i32(c->budget)),
                                      0x7fffffffL);
    return e.object;
}

static void *cache_fetch(MMResourceCache *c, const MMResourceRequest *q, const MMResourceObjectServices *objects,
                         int *error)
{
    MMOwnedBlock block;
    MMResourceEntry e;
    MMU32 i;
    void *object;
    int failed = 0;
    if (error)
        *error = 0;
    if (!q->reader || !scope(c, q->scope))
        return NULL;
    if (find(c, q->type, q->number, q->reader, &i))
    {
        mm_resource_cache_at(c, i, &e);
        retain_object(e.object, object_services(c, &e));
        return e.object;
    }
    if (!open_block(c, q, &block))
        return NULL;
    object = fetch_block(c, q, &block, objects, &failed);
    clear_block(c, q, &block);
    if (error)
        *error = failed;
    return object;
}
void *mm_resource_cache_fetch(MMResourceCache *c, const MMResourceRequest *q, int *error)
{
    return cache_fetch(c, q, NULL, error);
}
void *mm_resource_cache_fetch_chunk(MMResourceCache *c, MMU32 type, MMU32 number, MMResourceBlockReader reader)
{
    return mm_resource_cache_fetch_owned_chunk(c, type, number, reader, NULL);
}

void *mm_resource_cache_fetch_owned_chunk(MMResourceCache *c, MMU32 type, MMU32 number, MMResourceBlockReader reader,
                                          const MMResourceObjectServices *objects)
{
    MMResourceRequest request;
    request.type = type;
    request.number = number;
    request.scope = 0;
    request.reader = reader;
    request.priority = MM_RESOURCE_NORMAL_PRIORITY;
    return cache_fetch(c, &request, objects, NULL);
}

void *mm_resource_cache_find(MMResourceCache *c, MMU32 type, MMU32 number,
                                   MMResourceBlockReader reader, MMU32 rsc)
{
    MMResourceEntry e;
    MMU32 i;
    if (!reader || !find(c, type, number, reader, &i) || !scope(c, rsc))
        return NULL;
    mm_resource_cache_at(c, i, &e);
    retain_object(e.object, object_services(c, &e));
    return e.object;
}
int mm_resource_cache_priority(MMResourceCache *c, MMI32 priority, MMU32 type, MMU32 number,
                                     MMResourceBlockReader reader, MMU32 rsc)
{
    MMResourceEntry e;
    MMU32 i;
    if (!reader || !find(c, type, number, reader, &i) || !scope(c, rsc))
        return 0;
    mm_resource_cache_at(c, i, &e);
    mm_resource_object_priority(e.object, priority, object_services(c, &e));
    return 1;
}
int mm_resource_cache_dispose(MMResourceCache *c)
{
    MMResourceEntry e;
    MMResourceObject *state;
    const MMResourceObjectServices *objects;
    void *file;
    int result;
    if (!c || c->storage.buffers[0].locks || c->storage.buffers[1].locks)
        return MM_STORAGE_ARGUMENT;
    mm_reference_add(c->references);
    while (c->count)
    {
        mm_resource_cache_at(c, 0, &e);
        objects = object_services(c, &e);
        retain_object(e.object, objects);
        mm_resource_object_detach(e.object, objects);
        state = objects->state(e.object);
        state->cache = NULL;
        if (c->diagnostic_refs)
            mm_reference_drop(c->references);
        mm_resource_object_release(e.object, objects);
    }
    if (c->diagnostic_refs && c->references->count != 1)
        issue(c, MM_RESOURCE_BAD_OWNER, 0, 0);
    result = mm_storage_dispose(&c->storage);
    if (c->wire_index)
    {
        const MMBlockFileServices *files = c->services.block_files;
        mm_chunk_index_close(c->wire_index);
        files->release(files->context, c->wire_file);
        c->wire_index = NULL;
        c->wire_file = NULL;
    }
    file = c->file;
    c->file = NULL;
    if (file)
        c->services.release_file(file);
    return result;
}

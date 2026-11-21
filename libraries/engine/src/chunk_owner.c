/* Container ownership converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_owner.h"
#include <string.h>
int mm_chunk_owner_init(MMChunkOwner *o, const MMStorageAllocator *allocator, int expanded)
{
    if (!o)
        return MM_CHUNK_BAD_FILE;
    memset(o, 0, sizeof(*o));
    o->expanded_runtime = expanded != 0;
    return expanded ? MM_CHUNK_OK : mm_chunk_runtime_init(&o->runtime, allocator);
}
int mm_chunk_owner_dispose(MMChunkOwner *o, const MMChunkDisposal *services)
{
    if (!o)
        return MM_CHUNK_BAD_FILE;
    return mm_chunk_dispose(&o->files, o->expanded_runtime ? NULL : &o->runtime, services);
}
int mm_chunk_owner_reopen(MMChunkOwner *o, const MMChunkReopenServices *services)
{
    if (!o)
        return MM_CHUNK_BAD_FILE;
    return mm_chunk_reopen(&o->files, o->expanded_runtime ? NULL : &o->runtime, services);
}
int mm_chunk_owner_options(MMChunkOwner *o, const MMChunkOptionServices *s, MMU32 flags,
                                 MMU32 mask)
{
    MMChunkOptions options;
    int result;
    if (!o)
        return MM_CHUNK_BAD_FILE;
    options.marked = o->marked;
    options.add_to_extra = o->add_to_extra;
    options.read_from_extra = o->read_from_extra;
    options.free_map_unread = o->files.free_map_unread;
    options.invalid_main = o->files.invalid_main;
    result = mm_chunk_options_set(&options, s, flags, mask);
    if (result == MM_CHUNK_OK)
    {
        o->marked = options.marked;
        o->add_to_extra = options.add_to_extra;
        o->read_from_extra = options.read_from_extra;
        o->files.free_map_unread = options.free_map_unread;
    }
    return result;
}
MMChunkSaveState mm_chunk_owner_save_state(const MMChunkOwner *o)
{
    MMChunkSaveState state;
    state.main_file = o->files.main_file;
    state.extra_file = o->files.extra_file;
    state.main_map = o->files.main_map;
    state.extra_map = o->files.extra_map;
    state.main_end = o->files.main_end;
    state.extra_end = o->files.extra_end;
    state.invalid_main = o->files.invalid_main;
    state.add_to_extra = o->add_to_extra;
    state.free_map_unread = o->files.free_map_unread;
    return state;
}
void mm_chunk_owner_publish_save(MMChunkOwner *o, const MMChunkSaveState *s)
{
    o->files.main_file = s->main_file;
    o->files.extra_file = s->extra_file;
    o->files.main_map = (MMChunkFreeMap *)s->main_map;
    o->files.extra_map = (MMChunkFreeMap *)s->extra_map;
    o->files.main_end = s->main_end;
    o->files.extra_end = s->extra_end;
    o->files.invalid_main = s->invalid_main != 0;
    o->files.free_map_unread = s->free_map_unread != 0;
    o->add_to_extra = s->add_to_extra != 0;
}
MMChunkStoragePolicy mm_chunk_owner_storage_policy(const MMChunkOwner *o, MMU32 max_bytes)
{
    MMChunkStoragePolicy policy;
    policy.max_bytes = max_bytes;
    policy.add_to_extra = o->add_to_extra;
    policy.read_from_extra = o->read_from_extra;
    policy.free_map_unread = o->files.free_map_unread;
    return policy;
}

static MMChunkTable *owner_table(void *context)
{
    const MMChunkOwnerStorage *storage = context;
    return storage->owner->files.index;
}

static void owner_issue(void *context, int kind)
{
    const MMChunkOwnerStorage *storage = context;
    if (storage->io.issue)
        storage->io.issue(storage->io.context, kind);
}

int mm_chunk_owner_read_free_map(MMChunkOwnerStorage *storage)
{
    MMChunkOwner *owner = storage->owner;
    MMChunkReopenState *files = &owner->files;
    MMReadStream stream;
    int existed, status;
    if (!files->free_map_unread)
        return MM_CHUNK_OK;
    existed = files->main_map != NULL;
    stream = storage->io.read_stream(files->main_file);
    status = mm_free_map_load(&files->main_map, &files->free_map_unread, &stream,
                              owner->map_offset, owner->map_bytes, storage->allocator);
    if (existed)
        owner_issue(storage, MM_CHUNK_OWNER_MAP_PRESENT);
    return status;
}

typedef struct OwnerHeap
{
    MMChunkOwnerStorage *storage;
    void *file;
    MMChunkFreeMap **map;
    MMU32 *end;
} OwnerHeap;

static MMU32 heap_count(void *context)
{
    const OwnerHeap *heap = context;
    return *heap->map ? (*heap->map)->count : 0;
}

static void heap_get(void *context, MMU32 position, MMChunkFreeRange *range)
{
    const OwnerHeap *heap = context;
    mm_free_map_get(*heap->map, position, range);
}

static void heap_put(void *context, MMU32 position, const MMChunkFreeRange *range)
{
    const OwnerHeap *heap = context;
    mm_free_map_put(*heap->map, position, range);
}

static void heap_erase(void *context, MMU32 position)
{
    const OwnerHeap *heap = context;
    mm_free_map_erase(*heap->map, position);
}

static int heap_insert(void *context, MMU32 position, const MMChunkFreeRange *range)
{
    const OwnerHeap *heap = context;
    return mm_free_map_insert(*heap->map, position, range) == MM_CHUNK_OK;
}

static int heap_ensure(void *context)
{
    const OwnerHeap *heap = context;
    return *heap->map || mm_free_map_create(heap->storage->allocator, 1, heap->map) == MM_CHUNK_OK;
}

static MMU32 heap_end(void *context)
{
    const OwnerHeap *heap = context;
    return heap->storage->io.size(heap->file);
}

static int heap_resize(void *context, MMU32 bytes)
{
    const OwnerHeap *heap = context;
    return heap->storage->io.resize(heap->file, bytes);
}

static MMChunkHeap owner_heap(OwnerHeap *heap, MMChunkOwnerStorage *storage, int extra)
{
    MMChunkReopenState *files = &storage->owner->files;
    MMChunkHeap binding = {NULL, heap_count, heap_get, heap_put, heap_erase,
                          heap_insert, heap_ensure, heap_end, heap_resize};
    heap->storage = storage;
    heap->file = extra ? files->extra_file : files->main_file;
    heap->map = extra ? &files->extra_map : &files->main_map;
    heap->end = extra ? &files->extra_end : &files->main_end;
    binding.context = heap;
    return binding;
}

static void *owner_file(void *context, int extra)
{
    const MMChunkOwnerStorage *storage = context;
    return extra ? storage->owner->files.extra_file : storage->owner->files.main_file;
}

static int owner_create_extra(void *context)
{
    MMChunkOwnerStorage *storage = context;
    MMChunkReopenState *files = &storage->owner->files;
    void *file = storage->io.create_extra(storage->io.context);
    if (!file)
        return 0;
    files->extra_end = 0;
    files->extra_file = file;
    return 1;
}

static int owner_reserve(void *context, int extra, MMU32 bytes, MMU32 *offset)
{
    OwnerHeap heap;
    const MMChunkHeap binding = owner_heap(&heap, context, extra);
    return mm_chunk_heap_allocate(&binding, heap.end, bytes, offset) == MM_CHUNK_OK;
}

static void owner_free(void *context, int extra, MMU32 offset, MMU32 bytes)
{
    MMChunkOwnerStorage *storage = context;
    OwnerHeap heap;
    MMChunkHeap binding;
    int status;
    if (!bytes)
        return;
    if (!extra)
        mm_chunk_owner_read_free_map(storage);
    binding = owner_heap(&heap, storage, extra);
    status = mm_chunk_heap_free(&binding, heap.end, offset, bytes);
    if (status != MM_CHUNK_OK && status != MM_CHUNK_MEMORY)
        owner_issue(storage, MM_CHUNK_OWNER_FREE_RANGE);
}

static void owner_entry(void *context, MMU32 position, MMChunkEntry *entry)
{
    mm_chunk_table_entry(owner_table(context), position, entry);
}

static void owner_cache_place(void *context, MMU32 position, MMU32 offset)
{
    MMChunkTable *table = owner_table(context);
    void *row = mm_chunk_table_fixed(table, position, NULL);
    const int expanded = table->fixed_bytes == sizeof(MMChunkRecordBig);
    if (expanded)
        ((MMChunkRecordBig *)row)->fp = (MMI32)offset;
    else
        ((MMChunkRecordSmall *)row)->fp = (MMI32)offset;
    mm_chunk_record_set_flags(row, expanded, 1, 1);
}

static int owner_copy(void *context, const MMChunkLocation *source, const MMChunkLocation *destination)
{
    const MMChunkOwnerStorage *storage = context;
    return storage->io.copy(storage->io.context, source, destination);
}

static void owner_trace(void *context, int kind, const MMChunkEntry *entry)
{
    const MMChunkOwnerStorage *storage = context;
    storage->io.trace(storage->io.context, kind, entry);
}

static MMChunkStorageServices storage_services(MMChunkOwnerStorage *storage)
{
    MMChunkStorageServices services = {NULL, owner_file, owner_create_extra, owner_reserve, owner_free,
                                      owner_entry, owner_cache_place, owner_copy, NULL};
    services.context = storage;
    if (storage->io.trace)
        services.trace = owner_trace;
    return services;
}

static int owner_allocate(void *context, MMU32 bytes, MMChunkLocation *location)
{
    MMChunkOwnerStorage *storage = context;
    const MMChunkStoragePolicy policy = mm_chunk_owner_storage_policy(storage->owner, storage->max_bytes);
    const MMChunkStorageServices services = storage_services(storage);
    if (bytes > policy.max_bytes)
        owner_issue(storage, MM_CHUNK_OWNER_EXTENT_LIMIT);
    else if (bytes && !policy.add_to_extra && policy.free_map_unread)
        owner_issue(storage, MM_CHUNK_OWNER_MAP_UNREAD);
    return mm_chunk_storage_allocate(&policy, &services, bytes, 0, location) == MM_CHUNK_OK;
}

static MMChunkRecordBig *expanded_row(void *context, MMU32 type, MMU32 number)
{
    MMChunkTable *table = owner_table(context);
    const MMChunkAccess access = mm_chunk_table_access(table);
    MMU32 position;
    if (mm_chunk_find(&access, type, number, &position) != MM_CHUNK_OK)
        return NULL;
    return mm_chunk_table_fixed(table, position, NULL);
}

static MMI32 owner_runtime(void *context, MMU32 type, MMU32 number)
{
    MMChunkOwnerStorage *storage = context;
    MMChunkRecordBig *row;
    if (!storage->owner->expanded_runtime)
        return mm_chunk_runtime_get(&storage->owner->runtime, type, number);
    row = expanded_row(context, type, number);
    return row ? row->rti : 0;
}

static int owner_set_runtime(void *context, MMU32 type, MMU32 number, MMI32 runtime)
{
    MMChunkOwnerStorage *storage = context;
    MMChunkRecordBig *row;
    if (!storage->owner->expanded_runtime)
        return mm_chunk_runtime_set(&storage->owner->runtime, type, number, runtime) == MM_CHUNK_OK;
    row = expanded_row(context, type, number);
    if (!row)
        return 0;
    row->rti = runtime;
    return 1;
}

void mm_chunk_owner_bind_storage(MMChunkOwnerStorage *storage, MMChunkOwner *owner,
                                const MMStorageAllocator *allocator, MMU32 max_bytes,
                                const MMChunkOwnerFileIO *io)
{
    const MMChunkTableEditBinding edit = {NULL, owner_table, owner_allocate, owner_free,
                                         owner_runtime, owner_set_runtime, owner_issue};
    storage->owner = owner;
    storage->allocator = allocator;
    storage->io = *io;
    storage->edit = edit;
    storage->edit.context = storage;
    storage->max_bytes = max_bytes;
}

int mm_chunk_owner_cache(MMChunkOwnerStorage *storage, MMU32 position, MMChunkLocation *out)
{
    const MMChunkStoragePolicy policy = mm_chunk_owner_storage_policy(storage->owner, storage->max_bytes);
    const MMChunkStorageServices services = storage_services(storage);
    if (position >= owner_table(storage)->count)
        return MM_CHUNK_NOT_FOUND;
    return mm_chunk_storage_cache(&policy, &services, position, out);
}

int mm_chunk_owner_fetch(MMChunkOwnerStorage *storage, MMU32 position, MMChunkLocation *out)
{
    const MMChunkStoragePolicy policy = mm_chunk_owner_storage_policy(storage->owner, storage->max_bytes);
    const MMChunkStorageServices services = storage_services(storage);
    if (position >= owner_table(storage)->count)
        return MM_CHUNK_NOT_FOUND;
    return mm_chunk_storage_fetch(&policy, &services, position, out);
}

MMChunkEdit mm_chunk_owner_editor(MMChunkOwnerStorage *storage, MMU32 max_references, int scan_all_references)
{
    return mm_chunk_table_editor(&storage->edit, storage->allocator, max_references, scan_all_references);
}

MMI32 mm_chunk_owner_error(const MMChunkOwner *o, const MMChunkErrorServices *s)
{
    MMI32 level = s->get(o->files.main_file), extra;
    if (o->files.extra_file)
    {
        extra = s->get(o->files.extra_file);
        if (extra > level)
            level = extra;
    }
    return level;
}
void mm_chunk_owner_reset_error(MMChunkOwner *o, const MMChunkErrorServices *s, MMI32 level)
{
    if (s->get(o->files.main_file) > level)
        s->set(o->files.main_file, level);
    if (o->files.extra_file && s->get(o->files.extra_file) > level)
        s->set(o->files.extra_file, level);
}

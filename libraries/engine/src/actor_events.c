/* Actor event storage converted from the engine's native group adapters.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_events.h"
#include <string.h>

MMActorEventTable *mm_actor_events_create(const MMActorEventFactory *factory, MMU32 entries, MMU32 variable)
{
    MMCollectionStorage storage;
    MMActorEventTable *table;
    if (mm_storage_init(&storage, factory ? factory->allocator : NULL) != MM_STORAGE_OK)
        return NULL;
    table = (MMActorEventTable *)storage.allocator.resize(storage.allocator.context, NULL, 0,
                                                           sizeof(*table), MM_STORAGE_NORMAL);
    if (!table)
        return NULL;
    memset(table, 0, sizeof(*table));
    table->storage = storage;
    if (factory)
    {
        table->storage.min_grow[0] = 16 * MM_ACTOR_EVENT_BYTES;
        table->storage.min_grow[1] = 16 * sizeof(MMGroupLocation);
        table->rows.trash_unused = factory->trash_unused;
    }
    table->rows.storage = &table->storage;
    table->rows.fixed_bytes = MM_ACTOR_EVENT_BYTES;
    table->rows.free_count = -1;
    if ((entries || variable) && !mm_actor_events_reserve(table, entries, variable))
    {
        mm_actor_events_release(table);
        return NULL;
    }
    return table;
}

void mm_actor_events_release(MMActorEventTable *table)
{
    MMStorageAllocator allocator;
    if (!table)
        return;
    allocator = table->storage.allocator;
    if (mm_storage_dispose(&table->storage) == MM_STORAGE_OK)
        allocator.release(allocator.context, table, sizeof(*table));
}

MMActorEventTable *mm_actor_events_clone(const MMActorEventTable *source)
{
    MMActorEventFactory factory;
    MMActorEventTable *table;
    factory.allocator = &source->storage.allocator;
    factory.trash_unused = source->rows.trash_unused;
    table = mm_actor_events_create(&factory, 0, 0);
    if (!table)
        return NULL;
    table->storage.min_grow[0] = source->storage.min_grow[0];
    table->storage.min_grow[1] = source->storage.min_grow[1];
    if (mm_storage_copy(&table->storage, &source->storage, source->rows.bytes,
                        source->rows.count * sizeof(MMGroupLocation)) != MM_STORAGE_OK)
    {
        mm_actor_events_release(table);
        return NULL;
    }
    table->rows.count = source->rows.count;
    table->rows.bytes = source->rows.bytes;
    return table;
}

MMI32 mm_actor_events_count(const MMActorEventTable *table)
{
    return (MMI32)table->rows.count;
}

void *mm_actor_events_payload(const MMActorEventTable *table, MMU32 index, MMI32 *bytes)
{
    MMGroupLocation loc;
    if (mm_group_location(&table->rows, index, &loc) != MM_STORAGE_OK)
        return NULL;
    if (bytes)
        *bytes = loc.bytes - MM_ACTOR_EVENT_BYTES;
    return table->storage.buffers[0].data + loc.offset + MM_ACTOR_EVENT_BYTES;
}

void *mm_actor_events_fixed(const MMActorEventTable *table, MMU32 index)
{
    unsigned char *data = (unsigned char *)mm_actor_events_payload(table, index, NULL);
    return data ? data - MM_ACTOR_EVENT_BYTES : NULL;
}

MMI32 mm_actor_events_size(const MMActorEventTable *table, MMU32 index)
{
    MMI32 bytes = -1;
    mm_actor_events_payload(table, index, &bytes);
    return bytes;
}

int mm_actor_events_get_fixed(const MMActorEventTable *table, MMU32 index, void *fixed)
{
    const void *data = mm_actor_events_fixed(table, index);
    if (!data)
        return 0;
    memmove(fixed, data, MM_ACTOR_EVENT_BYTES);
    return 1;
}

int mm_actor_events_get_payload(const MMActorEventTable *table, MMU32 index, void *payload)
{
    MMI32 bytes;
    const void *data = mm_actor_events_payload(table, index, &bytes);
    if (!data)
        return 0;
    if (bytes)
        memmove(payload, data, bytes);
    return 1;
}

int mm_actor_events_set_fixed(MMActorEventTable *table, MMU32 index, const void *fixed)
{
    void *data = mm_actor_events_fixed(table, index);
    if (!data)
        return 0;
    memmove(data, fixed, MM_ACTOR_EVENT_BYTES);
    return 1;
}

int mm_actor_events_set_payload(MMActorEventTable *table, MMU32 index, const void *payload)
{
    MMI32 bytes;
    void *data = mm_actor_events_payload(table, index, &bytes);
    if (!data)
        return 0;
    if (bytes)
        memmove(data, payload, bytes);
    return 1;
}

int mm_actor_events_copy_payload(MMActorEventTable *table, MMU32 destination, MMU32 source)
{
    MMI32 bytes;
    const void *data = mm_actor_events_payload(table, source, &bytes);
    return data && mm_actor_events_size(table, destination) == bytes &&
           mm_actor_events_set_payload(table, destination, data);
}

int mm_actor_events_insert(MMActorEventTable *table, MMU32 index, MMU32 bytes, const void *payload, const void *fixed)
{
    return mm_group_insert(&table->rows, index, bytes, payload, fixed) == MM_STORAGE_OK;
}

int mm_actor_events_delete(MMActorEventTable *table, MMU32 index)
{
    return mm_group_delete(&table->rows, index) == MM_STORAGE_OK;
}

int mm_actor_events_swap(MMActorEventTable *table, MMU32 first, MMU32 second)
{
    return mm_group_swap(&table->rows, first, second) == MM_STORAGE_OK;
}

int mm_actor_events_reserve(MMActorEventTable *table, MMU32 entries, MMU32 variable)
{
    return mm_group_reserve(&table->rows, entries, variable, 0) == MM_STORAGE_OK;
}

int mm_actor_events_lock(MMActorEventTable *table)
{
    return mm_storage_lock(&table->storage, 0) == MM_STORAGE_OK;
}

int mm_actor_events_unlock(MMActorEventTable *table)
{
    return mm_storage_unlock(&table->storage, 0) == MM_STORAGE_OK;
}

static MMU32 event_width(void *p)
{
    return ((MMActorEventTable *)p)->rows.fixed_bytes;
}

static MMU32 event_count(void *p)
{
    return ((MMActorEventTable *)p)->rows.count;
}

static void *event_fixed(void *p, MMU32 index) { return mm_actor_events_fixed((MMActorEventTable *)p, index); }
static MMI32 event_size(void *p, MMU32 index) { return mm_actor_events_size((MMActorEventTable *)p, index); }
static void *event_data(void *p, MMU32 index) { return mm_actor_events_payload((MMActorEventTable *)p, index, NULL); }
static void *new_events(void *p, MMU32 width)
{
    return width == MM_ACTOR_EVENT_BYTES ? mm_actor_events_create((MMActorEventFactory *)p, 0, 0) : NULL;
}
static int insert_event(void *p, MMU32 index, MMU32 bytes, const void *data, const void *fixed)
{
    MMActorEventTable *table = (MMActorEventTable *)p;
    int status = mm_group_insert(&table->rows, index, bytes, data, fixed);
    return status == MM_STORAGE_OK ? 1 : status == MM_STORAGE_MEMORY ? 0 : -1;
}
static void release_events(void *p) { mm_actor_events_release((MMActorEventTable *)p); }

MMActorEventServices mm_actor_events_services(MMActorEventFactory *factory)
{
    MMActorEventServices services = {
        NULL, event_width, event_count, event_fixed, event_size, event_data, new_events, insert_event, release_events
    };
    services.context = factory;
    return services;
}

static int event_layout(const MMActorEventTable *table, int system, MMCollectionHeader *header)
{
    const unsigned short one = 1;
    memset(header, 0, sizeof(*header));
    header->kind = MM_COLLECTION_GROUP;
    header->system = system;
    header->big_endian = *(const unsigned char *)&one == 0;
    header->width = table->rows.fixed_bytes;
    header->count = table->rows.count;
    header->bytes = table->rows.bytes;
    header->free_count = table->rows.free_count;
    return mm_collection_layout(header) == MM_STORAGE_OK;
}

MMU32 mm_actor_events_wire_size(const MMActorEventTable *table)
{
    MMCollectionHeader header;
    if (!event_layout(table, 0, &header))
        return 0;
    return header.header_bytes + header.first_bytes + header.second_bytes;
}

int mm_actor_events_write(MMActorEventTable *table, int system, int other, const MMStorageWriter *writer)
{
    MMCollectionHeader header;
    unsigned char bytes[20];
    int result;
    if (!event_layout(table, system, &header))
        return 0;
    if (other)
        header.big_endian = !header.big_endian;
    if (mm_collection_header_write(&header, bytes, sizeof(bytes)) != MM_STORAGE_OK)
        return 0;
    if (other)
        mm_group_swap_locations(&table->rows);
    result = mm_storage_write(&table->storage, writer, bytes, header.header_bytes, header.first_bytes,
                              header.second_bytes) == MM_STORAGE_OK;
    if (other)
        mm_group_swap_locations(&table->rows);
    return result;
}

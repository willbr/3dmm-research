/* Actor event storage converted from the engine's native group adapters.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_EVENTS_H
#define MM_ENGINE_ACTOR_EVENTS_H
#include "actor_data.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMActorEventTable
    {
        MMGroup rows;
        MMCollectionStorage storage;
    } MMActorEventTable;
    typedef struct MMActorEventFactory
    {
        const MMStorageAllocator *allocator;
        int trash_unused;
    } MMActorEventFactory;
    /* Owns the row bytes only. Track owners retain and close any runtime tags. */
    MMActorEventTable *mm_actor_events_create(const MMActorEventFactory *, MMU32 entries, MMU32 variable);
    MMActorEventTable *mm_actor_events_clone(const MMActorEventTable *);
    void mm_actor_events_release(MMActorEventTable *);
    MMI32 mm_actor_events_count(const MMActorEventTable *);
    /* Views have four-byte alignment and are valid until the next mutation. */
    void *mm_actor_events_fixed(const MMActorEventTable *, MMU32 index);
    void *mm_actor_events_payload(const MMActorEventTable *, MMU32 index, MMI32 *bytes);
    MMI32 mm_actor_events_size(const MMActorEventTable *, MMU32 index);
    int mm_actor_events_get_fixed(const MMActorEventTable *, MMU32 index, void *);
    int mm_actor_events_get_payload(const MMActorEventTable *, MMU32 index, void *);
    int mm_actor_events_set_fixed(MMActorEventTable *, MMU32 index, const void *);
    int mm_actor_events_set_payload(MMActorEventTable *, MMU32 index, const void *);
    int mm_actor_events_copy_payload(MMActorEventTable *, MMU32 destination, MMU32 source);
    int mm_actor_events_insert(MMActorEventTable *, MMU32 index, MMU32 bytes, const void *payload, const void *fixed);
    int mm_actor_events_delete(MMActorEventTable *, MMU32 index);
    int mm_actor_events_swap(MMActorEventTable *, MMU32 first, MMU32 second);
    int mm_actor_events_reserve(MMActorEventTable *, MMU32 entries, MMU32 variable);
    int mm_actor_events_lock(MMActorEventTable *);
    int mm_actor_events_unlock(MMActorEventTable *);
    /* The factory is borrowed during the synchronous codec operation. */
    MMActorEventServices mm_actor_events_services(MMActorEventFactory *);
    /* The caller encodes payloads in the requested byte order. The writer
     * converts the collection header and location table. */
    MMU32 mm_actor_events_wire_size(const MMActorEventTable *);
    int mm_actor_events_write(MMActorEventTable *, int system, int other_order, const MMStorageWriter *);
#ifdef __cplusplus
}
#endif
#endif

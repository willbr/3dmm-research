/* Actor path storage converted from the engine's native array adapters.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_PATH_H
#define MM_ENGINE_ACTOR_PATH_H
#include "actor_data.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMActorPath
    {
        MMCollectionStorage storage;
        MMArray rows;
    } MMActorPath;
    typedef struct MMActorPathFactory
    {
        const MMStorageAllocator *allocator;
        int trash_unused;
    } MMActorPathFactory;
    /* Heap-owned path with 16-byte native-order rows. Borrowed rows are
     * invalidated by resize or disposal. */
    MMActorPath *mm_actor_path_create(const MMStorageAllocator *, MMU32 reserve, int trash_unused);
    MMActorPath *mm_actor_path_clone(const MMActorPath *);
    void mm_actor_path_release(MMActorPath *);
    MMI32 mm_actor_path_count(const MMActorPath *);
    int mm_actor_path_reserve(MMActorPath *, MMU32 additional);
    int mm_actor_path_get(const MMActorPath *, MMU32 index, void *row);
    int mm_actor_path_insert(MMActorPath *, MMU32 index, const void *row);
    int mm_actor_path_put(MMActorPath *, MMU32 index, const void *row);
    int mm_actor_path_copy(MMActorPath *destination, MMU32 to, const MMActorPath *source, MMU32 first, MMU32 count);
    int mm_actor_path_layout(MMU32 count, int system, MMCollectionHeader *);
    MMU32 mm_actor_path_wire_size(const MMActorPath *);
    int mm_actor_path_write(MMActorPath *, int system, const MMStorageWriter *);
    MMActorRouteReader mm_actor_path_reader(const MMActorPathFactory *);
    MMActorPath *mm_actor_path_read(const MMReadStream *, const MMActorPathFactory *);
#ifdef __cplusplus
}
#endif
#endif

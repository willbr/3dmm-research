/* Resource caching converted from Kauai crf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_RESOURCE_CACHE_H
#define MM_ENGINE_RESOURCE_CACHE_H
#include "chunky_file.h"
#include "block_owner.h"
#include "collections.h"
#include "wide.h"
#include "reference.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Function-pointer keys are cast back by the host before comparison/calling.
     * No function/data pointer conversions or ordered function comparisons enter C. */
    typedef void (*MMResourceReader)(void);
    enum { MM_RESOURCE_NORMAL_PRIORITY = 2 };
    typedef struct MMResourceCache MMResourceCache;
    typedef struct MMResourceObject
    {
        MMResourceCache *cache;
        MMU32 type, number;
        MMI32 priority;
        int attached;
    } MMResourceObject;
    enum
    {
        MM_RESOURCE_BAD_REFS,
        MM_RESOURCE_BAD_OBJECT,
        MM_RESOURCE_DUPLICATE,
        MM_RESOURCE_BAD_CHARGE,
        MM_RESOURCE_LOAD_FAILED,
        MM_RESOURCE_BAD_OWNER,
        MM_RESOURCE_BAD_RETAIN,
        MM_RESOURCE_BAD_BLOCK
    };
    typedef struct MMResourceObjectServices
    {
        MMResourceObject *(*state)(void *object);
        MMReference *(*references)(void *object);
        void (*destroy)(void *object);
        void (*issue)(void *object, int kind);
    } MMResourceObjectServices;
    void mm_resource_object_init(MMResourceObject *);
    void mm_resource_object_dispose(void *, const MMResourceObjectServices *);
    void mm_resource_object_release(void *, const MMResourceObjectServices *);
    void mm_resource_object_detach(void *, const MMResourceObjectServices *);
    void mm_resource_object_priority(void *, MMI32, const MMResourceObjectServices *);
    typedef struct MMResourceEntry
    {
        MMResourceReader reader;
        MMI32 released;
        void *object;
        MMI32 bytes;
    } MMResourceEntry;
    typedef struct MMResourceRequest
    {
        MMU32 type, number, scope;
        MMResourceReader reader;
        MMI32 priority;
    } MMResourceRequest;
    typedef struct MMResourceServices
    {
        void *context;
        MMResourceObjectServices objects;
        int (*compare_reader)(MMResourceReader, MMResourceReader);
        int (*contains)(void *file, MMU32 type, MMU32 number);
        MMChunkAccess (*live_chunks)(void *file);
        const MMHandleServices *handles;
        const MMBlockFileServices *block_files;
        /* Report an existing chunk's borrowed physical extent and packed bit.
         * The cache owns the temporary block from lookup through publication. */
        int (*locate)(void *file, MMU32 type, MMU32 number, MMBlockFile *, int *packed);
        int (*read)(void *, MMResourceReader, MMU32 type, MMU32 number, MMOwnedBlock *,
                    void **object, MMI32 *bytes);
        void (*retain_file)(void *file);
        void (*release_file)(void *file);
        void (*destroy_owner)(void *owner);
        void (*issue)(void *, int kind, MMU32 type, MMU32 number);
    } MMResourceServices;
    /* Select exactly one source. A wire index and backing file describe the
     * same immutable archive. */
    typedef struct MMResourceSource
    {
        void *live;
        const MMChunkIndex *index;
        void *backing;
    } MMResourceSource;
    struct MMResourceCache
    {
        MMCollectionStorage storage;
        MMResourceServices services;
        void *owner, *file;
        MMChunkIndex *wire_index;
        void *wire_file;
        MMReference *references;
        MMU32 count;
        MMI32 budget, releases;
        MMWide resident;
        int diagnostic_refs;
    };
    /* Cache and object lifetimes cover their borrowed C counters. State and
     * reference accessors are side-effect free. The cache owns its file reference
     * and array storage. Attached objects link to the cache without retaining it.
     * Detached objects retain it until disposal. Callbacks receive copied entries
     * or object pointers that are independent of movable array storage. Readers
     * may load other keys. Destructors may release other objects and change the
     * table, so insertion searches again and eviction scans restart.
     * Readers borrow the block for the call. Preload passes the same owning
     * record to the estimate and object read, including changes to its view.
     * Recursive loading of the same key and disposal re-entry are invalid.
     * Reader success transfers a fresh object reference and a nonnegative i32 charge.
     * Priority retains the original signed-16-bit storage behavior. */
    int mm_resource_cache_init(MMResourceCache *, void *owner, MMReference *references,
                               const MMResourceSource *, MMI32 budget, const MMResourceServices *,
                               const MMStorageAllocator *, int diagnostic_refs);
    /* Borrow the current graph for synchronous lookup. Reacquire after edits. */
    MMChunkAccess mm_resource_cache_chunks(const MMResourceCache *);
    /* Fill an initialized block with the stored extent from this cache's
     * source. The caller owns the block, which is empty on a miss. */
    int mm_resource_cache_read_block(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *);
    int mm_resource_cache_reserve(MMResourceCache *, MMU32 additional);
    /* A final release may destroy the owner and cache before returning. */
    void mm_resource_cache_release(MMResourceCache *);
    int mm_resource_cache_dispose(MMResourceCache *);
    int mm_resource_cache_at(const MMResourceCache *, MMU32, MMResourceEntry *);
    void mm_resource_cache_budget(MMResourceCache *, MMI32);
    int mm_resource_cache_purge(MMResourceCache *, MMWide bytes, MMI32 last_priority);
    int mm_resource_cache_load(MMResourceCache *, const MMResourceRequest *);
    void *mm_resource_cache_fetch(MMResourceCache *, const MMResourceRequest *, int *error);
    /* Fetch an unscoped chunk with normal priority and return its owned reference. */
    void *mm_resource_cache_fetch_chunk(MMResourceCache *, MMU32 type, MMU32 number, MMResourceReader);
    int mm_resource_cache_contains(MMResourceCache *, MMU32 type, MMU32 number, MMU32 scope);
    void *mm_resource_cache_find(MMResourceCache *, MMU32 type, MMU32 number,
                                       MMResourceReader, MMU32 scope);
    int mm_resource_cache_priority(MMResourceCache *, MMI32, MMU32 type, MMU32 number,
                                         MMResourceReader, MMU32 scope);
    typedef struct MMResourceManagerServices
    {
        void *context;
        /* Success transfers one reference to the initialized cache's owner. */
        MMResourceCache *(*create)(void *, void *file, MMI32 budget);
    } MMResourceManagerServices;
    typedef struct MMResourceManager
    {
        MMCollectionStorage storage;
        MMResourceManagerServices services;
        MMU32 count;
    } MMResourceManager;
    /* Manager adopts each newly created cache reference, releases in reverse order,
     * and captures the initial count for forward searches. Query callbacks may append
     * caches but cannot remove entries or dispose the manager. Fetch stops on object
     * OR error; preload stops on YES or MAYBE; cached find only checks the first file
     * containing the requested chunk/scope and omits scope on the final cache find. */
    int mm_resource_manager_init(MMResourceManager *, const MMResourceManagerServices *,
                                       const MMStorageAllocator *);
    int mm_resource_manager_reserve(MMResourceManager *, MMU32);
    int mm_resource_manager_dispose(MMResourceManager *);
    MMResourceCache *mm_resource_manager_at(const MMResourceManager *, MMU32);
    /* IO denotes failed cache creation; MEMORY denotes failed append. Output is
     * unchanged on either failure. Append failure releases the new cache. */
    int mm_resource_manager_add(MMResourceManager *, void *file, MMI32 budget, MMU32 *position);
    int mm_resource_manager_load(MMResourceManager *, const MMResourceRequest *);
    void *mm_resource_manager_fetch(MMResourceManager *, const MMResourceRequest *, int *error);
    MMResourceCache *mm_resource_manager_find_file(MMResourceManager *, MMU32 type,
                                                    MMU32 number, MMU32 scope);
    void *mm_resource_manager_find(MMResourceManager *, const MMResourceRequest *);
    int mm_resource_manager_priority(MMResourceManager *, const MMResourceRequest *);
#ifdef __cplusplus
}
#endif
#endif

/* Resource caching converted from Kauai crf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_RESOURCE_CACHE_H
#define MM_ENGINE_RESOURCE_CACHE_H
#include "collections.h"
#include "wide.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Function-pointer keys are cast back by the host before comparison/calling.
     * No function/data pointer conversions or ordered function comparisons enter C. */
    typedef void (*MMResourceReader)(void);
    typedef struct MMResourceObject
    {
        void *owner;
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
        MM_RESOURCE_BAD_OWNER
    };
    typedef struct MMResourceObjectServices
    {
        MMResourceObject *(*state)(void *object);
        MMI32 (*refs)(void *object);
        void (*set_refs)(void *object, MMI32);
        void (*retain)(void *object);
        void (*release)(void *object);
        void (*detach)(void *object);
        void (*priority)(void *object, MMI32);
        void (*destroy)(void *object);
        void (*retain_owner)(void *owner);
        void (*release_owner)(void *owner);
        void (*released)(void *owner, void *object);
        void (*detached)(void *owner, void *object);
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
    typedef struct MMResourceServices
    {
        void *context;
        MMResourceObjectServices objects;
        int (*compare_reader)(MMResourceReader, MMResourceReader);
        int (*contains)(void *, MMU32 type, MMU32 number);
        int (*find_block)(void *, MMU32 type, MMU32 number, void *block);
        int (*read)(void *, MMResourceReader, MMU32 type, MMU32 number, void *block, void **object,
                    MMI32 *bytes);
        void (*retain_file)(void *file);
        void (*release_file)(void *file);
        MMI32 (*owner_refs)(void *);
        void (*set_owner_refs)(void *, MMI32);
        void (*issue)(void *, int kind, MMU32 type, MMU32 number);
    } MMResourceServices;
    typedef struct MMResourceCache
    {
        MMCollectionStorage storage;
        MMResourceServices services;
        void *owner, *file;
        MMU32 count;
        MMI32 budget, releases;
        MMWide resident;
        int diagnostic_refs;
    } MMResourceCache;
    /* Cache owns its file reference and array storage. Attached objects retain no
     * owner reference; detached objects retain one. Every callback uses copied
     * entries, never pointers into movable array storage. Readers may load other
     * resources; insertion searches again afterwards. Detach callbacks may remove
     * other entries/change references; purge/zero-budget scans restart accordingly.
     * Recursive loading of the same key and disposal re-entry are invalid.
     * Reader success transfers a fresh object reference and a nonnegative i32 charge.
     * Priority retains the original signed-16-bit storage behavior. */
    int mm_resource_cache_init(MMResourceCache *, void *owner, void *file, MMI32 budget,
                                     const MMResourceServices *, const MMStorageAllocator *,
                                     int diagnostic_refs);
    int mm_resource_cache_reserve(MMResourceCache *, MMU32 additional);
    int mm_resource_cache_dispose(MMResourceCache *);
    int mm_resource_cache_at(const MMResourceCache *, MMU32, MMResourceEntry *);
    void mm_resource_cache_budget(MMResourceCache *, MMI32);
    int mm_resource_cache_purge(MMResourceCache *, MMWide bytes, MMI32 last_priority);
    int mm_resource_cache_load(MMResourceCache *, MMU32 type, MMU32 number, MMResourceReader,
                                     MMU32 scope, MMI32 priority, void *block);
    void *mm_resource_cache_fetch(MMResourceCache *, MMU32 type, MMU32 number,
                                        MMResourceReader, MMU32 scope, void *block, int *error);
    int mm_resource_cache_contains(MMResourceCache *, MMU32 type, MMU32 number, MMU32 scope);
    void *mm_resource_cache_find(MMResourceCache *, MMU32 type, MMU32 number,
                                       MMResourceReader, MMU32 scope);
    int mm_resource_cache_priority(MMResourceCache *, MMI32, MMU32 type, MMU32 number,
                                         MMResourceReader, MMU32 scope);
    void mm_resource_cache_detached(MMResourceCache *, void *object);
    void mm_resource_cache_released(MMResourceCache *, void *object);
    typedef struct MMResourceRequest
    {
        MMU32 type, number, scope;
        MMResourceReader reader;
        MMI32 priority;
    } MMResourceRequest;
    typedef struct MMResourceManagerServices
    {
        void *context;
        void *(*create)(void *, void *file, MMI32 budget);
        void (*release)(void *cache);
        int (*load)(void *cache, const MMResourceRequest *);
        void *(*fetch)(void *cache, const MMResourceRequest *, int *error);
        void *(*find)(void *cache, const MMResourceRequest *);
        int (*priority)(void *cache, const MMResourceRequest *);
        int (*contains)(void *cache, MMU32 type, MMU32 number, MMU32 scope);
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
    void *mm_resource_manager_at(const MMResourceManager *, MMU32);
    /* IO denotes failed cache creation; MEMORY denotes failed append. Output is
     * unchanged on either failure. Append failure releases the new cache. */
    int mm_resource_manager_add(MMResourceManager *, void *file, MMI32 budget, MMU32 *position);
    int mm_resource_manager_load(MMResourceManager *, const MMResourceRequest *);
    void *mm_resource_manager_fetch(MMResourceManager *, const MMResourceRequest *, int *error);
    void *mm_resource_manager_find_file(MMResourceManager *, MMU32 type, MMU32 number,
                                              MMU32 scope);
    void *mm_resource_manager_find(MMResourceManager *, const MMResourceRequest *);
    int mm_resource_manager_priority(MMResourceManager *, const MMResourceRequest *);
#ifdef __cplusplus
}
#endif
#endif

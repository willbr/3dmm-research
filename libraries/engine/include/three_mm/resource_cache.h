/* Resource caching converted from Kauai crf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_RESOURCE_CACHE_H
#define THREE_MM_ENGINE_RESOURCE_CACHE_H
#include "collections.h"
#include "wide.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Function-pointer keys are cast back by the host before comparison/calling.
     * No function/data pointer conversions or ordered function comparisons enter C. */
    typedef void (*ThreeMMResourceReader)(void);
    typedef struct ThreeMMResourceObject
    {
        void *owner;
        ThreeMMU32 type, number;
        ThreeMMI32 priority;
        int attached;
    } ThreeMMResourceObject;
    enum
    {
        THREE_MM_RESOURCE_BAD_REFS,
        THREE_MM_RESOURCE_BAD_OBJECT,
        THREE_MM_RESOURCE_DUPLICATE,
        THREE_MM_RESOURCE_BAD_CHARGE,
        THREE_MM_RESOURCE_LOAD_FAILED,
        THREE_MM_RESOURCE_BAD_OWNER
    };
    typedef struct ThreeMMResourceObjectServices
    {
        ThreeMMResourceObject *(*state)(void *object);
        ThreeMMI32 (*refs)(void *object);
        void (*set_refs)(void *object, ThreeMMI32);
        void (*retain)(void *object);
        void (*release)(void *object);
        void (*detach)(void *object);
        void (*priority)(void *object, ThreeMMI32);
        void (*destroy)(void *object);
        void (*retain_owner)(void *owner);
        void (*release_owner)(void *owner);
        void (*released)(void *owner, void *object);
        void (*detached)(void *owner, void *object);
        void (*issue)(void *object, int kind);
    } ThreeMMResourceObjectServices;
    void three_mm_resource_object_init(ThreeMMResourceObject *);
    void three_mm_resource_object_dispose(void *, const ThreeMMResourceObjectServices *);
    void three_mm_resource_object_release(void *, const ThreeMMResourceObjectServices *);
    void three_mm_resource_object_detach(void *, const ThreeMMResourceObjectServices *);
    void three_mm_resource_object_priority(void *, ThreeMMI32, const ThreeMMResourceObjectServices *);
    typedef struct ThreeMMResourceEntry
    {
        ThreeMMResourceReader reader;
        ThreeMMI32 released;
        void *object;
        ThreeMMI32 bytes;
    } ThreeMMResourceEntry;
    typedef struct ThreeMMResourceServices
    {
        void *context;
        ThreeMMResourceObjectServices objects;
        int (*compare_reader)(ThreeMMResourceReader, ThreeMMResourceReader);
        int (*contains)(void *, ThreeMMU32 type, ThreeMMU32 number);
        int (*find_block)(void *, ThreeMMU32 type, ThreeMMU32 number, void *block);
        int (*read)(void *, ThreeMMResourceReader, ThreeMMU32 type, ThreeMMU32 number, void *block, void **object,
                    ThreeMMI32 *bytes);
        void (*retain_file)(void *file);
        void (*release_file)(void *file);
        ThreeMMI32 (*owner_refs)(void *);
        void (*set_owner_refs)(void *, ThreeMMI32);
        void (*issue)(void *, int kind, ThreeMMU32 type, ThreeMMU32 number);
    } ThreeMMResourceServices;
    typedef struct ThreeMMResourceCache
    {
        ThreeMMCollectionStorage storage;
        ThreeMMResourceServices services;
        void *owner, *file;
        ThreeMMU32 count;
        ThreeMMI32 budget, releases;
        ThreeMMWide resident;
        int diagnostic_refs;
    } ThreeMMResourceCache;
    /* Cache owns its file reference and array storage. Attached objects retain no
     * owner reference; detached objects retain one. Every callback uses copied
     * entries, never pointers into movable array storage. Readers may load other
     * resources; insertion searches again afterwards. Detach callbacks may remove
     * other entries/change references; purge/zero-budget scans restart accordingly.
     * Recursive loading of the same key and disposal re-entry are invalid.
     * Reader success transfers a fresh object reference and a nonnegative i32 charge.
     * Priority retains the original signed-16-bit storage behavior. */
    int three_mm_resource_cache_init(ThreeMMResourceCache *, void *owner, void *file, ThreeMMI32 budget,
                                     const ThreeMMResourceServices *, const ThreeMMStorageAllocator *,
                                     int diagnostic_refs);
    int three_mm_resource_cache_reserve(ThreeMMResourceCache *, ThreeMMU32 additional);
    int three_mm_resource_cache_dispose(ThreeMMResourceCache *);
    int three_mm_resource_cache_at(const ThreeMMResourceCache *, ThreeMMU32, ThreeMMResourceEntry *);
    void three_mm_resource_cache_budget(ThreeMMResourceCache *, ThreeMMI32);
    int three_mm_resource_cache_purge(ThreeMMResourceCache *, ThreeMMWide bytes, ThreeMMI32 last_priority);
    int three_mm_resource_cache_load(ThreeMMResourceCache *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMResourceReader,
                                     ThreeMMU32 scope, ThreeMMI32 priority, void *block);
    void *three_mm_resource_cache_fetch(ThreeMMResourceCache *, ThreeMMU32 type, ThreeMMU32 number,
                                        ThreeMMResourceReader, ThreeMMU32 scope, void *block, int *error);
    int three_mm_resource_cache_contains(ThreeMMResourceCache *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 scope);
    void *three_mm_resource_cache_find(ThreeMMResourceCache *, ThreeMMU32 type, ThreeMMU32 number,
                                       ThreeMMResourceReader, ThreeMMU32 scope);
    int three_mm_resource_cache_priority(ThreeMMResourceCache *, ThreeMMI32, ThreeMMU32 type, ThreeMMU32 number,
                                         ThreeMMResourceReader, ThreeMMU32 scope);
    void three_mm_resource_cache_detached(ThreeMMResourceCache *, void *object);
    void three_mm_resource_cache_released(ThreeMMResourceCache *, void *object);
    typedef struct ThreeMMResourceRequest
    {
        ThreeMMU32 type, number, scope;
        ThreeMMResourceReader reader;
        ThreeMMI32 priority;
    } ThreeMMResourceRequest;
    typedef struct ThreeMMResourceManagerServices
    {
        void *context;
        void *(*create)(void *, void *file, ThreeMMI32 budget);
        void (*release)(void *cache);
        int (*load)(void *cache, const ThreeMMResourceRequest *);
        void *(*fetch)(void *cache, const ThreeMMResourceRequest *, int *error);
        void *(*find)(void *cache, const ThreeMMResourceRequest *);
        int (*priority)(void *cache, const ThreeMMResourceRequest *);
        int (*contains)(void *cache, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 scope);
    } ThreeMMResourceManagerServices;
    typedef struct ThreeMMResourceManager
    {
        ThreeMMCollectionStorage storage;
        ThreeMMResourceManagerServices services;
        ThreeMMU32 count;
    } ThreeMMResourceManager;
    /* Manager adopts each newly created cache reference, releases in reverse order,
     * and captures the initial count for forward searches. Query callbacks may append
     * caches but cannot remove entries or dispose the manager. Fetch stops on object
     * OR error; preload stops on YES or MAYBE; cached find only checks the first file
     * containing the requested chunk/scope and omits scope on the final cache find. */
    int three_mm_resource_manager_init(ThreeMMResourceManager *, const ThreeMMResourceManagerServices *,
                                       const ThreeMMStorageAllocator *);
    int three_mm_resource_manager_reserve(ThreeMMResourceManager *, ThreeMMU32);
    int three_mm_resource_manager_dispose(ThreeMMResourceManager *);
    void *three_mm_resource_manager_at(const ThreeMMResourceManager *, ThreeMMU32);
    /* IO denotes failed cache creation; MEMORY denotes failed append. Output is
     * unchanged on either failure. Append failure releases the new cache. */
    int three_mm_resource_manager_add(ThreeMMResourceManager *, void *file, ThreeMMI32 budget, ThreeMMU32 *position);
    int three_mm_resource_manager_load(ThreeMMResourceManager *, const ThreeMMResourceRequest *);
    void *three_mm_resource_manager_fetch(ThreeMMResourceManager *, const ThreeMMResourceRequest *, int *error);
    void *three_mm_resource_manager_find_file(ThreeMMResourceManager *, ThreeMMU32 type, ThreeMMU32 number,
                                              ThreeMMU32 scope);
    void *three_mm_resource_manager_find(ThreeMMResourceManager *, const ThreeMMResourceRequest *);
    int three_mm_resource_manager_priority(ThreeMMResourceManager *, const ThreeMMResourceRequest *);
#ifdef __cplusplus
}
#endif
#endif

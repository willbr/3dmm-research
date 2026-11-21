/* Tag policy converted from engine tagman.cpp and tagl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TAG_H
#define MM_ENGINE_TAG_H
#include "resource_cache.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_TAG_PARENT_NOT_FOUND = 5
    };
    typedef struct MMTag
    {
        MMI32 sid;
        void *cache;
        MMU32 type, number;
    } MMTag;
    typedef struct MMTagServices
    {
        void *context;
        void *(*file)(void *cache);
        void (*retain)(void *cache);
        void (*release)(void *cache);
        int (*compare_cache)(void *, void *);
        int (*copy)(void *source, MMU32 type, MMU32 number, void *destination, MMU32 *new_number);
        int (*copy_cached)(void *source, MMU32 type, MMU32 number, void *destination, MMU32 *new_number);
        MMResourceCache *(*cache)(void *owner);
        MMResourceManager *(*source)(void *, MMI32 sid, int local_only, const MMTag *asset);
        void (*missing_parent)(void *);
        void (*missing)(void *, const MMTag *);
        void (*publish)(void *, const MMTag *);
    } MMTagServices;
    /* sid 0 owns a cache reference after open/child construction. Positive sids
     * resolve through the source provider and ignore cache identity. Open-copy failure
     * clears cache, save-copy failure changes nothing. Redirect retains destination
     * before releasing old cache; close clears cache after release. Optional publish
     * mirrors state at legacy callback points; callbacks must not mutate/re-enter it.
     * Close accepts sid -1. Other operations require initialized nonnegative tags. */
    int mm_tag_open(MMTag *, void *destination, void *source_file, const MMTagServices *);
    int mm_tag_save(MMTag *, void *destination, int redirect, const MMTagServices *);
    void mm_tag_dup(const MMTag *, const MMTagServices *);
    void mm_tag_close(MMTag *, const MMTagServices *);
    int mm_tag_compare(const MMTag *, const MMTag *, const MMTagServices *);
    int mm_tag_child(const MMTag *, MMU32 slot, MMU32 type, MMTag *out,
                           const MMTagServices *);
    void *mm_tag_fetch(const MMTag *, MMResourceLoader, int use_media, const MMTagServices *);
    /* File caching is currently disabled in the legacy engine. This does no I/O. */
    int mm_tag_cache_file(const MMTag *, int children);
    typedef struct MMTagCacheServices
    {
        void *context;
        MMU32 (*sources)(void *);
        void (*source_at)(void *, MMU32, MMI32 *, MMResourceManager **);
        MMResourceManager *(*resolve)(void *, MMI32);
        void (*reopen)(void *file);
    } MMTagCacheServices;
    /* flags 1=file, 2=memory. File clearing resolves media first, then ignores
     * reopen failure; memory clearing sets zero budget then restores the old value.
     * Captures initial source/cache counts and visits live files of resolved sources. */
    void mm_tag_clear_cache(MMI32 sid, MMU32 flags, const MMTagCacheServices *);
    typedef struct MMTagCached
    {
        MMTag tag;
        int children;
    } MMTagCached;
    typedef struct MMTagChild
    {
        MMU32 slot, type;
    } MMTagChild;
    typedef struct MMTagList
    {
        MMCollectionStorage storage;
        MMU32 count, bytes;
    } MMTagList;
    typedef struct MMTagListServices
    {
        void *context;
        int (*cache)(void *, const MMTag *, int children);
        int (*child)(void *, const MMTag *, MMU32 slot, MMU32 type, MMTag *);
    } MMTagListServices;
    /* Owns storage, borrows tags without retaining caches. Tags sort/deduplicate by
     * sid/type/number, deliberately ignoring embedded cache identity. First tag wins;
     * child-cache flags only become true. Children sort by type then slot and retain
     * duplicates. Cache callbacks may not mutate/re-enter this list. As in legacy
     * FCacheTags, returned child tags are passed to cache without a close operation. */
    int mm_tag_list_init(MMTagList *, const MMStorageAllocator *);
    int mm_tag_list_dispose(MMTagList *);
    int mm_tag_list_get(MMTagList *, MMU32, MMTagCached *);
    int mm_tag_list_insert(MMTagList *, const MMTag *, int children);
    int mm_tag_list_child(MMTagList *, const MMTag *, MMU32 slot, MMU32 type);
    int mm_tag_list_cache(MMTagList *, const MMTagListServices *);
#ifdef __cplusplus
}
#endif
#endif

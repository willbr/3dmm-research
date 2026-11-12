/* Tag policy converted from engine tagman.cpp and tagl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_TAG_H
#define THREE_MM_ENGINE_TAG_H
#include "resource_cache.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_TAG_PARENT_NOT_FOUND = 5
    };
    typedef struct ThreeMMTag
    {
        ThreeMMI32 sid;
        void *cache;
        ThreeMMU32 type, number;
    } ThreeMMTag;
    typedef struct ThreeMMTagServices
    {
        void *context;
        void *(*file)(void *cache);
        void (*retain)(void *cache);
        void (*release)(void *cache);
        int (*compare_cache)(void *, void *);
        int (*copy)(void *source, ThreeMMU32 type, ThreeMMU32 number, void *destination, ThreeMMU32 *new_number);
        void *(*source)(void *, ThreeMMI32 sid, int local_only, const ThreeMMTag *asset);
        void *(*find_cache)(void *manager, ThreeMMU32 type, ThreeMMU32 number);
        int (*child)(void *cache, const ThreeMMTag *, ThreeMMU32 slot, ThreeMMU32 type, ThreeMMU32 *number);
        void *(*fetch)(void *cache_or_manager, int manager, const ThreeMMTag *, ThreeMMResourceReader);
        void (*missing)(void *, const ThreeMMTag *);
        void (*publish)(void *, const ThreeMMTag *);
    } ThreeMMTagServices;
    /* sid 0 owns a cache reference after open/child construction. Positive sids
     * resolve through the source provider and ignore cache identity. Open-copy failure
     * clears cache, save-copy failure changes nothing. Redirect retains destination
     * before releasing old cache; close clears cache after release. Optional publish
     * mirrors state at legacy callback points; callbacks must not mutate/re-enter it.
     * Close accepts sid -1. Other operations require initialized nonnegative tags. */
    int three_mm_tag_open(ThreeMMTag *, void *destination, void *source_file, const ThreeMMTagServices *);
    int three_mm_tag_save(ThreeMMTag *, void *destination, int redirect, const ThreeMMTagServices *);
    void three_mm_tag_dup(const ThreeMMTag *, const ThreeMMTagServices *);
    void three_mm_tag_close(ThreeMMTag *, const ThreeMMTagServices *);
    int three_mm_tag_compare(const ThreeMMTag *, const ThreeMMTag *, const ThreeMMTagServices *);
    int three_mm_tag_child(const ThreeMMTag *, ThreeMMU32 slot, ThreeMMU32 type, ThreeMMTag *out,
                           const ThreeMMTagServices *);
    void *three_mm_tag_fetch(const ThreeMMTag *, ThreeMMResourceReader, int use_media, const ThreeMMTagServices *);
    /* File caching is currently disabled in the legacy engine. This does no I/O. */
    int three_mm_tag_cache_file(const ThreeMMTag *, int children);
    typedef struct ThreeMMTagCacheServices
    {
        void *context;
        ThreeMMU32 (*sources)(void *);
        void (*source_at)(void *, ThreeMMU32, ThreeMMI32 *, void **manager);
        void *(*resolve)(void *, ThreeMMI32);
        ThreeMMU32 (*caches)(void *manager);
        void *(*cache_at)(void *manager, ThreeMMU32);
        void *(*file)(void *cache);
        void (*reopen)(void *file);
        ThreeMMI32 (*budget)(void *cache);
        void (*set_budget)(void *cache, ThreeMMI32);
    } ThreeMMTagCacheServices;
    /* flags 1=file, 2=memory. File clearing resolves media first, then ignores
     * reopen failure; memory clearing sets zero budget then restores the old value.
     * Captures initial source/cache counts and skips unresolved sources. */
    void three_mm_tag_clear_cache(ThreeMMI32 sid, ThreeMMU32 flags, const ThreeMMTagCacheServices *);
    typedef struct ThreeMMTagCached
    {
        ThreeMMTag tag;
        int children;
    } ThreeMMTagCached;
    typedef struct ThreeMMTagChild
    {
        ThreeMMU32 slot, type;
    } ThreeMMTagChild;
    typedef struct ThreeMMTagList
    {
        ThreeMMCollectionStorage storage;
        ThreeMMU32 count, bytes;
    } ThreeMMTagList;
    typedef struct ThreeMMTagListServices
    {
        void *context;
        int (*cache)(void *, const ThreeMMTag *, int children);
        int (*child)(void *, const ThreeMMTag *, ThreeMMU32 slot, ThreeMMU32 type, ThreeMMTag *);
    } ThreeMMTagListServices;
    /* Owns storage, borrows tags without retaining caches. Tags sort/deduplicate by
     * sid/type/number, deliberately ignoring embedded cache identity. First tag wins;
     * child-cache flags only become true. Children sort by type then slot and retain
     * duplicates. Cache callbacks may not mutate/re-enter this list. As in legacy
     * FCacheTags, returned child tags are passed to cache without a close operation. */
    int three_mm_tag_list_init(ThreeMMTagList *, const ThreeMMStorageAllocator *);
    int three_mm_tag_list_dispose(ThreeMMTagList *);
    int three_mm_tag_list_get(ThreeMMTagList *, ThreeMMU32, ThreeMMTagCached *);
    int three_mm_tag_list_insert(ThreeMMTagList *, const ThreeMMTag *, int children);
    int three_mm_tag_list_child(ThreeMMTagList *, const ThreeMMTag *, ThreeMMU32 slot, ThreeMMU32 type);
    int three_mm_tag_list_cache(ThreeMMTagList *, const ThreeMMTagListServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Source catalogue policy converted from engine tagman.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SOURCE_H
#define MM_ENGINE_SOURCE_H
#include "collections.h"
#include "resource_cache.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SOURCE_LEGACY,
        MM_SOURCE_AVAILABLE,
        MM_SOURCE_DISABLED,
        MM_SOURCE_MISSING
    };
    typedef struct MMSourceRecord
    {
        MMI32 sid;
        int local_content; /* 0=no, 1=yes, 2=unknown */
        void *manager, *paths;
    } MMSourceRecord;
    typedef struct MMSourceOwners
    {
        void *context;
        void *(*new_paths)(void *);
        void (*copy_paths)(void *destination, const void *source);
        void (*release_paths)(void *);
        void (*release_manager)(void *);
    } MMSourceOwners;
    typedef struct MMSourceCatalogue
    {
        MMCollectionStorage storage;
        MMSourceOwners owners;
        MMU32 count;
    } MMSourceCatalogue;
    typedef struct MMSourceServices
    {
        void *context;
        /* kind 0=query without output, 1=query local-path output. */
        int (*access)(void *, MMI32 sid, void *work_paths, int kind);
        int (*path_empty)(void *paths, int media);
        void (*clear_local)(void *paths);
        int (*build_local)(void *, MMI32 sid, void *paths, int *exists);
        int (*archive_source)(void *, MMI32 sid);
        int (*content)(void *, void *paths, int *exists);
        int (*find_media)(void *, MMI32 sid, void *paths, int *changed);
        int (*retry)(void *, MMI32 sid);
        void *(*new_manager)(void *, MMI32 sid, void *paths, int media);
        void (*output)(void *, void *paths, int media);
        void (*moved_open_source)(void *); /* Optional legacy diagnostic, not a failure. */
        int (*prepare_file)(void *);
        int (*find_leaf)(void *);
        void (*clear_output)(void *);
    } MMSourceServices;
    /* Catalogue uniquely owns paths and manager references. Initialized record cannot
     * move/copy. Work paths are caller-owned scratch in the same path-service domain;
     * queries copy stored paths to scratch and publish at legacy mutation points.
     * Callbacks may append other sources but, after acquisition, cannot invalidate
     * the active sid. Disposal callbacks cannot re-enter. Returned records/managers
     * are borrowed.
     * Disposal releases managers in forward order, then paths and array storage. */
    int mm_source_init(MMSourceCatalogue *, const MMSourceOwners *, const MMStorageAllocator *);
    int mm_source_dispose(MMSourceCatalogue *);
    int mm_source_at(const MMSourceCatalogue *, MMU32, MMSourceRecord *);
    int mm_source_invalidate(MMSourceCatalogue *, MMI32 sid);
    void *mm_source_manager(MMSourceCatalogue *, MMI32 sid, int local_only, void *work_paths,
                                  const MMSourceServices *);
    int mm_source_local_content(MMSourceCatalogue *, MMI32 sid, void *work_paths, int *exists,
                                      const MMSourceServices *);
    int mm_source_local_path(MMSourceCatalogue *, MMI32 sid, void *work_paths,
                                   const MMSourceServices *);
    int mm_source_media_path(MMSourceCatalogue *, MMI32 sid, int ask, void *work_paths,
                                   const MMSourceServices *);
    int mm_source_find_file(MMSourceCatalogue *, MMI32 sid, int ask, void *work_paths,
                                  const MMSourceServices *);
    typedef struct MMSourceBuildServices
    {
        void *context;
        int (*archive_source)(void *, MMI32 sid);
        int (*begin)(void *);
        int (*next)(void *, MMI32 sid, MMU32 index, int archive);
        MMResourceCache *(*open)(void *, MMI32 budget);
    } MMSourceBuildServices;
    /* Ordered archive sets bypass directory enumeration. False next means normal
     * end, as in legacy enumeration. Open transfers one cache reference. This
     * operation returns an owned manager or releases all partial state on failure. */
    MMResourceManager *mm_source_build(MMI32 sid, MMI32 budget, const MMSourceBuildServices *,
                                      const MMStorageAllocator *owner, const MMStorageAllocator *storage);
#ifdef __cplusplus
}
#endif
#endif

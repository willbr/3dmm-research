/* Source catalogue policy converted from engine tagman.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SOURCE_H
#define THREE_MM_ENGINE_SOURCE_H
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_SOURCE_LEGACY,
        THREE_MM_SOURCE_AVAILABLE,
        THREE_MM_SOURCE_DISABLED,
        THREE_MM_SOURCE_MISSING
    };
    typedef struct ThreeMMSourceRecord
    {
        ThreeMMI32 sid;
        int local_content; /* 0=no, 1=yes, 2=unknown */
        void *manager, *paths;
    } ThreeMMSourceRecord;
    typedef struct ThreeMMSourceOwners
    {
        void *context;
        void *(*new_paths)(void *);
        void (*copy_paths)(void *destination, const void *source);
        void (*release_paths)(void *);
        void (*release_manager)(void *);
    } ThreeMMSourceOwners;
    typedef struct ThreeMMSourceCatalogue
    {
        ThreeMMCollectionStorage storage;
        ThreeMMSourceOwners owners;
        ThreeMMU32 count;
    } ThreeMMSourceCatalogue;
    typedef struct ThreeMMSourceServices
    {
        void *context;
        /* kind 0=query without output, 1=query local-path output. */
        int (*access)(void *, ThreeMMI32 sid, void *work_paths, int kind);
        int (*path_empty)(void *paths, int media);
        void (*clear_local)(void *paths);
        int (*build_local)(void *, ThreeMMI32 sid, void *paths, int *exists);
        int (*archive_source)(void *, ThreeMMI32 sid);
        int (*content)(void *, void *paths, int *exists);
        int (*find_media)(void *, ThreeMMI32 sid, void *paths, int *changed);
        int (*retry)(void *, ThreeMMI32 sid);
        void *(*new_manager)(void *, ThreeMMI32 sid, void *paths, int media);
        void (*output)(void *, void *paths, int media);
        void (*moved_open_source)(void *); /* Optional legacy diagnostic, not a failure. */
        int (*prepare_file)(void *);
        int (*find_leaf)(void *);
        void (*clear_output)(void *);
    } ThreeMMSourceServices;
    /* Catalogue uniquely owns paths and manager references. Initialized record cannot
     * move/copy. Work paths are caller-owned scratch in the same path-service domain;
     * queries copy stored paths to scratch and publish at legacy mutation points.
     * Callbacks may append other sources but, after acquisition, cannot invalidate
     * the active sid. Disposal callbacks cannot re-enter. Returned records/managers
     * are borrowed.
     * Disposal releases managers in forward order, then paths and array storage. */
    int three_mm_source_init(ThreeMMSourceCatalogue *, const ThreeMMSourceOwners *, const ThreeMMStorageAllocator *);
    int three_mm_source_dispose(ThreeMMSourceCatalogue *);
    int three_mm_source_at(const ThreeMMSourceCatalogue *, ThreeMMU32, ThreeMMSourceRecord *);
    int three_mm_source_invalidate(ThreeMMSourceCatalogue *, ThreeMMI32 sid);
    void *three_mm_source_manager(ThreeMMSourceCatalogue *, ThreeMMI32 sid, int local_only, void *work_paths,
                                  const ThreeMMSourceServices *);
    int three_mm_source_local_content(ThreeMMSourceCatalogue *, ThreeMMI32 sid, void *work_paths, int *exists,
                                      const ThreeMMSourceServices *);
    int three_mm_source_local_path(ThreeMMSourceCatalogue *, ThreeMMI32 sid, void *work_paths,
                                   const ThreeMMSourceServices *);
    int three_mm_source_media_path(ThreeMMSourceCatalogue *, ThreeMMI32 sid, int ask, void *work_paths,
                                   const ThreeMMSourceServices *);
    int three_mm_source_find_file(ThreeMMSourceCatalogue *, ThreeMMI32 sid, int ask, void *work_paths,
                                  const ThreeMMSourceServices *);
    typedef struct ThreeMMSourceBuildServices
    {
        void *context;
        void *(*create)(void *);
        int (*archive_source)(void *, ThreeMMI32 sid);
        int (*begin)(void *);
        int (*next)(void *, ThreeMMI32 sid, ThreeMMU32 index, int archive);
        void *(*open)(void *);
        int (*add)(void *manager, void *file, ThreeMMI32 budget);
        void (*release_file)(void *);
        void (*release_manager)(void *);
    } ThreeMMSourceBuildServices;
    /* Ordered archive sets bypass directory enumeration. False next means normal
     * end, as in legacy enumeration. Failure releases the current file and partial
     * manager; successful adds consume no caller file reference. */
    void *three_mm_source_build(ThreeMMI32 sid, ThreeMMI32 budget, const ThreeMMSourceBuildServices *);
#ifdef __cplusplus
}
#endif
#endif

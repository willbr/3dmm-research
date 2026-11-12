/* Source discovery converted from engine tagman.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SOURCE_DISCOVERY_H
#define THREE_MM_ENGINE_SOURCE_DISCOVERY_H
#include "source.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMSourceDiscovery
    {
        void *context;
        int (*access)(void *, ThreeMMI32 sid);
        int (*names)(void *, ThreeMMI32 sid);
        void (*root)(void *);
        int (*descend)(void *, int short_name);
        void (*output)(void *);
        int (*known)(void *);
        int (*ensure_known)(void *);
        int (*begin_drives)(void *);
        int (*next_drive)(void *);
        int (*floppy)(void *);
        void (*candidate)(void *);
    } ThreeMMSourceDiscovery;
    /* Local tries long then short on one root candidate. Media never searches after
     * a known-path miss, skips floppy drives, and tries short then long per drive.
     * Failed descent uses the same candidate for fallback, as in legacy Filename.
     * Access providers may update output directly. Callbacks are synchronous. */
    int three_mm_source_discover_local(ThreeMMI32 sid, const ThreeMMSourceDiscovery *, int *exists);
    int three_mm_source_discover_media(ThreeMMI32 sid, const ThreeMMSourceDiscovery *, int *changed);
#ifdef __cplusplus
}
#endif
#endif

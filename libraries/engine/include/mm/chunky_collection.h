/* Chunky collection compilation converted from Kauai chcm.cpp by ShonK.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNKY_COLLECTION_H
#define MM_ENGINE_CHUNKY_COLLECTION_H

#include "chunky_source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*MMChunkyCollectionWrite)(void *, const MMStorageWriter *);

typedef struct MMChunkyCollectionHost
{
    void *context;
    const MMStorageAllocator *allocator;
    int trash_unused;
    /* Sticky compiler error state, including diagnostics sent to source. */
    int (*has_error)(void *);
    void (*clear_payload)(void *);
    /* Read the requested prefix of the current source payload. */
    void (*read_payload)(void *, void *, MMU32 count);
    /* Allocate the output block, invoke write synchronously, and publish it.
     * The supplied object and its buffers are borrowed only during this call. */
    int (*save)(void *, MMU32 bytes, MMChunkyCollectionWrite write, void *collection);
} MMChunkyCollectionHost;

/* Starts at the collection's opening parenthesis. Accepts any of the six
 * array, group, or string-table directives as kind. Diagnostics go to source. */
void mm_chunky_parse_collection(const MMChunkySource *, const MMChunkyCollectionHost *, int kind);

#ifdef __cplusplus
}
#endif
#endif

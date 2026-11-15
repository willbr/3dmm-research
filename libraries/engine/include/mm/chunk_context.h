/* Container links converted from Kauai BaseLinkedList.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_CONTEXT_H
#define MM_ENGINE_CHUNK_CONTEXT_H
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkLink MMChunkLink;
    typedef struct MMChunkContext
    {
        MMChunkLink *first;
    } MMChunkContext;
    struct MMChunkLink
    {
        MMChunkLink *next, **previous;
        MMChunkContext *context;
        void *owner;
    };
    /* Explicit independent registries; a zero-initialized context is empty.
     * Initialize links once before use. Attached context/link addresses must remain
     * stable. Links borrow owner tokens and do not retain them. Hosts detach after
     * disposing associated resources, before freeing the owner. No callbacks or
     * allocation occur here. Repeated detach is safe; attaching twice is rejected.
     * Initialize contexts only before their first use, never while nonempty. */
    void mm_chunk_context_init(MMChunkContext *);
    void mm_chunk_link_init(MMChunkLink *, void *owner);
    int mm_chunk_context_attach(MMChunkContext *, MMChunkLink *);
    int mm_chunk_context_detach(MMChunkLink *);
    int mm_chunk_context_valid(const MMChunkLink *);
    void *mm_chunk_context_first(const MMChunkContext *);
    void *mm_chunk_context_next(const MMChunkLink *);
#ifdef __cplusplus
}
#endif
#endif

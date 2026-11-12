/* Container links converted from Kauai BaseLinkedList.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_CONTEXT_H
#define THREE_MM_ENGINE_CHUNK_CONTEXT_H
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkLink ThreeMMChunkLink;
    typedef struct ThreeMMChunkContext
    {
        ThreeMMChunkLink *first;
    } ThreeMMChunkContext;
    struct ThreeMMChunkLink
    {
        ThreeMMChunkLink *next, **previous;
        ThreeMMChunkContext *context;
        void *owner;
    };
    /* Explicit independent registries; a zero-initialized context is empty.
     * Initialize links once before use. Attached context/link addresses must remain
     * stable. Links borrow owner tokens and do not retain them. Hosts detach after
     * disposing associated resources, before freeing the owner. No callbacks or
     * allocation occur here. Repeated detach is safe; attaching twice is rejected.
     * Initialize contexts only before their first use, never while nonempty. */
    void three_mm_chunk_context_init(ThreeMMChunkContext *);
    void three_mm_chunk_link_init(ThreeMMChunkLink *, void *owner);
    int three_mm_chunk_context_attach(ThreeMMChunkContext *, ThreeMMChunkLink *);
    int three_mm_chunk_context_detach(ThreeMMChunkLink *);
    int three_mm_chunk_context_valid(const ThreeMMChunkLink *);
    void *three_mm_chunk_context_first(const ThreeMMChunkContext *);
    void *three_mm_chunk_context_next(const ThreeMMChunkLink *);
#ifdef __cplusplus
}
#endif
#endif

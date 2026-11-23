/* Container links converted from Kauai BaseLinkedList.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_CONTEXT_H
#define MM_ENGINE_CHUNK_CONTEXT_H
#include "chunky_file.h"
#include "reference.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkLink MMChunkLink;
    struct MMChunkOwner;
    typedef struct MMChunkContext
    {
        MMChunkLink *first;
    } MMChunkContext;
    struct MMChunkLink
    {
        MMChunkLink *next, **previous;
        MMChunkContext *context;
        void *owner;
        struct MMChunkOwner *state;
        const MMReference *references;
    };
    /* Explicit independent registries; a zero-initialized context is empty.
     * Initialize links once before use. Attached context/link addresses must remain
     * stable. Links borrow the owner token, state and reference count. Hosts
     * detach before freeing the owner. Attachment and detachment use no callbacks
     * or allocation. Repeated detach is safe; attaching twice is rejected.
     * Initialize contexts only before their first use, never while nonempty. */
    void mm_chunk_context_init(MMChunkContext *);
    void mm_chunk_link_init(MMChunkLink *, void *owner, struct MMChunkOwner *, const MMReference *);
    int mm_chunk_context_attach(MMChunkContext *, MMChunkLink *);
    int mm_chunk_context_detach(MMChunkLink *);
    int mm_chunk_context_valid(const MMChunkLink *);
    void *mm_chunk_context_first(const MMChunkContext *);
    void *mm_chunk_context_next(const MMChunkLink *);
    /* Borrow the first valid main owner for this file. Report each matching
     * extra file encountered before the match through the optional hook. */
    void *mm_chunk_context_find(const MMChunkContext *, void *file, void (*extra_collision)(void *));
    /* Diagnostic hooks are optional and must leave the registry intact. Destroy
     * must detach the current link and leave other links live. Callers serialize
     * lookup and registry mutation. */
    void mm_chunk_context_clear_marks(const MMChunkContext *, void (*validate)(void *));
    void mm_chunk_context_close_unmarked(const MMChunkContext *, void (*validate)(void *),
                                         void (*destroy)(void *));
#ifdef __cplusplus
}
#endif
#endif

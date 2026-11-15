/* Graph traversal converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_GRAPH_H
#define MM_ENGINE_CHUNK_GRAPH_H
#include "chunky_file.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_WALK_PRE = 0x10,
        MM_WALK_POST = 0x20,
        MM_WALK_ROOT = 0x40,
        MM_WALK_ERROR = 0x80
    };
    enum
    {
        MM_WALK_START,
        MM_WALK_GO,
        MM_WALK_NO_SKIP,
        MM_WALK_DONE
    };
    typedef struct MMChunkGraph
    {
        void *context;
        /* Trusted live graph lookups. Children stay ordered; callbacks must not
         * re-enter/mutate the walker. Graph edits between yielded events are allowed,
         * as in deletion: traversal stores identities/cursors, not index positions. */
        MMU32 (*children)(void *, MMU32 type, MMU32 number);
        int (*child)(void *, MMU32 type, MMU32 number, MMU32 position, MMChunkChild *);
    } MMChunkGraph;
    typedef struct MMChunkWalkFrame
    {
        MMChunkChild node;
        MMU32 next;
    } MMChunkWalkFrame;
    typedef struct MMChunkWalk
    {
        MMCollectionStorage stack;
        MMChunkGraph graph;
        MMChunkWalkFrame current;
        MMU32 depth;
        int phase, created;
    } MMChunkWalk;
    /* Owns lazy C stack storage. Initialize once, reset for each traversal, dispose
     * before destruction; do not copy the owning record. No graph/file references
     * are retained. Stack reservation starts at ten frames, then uses the existing
     * collection growth policy. Native DynamicArray object allocation is removed. */
    int mm_chunk_walk_init(MMChunkWalk *, const MMStorageAllocator *);
    int mm_chunk_walk_reset(MMChunkWalk *, const MMChunkGraph *, MMU32 type, MMU32 number);
    int mm_chunk_walk_dispose(MMChunkWalk *);
    /* 1 yields an event; 0 ends without publishing outputs. Leaves root parent and
     * slot invalid (C slot is zero). A failed push yields PRE|POST|ERROR and resumes
     * siblings. Skip suppresses the last node's descendants and post event only when
     * that node was yielded as PRE; it is ignored after leaf/post events. Stack is
     * released at completion, early root skip, reset and disposal. */
    int mm_chunk_walk_next(MMChunkWalk *, int skip, MMChunkChild *, MMChunkChild *parent,
                                 MMU32 *flags);
    /* Legacy target-reference shortcut and match-before-error policy: zero target
     * references returns 0 immediately; a matched ERROR node returns 1. Otherwise
     * return 0=no, 1=yes, 2=unknown after traversal allocation failure. */
    int mm_chunk_reaches(const MMChunkGraph *, const MMStorageAllocator *, MMU32 root_type,
                               MMU32 root_number, MMU32 target_type, MMU32 target_number,
                               MMU32 target_references);
#ifdef __cplusplus
}
#endif
#endif

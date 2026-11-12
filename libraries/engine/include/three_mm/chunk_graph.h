/* Graph traversal converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_GRAPH_H
#define THREE_MM_ENGINE_CHUNK_GRAPH_H
#include "chunky_file.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_WALK_PRE = 0x10,
        THREE_MM_WALK_POST = 0x20,
        THREE_MM_WALK_ROOT = 0x40,
        THREE_MM_WALK_ERROR = 0x80
    };
    enum
    {
        THREE_MM_WALK_START,
        THREE_MM_WALK_GO,
        THREE_MM_WALK_NO_SKIP,
        THREE_MM_WALK_DONE
    };
    typedef struct ThreeMMChunkGraph
    {
        void *context;
        /* Trusted live graph lookups. Children stay ordered; callbacks must not
         * re-enter/mutate the walker. Graph edits between yielded events are allowed,
         * as in deletion: traversal stores identities/cursors, not index positions. */
        ThreeMMU32 (*children)(void *, ThreeMMU32 type, ThreeMMU32 number);
        int (*child)(void *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 position, ThreeMMChunkChild *);
    } ThreeMMChunkGraph;
    typedef struct ThreeMMChunkWalkFrame
    {
        ThreeMMChunkChild node;
        ThreeMMU32 next;
    } ThreeMMChunkWalkFrame;
    typedef struct ThreeMMChunkWalk
    {
        ThreeMMCollectionStorage stack;
        ThreeMMChunkGraph graph;
        ThreeMMChunkWalkFrame current;
        ThreeMMU32 depth;
        int phase, created;
    } ThreeMMChunkWalk;
    /* Owns lazy C stack storage. Initialize once, reset for each traversal, dispose
     * before destruction; do not copy the owning record. No graph/file references
     * are retained. Stack reservation starts at ten frames, then uses the existing
     * collection growth policy. Native DynamicArray object allocation is removed. */
    int three_mm_chunk_walk_init(ThreeMMChunkWalk *, const ThreeMMStorageAllocator *);
    int three_mm_chunk_walk_reset(ThreeMMChunkWalk *, const ThreeMMChunkGraph *, ThreeMMU32 type, ThreeMMU32 number);
    int three_mm_chunk_walk_dispose(ThreeMMChunkWalk *);
    /* 1 yields an event; 0 ends without publishing outputs. Leaves root parent and
     * slot invalid (C slot is zero). A failed push yields PRE|POST|ERROR and resumes
     * siblings. Skip suppresses the last node's descendants and post event only when
     * that node was yielded as PRE; it is ignored after leaf/post events. Stack is
     * released at completion, early root skip, reset and disposal. */
    int three_mm_chunk_walk_next(ThreeMMChunkWalk *, int skip, ThreeMMChunkChild *, ThreeMMChunkChild *parent,
                                 ThreeMMU32 *flags);
    /* Legacy target-reference shortcut and match-before-error policy: zero target
     * references returns 0 immediately; a matched ERROR node returns 1. Otherwise
     * return 0=no, 1=yes, 2=unknown after traversal allocation failure. */
    int three_mm_chunk_reaches(const ThreeMMChunkGraph *, const ThreeMMStorageAllocator *, ThreeMMU32 root_type,
                               ThreeMMU32 root_number, ThreeMMU32 target_type, ThreeMMU32 target_number,
                               ThreeMMU32 target_references);
#ifdef __cplusplus
}
#endif
#endif

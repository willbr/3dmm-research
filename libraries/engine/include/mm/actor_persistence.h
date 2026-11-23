/* Actor persistence converted from actrsave.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_PERSISTENCE_H
#define MM_ENGINE_ACTOR_PERSISTENCE_H
#include "actor_owner.h"
#include "actor_track.h"
#include "movie_session.h"
#ifdef __cplusplus
extern "C" {
#endif

enum MMActorLoadDiagnostic
{
    MM_ACTOR_LOAD_TEMPLATE_LINK,
    MM_ACTOR_LOAD_TEMPLATE_SOURCE,
    MM_ACTOR_LOAD_FAILED
};

typedef struct MMActorLoadServices
{
    void *context;
    const MMTagServices *tags;
    const MMStorageAllocator *allocator;
    int trash_unused;
    /* Allocate an actor with an initialized owner. */
    void *(*create)(void *, MMActorOwner **);
    void (*release)(void *, void *actor);
    void *(*fetch_template)(void *, const MMTag *);
    /* Bind the fetched template when an older header needs its last frame. */
    MMActorTimelineServices (*timeline)(void *, void *actor);
    void (*diagnostic)(void *, int);
} MMActorLoadServices;

/* Return a movie status and transfer an owned table on success only. */
int mm_actor_movie_path(const MMMovieSessionGraph *, MMU32 number, const MMActorPathFactory *, MMActorPath **);
int mm_actor_movie_events(const MMMovieSessionGraph *, MMU32 number, MMActorEventFactory *, MMActorEventTable **);
/* Borrow the block and decode it in place before reading the native header or
 * transferring an owned path. A failed header read may modify its output. */
int mm_actor_block_header(MMOwnedBlock *, void *decode_context, MMBlockTransform, MMActorHeader *);
MMActorPath *mm_actor_block_path(MMOwnedBlock *, void *decode_context, MMBlockTransform, const MMActorPathFactory *);
/* Decode the owned block in place and transfer a validated runtime table. */
int mm_actor_block_events(MMOwnedBlock *, void *decode_context, MMBlockTransform,
                          MMActorEventFactory *, MMActorEventTable **);
/* Borrow the runtime table. The returned wire table uses the source allocator
 * and owns no tag references. Block writers require preallocated storage. */
MMActorEventTable *mm_actor_encode_events(MMActorEventTable *);
int mm_actor_write_path_block(MMActorPath *, int system, MMOwnedBlock *);
int mm_actor_write_events_block(MMActorEventTable *, int system, int foreign_order, MMOwnedBlock *);

/* On failure report the diagnostic before disposing any partially loaded actor.
 * The actor's owner releases tracks, opened tags and fetched template references. */
void *mm_actor_load(const MMMovieSessionGraph *, MMU32 actor, void *cache, const MMActorLoadServices *);

typedef struct MMActorStoreServices
{
    void *context;
    const MMArchiveWriter *archive;
    MMMovieEmbeddedArchive assets;
    const MMActorTimelineServices *timeline;
    void (*missing_first_frame)(void *);
} MMActorStoreServices;

/* Write into the new ACTR under a candidate scene. On failure the caller
 * drops the candidate scene and its children. Track ownership is unchanged.
 * Frame caches may change before the write, including on a partial refresh. */
int mm_actor_store(MMU32 actor, MMU32 scene, MMActorOwner *, const MMActorStoreServices *);

typedef struct MMActorTrackArchive
{
    void *context;
    int (*create)(void *, MMArchiveWriter *);
    int (*save)(void *);
} MMActorTrackArchive;

/* Encode before creating the scratch archive. Write GGAE:0, then PATH:0
 * if requested. The caller owns the file and can discard it after failure. */
int mm_actor_store_edit_tracks(const MMActorTrack *, int with_route, const MMActorTrackArchive *);

#ifdef __cplusplus
}
#endif
#endif

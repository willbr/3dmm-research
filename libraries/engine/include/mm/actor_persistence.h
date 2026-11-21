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
    int (*lifetime)(void *, void *actor);
    void (*diagnostic)(void *, int);
} MMActorLoadServices;

/* Return a movie status and transfer an owned table on success only. */
int mm_actor_movie_path(const MMMovieSessionGraph *, MMU32 number, const MMActorPathFactory *, MMActorPath **);
int mm_actor_movie_events(const MMMovieSessionGraph *, MMU32 number, MMActorEventFactory *, MMActorEventTable **);
/* Decode the owned block in place and transfer a validated runtime table. */
int mm_actor_block_events(MMOwnedBlock *, void *decode_context, MMBlockTransform,
                          MMActorEventFactory *, MMActorEventTable **);

/* On failure report the diagnostic before disposing any partially loaded actor.
 * The actor's owner releases tracks, opened tags and fetched template references. */
void *mm_actor_load(const MMMovieSessionGraph *, MMU32 actor, void *cache, const MMActorLoadServices *);

typedef struct MMActorStoreServices
{
    void *context;
    const MMArchiveWriter *archive;
    MMMovieEmbeddedArchive assets;
    int (*refresh_lifetime)(void *);
    void (*missing_first_frame)(void *);
    /* Write the native template object and return its allocated chunk number. */
    int (*write_template)(void *, void *object, MMU32 type, MMU32 *written);
} MMActorStoreServices;

/* Write into the new ACTR under a candidate scene. On failure the caller
 * drops the candidate scene and its children. Live track data is borrowed. */
int mm_actor_store(MMU32 actor, MMU32 scene, const MMActorOwner *, const MMActorStoreServices *);

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

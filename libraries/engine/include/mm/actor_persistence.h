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

/* On failure report the diagnostic before disposing any partially loaded actor.
 * The actor's owner releases tracks, opened tags and fetched template references. */
void *mm_actor_load(const MMMovieSessionGraph *, MMU32 actor, void *cache, const MMActorLoadServices *);

typedef struct MMActorStoreServices
{
    void *context;
    MMActorHeaderIO header;
    MMMovieEmbeddedArchive materials;
    int system;
    int (*refresh_lifetime)(void *);
    void (*missing_first_frame)(void *);
    int (*write_template)(void *, void *object, MMU32 type, MMU32 number);
    /* Add PATH/0 or GGAE/0 and return a synchronous positional writer. */
    int (*add_track)(void *, MMU32 actor, MMU32 type, MMU32 bytes, MMStorageWriter *);
} MMActorStoreServices;

/* Write into the new ACTR under a candidate scene. On failure the caller
 * drops the candidate scene and its children. Live track data is borrowed. */
int mm_actor_store(MMU32 actor, MMU32 scene, const MMActorOwner *, const MMActorStoreServices *);

#ifdef __cplusplus
}
#endif
#endif

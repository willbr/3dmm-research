/* Movie ownership and persistence converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_OWNER_H
#define MM_ENGINE_MOVIE_OWNER_H
#include "chunk_storage.h"
#include "movie_data.h"
#include "movie_title.h"
#include "resource_cache.h"
#include "rollcall_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMRollCallActor
    {
        MMMovieActor data;
        void *cache;
    } MMRollCallActor;
    typedef struct MMRollCallReader
    {
        void *context;
        MMStringConvert convert;
        int (*open_actor)(void *, MMRollCallActor *, void *resources);
        void (*close_actor)(void *, MMRollCallActor *);
    } MMRollCallReader;
    struct MMMovieSession;
    typedef int (*MMMovieOpenSession)(void *context, void *file, MMU32 *, struct MMMovieSession **);
    typedef struct MMMovieState
    {
        void *resources;
        struct MMMovieSession *session;
        MMU32 movieNumber;
        MMI32 actorIdLimit, sceneCount;
        MMMovieTitle title;
        MMRollCallTable actors;
    } MMMovieState;
    typedef struct MMMovieOwnerServices
    {
        void *context;
        MMStorageAllocator actor_allocator;
        int actor_encoding, actor_trash;
        void *(*create_file)(void *);
        void *(*open_file)(void *, void *path);
        MMMovieOpenSession open_session;
        void *(*new_resources)(void *, void *file);
        void (*release_file)(void *);
        void (*release_resources)(void *);
        void *(*file)(void *resources);
        MMArchiveWriter (*archive)(void *file);
        MMResourceCache *(*cache)(void *resources);
        MMRollCallReader rollcall;
        MMBlockTransform decode_rollcall;
        void (*missing_rollcall)(void *), (*unlink_failed)(void *);
        MMChunkTitleIO titles;
        int (*save)(void *, void *file);
        void (*make_persistent)(void *file);
    } MMMovieOwnerServices;
    /* Unique owning state. The session borrows its graph through the resource
     * reference and is closed before it. The actor table lives in the state and
     * owns its opened tags. Title and actor names use the same native unit width.
     * The archive and cache bindings borrow the active file and resources. A zero
     * actor allocator uses the C heap. Replacement publishes the scratch state
     * on success and disposes the prior table and resources in order.
     * The missing-roll-call and unlink-failed diagnostics are optional.
     * Callbacks cannot mutate/re-enter this state. No rollback after save publication:
     * successful roll-call replacement remains visible if naming/saving later fails. */
    void mm_movie_state_init(MMMovieState *);
    int mm_movie_init_actors(MMMovieState *, const MMMovieOwnerServices *);
    void mm_movie_clear_actors(MMMovieState *, const MMMovieOwnerServices *);
    void mm_movie_release_resources(MMMovieState *, const MMMovieOwnerServices *);
    void mm_movie_state_dispose(MMMovieState *, const MMMovieOwnerServices *);
    int mm_movie_init_empty(MMMovieState *, const MMMovieOwnerServices *);
    int mm_movie_load(MMMovieState *, void *path, MMU32 number, const MMMovieOwnerServices *);
    /* Resolve a scene from the live graph. A new or changed root is attached
     * only after validation. Failed lookups preserve the caller's number. */
    int mm_movie_state_scene_number(MMMovieState *, MMU32 slot, MMU32 *number, void *file,
                                     void *context, MMMovieOpenSession);
    int mm_movie_save(MMMovieState *, const MMMovieOwnerServices *);

    /* Borrows a decoded GST stream and fills an initialized, empty actor table.
     * Failed reads restore an empty table and preserve the caller's ID limit.
     * Failed tag opens acquire no reference. Failed insertions close the current
     * tag before closing earlier rows. Successful rows own their opened tags. */
    int mm_movie_read_rollcall(const MMReadStream *, void *resources, MMRollCallTable *actors, MMI32 *limit,
                                     const MMRollCallReader *);
    enum { MM_ROLLCALL_CHUNK = 0x47535420, MM_ROLLCALL_SLOT = 0 };
    /* Resolve the movie's roll-call child in the cache's current graph, then
     * decode its payload from the same source. A missing link or extent returns
     * MM_CHUNK_NOT_FOUND. Tag operations borrow the separate host resource owner. */
    int mm_movie_load_rollcall(MMResourceCache *, MMU32 movie, void *resources, MMRollCallTable *, MMI32 *limit,
                               MMBlockTransform, const MMRollCallReader *);
    /* Borrows the runtime table and its tags. Serializes names in actor order,
     * with native-order GST extras and zero cache pads. An added chunk is
     * discarded if writing or adoption fails. Callbacks cannot change the source. */
    int mm_movie_write_rollcall(const MMArchiveWriter *, MMU32 movie, MMRollCallTable *, MMU32 *written);
#ifdef __cplusplus
}
#endif
#endif

/* Movie ownership and persistence converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_OWNER_H
#define MM_ENGINE_MOVIE_OWNER_H
#include "movie_data.h"
#include "movie_title.h"
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
    struct MMMovieSession;
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
        void *(*open_file)(void *);
        int (*open_session)(void *, void *file, MMU32 *, struct MMMovieSession **);
        void *(*new_resources)(void *, void *file);
        void (*release_file)(void *);
        void (*release_resources)(void *);
        void (*close_actor)(void *, MMRollCallActor *);
        void *(*file)(void *resources);
        int (*create_root)(void *, void *file, MMU32 *);
        int (*read_rollcall)(void *, void *resources, MMU32, MMRollCallTable *, MMI32 *limit);
        MMChunkTitleIO titles;
        int (*previous_rollcall)(void *, void *file, MMU32 movie, MMU32 *previous);
        int (*write_rollcall)(void *, void *file, MMU32 movie, MMRollCallTable *, MMU32 *written);
        void (*delete_previous)(void *, void *file, MMU32 movie, MMU32 previous);
        int (*save)(void *, void *file);
        void (*make_persistent)(void *file);
    } MMMovieOwnerServices;
    /* Unique owning state. The session borrows its graph through the resource
     * reference and is closed before it. The actor table lives in the state and
     * owns its opened tags. Title and actor names use the same native unit width.
     * A zero actor allocator uses the C heap. Reading fills an initialized empty
     * table and leaves it empty on failure. Replacement publishes the scratch state
     * on success and disposes the prior table and resources in order.
     * Callbacks cannot mutate/re-enter this state. No rollback after save publication:
     * successful roll-call replacement remains visible if naming/saving later fails. */
    void mm_movie_state_init(MMMovieState *);
    int mm_movie_init_actors(MMMovieState *, const MMMovieOwnerServices *);
    void mm_movie_clear_actors(MMMovieState *, const MMMovieOwnerServices *);
    void mm_movie_release_resources(MMMovieState *, const MMMovieOwnerServices *);
    void mm_movie_state_dispose(MMMovieState *, const MMMovieOwnerServices *);
    int mm_movie_init_empty(MMMovieState *, const MMMovieOwnerServices *);
    int mm_movie_load(MMMovieState *, MMU32 number, const MMMovieOwnerServices *);
    /* Resolve a scene from the live graph. A new or changed root is attached
     * only after validation. Failed lookups preserve the caller's number. */
    int mm_movie_state_scene_number(MMMovieState *, MMU32 slot, MMU32 *number, const MMMovieOwnerServices *);
    int mm_movie_save(MMMovieState *, const MMMovieOwnerServices *);

    typedef struct MMRollCallReader
    {
        void *context;
        MMStringConvert convert;
        int (*open_actor)(void *, MMRollCallActor *, void *resources);
        void (*close_actor)(void *, MMRollCallActor *);
    } MMRollCallReader;
    /* Borrows a decoded GST stream and fills an initialized, empty actor table.
     * Failed reads restore an empty table and preserve the caller's ID limit.
     * Failed tag opens acquire no reference. Failed insertions close the current
     * tag before closing earlier rows. Successful rows own their opened tags. */
    int mm_movie_read_rollcall(const MMReadStream *, void *resources, MMRollCallTable *actors, MMI32 *limit,
                                     const MMRollCallReader *);
    typedef struct MMRollCallWriter
    {
        void *context;
        int (*add)(void *, MMU32 bytes, MMU32 *number, MMStorageWriter *);
        int (*adopt)(void *, MMU32 movie, MMU32 number);
        void (*discard)(void *, MMU32 number);
    } MMRollCallWriter;
    /* Borrows the runtime table and its tags. Serializes names in actor order,
     * with native-order GST extras and zero cache pads. An added chunk is
     * discarded if writing or adoption fails. Callbacks cannot change the source. */
    int mm_movie_write_rollcall(MMU32 movie, MMRollCallTable *, MMU32 *written, const MMRollCallWriter *);
#ifdef __cplusplus
}
#endif
#endif

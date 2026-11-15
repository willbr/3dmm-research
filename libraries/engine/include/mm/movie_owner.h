/* Movie ownership and persistence converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_OWNER_H
#define MM_ENGINE_MOVIE_OWNER_H
#include "movie_data.h"
#include "movie_title.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMRollCallActor
    {
        MMMovieActor data;
        void *cache;
    } MMRollCallActor;
    typedef struct MMMovieState
    {
        void *resources, *actors;
        MMU32 movieNumber;
        MMI32 actorIdLimit, sceneCount;
        MMMovieTitle title;
    } MMMovieState;
    typedef struct MMMovieOwnerServices
    {
        void *context;
        void *(*create_file)(void *);
        void *(*open_file)(void *);
        int (*verify)(void *, void *file, MMU32 *);
        void *(*new_resources)(void *, void *file);
        void (*release_file)(void *);
        void (*release_resources)(void *);
        void *(*new_actors)(void *, int wire);
        void (*release_table)(void *);
        MMU32 (*count)(void *table);
        void (*actor)(void *table, MMU32, MMRollCallActor *);
        void (*close_actor)(void *, MMRollCallActor *);
        void *(*file)(void *resources);
        int (*create_root)(void *, void *file, MMU32 *);
        int (*read_rollcall)(void *, void *resources, MMU32, void **actors, MMI32 *limit);
        void (*load_name)(void *, void *file, MMU32, MMMovieTitle *);
        int (*scene)(void *, void *file, MMU32 movie, MMI32 index);
        int (*previous_rollcall)(void *, void *file, MMU32 movie, MMU32 *previous);
        int (*write_rollcall)(void *, void *file, MMU32 movie, void *actors, MMU32 *written);
        void (*delete_previous)(void *, void *file, MMU32 movie, MMU32 previous);
        int (*set_name)(void *, void *file, MMU32 movie, const MMMovieTitle *);
        int (*save)(void *, void *file);
        void (*make_persistent)(void *file);
    } MMMovieOwnerServices;
    /* Unique owning state. File/table handles are transitional host objects; C owns
     * their references, transactions, replacement, title and ordered cleanup. Name
     * loading writes the uncommitted state; replacement publishes it on success.
     * Callbacks cannot mutate/re-enter this state. No rollback after save publication:
     * successful roll-call replacement remains visible if naming/saving later fails. */
    void mm_movie_state_init(MMMovieState *);
    void mm_movie_clear_actors(MMMovieState *, const MMMovieOwnerServices *);
    void mm_movie_release_resources(MMMovieState *, const MMMovieOwnerServices *);
    void mm_movie_state_dispose(MMMovieState *, const MMMovieOwnerServices *);
    int mm_movie_init_empty(MMMovieState *, const MMMovieOwnerServices *);
    int mm_movie_load(MMMovieState *, MMU32 number, const MMMovieOwnerServices *);
    int mm_movie_save(MMMovieState *, const MMMovieOwnerServices *);

    typedef struct MMRollCallServices
    {
        void *context;
        /* Wire acquisition locates the child and decodes its GST header/text, but
         * deliberately does not reject a foreign extra width. C owns that decision. */
        void *(*read_wire)(void *, void *resources, MMU32 movie, int *big_endian);
        MMU32 (*extra_width)(void *);
        MMU32 runtime_width;
        void *(*new_table)(void *, int wire);
        void (*release_table)(void *);
        MMU32 (*count)(void *);
        void (*wire_row)(void *, void *table, MMU32, unsigned char bytes[28]);
        void (*actor)(void *table, MMU32, MMRollCallActor *);
        /* row callbacks cache a borrowed name in host scratch. Appending consumes it
         * immediately. Appends do not duplicate tag ownership. */
        void (*runtime_row)(void *, void *table, MMU32, MMRollCallActor *);
        int (*append_runtime)(void *, void *table, const MMRollCallActor *);
        int (*append_wire)(void *, void *table, const unsigned char bytes[28]);
        int (*open_actor)(void *, MMRollCallActor *, void *resources);
        void (*close_actor)(void *, MMRollCallActor *);
        MMU32 (*wire_size)(void *table);
        int (*add_chunk)(void *, void *file, MMU32 bytes, MMU32 *number);
        int (*write_table)(void *, void *table);
        int (*adopt)(void *, void *file, MMU32 movie, MMU32 number);
        void (*delete_chunk)(void *, void *file, MMU32 number);
    } MMRollCallServices;
    /* Failed read clears output, preserves caller limit, closes the current tag
     * before earlier rows when insertion fails, then releases the runtime table.
     * Wire extras retain their 28-byte layout with a zero pointer slot. */
    int mm_movie_read_rollcall(void *resources, MMU32 movie, void **actors, MMI32 *limit,
                                     const MMRollCallServices *);
    int mm_movie_write_rollcall(void *file, MMU32 movie, void *actors, MMU32 *written,
                                      const MMRollCallServices *);
#ifdef __cplusplus
}
#endif
#endif

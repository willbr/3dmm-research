/* Movie ownership and persistence converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_OWNER_H
#define THREE_MM_ENGINE_MOVIE_OWNER_H
#include "movie_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMRollCallActor
    {
        ThreeMMMovieActor data;
        void *cache;
    } ThreeMMRollCallActor;
    typedef struct ThreeMMMovieState
    {
        void *resources, *actors;
        ThreeMMU32 movieNumber;
        ThreeMMI32 actorIdLimit, sceneCount;
    } ThreeMMMovieState;
    typedef struct ThreeMMMovieOwnerServices
    {
        void *context;
        void *(*create_file)(void *);
        void *(*open_file)(void *);
        int (*verify)(void *, void *file, ThreeMMU32 *);
        void *(*new_resources)(void *, void *file);
        void (*release_file)(void *);
        void (*release_resources)(void *);
        void *(*new_actors)(void *, int wire);
        void (*release_table)(void *);
        ThreeMMU32 (*count)(void *table);
        void (*actor)(void *table, ThreeMMU32, ThreeMMRollCallActor *);
        void (*close_actor)(void *, ThreeMMRollCallActor *);
        void *(*file)(void *resources);
        int (*create_root)(void *, void *file, ThreeMMU32 *);
        int (*read_rollcall)(void *, void *resources, ThreeMMU32, void **actors, ThreeMMI32 *limit);
        void (*load_name)(void *, void *file, ThreeMMU32);
        int (*scene)(void *, void *file, ThreeMMU32 movie, ThreeMMI32 index);
        int (*previous_rollcall)(void *, void *file, ThreeMMU32 movie, ThreeMMU32 *previous);
        int (*write_rollcall)(void *, void *file, ThreeMMU32 movie, void *actors, ThreeMMU32 *written);
        void (*delete_previous)(void *, void *file, ThreeMMU32 movie, ThreeMMU32 previous);
        int (*set_name)(void *, void *file, ThreeMMU32 movie);
        int (*save)(void *, void *file);
        void (*make_persistent)(void *file);
    } ThreeMMMovieOwnerServices;
    /* Unique owning state. File/table handles are transitional host objects; C owns
     * their references, transactions, replacement and ordered cleanup. Name loading
     * writes host scratch, published by the caller only after a successful load.
     * Callbacks cannot mutate/re-enter this state. No rollback after save publication:
     * successful roll-call replacement remains visible if naming/saving later fails. */
    void three_mm_movie_state_init(ThreeMMMovieState *);
    void three_mm_movie_clear_actors(ThreeMMMovieState *, const ThreeMMMovieOwnerServices *);
    void three_mm_movie_release_resources(ThreeMMMovieState *, const ThreeMMMovieOwnerServices *);
    void three_mm_movie_state_dispose(ThreeMMMovieState *, const ThreeMMMovieOwnerServices *);
    int three_mm_movie_init_empty(ThreeMMMovieState *, const ThreeMMMovieOwnerServices *);
    int three_mm_movie_load(ThreeMMMovieState *, ThreeMMU32 number, const ThreeMMMovieOwnerServices *);
    int three_mm_movie_save(ThreeMMMovieState *, const ThreeMMMovieOwnerServices *);

    typedef struct ThreeMMRollCallServices
    {
        void *context;
        /* Wire acquisition locates the child and decodes its GST header/text, but
         * deliberately does not reject a foreign extra width. C owns that decision. */
        void *(*read_wire)(void *, void *resources, ThreeMMU32 movie, int *big_endian);
        ThreeMMU32 (*extra_width)(void *);
        ThreeMMU32 runtime_width;
        void *(*new_table)(void *, int wire);
        void (*release_table)(void *);
        ThreeMMU32 (*count)(void *);
        void (*wire_row)(void *, void *table, ThreeMMU32, unsigned char bytes[28]);
        void (*actor)(void *table, ThreeMMU32, ThreeMMRollCallActor *);
        /* row callbacks cache a borrowed name in host scratch. Appending consumes it
         * immediately. Appends do not duplicate tag ownership. */
        void (*runtime_row)(void *, void *table, ThreeMMU32, ThreeMMRollCallActor *);
        int (*append_runtime)(void *, void *table, const ThreeMMRollCallActor *);
        int (*append_wire)(void *, void *table, const unsigned char bytes[28]);
        int (*open_actor)(void *, ThreeMMRollCallActor *, void *resources);
        void (*close_actor)(void *, ThreeMMRollCallActor *);
        ThreeMMU32 (*wire_size)(void *table);
        int (*add_chunk)(void *, void *file, ThreeMMU32 bytes, ThreeMMU32 *number);
        int (*write_table)(void *, void *table);
        int (*adopt)(void *, void *file, ThreeMMU32 movie, ThreeMMU32 number);
        void (*delete_chunk)(void *, void *file, ThreeMMU32 number);
    } ThreeMMRollCallServices;
    /* Failed read clears output, preserves caller limit, closes the current tag
     * before earlier rows when insertion fails, then releases the runtime table.
     * Wire extras retain their 28-byte layout with a zero pointer slot. */
    int three_mm_movie_read_rollcall(void *resources, ThreeMMU32 movie, void **actors, ThreeMMI32 *limit,
                                     const ThreeMMRollCallServices *);
    int three_mm_movie_write_rollcall(void *file, ThreeMMU32 movie, void *actors, ThreeMMU32 *written,
                                      const ThreeMMRollCallServices *);
#ifdef __cplusplus
}
#endif
#endif

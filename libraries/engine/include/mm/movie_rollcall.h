/* Movie roll-call editing converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_ROLLCALL_H
#define MM_ENGINE_MOVIE_ROLLCALL_H
#include "movie_owner.h"
#include "tag.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_MOVIE_PROP = 1,
        MM_MOVIE_TEXT = 2
    };
    enum
    {
        MM_ROLLCALL_NO_ACTOR,
        MM_ROLLCALL_BAD_REMOVE,
        MM_ROLLCALL_UNDERFLOW
    };
    typedef struct MMMovieRollCallServices
    {
        void *context;
        MMMovieState *data;
        MMI32 (*actor_id)(void *actor);
        void (*set_actor_id)(void *actor, MMI32 id), (*actor_tag)(void *actor, MMTag *);
        int (*is_prop)(void *actor), (*is_text)(void *actor);
        int (*same_tag)(const MMTag *, const MMTag *);
        void (*open_tag)(void *, MMTag *, void *resources);
        void (*duplicate_tag)(const MMTag *), (*close_tag)(MMTag *);
        void (*changed)(void *), (*diagnostic)(void *, int issue);
    } MMMovieRollCallServices;
    /* Runtime table and actor/tag services are initialized, synchronous and not
     * reentrant. New rows require a non-null counted name in the table's native
     * units. Mutation inputs do not alias the table. Query names are borrowed
     * until the next table mutation. Output tags are borrowed. Storage owns one
     * opened tag per committed row. The native open_tag primitive must succeed.
     * Existing rows increment before tag refresh and notify even if already used.
     * New IDs publish before insertion, and remain assigned if insertion fails.
     * Failed append closes its newly opened tag instead of leaking that reference.
     * change_tag is a shallow replacement: its caller arranges tag ownership.
     * Removal closes before erasing and notifies only when the count reaches zero. */
    MMTag mm_movie_rollcall_tag(const MMRollCallActor *);
    MMI32 mm_movie_rollcall_find(MMRollCallTable *, MMI32 id, MMRollCallActor *);
    int mm_movie_rollcall_get(MMRollCallTable *, MMI32 index, MMI32 *id, const void **name,
                             MMU32 *count, MMI32 *refs, MMTag *tag);
    int mm_movie_rollcall_name(MMRollCallTable *, MMI32 id, const void **name, MMU32 *count);
    int mm_movie_rollcall_rename(MMI32 id, const void *name, MMU32 count, const MMMovieRollCallServices *);
    int mm_movie_rollcall_has(MMRollCallTable *, MMI32 index, MMU32 flag);
    void mm_movie_rollcall_change_tag(MMI32 id, const MMTag *, const MMMovieRollCallServices *);
    int mm_movie_rollcall_add(void *actor, const void *name, MMU32 count, const MMMovieRollCallServices *);
    void mm_movie_rollcall_remove(void *actor, int delete_last, const MMMovieRollCallServices *);
#ifdef __cplusplus
}
#endif
#endif

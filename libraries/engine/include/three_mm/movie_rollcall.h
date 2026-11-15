/* Movie roll-call editing converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_ROLLCALL_H
#define THREE_MM_ENGINE_MOVIE_ROLLCALL_H
#include "movie_owner.h"
#include "tag.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_MOVIE_PROP = 1,
        THREE_MM_MOVIE_TEXT = 2
    };
    enum
    {
        THREE_MM_ROLLCALL_NO_ACTOR,
        THREE_MM_ROLLCALL_BAD_REMOVE,
        THREE_MM_ROLLCALL_UNDERFLOW
    };
    typedef struct ThreeMMMovieRollCallServices
    {
        void *context;
        ThreeMMMovieState *data;
        ThreeMMI32 (*count)(void *table);
        void (*row)(void *table, ThreeMMI32 index, ThreeMMRollCallActor *);
        void (*put)(void *table, ThreeMMI32 index, const ThreeMMRollCallActor *);
        void (*name)(void *table, ThreeMMI32 index, void *output);
        int (*rename)(void *table, ThreeMMI32 index, const void *name);
        int (*append)(void *table, const void *name, const ThreeMMRollCallActor *);
        void (*erase)(void *table, ThreeMMI32 index);
        ThreeMMI32 (*actor_id)(void *actor);
        void (*set_actor_id)(void *actor, ThreeMMI32 id), (*actor_tag)(void *actor, ThreeMMTag *);
        int (*is_prop)(void *actor), (*is_text)(void *actor);
        int (*same_tag)(const ThreeMMTag *, const ThreeMMTag *);
        void (*open_tag)(void *, ThreeMMTag *, void *resources);
        void (*duplicate_tag)(const ThreeMMTag *), (*close_tag)(ThreeMMTag *);
        void (*changed)(void *), (*diagnostic)(void *, int issue);
    } ThreeMMMovieRollCallServices;
    /* Runtime table and actor/tag services are initialized, synchronous and not
     * reentrant. New rows require a name. open_tag is a must-succeed primitive
     * (the native facade retains its diagnostic). Host services marshal names;
     * output tags are borrowed. Storage owns one opened tag per committed row.
     * Existing rows increment before tag refresh and notify even if already used.
     * New IDs publish before insertion, and remain assigned if insertion fails.
     * Failed append closes its newly opened tag instead of leaking that reference.
     * change_tag is a shallow replacement: its caller arranges tag ownership.
     * Removal closes before erasing and notifies only when the count reaches zero. */
    ThreeMMTag three_mm_movie_rollcall_tag(const ThreeMMRollCallActor *);
    ThreeMMI32 three_mm_movie_rollcall_find(ThreeMMI32 id, ThreeMMRollCallActor *,
                                            const ThreeMMMovieRollCallServices *);
    int three_mm_movie_rollcall_get(ThreeMMI32 index, ThreeMMI32 *id, void *name, ThreeMMI32 *refs, ThreeMMTag *tag,
                                    const ThreeMMMovieRollCallServices *);
    int three_mm_movie_rollcall_name(ThreeMMI32 id, void *name, const ThreeMMMovieRollCallServices *);
    int three_mm_movie_rollcall_rename(ThreeMMI32 id, const void *name, const ThreeMMMovieRollCallServices *);
    int three_mm_movie_rollcall_has(ThreeMMI32 index, ThreeMMU32 flag, const ThreeMMMovieRollCallServices *);
    void three_mm_movie_rollcall_change_tag(ThreeMMI32 id, const ThreeMMTag *, const ThreeMMMovieRollCallServices *);
    int three_mm_movie_rollcall_add(void *actor, const void *name, const ThreeMMMovieRollCallServices *);
    void three_mm_movie_rollcall_remove(void *actor, int delete_last, const ThreeMMMovieRollCallServices *);
#ifdef __cplusplus
}
#endif
#endif

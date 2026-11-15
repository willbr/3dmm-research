/* Actor track ownership converted from engine actor_track.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_TRACK_H
#define MM_ENGINE_ACTOR_TRACK_H
#include "actor_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMActorTrack
    {
        void *events, *route;
    } MMActorTrack;
    typedef struct MMActorTrackServices
    {
        void *context;
        void *(*new_events)(void *, MMU32 fixed, MMU32 entries, MMU32 variable);
        void *(*new_route)(void *, MMU32 width, MMU32 entries);
        void (*route_growth)(void *, MMU32);
        void *(*clone_events)(void *);
        void *(*clone_route)(void *);
        void (*release_events)(void *);
        void (*release_route)(void *);
        void (*lock)(void *);
        void (*unlock)(void *);
        MMU32 (*count)(void *);
        void *(*fixed)(void *, MMU32);
        void *(*data)(void *, MMU32);
        void (*retain_cache)(void *);
        void (*release_cache)(void *);
    } MMActorTrackServices;
    /* Unique owning state; tables contain validated ActorData runtime layouts.
     * Opened sid-zero tags transfer one reference into their row. Other tags and
     * custom costumes own no reference. Byte-address accessors may be unaligned.
     * Create/clone publish only on success, then dispose the previous tables.
     * Cloning retains copied event tags immediately after copying their bytes.
     * Close writes cleared cache pointers back to rows, and is idempotent.
     * Callbacks must not mutate/re-enter either participating track. */
    void mm_actor_track_init(MMActorTrack *);
    void mm_actor_track_dispose(MMActorTrack *, const MMActorTrackServices *);
    void mm_actor_track_close_tags(MMActorTrack *, const MMActorTrackServices *);
    void *mm_actor_event_tag(void *events, MMU32 index, const MMActorTrackServices *);
    void mm_actor_track_swap(MMActorTrack *, MMActorTrack *);
    int mm_actor_track_create(MMActorTrack *, const MMActorTrackServices *);
    int mm_actor_track_clone(MMActorTrack *, const MMActorTrack *, const MMActorTrackServices *);
    /* Adopt transfers the supplied handle without retaining it; replacing a table
     * disposes the old table and its tags. Passing the current handle is a no-op. */
    void mm_actor_track_adopt_events(MMActorTrack *, void *, const MMActorTrackServices *);
    void mm_actor_track_adopt_route(MMActorTrack *, void *, const MMActorTrackServices *);
#ifdef __cplusplus
}
#endif
#endif

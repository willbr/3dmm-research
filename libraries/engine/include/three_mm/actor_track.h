/* Actor track ownership converted from engine actor_track.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_TRACK_H
#define THREE_MM_ENGINE_ACTOR_TRACK_H
#include "actor_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMActorTrack
    {
        void *events, *route;
    } ThreeMMActorTrack;
    typedef struct ThreeMMActorTrackServices
    {
        void *context;
        void *(*new_events)(void *, ThreeMMU32 fixed, ThreeMMU32 entries, ThreeMMU32 variable);
        void *(*new_route)(void *, ThreeMMU32 width, ThreeMMU32 entries);
        void (*route_growth)(void *, ThreeMMU32);
        void *(*clone_events)(void *);
        void *(*clone_route)(void *);
        void (*release_events)(void *);
        void (*release_route)(void *);
        void (*lock)(void *);
        void (*unlock)(void *);
        ThreeMMU32 (*count)(void *);
        void *(*fixed)(void *, ThreeMMU32);
        void *(*data)(void *, ThreeMMU32);
        void (*retain_cache)(void *);
        void (*release_cache)(void *);
    } ThreeMMActorTrackServices;
    /* Unique owning state; tables contain validated ActorData runtime layouts.
     * Opened sid-zero tags transfer one reference into their row. Other tags and
     * custom costumes own no reference. Byte-address accessors may be unaligned.
     * Create/clone publish only on success, then dispose the previous tables.
     * Cloning retains copied event tags immediately after copying their bytes.
     * Close writes cleared cache pointers back to rows, and is idempotent.
     * Callbacks must not mutate/re-enter either participating track. */
    void three_mm_actor_track_init(ThreeMMActorTrack *);
    void three_mm_actor_track_dispose(ThreeMMActorTrack *, const ThreeMMActorTrackServices *);
    void three_mm_actor_track_close_tags(ThreeMMActorTrack *, const ThreeMMActorTrackServices *);
    void *three_mm_actor_event_tag(void *events, ThreeMMU32 index, const ThreeMMActorTrackServices *);
    void three_mm_actor_track_swap(ThreeMMActorTrack *, ThreeMMActorTrack *);
    int three_mm_actor_track_create(ThreeMMActorTrack *, const ThreeMMActorTrackServices *);
    int three_mm_actor_track_clone(ThreeMMActorTrack *, const ThreeMMActorTrack *, const ThreeMMActorTrackServices *);
    /* Adopt transfers the supplied handle without retaining it; replacing a table
     * disposes the old table and its tags. Passing the current handle is a no-op. */
    void three_mm_actor_track_adopt_events(ThreeMMActorTrack *, void *, const ThreeMMActorTrackServices *);
    void three_mm_actor_track_adopt_route(ThreeMMActorTrack *, void *, const ThreeMMActorTrackServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Actor track ownership converted from engine actor_track.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_TRACK_H
#define MM_ENGINE_ACTOR_TRACK_H
#include "actor_events.h"
#include "actor_motion.h"
#include "actor_path.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMActorTrack
    {
        MMActorEventTable *events;
        MMActorPath *route;
    } MMActorTrack;
    /* Unique owning state; tables contain validated ActorData runtime layouts.
     * Opened sid-zero tags transfer one reference into their row. Other tags and
     * custom costumes own no reference. Tag access copies into aligned values.
     * Create/clone publish only on success, then dispose the previous tables.
     * Cloning retains copied event tags immediately after copying their bytes.
     * Close writes cleared cache pointers back to rows, and is idempotent.
     * Callbacks must not mutate/re-enter either participating track. */
    void mm_actor_track_init(MMActorTrack *);
    /* Tag services may be NULL for a decoded track with unopened tags. */
    void mm_actor_track_dispose(MMActorTrack *, const MMTagServices *);
    /* Open tags in a newly decoded track. On failure close the completed prefix
     * in reverse order. A source file is optional for embedded resource copies. */
    int mm_actor_track_open_tags(MMActorTrack *, void *destination, void *source_file, const MMTagServices *);
    void mm_actor_track_close_tags(MMActorTrack *, const MMTagServices *);
    int mm_actor_track_get_tag(const MMActorEventTable *, MMU32 index, MMTag *);
    void mm_actor_track_retain_tag(MMActorEventTable *, MMU32 index, const MMTagServices *);
    void mm_actor_track_close_tag(MMActorEventTable *, MMU32 index, const MMTagServices *);
    void mm_actor_track_swap(MMActorTrack *, MMActorTrack *);
    int mm_actor_track_create(MMActorTrack *, const MMStorageAllocator *, int trash_unused, const MMTagServices *);
    int mm_actor_track_clone(MMActorTrack *, const MMActorTrack *, const MMTagServices *);
    /* Adopt transfers the supplied handle without retaining it; replacing a table
     * disposes the old table and its tags. Passing the current handle is a no-op. */
    void mm_actor_track_adopt_events(MMActorTrack *, MMActorEventTable *, const MMTagServices *);
    void mm_actor_track_adopt_route(MMActorTrack *, MMActorPath *);
    MMActorMotionServices mm_actor_track_motion_services(void);
#ifdef __cplusplus
}
#endif
#endif

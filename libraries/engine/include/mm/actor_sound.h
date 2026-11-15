/* Actor sound policy converted from engine actrsnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_SOUND_H
#define MM_ENGINE_ACTOR_SOUND_H
#include "actor_motion.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum { MM_ACTOR_SOUND_GROW = 2 };
    int mm_actor_sound_cel(MMI32 cel, MMI32 count, MMI32 *out);
    MMI32 mm_sound_queue_number(MMI32 style, MMI32 object, int actor);
    typedef struct MMActorMotionSound
    {
        MMActorEvent event;
        MMActorSound sound;
    } MMActorMotionSound;
    /* Owns storage, borrows TAG references from the actor track. Initialize before
     * use, never copy by value, and keep track references alive during playback.
     * Clone copies records independently without acquiring TAG references. */
    typedef struct MMActorSounds
    {
        MMCollectionStorage storage;
        MMU32 count;
    } MMActorSounds;
    int mm_actor_sounds_init(MMActorSounds *, const MMStorageAllocator *);
    void mm_actor_sounds_dispose(MMActorSounds *);
    int mm_actor_sounds_reserve(MMActorSounds *, MMU32 additional);
    void mm_actor_sounds_clear(MMActorSounds *);
    int mm_actor_sounds_get(const MMActorSounds *, MMU32, MMActorMotionSound *);
    int mm_actor_sounds_put(MMActorSounds *, MMU32, const MMActorMotionSound *);
    int mm_actor_sounds_delete(MMActorSounds *, MMU32);
    /* Upsert replaces the first matching cel/style, otherwise appends. Inputs may
     * alias an existing record. Allocation failure preserves records/count. */
    int mm_actor_sounds_insert(MMActorSounds *, const MMActorEvent *, const MMActorSound *);
    /* Transactional clone; destination is initialized and keeps its allocator. */
    int mm_actor_sounds_clone(MMActorSounds *, const MMActorSounds *);
    void mm_actor_sounds_swap(MMActorSounds *, MMActorSounds *);
    enum
    {
        MM_ACTOR_SOUND_ORDINARY,
        MM_ACTOR_SOUND_LOOP,
        MM_ACTOR_SOUND_MATCH
    };
    typedef struct MMActorSoundServices
    {
        void *context;
        int cues_only;
        int (*cel_count)(void *, MMI32 action, MMI32 *);
        int (*resolve)(void *, MMTag *, MMU32 child);
        void (*write_event)(void *, MMI32 index, const MMActorSound *);
        void (*resolution_failed)(void *);
        void *(*fetch)(void *, const MMTag *);
        MMI32 (*priority)(void *, void *resource, int kind);
        int (*enqueue)(void *, void *resource, const MMActorSound *, MMI32 priority, int low_priority);
        void (*release)(void *, void *resource);
        int (*cue)(void *, const MMActorSound *);
    } MMActorSoundServices;
    /* Callbacks cannot mutate/re-enter the list. Runtime mode resolves imported
     * sid-0 tags, writes ordinary events after successful resolution, then fetches,
     * enqueues and releases. Cues-only mode emits borrowed cues without fetching.
     * Motion bindings are stored without playing on the binding event. */
    int mm_actor_sound_event(MMActorSounds *, const MMActorEvent *, const MMActorSound *,
                                   MMI32 event_index, const MMActorSoundServices *);
    /* Always queries cel count, even for an empty list. Positive count required.
     * Negative cels use the original signed remainder without normalization.
     * Missing resources mark failure but allow later matches. Resolution/enqueue
     * failures stop. Failed motion resolution retains resolver writes in the list;
     * failed ordinary resolution does not publish an event. Fetched references
     * are released on both successful and failed enqueue. */
    int mm_actor_sound_motion(MMActorSounds *, MMI32 action, MMI32 cel,
                                    const MMActorSoundServices *);
#ifdef __cplusplus
}
#endif
#endif

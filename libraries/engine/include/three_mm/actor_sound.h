/* Actor sound policy converted from engine actrsnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_SOUND_H
#define THREE_MM_ENGINE_ACTOR_SOUND_H
#include "actor_motion.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum { THREE_MM_ACTOR_SOUND_GROW = 2 };
    typedef struct ThreeMMActorMotionSound
    {
        ThreeMMActorEvent event;
        ThreeMMActorSound sound;
    } ThreeMMActorMotionSound;
    /* Owns storage, borrows TAG references from the actor track. Initialize before
     * use, never copy by value, and keep track references alive during playback.
     * Clone copies records independently without acquiring TAG references. */
    typedef struct ThreeMMActorSounds
    {
        ThreeMMCollectionStorage storage;
        ThreeMMU32 count;
    } ThreeMMActorSounds;
    int three_mm_actor_sounds_init(ThreeMMActorSounds *, const ThreeMMStorageAllocator *);
    void three_mm_actor_sounds_dispose(ThreeMMActorSounds *);
    int three_mm_actor_sounds_reserve(ThreeMMActorSounds *, ThreeMMU32 additional);
    void three_mm_actor_sounds_clear(ThreeMMActorSounds *);
    int three_mm_actor_sounds_get(const ThreeMMActorSounds *, ThreeMMU32, ThreeMMActorMotionSound *);
    int three_mm_actor_sounds_put(ThreeMMActorSounds *, ThreeMMU32, const ThreeMMActorMotionSound *);
    int three_mm_actor_sounds_delete(ThreeMMActorSounds *, ThreeMMU32);
    /* Upsert replaces the first matching cel/style, otherwise appends. Inputs may
     * alias an existing record. Allocation failure preserves records/count. */
    int three_mm_actor_sounds_insert(ThreeMMActorSounds *, const ThreeMMActorEvent *, const ThreeMMActorSound *);
    /* Transactional clone; destination is initialized and keeps its allocator. */
    int three_mm_actor_sounds_clone(ThreeMMActorSounds *, const ThreeMMActorSounds *);
    void three_mm_actor_sounds_swap(ThreeMMActorSounds *, ThreeMMActorSounds *);
    enum
    {
        THREE_MM_ACTOR_SOUND_ORDINARY,
        THREE_MM_ACTOR_SOUND_LOOP,
        THREE_MM_ACTOR_SOUND_MATCH
    };
    typedef struct ThreeMMActorSoundServices
    {
        void *context;
        int cues_only;
        int (*cel_count)(void *, ThreeMMI32 action, ThreeMMI32 *);
        int (*resolve)(void *, ThreeMMTag *, ThreeMMU32 child);
        void (*write_event)(void *, ThreeMMI32 index, const ThreeMMActorSound *);
        void (*resolution_failed)(void *);
        void *(*fetch)(void *, const ThreeMMTag *);
        ThreeMMI32 (*priority)(void *, void *resource, int kind);
        int (*enqueue)(void *, void *resource, const ThreeMMActorSound *, ThreeMMI32 priority, int low_priority);
        void (*release)(void *, void *resource);
        int (*cue)(void *, const ThreeMMActorSound *);
    } ThreeMMActorSoundServices;
    /* Callbacks cannot mutate/re-enter the list. Runtime mode resolves imported
     * sid-0 tags, writes ordinary events after successful resolution, then fetches,
     * enqueues and releases. Cues-only mode emits borrowed cues without fetching.
     * Motion bindings are stored without playing on the binding event. */
    int three_mm_actor_sound_event(ThreeMMActorSounds *, const ThreeMMActorEvent *, const ThreeMMActorSound *,
                                   ThreeMMI32 event_index, const ThreeMMActorSoundServices *);
    /* Always queries cel count, even for an empty list. Positive count required.
     * Negative cels use the original signed remainder without normalization.
     * Missing resources mark failure but allow later matches. Resolution/enqueue
     * failures stop. Failed motion resolution retains resolver writes in the list;
     * failed ordinary resolution does not publish an event. Fetched references
     * are released on both successful and failed enqueue. */
    int three_mm_actor_sound_motion(ThreeMMActorSounds *, ThreeMMI32 action, ThreeMMI32 cel,
                                    const ThreeMMActorSoundServices *);
#ifdef __cplusplus
}
#endif
#endif

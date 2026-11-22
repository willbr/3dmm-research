/* Sound management policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_SOUND_MANAGER_H
#define MM_SOUND_MANAGER_H
#include "resource_cache.h"
#include "sound_queue.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum MMSoundOperation
    {
        MM_SOUND_ACTIVE,
        MM_SOUND_SUSPEND,
        MM_SOUND_VOLUME,
        MM_SOUND_GET_VOLUME,
        MM_SOUND_INSTANCE_STOP,
        MM_SOUND_ALL_STOP,
        MM_SOUND_INSTANCE_PAUSE,
        MM_SOUND_ALL_PAUSE,
        MM_SOUND_INSTANCE_RESUME,
        MM_SOUND_ALL_RESUME,
        MM_SOUND_INSTANCE_PLAYING,
        MM_SOUND_ALL_PLAYING,
        MM_SOUND_FLUSH,
        MM_SOUND_BEGIN,
        MM_SOUND_END
    };
    typedef struct MMSoundControl
    {
        MMI32 suspended;
        int active;
    } MMSoundControl;
    typedef struct MMSoundEndpoint
    {
        void *object;
        MMU32 tag;
    } MMSoundEndpoint;
    typedef struct MMSoundPlay
    {
        MMResourceAccess source;
        MMU32 type, number;
        MMI32 queue, volume, plays;
        MMU32 offset;
        MMI32 priority, sound_class;
    } MMSoundPlay;
    typedef struct MMSoundManagerServices
    {
        void *context;
        MMI32 (*count)(void *);
        void (*get)(void *, MMI32, MMSoundEndpoint *);
        void (*put)(void *, MMI32, const MMSoundEndpoint *);
        int (*insert)(void *, MMI32, const MMSoundEndpoint *);
        void (*erase)(void *, MMI32);
        void (*retain)(void *, void *), (*release)(void *, void *);
        void *(*create_queue)(void *);
        MMI32 (*call)(void *, void *object, int operation, MMI32 first, MMI32 second);
        MMI32 (*play)(void *, void *object, const MMSoundPlay *);
        MMI32 (*allocate_id)(void *);
    } MMSoundManagerServices;
    int mm_sound_manager_find(const MMSoundManagerServices *, MMU32, MMSoundEndpoint *,
                                    MMI32 *);
    int mm_sound_manager_add(const MMSoundManagerServices *, const MMSoundControl *, MMU32,
                                   void *);
    void mm_sound_manager_remove(const MMSoundManagerServices *, MMU32);
    MMI32 mm_sound_dispatch(const MMSoundManagerServices *, int operation, int filter, MMU32 tag,
                                       MMI32 first, MMI32 second);
    void mm_sound_activate(const MMSoundManagerServices *, MMSoundControl *, int active,
                                 int queue_device);
    void mm_sound_suspend(const MMSoundManagerServices *, MMSoundControl *, int suspend,
                                int queue_device);
    MMI32 mm_sound_next_id(MMI32 previous);
    MMI32 mm_sound_manager_play(const MMSoundManagerServices *, const MMSoundPlay *,
                                           MMI32 first_class, MMI32 last_class);
    /* Resolve a sound before entering the queue lock. On a miss leave the
     * queue untouched. On a hit the queue takes the fetched reference. */
    void mm_sound_enqueue_resource(const MMSoundQueueServices *, const MMSoundPlay *, MMI32 id,
                                   MMResourceBlockReader, const MMResourceObjectServices *);
    int mm_sound_ensure_queue(const MMSoundManagerServices *, MMI32 queue, MMI32 anonymous,
                                    MMI32 any_class, MMSoundEndpoint *, MMI32 *);
    void mm_sound_queues_flush(const MMSoundManagerServices *, MMI32 any_class);
    void mm_sound_endpoints_release(const MMSoundManagerServices *, int reverse);
#ifdef __cplusplus
}
#endif
#endif

/* Sound management policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_SOUND_MANAGER_H
#define THREE_MM_SOUND_MANAGER_H
#include "sound_queue.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum ThreeMMSoundOperation
    {
        THREE_MM_SOUND_ACTIVE,
        THREE_MM_SOUND_SUSPEND,
        THREE_MM_SOUND_VOLUME,
        THREE_MM_SOUND_GET_VOLUME,
        THREE_MM_SOUND_INSTANCE_STOP,
        THREE_MM_SOUND_ALL_STOP,
        THREE_MM_SOUND_INSTANCE_PAUSE,
        THREE_MM_SOUND_ALL_PAUSE,
        THREE_MM_SOUND_INSTANCE_RESUME,
        THREE_MM_SOUND_ALL_RESUME,
        THREE_MM_SOUND_INSTANCE_PLAYING,
        THREE_MM_SOUND_ALL_PLAYING,
        THREE_MM_SOUND_FLUSH,
        THREE_MM_SOUND_BEGIN,
        THREE_MM_SOUND_END
    };
    typedef struct ThreeMMSoundControl
    {
        ThreeMMI32 suspended;
        int active;
    } ThreeMMSoundControl;
    typedef struct ThreeMMSoundEndpoint
    {
        void *object;
        ThreeMMU32 tag;
    } ThreeMMSoundEndpoint;
    typedef struct ThreeMMSoundPlay
    {
        void *cache;
        ThreeMMU32 type, number;
        ThreeMMI32 queue, volume, plays;
        ThreeMMU32 offset;
        ThreeMMI32 priority, sound_class;
    } ThreeMMSoundPlay;
    typedef struct ThreeMMSoundManagerServices
    {
        void *context;
        ThreeMMI32 (*count)(void *);
        void (*get)(void *, ThreeMMI32, ThreeMMSoundEndpoint *);
        void (*put)(void *, ThreeMMI32, const ThreeMMSoundEndpoint *);
        int (*insert)(void *, ThreeMMI32, const ThreeMMSoundEndpoint *);
        void (*erase)(void *, ThreeMMI32);
        void (*retain)(void *, void *), (*release)(void *, void *);
        void *(*create_queue)(void *);
        ThreeMMI32 (*call)(void *, void *object, int operation, ThreeMMI32 first, ThreeMMI32 second);
        ThreeMMI32 (*play)(void *, void *object, const ThreeMMSoundPlay *);
        ThreeMMI32 (*allocate_id)(void *);
    } ThreeMMSoundManagerServices;
    int three_mm_sound_manager_find(const ThreeMMSoundManagerServices *, ThreeMMU32, ThreeMMSoundEndpoint *,
                                    ThreeMMI32 *);
    int three_mm_sound_manager_add(const ThreeMMSoundManagerServices *, const ThreeMMSoundControl *, ThreeMMU32,
                                   void *);
    void three_mm_sound_manager_remove(const ThreeMMSoundManagerServices *, ThreeMMU32);
    ThreeMMI32 three_mm_sound_dispatch(const ThreeMMSoundManagerServices *, int operation, int filter, ThreeMMU32 tag,
                                       ThreeMMI32 first, ThreeMMI32 second);
    void three_mm_sound_activate(const ThreeMMSoundManagerServices *, ThreeMMSoundControl *, int active,
                                 int queue_device);
    void three_mm_sound_suspend(const ThreeMMSoundManagerServices *, ThreeMMSoundControl *, int suspend,
                                int queue_device);
    ThreeMMI32 three_mm_sound_next_id(ThreeMMI32 previous);
    ThreeMMI32 three_mm_sound_manager_play(const ThreeMMSoundManagerServices *, const ThreeMMSoundPlay *,
                                           ThreeMMI32 first_class, ThreeMMI32 last_class);
    int three_mm_sound_ensure_queue(const ThreeMMSoundManagerServices *, ThreeMMI32 queue, ThreeMMI32 anonymous,
                                    ThreeMMI32 any_class, ThreeMMSoundEndpoint *, ThreeMMI32 *);
    void three_mm_sound_queues_flush(const ThreeMMSoundManagerServices *, ThreeMMI32 any_class);
    void three_mm_sound_endpoints_release(const ThreeMMSoundManagerServices *, int reverse);
#ifdef __cplusplus
}
#endif
#endif

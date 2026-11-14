/* Sound queue policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_SOUND_QUEUE_H
#define THREE_MM_SOUND_QUEUE_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMSoundEntry
    {
        void *object;
        ThreeMMI32 id, volume, plays;
        ThreeMMU32 offset;
        ThreeMMI32 priority, sound_class, pause;
    } ThreeMMSoundEntry;
    enum
    {
        THREE_MM_SOUND_STOP,
        THREE_MM_SOUND_PAUSE,
        THREE_MM_SOUND_RESUME
    };
    typedef struct ThreeMMSoundQueueServices
    {
        void *context;
        void (*enter)(void *), (*leave)(void *), (*flush)(void *);
        ThreeMMI32 (*count)(void *), (*current)(void *);
        void (*set_current)(void *, ThreeMMI32);
        void (*get)(void *, ThreeMMI32, ThreeMMSoundEntry *);
        void (*put)(void *, ThreeMMI32, const ThreeMMSoundEntry *);
        int (*add)(void *, const ThreeMMSoundEntry *, ThreeMMI32 *);
        void (*erase)(void *, ThreeMMI32), (*release)(void *, void *);
        void (*changed)(void *, int operation, ThreeMMI32 first);
    } ThreeMMSoundQueueServices;
    /* Callbacks retain virtual device hooks/locking. flush may re-enter, advance the
     * cursor and release completed payloads. Entries are copied across callbacks. */
    void three_mm_sound_queue_flush(const ThreeMMSoundQueueServices *);
    void three_mm_sound_queue_enqueue(const ThreeMMSoundQueueServices *, const ThreeMMSoundEntry *);
    ThreeMMI32 three_mm_sound_queue_priority(const ThreeMMSoundQueueServices *);
    void three_mm_sound_queue_edit(const ThreeMMSoundQueueServices *, int operation, int all, ThreeMMI32 id_or_class,
                                   ThreeMMI32 any_class);
    int three_mm_sound_queue_playing(const ThreeMMSoundQueueServices *, int all, ThreeMMI32 id_or_class,
                                     ThreeMMI32 any_class);
    void three_mm_sound_queue_release(const ThreeMMSoundQueueServices *);
    ThreeMMU32 three_mm_sound_volume_scale(ThreeMMU32 system_volume, ThreeMMI32 volume);
#ifdef __cplusplus
}
#endif
#endif

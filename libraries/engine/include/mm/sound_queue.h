/* Sound queue policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_SOUND_QUEUE_H
#define MM_SOUND_QUEUE_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMSoundEntry
    {
        void *object;
        MMI32 id, volume, plays;
        MMU32 offset;
        MMI32 priority, sound_class, pause;
    } MMSoundEntry;
    enum
    {
        MM_SOUND_STOP,
        MM_SOUND_PAUSE,
        MM_SOUND_RESUME
    };
    typedef struct MMSoundQueueServices
    {
        void *context;
        void (*enter)(void *), (*leave)(void *), (*flush)(void *);
        MMI32 (*count)(void *), (*current)(void *);
        void (*set_current)(void *, MMI32);
        void (*get)(void *, MMI32, MMSoundEntry *);
        void (*put)(void *, MMI32, const MMSoundEntry *);
        int (*add)(void *, const MMSoundEntry *, MMI32 *);
        void (*erase)(void *, MMI32), (*release)(void *, void *);
        void (*changed)(void *, int operation, MMI32 first);
    } MMSoundQueueServices;
    /* Callbacks retain virtual device hooks/locking. flush may re-enter, advance the
     * cursor and release completed payloads. Entries are copied across callbacks. */
    void mm_sound_queue_flush(const MMSoundQueueServices *);
    void mm_sound_queue_enqueue(const MMSoundQueueServices *, const MMSoundEntry *);
    MMI32 mm_sound_queue_priority(const MMSoundQueueServices *);
    void mm_sound_queue_edit(const MMSoundQueueServices *, int operation, int all, MMI32 id_or_class,
                                   MMI32 any_class);
    int mm_sound_queue_playing(const MMSoundQueueServices *, int all, MMI32 id_or_class,
                                     MMI32 any_class);
    void mm_sound_queue_release(const MMSoundQueueServices *);
    MMU32 mm_sound_volume_scale(MMU32 system_volume, MMI32 volume);
#ifdef __cplusplus
}
#endif
#endif

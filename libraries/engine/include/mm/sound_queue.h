/* Sound queue policy converted from Kauai sndm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_SOUND_QUEUE_H
#define MM_SOUND_QUEUE_H
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMSoundEntry
    {
        void *object;
        MMI32 id, volume, plays;
        MMU32 offset;
        MMI32 priority, sound_class;
        MMI32 pause; /* Negative stops the instance, positive counts pause requests. */
    } MMSoundEntry;
    /* Owns dense sound instances and the playback cursor. Initialize in place. */
    typedef struct MMSoundQueue
    {
        MMCollectionStorage storage;
        MMArray rows;
        MMI32 current;
    } MMSoundQueue;
    enum
    {
        MM_SOUND_STOP,
        MM_SOUND_PAUSE,
        MM_SOUND_RESUME
    };
    typedef struct MMSoundQueueServices
    {
        void *context;
        MMSoundQueue *queue;
        void (*enter)(void *), (*leave)(void *), (*flush)(void *);
        void (*release)(void *, void *);
        void (*changed)(void *, int operation, MMI32 first);
    } MMSoundQueueServices;
    /* Callbacks retain virtual device hooks/locking. flush may re-enter, advance the
     * cursor and release completed payloads. Entries are copied across callbacks. */
    int mm_sound_queue_init(MMSoundQueue *, const MMStorageAllocator *, MMU32 min_grow, int trash_unused);
    /* Direct access requires the host queue lock. An uninitialized queue has count -1.
     * Get and put take the index of a live row. */
    MMI32 mm_sound_queue_count(const MMSoundQueue *);
    MMSoundEntry mm_sound_queue_get(const MMSoundQueue *, MMI32 index);
    void mm_sound_queue_put(MMSoundQueue *, MMI32 index, const MMSoundEntry *);
    void mm_sound_queue_flush(const MMSoundQueueServices *);
    void mm_sound_queue_enqueue(const MMSoundQueueServices *, const MMSoundEntry *);
    MMI32 mm_sound_queue_priority(const MMSoundQueueServices *);
    void mm_sound_queue_edit(const MMSoundQueueServices *, int operation, int all, MMI32 id_or_class,
                                   MMI32 any_class);
    int mm_sound_queue_playing(const MMSoundQueueServices *, int all, MMI32 id_or_class,
                                     MMI32 any_class);
    void mm_sound_queue_dispose(const MMSoundQueueServices *);
    MMU32 mm_sound_volume_scale(MMU32 system_volume, MMI32 volume);
#ifdef __cplusplus
}
#endif
#endif

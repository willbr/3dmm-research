/* AudioMan runtime policy converted from Kauai sndam.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_AUDIOMAN_H
#define MM_ENGINE_AUDIOMAN_H
#include "block_owner.h"
#include "sound_manager.h"
#include "wide.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMAudioManStream
    {
        MMOwnedBlock block;
        MMU32 position;
    } MMAudioManStream;
    enum { MM_AUDIOMAN_SEEK_SET, MM_AUDIOMAN_SEEK_CUR, MM_AUDIOMAN_SEEK_END };
    /* Return MM_DATA_* status. Short reads and EOF succeed, and I/O failure
     * leaves the position unchanged. A rejected seek reports the old position.
     * Unknown origins retain the classic absolute-seek behavior. */
    int mm_audioman_stream_init(MMAudioManStream *, const MMHandleServices *, const MMBlockFileServices *);
    void mm_audioman_stream_dispose(MMAudioManStream *);
    int mm_audioman_stream_read(MMAudioManStream *, void *out, MMU32 requested, MMU32 *read);
    int mm_audioman_stream_seek(MMAudioManStream *, MMWide offset, MMU32 origin, MMU32 *position);
    MMU32 mm_audioman_stream_resident(const MMAudioManStream *);

    enum MMAudioManFormat
    {
        MM_AUDIOMAN_11M8,
        MM_AUDIOMAN_22M8,
        MM_AUDIOMAN_44M8,
        MM_AUDIOMAN_11S8,
        MM_AUDIOMAN_22S8,
        MM_AUDIOMAN_44S8,
        MM_AUDIOMAN_11M16,
        MM_AUDIOMAN_22M16,
        MM_AUDIOMAN_44M16,
        MM_AUDIOMAN_11S16,
        MM_AUDIOMAN_22S16,
        MM_AUDIOMAN_44S16,
        MM_AUDIOMAN_FORMAT_COUNT
    };
    typedef struct MMAudioManMixer
    {
        void *object;
        MMI32 format;
        MMU32 cache_ms;
    } MMAudioManMixer;
    typedef struct MMAudioManDevice
    {
        MMU32 system_volume;
        MMI32 volume;
        int attached;
    } MMAudioManDevice;
    typedef struct MMAudioManDeviceServices
    {
        void *context;
        void *(*create)(void *);
        void (*retain)(void *, void *mixer);
        MMU32 (*release)(void *, void *mixer);
        int (*supports_format)(void *, MMI32 format);
        int (*initialize)(void *, void *mixer, MMI32 format, MMU32 voices, int remix, MMU32 buffer_ms);
        int (*activate)(void *, void *mixer);
        int (*suspend)(void *, void *mixer, int suspended);
        void (*get_volume)(void *, void *mixer, MMU32 *volume);
        void (*set_volume)(void *, void *mixer, MMU32 volume);
        void (*publish_volume)(void *, MMU32 system_volume);
        void (*device_busy)(void *);
    } MMAudioManDeviceServices;
    void mm_audioman_device_init(MMAudioManDevice *);
    /* Always close after opening, including when a newly attached mixer's
     * configuration fails. The mixer starts zero-initialized and is shared
     * by devices that use the same host mixer. */
    int mm_audioman_device_open(MMAudioManDevice *, MMAudioManMixer *, MMI32 format,
                                const MMAudioManDeviceServices *);
    void mm_audioman_device_close(MMAudioManDevice *, MMAudioManMixer *, const MMAudioManDeviceServices *);
    void mm_audioman_device_suspend(MMAudioManDevice *, const MMAudioManMixer *, int suspended,
                                    const MMAudioManDeviceServices *);
    void mm_audioman_device_set_volume(MMAudioManDevice *, const MMAudioManMixer *, MMI32 volume,
                                       const MMSoundControl *, const MMAudioManDeviceServices *);

    typedef struct MMAudioManGroup
    {
        MMI32 depth;
        int active;
        MMU32 id;
    } MMAudioManGroup;
    typedef struct MMAudioManGroupServices
    {
        void *context;
        int (*allocate)(void *, MMU32 *);
        void (*start)(void *, MMU32), (*release)(void *, MMU32);
    } MMAudioManGroupServices;
    void mm_audioman_group_begin(MMAudioManGroup *, const MMAudioManGroupServices *);
    void mm_audioman_group_end(MMAudioManGroup *, const MMAudioManGroupServices *);

    typedef struct MMAudioManQueueServices
    {
        MMSoundQueueServices entries;
        void *context;
        void *(*loop)(void *, void *object, MMI32 plays);
        void *(*identity)(void *, void *object);
        int (*in_memory)(void *, void *object);
        void (*stop)(void *), (*play)(void *);
        void (*volume)(void *, MMU32), (*seek)(void *, MMU32);
        void (*source)(void *, void *object), (*cached_source)(void *, void *object);
        int (*enlist)(void *, MMU32 group);
        MMU32 (*clock)(void *);
    } MMAudioManQueueServices;
    /* The queue count may be -1 before its list exists. The lock is recursive.
     * A successful loop callback transfers one new reference. Source takes no
     * reference from the queue and accepts NULL to detach the channel. */
    void mm_audioman_queue_run(MMU32 *start, MMI32 first, const MMAudioManGroup *, const MMAudioManQueueServices *);
    void mm_audioman_queue_pause(MMU32 *start, MMI32 first, const MMAudioManGroup *, const MMAudioManQueueServices *);
    void mm_audioman_queue_notify(MMU32 *start, void *sound, const MMAudioManGroup *, const MMAudioManQueueServices *);
#ifdef __cplusplus
}
#endif
#endif

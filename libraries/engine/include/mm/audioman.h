/* AudioMan runtime policy converted from Kauai sndam.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_AUDIOMAN_H
#define MM_ENGINE_AUDIOMAN_H
#include "block_owner.h"
#include "sound_queue.h"
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

/* Scene event storage converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_TRACK_H
#define MM_ENGINE_SCENE_TRACK_H
#include "collections.h"
#include "scene_data.h"
#ifdef __cplusplus
extern "C" {
#endif
    typedef struct MMSceneTrackEvent
    {
        MMI32 frame, kind;
    } MMSceneTrackEvent;
    typedef struct MMSceneTrack
    {
        MMCollectionStorage storage;
        MMGroup rows;
    } MMSceneTrack;
    /* The row view borrows this track's storage. Keep the record at a stable
     * address until disposal. Payloads are borrowed through the next edit. */
    int mm_scene_track_init(MMSceneTrack *, const MMStorageAllocator *, int trash_unused);
    void mm_scene_track_dispose(MMSceneTrack *);
    int mm_scene_track_get(const MMSceneTrack *, MMI32 index, MMSceneTrackEvent *);
    void *mm_scene_track_payload(const MMSceneTrack *, MMI32 index, MMI32 *bytes);
    int mm_scene_track_insert(MMSceneTrack *, MMI32 index, MMI32 bytes, const void *, const MMSceneTrackEvent *);
    int mm_scene_track_put(MMSceneTrack *, MMI32 index, MMI32 bytes, const void *);
    int mm_scene_track_set(MMSceneTrack *, MMI32 index, const MMSceneTrackEvent *);
    /* The wire writer takes a track of encoded payloads in host byte order. */
    MMSceneGroupServices mm_scene_track_wire_groups(void);
    MMU32 mm_scene_track_wire_size(const MMSceneTrack *);
    int mm_scene_track_write(MMSceneTrack *, int system, const MMStorageWriter *);
    /* The dense group's event bytes already use the requested wire order.
     * Its location table stays in host order, including after failed writes. */
    MMU32 mm_scene_group_wire_size(const MMGroup *);
    int mm_scene_group_write(MMGroup *, int system, int big_endian, const MMStorageWriter *);
#ifdef __cplusplus
}
#endif
#endif

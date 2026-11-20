/* Scene state and persistence converted from engine scene_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_DATA_H
#define MM_ENGINE_SCENE_DATA_H
#include "movie_title.h"
#include "chunky_file.h"
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SCENE_HEADER_BYTES = 16,
        MM_SCENE_EVENT_BYTES = 8,
        MM_SCENE_EMPTY_GROUP_BYTES = 20,
        MM_SCENE_BACKGROUND_GROUP_BYTES = 52,
        MM_SCENE_TRANSITION_LIMIT = 5
    };
    enum MMSceneEventKind
    {
        MM_SCENE_ADD_ACTOR,
        MM_SCENE_PLAY_SOUND,
        MM_SCENE_ADD_TEXT,
        MM_SCENE_CHANGE_CAMERA,
        MM_SCENE_SET_BACKGROUND,
        MM_SCENE_PAUSE
    };
    enum
    {
        MM_SCENE_CHUNK = 0x5343454e,
        MM_SCENE_FRAME_GROUP = 0x47474652,
        MM_SCENE_START_GROUP = 0x47475354,
        MM_SCENE_ACTOR_CHUNK = 0x41435452,
        MM_SCENE_THUMBNAIL_CHUNK = 0x5448554d
    };
    typedef struct MMSceneState
    {
        MMI32 currentFrame, firstFrame, lastFrame, transition;
        MMMovieTitle name; /* Counted native text, without movie-title selection policy. */
    } MMSceneState;
    int mm_scene_init(MMSceneState *, unsigned unit);
    int mm_scene_decode_header(const MMReadStream *, MMSceneState *);
    struct MMMovieArchiveEditor;
    int mm_scene_archive_read_header(const struct MMMovieArchiveEditor *, MMU32 scene, MMSceneState *);
    /* Write the SCEN header followed by its counted name. */
    int mm_scene_archive_write(const struct MMMovieArchiveEditor *, MMU32 scene, const MMSceneState *);
    int mm_scene_encode_header(const MMSceneState *, int system, int big_endian, unsigned char bytes[16]);
    typedef struct MMSceneIO
    {
        void *context;
        /* Calls the receiver with the decoded SCEN and returns its success. */
        int (*read)(void *, MMU32 number, MMReadReceiver, void *out);
        int (*put_header)(void *, MMU32 number, const unsigned char bytes[16]);
        MMChunkTitleIO titles;
    } MMSceneIO;
    /* Read commits name and metadata together; failure preserves the old state.
     * Header validation retains legacy frame/transition rules. Writing publishes
     * header before name and does not roll it back if setting the name fails. */
    int mm_scene_read(MMSceneState *, MMU32 number, const MMSceneIO *);
    int mm_scene_write(const MMSceneState *, MMU32 number, int system, int big_endian,
                             const MMSceneIO *);
    /* Transition file helpers retain full metadata/name read/write ordering.
     * Failed reads preserve output; failed name write may follow committed header. */
    int mm_scene_transition_read(MMU32, unsigned unit, MMI32 *, const MMSceneIO *);
    int mm_scene_transition_write(MMU32, unsigned unit, MMI32 transition, int system, int big_endian,
                                        const MMSceneIO *);
    typedef struct MMSceneGroupServices
    {
        MMU32 (*fixed_width)(void *group);
        MMU32 (*count)(void *group);
        void (*fixed)(void *group, MMU32 index, unsigned char bytes[8]);
        MMI32 (*size)(void *group, MMU32 index);
        const void *(*data)(void *group, MMU32 index);
    } MMSceneGroupServices;
    /* Checks layout/type/size only, preserving historical acceptance of payload
     * contents and frame ordering. Byte order describes fixed/variable data, not
     * the already-decoded GG offsets/header. */
    int mm_scene_valid_events(void *group, int big_endian, int start, const MMSceneGroupServices *);
    int mm_scene_encode_empty_group(int system, int big_endian, unsigned char bytes[MM_SCENE_EMPTY_GROUP_BYTES]);
    int mm_scene_encode_background_group(int system, int big_endian, MMI32 first_frame, MMI32 source,
                                         MMU32 background, unsigned char bytes[MM_SCENE_BACKGROUND_GROUP_BYTES]);
    /* Replace every background tag in a decoded start group. Preserve the
     * storage order, other events and the collection's wire byte order. Deliver
     * a complete group only if all rows are valid and a background is present. */
    int mm_scene_retarget_start_background(const MMReadStream *, MMI32 source, MMU32 background,
                                            MMByteReceiver, void *context);
    /* Map document-owned background numbers. Deliver a replacement only when
     * a number changes. All other bytes stay in their original wire order. */
    int mm_scene_rebind_start_backgrounds(const MMReadStream *, int (*map)(void *, MMU32, MMU32 *),
                                          MMByteReceiver, void *context);
    /* Append an Add Actor row to a validated start group. Keep its byte order
     * and existing records/locations. Write the supplied output system marker.
     * Returns a movie read result and propagates a rejected write as IO. */
    int mm_scene_append_start_actor(const MMReadStream *, int system, MMI32 frame, MMU32 slot,
                                    MMByteReceiver, void *context);
    enum
    {
        MM_SCENE_GROUP_NATIVE_ORDER = 1,
        MM_SCENE_GROUP_OTHER_ORDER = 256
    };
    typedef struct MMSceneReadGroupServices
    {
        void *context;
        MMSceneGroupServices groups;
        void *(*read)(void *, int *order);
        void (*release)(void *, void *group);
    } MMSceneReadGroupServices;
    /* read returns an owned group with collection headers/locations decoded but
     * Scene payloads still in the returned order. Reject unknown markers or invalid
     * Scene layout, releasing exactly once. Success transfers ownership and publishes
     * optional order only after validation. Failures leave order untouched. No payload
     * rewriting, host effects or reentrancy. native_big describes the host layout.
     * start=0/1 validates full frame/start layout. */
    void *mm_scene_read_event_group(int start, int native_big, int *order, const MMSceneReadGroupServices *);
    typedef struct MMSceneEventGroups
    {
        MMSceneGroupServices groups;
        MMU32 (*wire_size)(void *group);
        int (*write)(void *group, int system, const MMStorageWriter *);
    } MMSceneEventGroups;
    /* Both new children are adopted before old links are removed. Failure cleans
     * frame then start, removing adopted links or unadopted chunks as appropriate.
     * Payload groups passed to write are native endian, synchronous and borrowed. */
    int mm_scene_archive_write_events(const struct MMMovieArchiveEditor *, MMU32 scene, void *frames,
                                      void *starts, const MMSceneEventGroups *);
    struct MMSceneTrack;
    int mm_scene_archive_write_tracks(const struct MMMovieArchiveEditor *, MMU32 scene,
                                      struct MMSceneTrack *frames, struct MMSceneTrack *starts);
    /* Create a scene with two empty event tracks. Any failure publishes nil. */
    int mm_scene_archive_create(const struct MMMovieArchiveEditor *, const MMSceneState *, MMU32 *number);
    /* Replace both event groups with an empty frame group and one background start. */
    int mm_scene_archive_opening_background(const struct MMMovieArchiveEditor *, MMU32 scene, MMI32 first_frame,
                                            MMI32 source, MMU32 background);
#ifdef __cplusplus
}
#endif
#endif

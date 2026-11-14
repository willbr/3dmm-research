/* Scene state and persistence converted from engine scene_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_DATA_H
#define THREE_MM_ENGINE_SCENE_DATA_H
#include "movie_title.h"
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_SCENE_HEADER_BYTES = 16,
        THREE_MM_SCENE_EVENT_BYTES = 8,
        THREE_MM_SCENE_TRANSITION_LIMIT = 5
    };
    enum ThreeMMSceneEventKind
    {
        THREE_MM_SCENE_ADD_ACTOR,
        THREE_MM_SCENE_PLAY_SOUND,
        THREE_MM_SCENE_ADD_TEXT,
        THREE_MM_SCENE_CHANGE_CAMERA,
        THREE_MM_SCENE_SET_BACKGROUND,
        THREE_MM_SCENE_PAUSE
    };
    typedef struct ThreeMMSceneState
    {
        ThreeMMI32 currentFrame, firstFrame, lastFrame, transition;
        ThreeMMMovieTitle name; /* Counted native text, without movie-title selection policy. */
    } ThreeMMSceneState;
    int three_mm_scene_init(ThreeMMSceneState *, unsigned unit);
    int three_mm_scene_decode_header(const ThreeMMReadStream *, ThreeMMSceneState *);
    int three_mm_scene_encode_header(const ThreeMMSceneState *, int system, int big_endian, unsigned char bytes[16]);
    typedef struct ThreeMMSceneIO
    {
        void *context;
        int (*read)(void *, ThreeMMU32 number, ThreeMMReadStream *);
        void (*load_name)(void *, ThreeMMU32 number, ThreeMMMovieTitle *);
        int (*put_header)(void *, ThreeMMU32 number, const unsigned char bytes[16]);
        int (*set_name)(void *, ThreeMMU32 number, const ThreeMMMovieTitle *);
    } ThreeMMSceneIO;
    /* Read commits name and metadata together; failure preserves the old state.
     * Header validation retains legacy frame/transition rules. Writing publishes
     * header before name and does not roll it back if setting the name fails. */
    int three_mm_scene_read(ThreeMMSceneState *, ThreeMMU32 number, const ThreeMMSceneIO *);
    int three_mm_scene_write(const ThreeMMSceneState *, ThreeMMU32 number, int system, int big_endian,
                             const ThreeMMSceneIO *);
    /* Transition file helpers retain full metadata/name read/write ordering.
     * Failed reads preserve output; failed name write may follow committed header. */
    int three_mm_scene_transition_read(ThreeMMU32, unsigned unit, ThreeMMI32 *, const ThreeMMSceneIO *);
    int three_mm_scene_transition_write(ThreeMMU32, unsigned unit, ThreeMMI32 transition, int system, int big_endian,
                                        const ThreeMMSceneIO *);
    typedef struct ThreeMMSceneGroupServices
    {
        ThreeMMU32 (*fixed_width)(void *group);
        ThreeMMU32 (*count)(void *group);
        void (*fixed)(void *group, ThreeMMU32 index, unsigned char bytes[8]);
        ThreeMMI32 (*size)(void *group, ThreeMMU32 index);
        const void *(*data)(void *group, ThreeMMU32 index);
    } ThreeMMSceneGroupServices;
    /* Checks layout/type/size only, preserving historical acceptance of payload
     * contents and frame ordering. Byte order describes fixed/variable data, not
     * the already-decoded GG offsets/header. */
    int three_mm_scene_valid_events(void *group, int big_endian, int start, const ThreeMMSceneGroupServices *);
    enum
    {
        THREE_MM_SCENE_GROUP_HEADERS_ONLY = -1,
        THREE_MM_SCENE_GROUP_NATIVE_ORDER = 1,
        THREE_MM_SCENE_GROUP_OTHER_ORDER = 256
    };
    typedef struct ThreeMMSceneReadGroupServices
    {
        void *context;
        ThreeMMSceneGroupServices groups;
        void *(*read)(void *, int *order);
        void (*release)(void *, void *group);
    } ThreeMMSceneReadGroupServices;
    /* read returns an owned group with collection headers/locations decoded but
     * Scene payloads still in the returned order. Reject unknown markers or invalid
     * Scene layout, releasing exactly once. Success transfers ownership and publishes
     * optional order only after validation. Failures leave order untouched. No payload
     * rewriting, host effects or reentrancy. native_big describes the host layout.
     * HEADERS_ONLY mode checks fixed width only, retaining unknown event/payload
     * acceptance for tag collection; start=0/1 validates full frame/start layout. */
    void *three_mm_scene_read_event_group(int start, int native_big, int *order, const ThreeMMSceneReadGroupServices *);
    typedef struct ThreeMMSceneEventIO
    {
        void *context;
        ThreeMMSceneGroupServices groups;
        ThreeMMU32 (*wire_size)(void *group);
        int (*previous)(void *, ThreeMMU32 scene, int start, ThreeMMU32 *number);
        int (*add)(void *, int start, ThreeMMU32 bytes, ThreeMMU32 *number);
        int (*write)(void *, void *group);
        int (*adopt)(void *, ThreeMMU32 scene, int start, ThreeMMU32 number);
        void (*remove)(void *, ThreeMMU32 scene, int start, ThreeMMU32 number, int linked);
        void (*clear_loner)(void *, int start, ThreeMMU32 number);
    } ThreeMMSceneEventIO;
    /* Both new children are adopted before old links are removed. Failure cleans
     * frame then start, removing adopted links or unadopted chunks as appropriate.
     * Payload groups passed to write are native endian, synchronous and borrowed. */
    int three_mm_scene_write_events(ThreeMMU32 scene, void *frames, void *starts, const ThreeMMSceneEventIO *);
    typedef struct ThreeMMSceneCreateServices
    {
        ThreeMMSceneIO io;
        ThreeMMSceneEventIO events;
        void *(*new_group)(void *);
        void (*release_group)(void *);
        int (*add_scene)(void *, ThreeMMU32 bytes, ThreeMMU32 *number);
        void (*remove_scene)(void *, ThreeMMU32 number);
    } ThreeMMSceneCreateServices;
    /* Allocates both groups before publishing nil. Releases frame then start;
     * deletes a successfully created scene on later failure. Every failure publishes
     * nil, including an add callback that changed its output before failing. */
    int three_mm_scene_create(const ThreeMMSceneState *, ThreeMMU32 *number, int system, int big_endian,
                              const ThreeMMSceneCreateServices *);
#ifdef __cplusplus
}
#endif
#endif

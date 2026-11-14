/* Scene event dispatch converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_EVENTS_H
#define THREE_MM_ENGINE_SCENE_EVENTS_H
#include "scene_owner.h"
#include "scene_runtime.h"
#include "tag.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum ThreeMMSceneEventIssue
    {
        THREE_MM_SCENE_EVENT_NO_BACKGROUND,
        THREE_MM_SCENE_EVENT_UNKNOWN,
        THREE_MM_SCENE_EVENT_RESTORE_FAILED,
        THREE_MM_SCENE_EVENT_FIRST_CAMERA_FAILED,
        THREE_MM_SCENE_EVENT_OWNER_OCCUPIED
    };
    enum
    {
        THREE_MM_SCENE_EVENT_MIDI = 4,
        THREE_MM_SCENE_EVENT_NIL_TRANSITION = -1,
        THREE_MM_SCENE_EVENT_CUT_TRANSITION = 0
    };
    typedef struct ThreeMMSceneEventServices
    {
        void *context, *scene;
        ThreeMMSceneRuntime *runtime;
        const ThreeMMSceneState *data;
        ThreeMMSceneOwner *owner;
        ThreeMMSceneOwnerServices owners;
        void (*event)(void *, void *group, ThreeMMI32 index, ThreeMMI32 *frame, int *kind);
        void *(*payload)(void *, void *group, ThreeMMI32 index);
        int (*sound_style)(void *, void *payload);
        void *(*duplicate_sound)(void *, void *payload);
        void (*play_sound)(void *, void *payload);
        void (*read_tag)(void *, void *payload, ThreeMMTag *);
        void *(*fetch_background)(void *, ThreeMMTag *);
        int (*set_camera)(void *, void *background, ThreeMMI32 camera);
        int (*transition)(void *);
        void (*set_transition)(void *, int);
        void (*publish_tag)(void *, const ThreeMMTag *);
        void *(*read_object)(void *, void *payload);
        int (*append)(void *, int text, void *object);
        void (*remove_last)(void *, int text);
        void (*bind_actor)(void *, void *actor);
        int (*seek)(void *, int text, void *object, ThreeMMI32 frame);
        int (*pause_reason)(void *, void *payload);
        ThreeMMI32 (*pause_delay)(void *, void *payload);
        void (*pause_type)(void *, int reason);
        void (*pause)(void *, int reason, ThreeMMI32 delay);
        ThreeMMI32 (*camera)(void *, void *payload);
        void (*diagnostic)(void *, int issue);
    } ThreeMMSceneEventServices;
    /* Valid native-order event payloads and initialized state/services required.
     * Synchronous callbacks must not reenter dispatch or mutate the event groups.
     * Groups/payloads and roll-call objects are borrowed; start events own objects.
     * Background fetch/duplicate_sound return newly owned references. Camera
     * payloads are host-order ThreeMMI32 values, including synthetic zero.
     * Set-background requires a vacant owner slot. Sound/pause/camera suppression
     * does not suppress MIDI caching, pause notifications or the background-required diagnostic.
     * Failed actor seek removes the list entry but retains its scene binding.
     * A failed start leaves its completed prefix applied. Unplay deliberately ignores
     * previous-sound failures and diagnoses/ignores previous-camera failures; only
     * a failed camera-zero fallback returns failure. Background frame remains stale
     * when unplay clears the background sound, matching the native implementation. */
    int three_mm_scene_play_event(ThreeMMI32 frame, int kind, void *payload, ThreeMMU32 disabled,
                                  const ThreeMMSceneEventServices *);
    int three_mm_scene_unplay_event(int kind, const ThreeMMSceneEventServices *);
    int three_mm_scene_start_events(int actors_only, const ThreeMMSceneEventServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Scene event dispatch converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_EVENTS_H
#define MM_ENGINE_SCENE_EVENTS_H
#include "scene_owner.h"
#include "tag.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum MMSceneEventIssue
    {
        MM_SCENE_EVENT_NO_BACKGROUND,
        MM_SCENE_EVENT_UNKNOWN,
        MM_SCENE_EVENT_RESTORE_FAILED,
        MM_SCENE_EVENT_FIRST_CAMERA_FAILED,
        MM_SCENE_EVENT_OWNER_OCCUPIED
    };
    enum
    {
        MM_SCENE_EVENT_MIDI = 4,
        MM_SCENE_EVENT_NIL_TRANSITION = -1,
        MM_SCENE_EVENT_CUT_TRANSITION = 0
    };
    struct MMSceneRuntime;
    typedef struct MMSceneEventServices
    {
        void *context;
        struct MMSceneRuntime *runtime;
        const MMSceneState *data;
        MMSceneOwner *owner;
        MMSceneOwnerServices owners;
        void *(*duplicate_sound)(void *, void *payload);
        void (*play_sound)(void *, void *payload);
        void *(*fetch_background)(void *, MMTag *);
        int (*set_camera)(void *, void *background, MMI32 camera);
        int (*transition)(void *);
        void (*set_transition)(void *, int);
        void (*bind_actor)(void *, void *actor);
        int (*seek)(void *, int text, void *object, MMI32 frame);
        void (*pause_type)(void *, int reason);
        void (*pause)(void *, int reason, MMI32 delay);
        void (*diagnostic)(void *, int issue);
    } MMSceneEventServices;
    /* Valid native-order event payloads and initialized state/services required.
     * Synchronous callbacks must not reenter dispatch or mutate the event groups.
     * Groups/payloads and roll-call objects are borrowed; start events own objects.
     * Native payloads hold an MMSceneSound packet, MMTag, object pointer,
     * camera number, or a pause reason and delay. Camera and pause values
     * are host-order MMI32. Background fetch and duplicate_sound return
     * newly owned references.
     * Set-background requires a vacant owner slot. Sound/pause/camera suppression
     * does not suppress MIDI caching, pause notifications or the background-required diagnostic.
     * Failed actor seek removes the list entry but retains its scene binding.
     * A failed start leaves its completed prefix applied. Unplay deliberately ignores
     * previous-sound failures and diagnoses/ignores previous-camera failures; only
     * a failed camera-zero fallback returns failure. Background frame remains stale
     * when unplay clears the background sound, matching the native implementation. */
    int mm_scene_play_event(MMI32 frame, int kind, void *payload, MMU32 disabled,
                                  const MMSceneEventServices *);
    int mm_scene_play_frame_event(MMI32 index, MMU32 disabled, const MMSceneEventServices *);
    int mm_scene_unplay_event(int kind, const MMSceneEventServices *);
    int mm_scene_start_events(int actors_only, const MMSceneEventServices *);
    /* First background start event wins. The result borrows its cache; no retain,
     * fetch, playback or selection effects. Absence or a truncated tag leaves
     * output unchanged. Traversal cannot be reentered/mutated. */
    int mm_scene_background_tag(MMTag *, const MMSceneEventServices *);
    typedef struct MMScenePickServices
    {
        void *context;
        /* The backend owns hit-part output, including no-hit/failure prefixes. */
        void *(*pick)(void *, MMI32 x, MMI32 y);
        MMI32 (*count)(void *);
        void *(*actor)(void *, MMI32 index);
        int (*owns_body)(void *, void *actor, void *body);
        void (*orphan_body)(void *);
    } MMScenePickServices;
    /* Borrowed actors/body. A miss skips actor traversal and diagnostics. A hit
     * searches in list order, returning the first owner; an orphan diagnoses and
     * returns NULL. No selection, retain/release or hit-part restoration. Callbacks
     * are synchronous and must not mutate/reenter the actor traversal. */
    void *mm_scene_pick_actor(MMI32 x, MMI32 y, const MMScenePickServices *);
#ifdef __cplusplus
}
#endif
#endif

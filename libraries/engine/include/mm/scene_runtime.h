/* Scene frame/replay and prerender policy converted from engine scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_RUNTIME_H
#define MM_ENGINE_SCENE_RUNTIME_H
#include "scene_events.h"
#include "movie_session.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum MMSceneEffects
    {
        MM_SCENE_EFFECT_SOUNDS = 1,
        MM_SCENE_EFFECT_PAUSES = 2,
        MM_SCENE_EFFECT_TEXTS = 4,
        MM_SCENE_EFFECT_ACTORS = 8,
        MM_SCENE_EFFECT_CAMERAS = 64
    };
    enum MMSceneNotice
    {
        MM_SCENE_RESET_PAUSE,
        MM_SCENE_RECORD_SOUND,
        MM_SCENE_DIRTY,
        MM_SCENE_INVALIDATE,
        MM_SCENE_SOUND_INDICATOR,
        MM_SCENE_GOTO_FAILURE,
        MM_SCENE_PRERENDER_ASSERT
    };
    /* Synchronous, borrowed services. Event groups must not be mutated/reentered
     * during play/unplay; changing their length invalidates the event cursor too.
     * Object counts are queried each iteration, as in the original engine.
     * A seek's optional sound pointer is in/out, including on failure.
     * Frame metadata and runtime state are live throughout callbacks; failures
     * preserve completed prefixes rather than rolling back a seek.
     * text=0 selects actors, text=1 selects captions. */
    typedef struct MMSceneRuntimeServices
    {
        void *context;
        struct MMMovieSession *movie;
        MMSceneEventServices events;
        int (*seek)(void *, int text, void *object, MMI32 frame, int *sound);
        int (*lifetime)(void *, int text, void *object, MMI32 *first, MMI32 *last);
        int (*replay_actor)(void *, void *actor, MMU32 enabled);
        int (*playing)(void *);
        int (*recording)(void *);
        int (*must_render)(void *, void *actor, MMI32 until);
        int (*prerendered)(void *, void *actor);
        void (*show)(void *, void *actor, int visible);
        void (*set_prerendered)(void *, void *actor, int value);
        void (*snapshot)(void *, int begin);
        void (*notice)(void *, int notice, int value);
    } MMSceneRuntimeServices;
    int mm_scene_goto(MMI32 target, const MMSceneRuntimeServices *);
    int mm_scene_replay(MMU32 enabled, const MMSceneRuntimeServices *);
    int mm_scene_force(MMI32 frame, int text, int *sound, const MMSceneRuntimeServices *);
    void mm_scene_move_first(MMSceneOwner *, MMI32 target);
    typedef void (*MMSceneMoveCamera)(void *context, void *group, MMI32 index, MMI32 frame);
    /* Move camera rows in the opening-frame prefix to the front in traversal order.
     * The reader observes each move and row count stays fixed. */
    void mm_scene_move_opening_cameras(MMI32 first, MMI32 target, const MMMovieEventView *, MMSceneMoveCamera);
    void mm_scene_extend_range(const MMSceneRuntimeServices *);
    /* The minimum 32-bit frame is reserved. Extensions retain every authored frame. */
    int mm_scene_range_contains(MMI32 authored_first, MMI32 authored_last, long first, long last);
    typedef struct MMSceneRangeStoreServices
    {
        void *context;
        int (*open_events)(void *, MMMovieEventView *);
        MMSceneMoveCamera move_camera;
        int (*replace_events)(void *);
        int (*drop_thumbnail)(void *);
        int (*write_scene)(void *);
    } MMSceneRangeStoreServices;
    enum { MM_SCENE_RANGE_INVALID = -1 };
    /* Operates on a private working copy. Returns the invalid-range status or
     * 1/0 from the write effects. Callbacks never unwind. */
    int mm_scene_range_store(MMSceneState *, long first, long last, const MMSceneRangeStoreServices *);
    void mm_scene_prerender(int start_now, const MMSceneRuntimeServices *);
    void mm_scene_end_prerender(const MMSceneRuntimeServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Scene frame/replay and prerender policy converted from engine scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_RUNTIME_H
#define MM_ENGINE_SCENE_RUNTIME_H
#include "scene_data.h"
#include "movie_frame.h"
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
    typedef struct MMSceneRuntime
    {
        MMI32 next_event, background_frame;
        MMU32 disabled;
        MMMovieLiveFrame publication;
    } MMSceneRuntime;
    /* Synchronous, borrowed services. Event groups must not be mutated/reentered
     * during play/unplay; changing their length invalidates the native cursor too.
     * Object counts are queried each iteration, as in the original engine.
     * A seek's optional sound pointer is in/out, including on failure.
     * Frame metadata and runtime state are live throughout callbacks; failures
     * preserve completed prefixes rather than rolling back a seek.
     * text=0 selects actors, text=1 selects captions. */
    typedef struct MMSceneRuntimeServices
    {
        void *context;
        struct MMMovieSession *movie;
        MMI32 (*event_count)(void *);
        void (*event)(void *, MMI32 index, MMI32 *frame, int *kind);
        int (*play)(void *, MMI32 index, MMU32 disabled);
        int (*unplay)(void *, MMI32 index);
        void (*move_camera)(void *, MMI32 index, MMI32 frame);
        MMI32 (*object_count)(void *, int text);
        void *(*object)(void *, int text, MMI32 index);
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
    void mm_scene_runtime_init(MMSceneRuntime *);
    int mm_scene_goto(MMSceneRuntime *, MMSceneState *, MMI32,
                            const MMSceneRuntimeServices *);
    int mm_scene_replay(MMSceneRuntime *, const MMSceneState *, MMU32 enabled,
                              const MMSceneRuntimeServices *);
    int mm_scene_force(MMI32 frame, int text, int *sound, const MMSceneRuntimeServices *);
    void mm_scene_move_first(MMSceneState *, MMI32, const MMSceneRuntimeServices *);
    void mm_scene_extend_range(MMSceneState *, const MMSceneRuntimeServices *);
    void mm_scene_prerender(const MMSceneRuntime *, const MMSceneState *, int start_now,
                                  const MMSceneRuntimeServices *);
    void mm_scene_end_prerender(const MMSceneRuntimeServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Scene frame/replay and prerender policy converted from engine scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_RUNTIME_H
#define THREE_MM_ENGINE_SCENE_RUNTIME_H
#include "scene_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum ThreeMMSceneEffects
    {
        THREE_MM_SCENE_EFFECT_SOUNDS = 1,
        THREE_MM_SCENE_EFFECT_PAUSES = 2,
        THREE_MM_SCENE_EFFECT_TEXTS = 4,
        THREE_MM_SCENE_EFFECT_ACTORS = 8,
        THREE_MM_SCENE_EFFECT_CAMERAS = 64
    };
    enum ThreeMMSceneNotice
    {
        THREE_MM_SCENE_RESET_PAUSE,
        THREE_MM_SCENE_RECORD_SOUND,
        THREE_MM_SCENE_DIRTY,
        THREE_MM_SCENE_INVALIDATE,
        THREE_MM_SCENE_SOUND_INDICATOR,
        THREE_MM_SCENE_GOTO_FAILURE,
        THREE_MM_SCENE_PRERENDER_ASSERT
    };
    typedef struct ThreeMMSceneRuntime
    {
        ThreeMMI32 next_event, background_frame;
        ThreeMMU32 disabled;
    } ThreeMMSceneRuntime;
    /* Synchronous, borrowed services. Event groups must not be mutated/reentered
     * during play/unplay; changing their length invalidates the native cursor too.
     * Object counts are queried each iteration, as in the original engine.
     * A seek's optional sound pointer is in/out, including on failure.
     * Frame metadata and runtime state are live throughout callbacks; failures
     * preserve completed prefixes rather than rolling back a seek.
     * text=0 selects actors, text=1 selects captions. */
    typedef struct ThreeMMSceneRuntimeServices
    {
        void *context;
        ThreeMMI32 (*event_count)(void *);
        void (*event)(void *, ThreeMMI32 index, ThreeMMI32 *frame, int *kind);
        int (*play)(void *, ThreeMMI32 index, ThreeMMU32 disabled);
        int (*unplay)(void *, ThreeMMI32 index);
        void (*move_camera)(void *, ThreeMMI32 index, ThreeMMI32 frame);
        ThreeMMI32 (*object_count)(void *, int text);
        void *(*object)(void *, int text, ThreeMMI32 index);
        int (*seek)(void *, int text, void *object, ThreeMMI32 frame, int *sound);
        int (*lifetime)(void *, int text, void *object, ThreeMMI32 *first, ThreeMMI32 *last);
        int (*replay_actor)(void *, void *actor, ThreeMMU32 enabled);
        int (*playing)(void *);
        int (*recording)(void *);
        int (*must_render)(void *, void *actor, ThreeMMI32 until);
        int (*prerendered)(void *, void *actor);
        void (*show)(void *, void *actor, int visible);
        void (*set_prerendered)(void *, void *actor, int value);
        void (*snapshot)(void *, int begin);
        void (*notice)(void *, int notice, int value);
    } ThreeMMSceneRuntimeServices;
    void three_mm_scene_runtime_init(ThreeMMSceneRuntime *);
    int three_mm_scene_goto(ThreeMMSceneRuntime *, ThreeMMSceneState *, ThreeMMI32,
                            const ThreeMMSceneRuntimeServices *);
    int three_mm_scene_replay(ThreeMMSceneRuntime *, const ThreeMMSceneState *, ThreeMMU32 enabled,
                              const ThreeMMSceneRuntimeServices *);
    int three_mm_scene_force(ThreeMMI32 frame, int text, int *sound, const ThreeMMSceneRuntimeServices *);
    void three_mm_scene_move_first(ThreeMMSceneState *, ThreeMMI32, const ThreeMMSceneRuntimeServices *);
    void three_mm_scene_extend_range(ThreeMMSceneState *, const ThreeMMSceneRuntimeServices *);
    void three_mm_scene_prerender(const ThreeMMSceneRuntime *, const ThreeMMSceneState *, int start_now,
                                  const ThreeMMSceneRuntimeServices *);
    void three_mm_scene_end_prerender(const ThreeMMSceneRuntimeServices *);
#ifdef __cplusplus
}
#endif
#endif

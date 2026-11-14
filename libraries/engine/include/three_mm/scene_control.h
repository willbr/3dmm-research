/* Scene property editing and playback lifecycle converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_CONTROL_H
#define THREE_MM_ENGINE_SCENE_CONTROL_H
#include "scene_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_SCENE_CONTROL_TRANSITION,
        THREE_MM_SCENE_CONTROL_NAME
    };
    typedef struct ThreeMMSceneTransitionUndo
    {
        ThreeMMI32 transition;
    } ThreeMMSceneTransitionUndo;
    typedef struct ThreeMMSceneTitleUndo
    {
        ThreeMMMovieTitle name;
    } ThreeMMSceneTitleUndo;
    typedef struct ThreeMMScenePropertyServices
    {
        void *context;
        ThreeMMSceneState *data;
        void *(*new_undo)(void *, int kind);
        ThreeMMSceneTitleUndo *(*title)(void *, void *);
        ThreeMMSceneTransitionUndo *(*transition)(void *, void *);
        int (*commit)(void *, void *);
        void (*release)(void *, void *);
    } ThreeMMScenePropertyServices;
    /* Valid matching native text units are required. Saved values are copied before
     * undo commit; every local undo reference releases on commit failure or success.
     * Failure leaves Scene metadata unchanged. Metadata changes after commit/release;
     * setters intentionally add no dirty/menu/client effects beyond common undo. */
    int three_mm_scene_name_edit(const ThreeMMMovieTitle *, const ThreeMMScenePropertyServices *);
    int three_mm_scene_transition_edit(ThreeMMI32, const ThreeMMScenePropertyServices *);
    typedef struct ThreeMMScenePropertyUndoServices
    {
        void *context;
        int (*switch_scene)(void *), (*seek)(void *);
        ThreeMMSceneState *(*scene)(void *);
        void (*flush)(void *), (*clear)(void *);
    } ThreeMMScenePropertyUndoServices;
    /* Name undo switches without seeking; transition undo switches then seeks.
     * Scene access follows navigation. Both swap C payloads and flush; failure
     * flushes before clear, which may destroy the payload. No access follows clear. */
    int three_mm_scene_title_swap(ThreeMMSceneTitleUndo *, const ThreeMMScenePropertyUndoServices *);
    int three_mm_scene_transition_swap(ThreeMMSceneTransitionUndo *, const ThreeMMScenePropertyUndoServices *);
    typedef struct ThreeMMScenePlaybackServices
    {
        void *context;
        const ThreeMMSceneState *data;
        ThreeMMI32 (*count)(void *, int text);
        void *(*object)(void *, int text, ThreeMMI32 index);
        int (*lifetime)(void *, int text, void *, ThreeMMI32 *first, ThreeMMI32 *last);
        int (*story)(void *, void *), (*visible)(void *, void *);
        void *(*view)(void *, void *);
        void (*scroll)(void *, void *view), (*missing_view)(void *);
        void (*prerender)(void *, int start), (*clean_text)(void *, void *);
    } ThreeMMScenePlaybackServices;
    /* Refresh all actor lifetimes first, stopping at the first failure. Captions
     * scroll if visible story text or nonstory text starting at the current frame;
     * missing views diagnose/skip. Then force prerender startup. Stop ends prerender
     * before cleaning caption views. Lists are borrowed, dynamically counted each
     * iteration and cannot be reentered/mutated by callbacks. */
    int three_mm_scene_start_playing(const ThreeMMScenePlaybackServices *);
    void three_mm_scene_stop_playing(const ThreeMMScenePlaybackServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Scene property editing and playback lifecycle converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_CONTROL_H
#define MM_ENGINE_SCENE_CONTROL_H
#include "scene_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SCENE_CONTROL_TRANSITION,
        MM_SCENE_CONTROL_NAME
    };
    typedef struct MMSceneTransitionUndo
    {
        MMI32 transition;
    } MMSceneTransitionUndo;
    typedef struct MMSceneTitleUndo
    {
        MMMovieTitle name;
    } MMSceneTitleUndo;
    typedef struct MMScenePropertyServices
    {
        void *context;
        MMSceneState *data;
        void *(*new_undo)(void *, int kind);
        MMSceneTitleUndo *(*title)(void *, void *);
        MMSceneTransitionUndo *(*transition)(void *, void *);
        int (*commit)(void *, void *);
        void (*release)(void *, void *);
    } MMScenePropertyServices;
    /* Valid matching native text units are required. Saved values are copied before
     * undo commit; every local undo reference releases on commit failure or success.
     * Failure leaves Scene metadata unchanged. Metadata changes after commit/release;
     * setters intentionally add no dirty/menu/client effects beyond common undo. */
    int mm_scene_name_edit(const MMMovieTitle *, const MMScenePropertyServices *);
    int mm_scene_transition_edit(MMI32, const MMScenePropertyServices *);
    typedef struct MMScenePropertyUndoServices
    {
        void *context;
        int (*switch_scene)(void *), (*seek)(void *);
        MMSceneState *(*scene)(void *);
        void (*flush)(void *), (*clear)(void *);
    } MMScenePropertyUndoServices;
    /* Name undo switches without seeking; transition undo switches then seeks.
     * Scene access follows navigation. Both swap C payloads and flush; failure
     * flushes before clear, which may destroy the payload. No access follows clear. */
    int mm_scene_title_swap(MMSceneTitleUndo *, const MMScenePropertyUndoServices *);
    int mm_scene_transition_swap(MMSceneTransitionUndo *, const MMScenePropertyUndoServices *);
    typedef struct MMScenePlaybackServices
    {
        void *context;
        const MMSceneState *data;
        MMI32 (*count)(void *, int text);
        void *(*object)(void *, int text, MMI32 index);
        int (*lifetime)(void *, int text, void *, MMI32 *first, MMI32 *last);
        int (*story)(void *, void *), (*visible)(void *, void *);
        void *(*view)(void *, void *);
        void (*scroll)(void *, void *view), (*missing_view)(void *);
        void (*prerender)(void *, int start), (*clean_text)(void *, void *);
    } MMScenePlaybackServices;
    /* Refresh all actor lifetimes first, stopping at the first failure. Captions
     * scroll if visible story text or nonstory text starting at the current frame;
     * missing views diagnose/skip. Then force prerender startup. Stop ends prerender
     * before cleaning caption views. Lists are borrowed, dynamically counted each
     * iteration and cannot be reentered/mutated by callbacks. */
    int mm_scene_start_playing(const MMScenePlaybackServices *);
    void mm_scene_stop_playing(const MMScenePlaybackServices *);
#ifdef __cplusplus
}
#endif
#endif

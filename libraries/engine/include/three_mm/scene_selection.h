/* Scene selection converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_SELECTION_H
#define THREE_MM_ENGINE_SCENE_SELECTION_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMSceneSelection
    {
        void *actor, *text; /* Borrowed primary objects; no retain/release. */
    } ThreeMMSceneSelection;
    typedef struct ThreeMMSceneSelectionServices
    {
        void *context;
        ThreeMMSceneSelection *state;
        void (*publish)(void *, int text, void *);
        /* -1: no view; 0: actor mode; 1: text mode. */
        int (*view_mode)(void *);
        ThreeMMI32 (*count)(void *, int extras);
        void *(*at)(void *, int extras, ThreeMMI32);
        int (*ensure_extras)(void *), (*append_extra)(void *, void *);
        void (*erase_extra)(void *, ThreeMMI32), (*clear_extras)(void *);
        void (*hilite)(void *, void *, int);
        int (*text_selected)(void *, void *);
        void (*select_text)(void *, void *, int);
        void (*invalidate)(void *), (*action_menu)(void *), (*text_notice)(void *);
        /* Copy a counted native display name (at most 255 units). */
        ThreeMMI32 (*name)(void *, void *, unsigned short out[256]);
        /* False for missing body/offscreen actors; leaves output unspecified. */
        int (*visible_position)(void *, void *, ThreeMMActorPoint *);
    } ThreeMMSceneSelectionServices;
    /* Zero-initialize state before use. Publish observes the updated C state.
     * Lists borrow actor references. Callbacks do not reenter or mutate traversed
     * lists. Plain actor selection clears extras/highlights only in actor view mode;
     * no-view and text-mode selection retain the original extras. Text selection
     * preserves the actor pointer, early-return and notification order. Allocation
     * failures retain completed selection prefixes and any newly allocated list.
     * Names/tags use ASCII token boundaries and case folding. Shared seed tokens
     * retain the native 63-unit truncation. Tag input is counted bytes or UTF-16. */
    void three_mm_scene_select_actor(void *, const ThreeMMSceneSelectionServices *);
    void three_mm_scene_select_text(void *, const ThreeMMSceneSelectionServices *);
    ThreeMMI32 three_mm_scene_selection_count(const ThreeMMSceneSelectionServices *);
    void *three_mm_scene_selection_at(ThreeMMI32, const ThreeMMSceneSelectionServices *);
    int three_mm_scene_selection_contains(void *, const ThreeMMSceneSelectionServices *);
    int three_mm_scene_selection_toggle(void *, const ThreeMMSceneSelectionServices *);
    int three_mm_scene_selection_all(const ThreeMMSceneSelectionServices *);
    int three_mm_scene_selection_tag(const void *, ThreeMMI32 length, int wide, const ThreeMMSceneSelectionServices *);
    int three_mm_scene_selection_shared(void *, const ThreeMMSceneSelectionServices *);
    /* Signed 16.16 wrapped sum/division, in selection order. Empty/nonvisible
     * selections leave the output untouched. */
    int three_mm_scene_selection_centroid(ThreeMMActorPoint *, const ThreeMMSceneSelectionServices *);
#ifdef __cplusplus
}
#endif
#endif

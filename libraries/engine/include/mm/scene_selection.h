/* Scene selection converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_SELECTION_H
#define MM_ENGINE_SCENE_SELECTION_H
#include "actor_group_undo.h"
#include "actor_motion.h"
#include "movie_title.h"
#include "scene_owner.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMSceneSelection
    {
        void *actor, *text; /* Borrowed primary objects; no retain/release. */
    } MMSceneSelection;
    typedef struct MMSceneSelectionServices
    {
        void *context;
        MMSceneSelection *state;
        MMSceneOwner *owner;
        void (*publish)(void *, int text, void *);
        /* -1: no view; 0: actor mode; 1: text mode. */
        int (*view_mode)(void *);
        void (*hilite)(void *, void *, int);
        int (*text_selected)(void *, void *);
        void (*select_text)(void *, void *, int);
        void (*invalidate)(void *), (*action_menu)(void *), (*text_notice)(void *);
        /* Initialize and copy the actor's counted native display name. */
        void (*name)(void *, void *, MMMovieTitle *);
        /* False for missing body/offscreen actors; leaves output unspecified. */
        int (*visible_position)(void *, void *, MMActorPoint *);
    } MMSceneSelectionServices;
    typedef struct MMSceneTagEditServices
    {
        void *context;
        MMActorRenameGroup *(*create_undo)(void *, void **undo);
        MMI32 (*actor_id)(void *, void *actor);
        int (*rename)(void *, MMI32 actor, const void *text, MMU32 count);
        int (*commit_undo)(void *, void *undo);
        void (*release_undo)(void *, void *undo);
        void (*clear_undo)(void *);
        void (*not_undoable)(void *);
        void (*dirty)(void *);
    } MMSceneTagEditServices;
    /* Zero-initialize state before use. Publish observes the updated C state.
     * Lists borrow actor references. Callbacks do not reenter or mutate traversed
     * lists. Plain actor selection clears extras/highlights only in actor view mode;
     * no-view and text-mode selection retain the original extras. Text selection
     * preserves the actor pointer, early-return and notification order. Allocation
     * failures retain completed selection prefixes.
     * Names/tags use ASCII token boundaries and case folding. Shared seed tokens
     * retain the native 63-unit truncation. Tag input is counted bytes or UTF-16. */
    void mm_scene_select_actor(void *, const MMSceneSelectionServices *);
    void mm_scene_select_text(void *, const MMSceneSelectionServices *);
    MMI32 mm_scene_selection_count(const MMSceneSelectionServices *);
    void *mm_scene_selection_at(MMI32, const MMSceneSelectionServices *);
    int mm_scene_selection_contains(void *, const MMSceneSelectionServices *);
    int mm_scene_selection_toggle(void *, const MMSceneSelectionServices *);
    int mm_scene_selection_all(const MMSceneSelectionServices *);
    int mm_scene_selection_tag(const void *, MMI32 length, int wide, const MMSceneSelectionServices *);
    int mm_scene_selection_shared(void *, const MMSceneSelectionServices *);
    /* Edit in selection order. Append max(scene numeric tags) + 1 or remove
     * one trailing token. Numbers fit the counted-name limit without machine
     * integer overflow. A failed edit retains its completed prefix and clears
     * undo. Failed undo registration also leaves the scene dirty. */
    void mm_scene_selection_edit_tags(int strip, const MMSceneSelectionServices *,
                                      const MMSceneTagEditServices *);
    /* Signed 16.16 wrapped sum/division, in selection order. Empty/nonvisible
     * selections leave the output untouched. */
    int mm_scene_selection_centroid(MMActorPoint *, const MMSceneSelectionServices *);
#ifdef __cplusplus
}
#endif
#endif

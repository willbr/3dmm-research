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
    typedef struct MMSceneSelectionServices
    {
        void *context;
        MMSceneOwner *owner;
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
    /* Selection borrows its objects. Callbacks do not reenter or mutate traversed
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

    enum
    {
        MM_SCENE_POINTER_OTHER,
        MM_SCENE_POINTER_DEFAULT,
        MM_SCENE_POINTER_SELECT,
        MM_SCENE_POINTER_TRANSFORM
    };
    typedef struct MMSceneActorPointer
    {
        void *target;
        const MMSceneOwner *origin;
        MMI32 x, y;
        int cancelled;
    } MMSceneActorPointer;
    typedef struct MMSceneActorPointerInput
    {
        MMI32 x, y;
        int tool, shift;
    } MMSceneActorPointerInput;
    typedef struct MMSceneActorPointerView
    {
        void *context;
        void *(*pick)(void *, MMI32 x, MMI32 y, MMI32 *part);
        int (*covers)(void *, void *actor, MMI32 x, MMI32 y, MMI32 *part);
        void (*clicked)(void *, void *actor);
        void (*world_dirty)(void *);
        void (*mark_views)(void *);
    } MMSceneActorPointerView;
    /* Zero-initialize the record and reset it at a new press or on teardown.
     * Down preserves the original hit/part precedence and retains an armed
     * target until the gesture resolves. It returns the actor for the tool. */
    void mm_scene_actor_pointer_reset(MMSceneActorPointer *, const struct MMActorOwnerServices *);
    void *mm_scene_actor_pointer_down(MMSceneActorPointer *, const MMSceneActorPointerInput *, MMI32 *part,
                                      const MMSceneSelectionServices *, const struct MMActorOwnerServices *,
                                      const MMSceneActorPointerView *);
    /* A true drag return suppresses the transform tick. Crossing tolerance
     * selects a live nonmember and recaptures single-actor undo before editing.
     * If the target goes away, suppress further held ticks until release. */
    int mm_scene_actor_pointer_drag(MMSceneActorPointer *, const MMSceneActorPointerInput *, MMActorDragUndo *,
                                    const MMSceneSelectionServices *, const struct MMActorOwnerServices *,
                                    const MMActorDragUndoServices *);
    /* Return true when the release attempted a toggle, so the view can leave
     * the cursor in place. Pass NULL selection if the scene has closed. */
    int mm_scene_actor_pointer_up(MMSceneActorPointer *, MMI32 x, MMI32 y, const MMSceneSelectionServices *,
                                  const struct MMActorOwnerServices *, const MMSceneActorPointerView *);

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

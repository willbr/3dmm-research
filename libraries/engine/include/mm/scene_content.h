/* Scene actor/caption editing converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_CONTENT_H
#define MM_ENGINE_SCENE_CONTENT_H
#include "scene_runtime.h"
#include "scene_selection.h"
#include "actor_paste.h"
#include "actor_stage_edit.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SCENE_ACTOR_ADD = 1,
        MM_SCENE_ACTOR_DELETE,
        MM_SCENE_ACTOR_REPLACE
    };
    enum
    {
        MM_SCENE_CONTENT_ACTOR_MISSING,
        MM_SCENE_CONTENT_ACTOR_ROLL_MISSING,
        MM_SCENE_CONTENT_TEXT_MISSING,
        MM_SCENE_CONTENT_TEXT_ROLL_MISSING,
        MM_SCENE_CONTENT_TEXT_HIDE_FAILED,
        MM_SCENE_CONTENT_LIFETIME,
        MM_SCENE_CONTENT_BAD_UNDO
    };
    typedef struct MMSceneActorUndo
    {
        void *actor;
        MMI32 operation;
    } MMSceneActorUndo;
    typedef struct MMSceneTextUndo
    {
        void *text;
        int add;
        MMI32 index, first, last;
    } MMSceneTextUndo;
    typedef struct MMSceneContentServices
    {
        void *context;
        MMSceneEventServices events;
        MMSceneSelectionServices selection;
        MMActorOwnerServices actors;
        void (*retain)(void *, void *object);
        MMI32 (*actor_id)(void *, void *actor);
        void (*set_actor_id)(void *, void *actor, MMI32 id);
        int (*actor_ready)(void *, void *actor);
        MMActorPasteServices (*actor_paste)(void *actor);
        MMActorStageBind stage;
        void (*hide_actor)(void *, void *actor);
        void (*end_placement)(void *);
        int (*add_roll)(void *, void *actor, int template_name);
        void (*remove_roll)(void *, void *actor, int delete_if_only);
        void (*dirty)(void *), (*invalidate)(void *), (*indicator)(void *), (*update_roll)(void *);
        void (*bind_text)(void *, void *text);
        int (*text_lifetime)(void *, void *text, MMI32 *first, MMI32 *last);
        void (*hide_text)(void *, void *text);
        void *(*new_undo)(void *, int text);
        MMSceneActorUndo *(*actor_state)(void *, void *undo);
        MMSceneTextUndo *(*text_state)(void *, void *undo);
        int (*commit_undo)(void *, void *undo);
        void (*release_undo)(void *, void *undo), (*clear_undo)(void *), (*not_undoable)(void *);
        void (*diagnostic)(void *, int issue);
        const struct MMMovieSaveServices *working_save;
    } MMSceneContentServices;
    /* Event and selection services refer to the same initialized scene owner.
     * Valid object inputs are required. Start groups own
     * object references; roll/selection lists borrow them. Callbacks may relocate group
     * storage but cannot reenter or change traversal contents. New start events have
     * frame zero. Actor binding/ID and other completed prefixes survive later
     * failure. Replacement retains/seeks before publishing pointers; selection precedes
     * old actor hide/release. Caption binding precedes insertion. Failed caption play
     * releases its just-retained reference. Removal clears selection before searches,
     * hides before release and preserves dirty/notification order. Actor/caption
     * effects and common undo object/list lifetime are host primitives. */
    int mm_scene_actor_add(void *, const MMSceneContentServices *);
    int mm_scene_actor_add_onstage(void *, const MMSceneContentServices *);
    void mm_scene_actor_remove(MMI32 id, const MMSceneContentServices *);
    int mm_scene_actor_remove_undo(MMI32 id, const MMSceneContentServices *);
    int mm_scene_actor_paste(void *, const MMSceneContentServices *);
    void *mm_scene_actor_find(MMI32 id, const MMSceneContentServices *);
    int mm_scene_roll_add(const MMSceneContentServices *);
    void mm_scene_roll_remove(int delete_if_only, const MMSceneContentServices *);
    int mm_scene_text_add(void *, const MMSceneContentServices *);
    int mm_scene_text_remove(void *, const MMSceneContentServices *);
    int mm_scene_text_add_undo(void *, const MMSceneContentServices *);
    int mm_scene_text_remove_undo(void *, const MMSceneContentServices *);
    void *mm_scene_text_at(MMI32 index, const MMSceneContentServices *);
    typedef struct MMSceneContentUndoServices
    {
        void *context;
        int (*switch_scene)(void *), (*seek)(void *, MMI32 frame);
        MMI32 frame;
        MMSceneContentServices (*scene)(void *);
        void (*flush)(void *), (*clear)(void *), (*update_roll)(void *);
    } MMSceneContentUndoServices;
    /* Undo records own actor/text references. set payload transfers ownership; dispose
     * releases before publishing NULL. Captions rebind identity by saved list index
     * after scene changes. Redo-add failure seeks back; undo-delete-add failure does not.
     * A failed final seek after a caption add remains nonfatal and keeps the old index.
     * Actor snapshots are copied and reset by the actor owner. Replacement releases
     * its temporary copy on failed installation. Failure flushes
     * before clear, which may destroy the record; no state is accessed afterward. */
    void mm_scene_actor_undo_dispose(MMSceneActorUndo *, const MMSceneContentServices *);
    void mm_scene_text_undo_dispose(MMSceneTextUndo *, const MMSceneContentServices *);
    int mm_scene_actor_undo(MMSceneActorUndo *, int redo, const MMSceneContentUndoServices *);
    int mm_scene_text_undo(MMSceneTextUndo *, int redo, const MMSceneContentUndoServices *);
#ifdef __cplusplus
}
#endif
#endif

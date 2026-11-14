/* Scene actor/caption editing converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_CONTENT_H
#define THREE_MM_ENGINE_SCENE_CONTENT_H
#include "scene_events.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_SCENE_ACTOR_ADD = 1,
        THREE_MM_SCENE_ACTOR_DELETE,
        THREE_MM_SCENE_ACTOR_REPLACE
    };
    enum
    {
        THREE_MM_SCENE_CONTENT_ACTOR_MISSING,
        THREE_MM_SCENE_CONTENT_ACTOR_ROLL_MISSING,
        THREE_MM_SCENE_CONTENT_TEXT_MISSING,
        THREE_MM_SCENE_CONTENT_TEXT_ROLL_MISSING,
        THREE_MM_SCENE_CONTENT_TEXT_HIDE_FAILED,
        THREE_MM_SCENE_CONTENT_LIFETIME,
        THREE_MM_SCENE_CONTENT_BAD_UNDO
    };
    typedef struct ThreeMMSceneActorUndo
    {
        void *actor;
        ThreeMMI32 operation;
    } ThreeMMSceneActorUndo;
    typedef struct ThreeMMSceneTextUndo
    {
        void *text;
        int add;
        ThreeMMI32 index, first, last;
    } ThreeMMSceneTextUndo;
    typedef struct ThreeMMSceneContentServices
    {
        void *context;
        ThreeMMSceneEventServices events;
        void (*retain)(void *, void *object);
        ThreeMMI32 (*actor_id)(void *, void *actor);
        void (*set_actor_id)(void *, void *actor, ThreeMMI32 id);
        int (*actor_ready)(void *, void *actor), (*actor_onstage)(void *, void *actor),
            (*actor_paste)(void *, void *actor, ThreeMMI32 frame);
        void (*hide_actor)(void *, void *actor);
        int (*insert_start)(void *, ThreeMMI32 index, int kind, void *object);
        void (*put_start)(void *, ThreeMMI32 index, void *object),
            (*put_list)(void *, void *list, ThreeMMI32 index, void *object),
            (*erase)(void *, void *group, ThreeMMI32 index);
        void *(*selected)(void *, int text);
        void (*set_selected)(void *, int text, void *object), (*select)(void *, int text, void *object),
            (*end_placement)(void *);
        int (*add_roll)(void *, void *actor, int template_name);
        void (*remove_roll)(void *, void *actor, int delete_if_only);
        void (*dirty)(void *), (*invalidate)(void *), (*indicator)(void *), (*update_roll)(void *);
        void (*bind_text)(void *, void *text), (*text_select)(void *, void *text, int selected);
        int (*text_lifetime)(void *, void *text, ThreeMMI32 *first, ThreeMMI32 *last);
        void (*hide_text)(void *, void *text);
        void *(*new_undo)(void *, int text);
        ThreeMMSceneActorUndo *(*actor_state)(void *, void *undo);
        ThreeMMSceneTextUndo *(*text_state)(void *, void *undo);
        int (*commit_undo)(void *, void *undo);
        void (*release_undo)(void *, void *undo), (*clear_undo)(void *), (*not_undoable)(void *);
        void (*diagnostic)(void *, int issue);
    } ThreeMMSceneContentServices;
    /* Initialized groups/lists and valid object inputs are required. Start groups own
     * object references; roll/selection lists borrow them. Callbacks may relocate group
     * storage but cannot reenter or change traversal contents. Start frames are unused
     * and hosts write zero. Actor binding/ID and other completed prefixes survive later
     * failure. Replacement retains/seeks before publishing pointers; selection precedes
     * old actor hide/release. Caption binding precedes insertion. Failed caption play
     * releases its just-retained reference. Removal clears selection before searches,
     * hides before release and preserves dirty/notification order. Generic selection,
     * actor/caption internals and common undo object/list lifetime are host primitives. */
    int three_mm_scene_actor_add(void *, const ThreeMMSceneContentServices *);
    int three_mm_scene_actor_add_onstage(void *, const ThreeMMSceneContentServices *);
    void three_mm_scene_actor_remove(ThreeMMI32 id, const ThreeMMSceneContentServices *);
    int three_mm_scene_actor_remove_undo(ThreeMMI32 id, const ThreeMMSceneContentServices *);
    int three_mm_scene_actor_paste(void *, const ThreeMMSceneContentServices *);
    void *three_mm_scene_actor_find(ThreeMMI32 id, const ThreeMMSceneContentServices *);
    int three_mm_scene_roll_add(const ThreeMMSceneContentServices *);
    void three_mm_scene_roll_remove(int delete_if_only, const ThreeMMSceneContentServices *);
    int three_mm_scene_text_add(void *, const ThreeMMSceneContentServices *);
    int three_mm_scene_text_remove(void *, const ThreeMMSceneContentServices *);
    int three_mm_scene_text_add_undo(void *, const ThreeMMSceneContentServices *);
    int three_mm_scene_text_remove_undo(void *, const ThreeMMSceneContentServices *);
    void *three_mm_scene_text_at(ThreeMMI32 index, const ThreeMMSceneContentServices *);
    void three_mm_scene_content_bind_movie(void *movie, const ThreeMMSceneContentServices *);
    typedef struct ThreeMMSceneContentUndoServices
    {
        void *context;
        int (*switch_scene)(void *), (*seek)(void *, ThreeMMI32 frame);
        ThreeMMI32 frame;
        ThreeMMSceneContentServices (*scene)(void *);
        void *(*duplicate)(void *, void *actor);
        void (*flush)(void *), (*clear)(void *), (*update_roll)(void *);
    } ThreeMMSceneContentUndoServices;
    /* Undo records own actor/text references. set payload transfers ownership; dispose
     * releases before publishing NULL. Captions rebind identity by saved list index
     * after scene changes. Redo-add failure seeks back; undo-delete-add failure does not.
     * A failed final seek after a caption add remains nonfatal and keeps the old index.
     * Actor replacement releases a duplicate on failed installation. Failure flushes
     * before clear, which may destroy the record; no state is accessed afterward. */
    void three_mm_scene_actor_undo_dispose(ThreeMMSceneActorUndo *, const ThreeMMSceneContentServices *);
    void three_mm_scene_text_undo_dispose(ThreeMMSceneTextUndo *, const ThreeMMSceneContentServices *);
    int three_mm_scene_actor_undo(ThreeMMSceneActorUndo *, int redo, const ThreeMMSceneContentUndoServices *);
    int three_mm_scene_text_undo(ThreeMMSceneTextUndo *, int redo, const ThreeMMSceneContentUndoServices *);
#ifdef __cplusplus
}
#endif
#endif

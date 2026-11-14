/* Scene pause/camera/background editing converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_EDIT_H
#define THREE_MM_ENGINE_SCENE_EDIT_H
#include "scene_sound_edit.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_SCENE_UNDO_PAUSE,
        THREE_MM_SCENE_UNDO_BACKGROUND
    };
    enum
    {
        THREE_MM_SCENE_EDIT_NO_BACKGROUND_EVENT,
        THREE_MM_SCENE_EDIT_FIRST_BACKGROUND
    };
    typedef struct ThreeMMScenePauseUndo
    {
        ThreeMMI32 reason, delay;
        int add;
    } ThreeMMScenePauseUndo;
    typedef struct ThreeMMSceneBackgroundUndo
    {
        ThreeMMTag tag;
        ThreeMMI32 camera;
        int background;
    } ThreeMMSceneBackgroundUndo;
    typedef struct ThreeMMSceneEditServices
    {
        void *context;
        ThreeMMSceneSoundEditServices sound;
        void (*put)(void *, void *group, ThreeMMI32 index, const void *payload);
        void (*erase)(void *, void *group, ThreeMMI32 index);
        int (*put_background)(void *, ThreeMMI32 index, const ThreeMMTag *);
        int (*insert_background)(void *, ThreeMMI32 frame, const ThreeMMTag *);
        int (*play_background)(void *, ThreeMMI32 frame, const ThreeMMTag *, ThreeMMU32 disabled);
        void (*background_name)(void *), (*scene_change)(void *), (*loaded)(void *);
        int (*can_notify)(void *);
        ThreeMMI32 (*current_camera)(void *);
        void *(*new_undo)(void *, int kind);
        ThreeMMScenePauseUndo *(*pause_state)(void *, void *undo);
        ThreeMMSceneBackgroundUndo *(*background_state)(void *, void *undo);
        int (*commit_undo)(void *, void *undo);
        void (*release_undo)(void *, void *undo), (*clear_undo)(void *), (*not_undoable)(void *);
        int (*camera_tag)(void *, ThreeMMI32 camera, ThreeMMTag *), (*cache_tag)(void *, const ThreeMMTag *);
        void (*close_tag)(void *, ThreeMMTag *);
        void (*diagnostic)(void *, int issue);
        void *output_context;
        void (*publish_tag)(void *, const ThreeMMTag *, int first);
        void (*publish_camera)(void *, ThreeMMI32 camera);
    } ThreeMMSceneEditServices;
    /* Valid current-frame cursor, initialized runtime groups and stable, distinct
     * input/output storage are required. Callbacks are synchronous and non-reentrant.
     * Pause replacement/removal retains native dirty behavior; nil without a pause
     * still inserts an event. Camera edits preserve destructive prefixes on failure,
     * remove a redundant later camera without changing the current cursor, and use
     * the existing C dispatcher. Background replacement releases the old owner before
     * fetching the new one; failed play attempts restoration without rolling back
     * dirty state. Output tags publish before group mutation. First-background output
     * is invalid/unowned; hosts may poison that unused native value. Its unused start
     * frame is zero. Successful replacement closes frame-owned sound tags while
     * removing stale events. Cached background sound ownership remains unchanged.
     * Camera preflight tags are closed after cache/preflight, before editing.
     * Public background editing requires an existing background; use core for first.
     * Common undo targets/list/object lifetime remain host services. */
    int three_mm_scene_pause_edit(ThreeMMI32 *reason, ThreeMMI32 *delay, const ThreeMMSceneEditServices *);
    int three_mm_scene_camera_edit(ThreeMMI32 camera, ThreeMMI32 *old, const ThreeMMSceneEditServices *);
    int three_mm_scene_background_edit(const ThreeMMTag *, ThreeMMTag *old, const ThreeMMSceneEditServices *);
    int three_mm_scene_is_empty(const ThreeMMSceneEditServices *);
    int three_mm_scene_pause_undo_edit(ThreeMMI32 reason, ThreeMMI32 delay, const ThreeMMSceneEditServices *);
    int three_mm_scene_camera_undo_edit(ThreeMMI32 camera, const ThreeMMSceneEditServices *);
    int three_mm_scene_background_undo_edit(ThreeMMTag *, const ThreeMMSceneEditServices *);
    typedef struct ThreeMMSceneEditUndoServices
    {
        void *context;
        int (*switch_scene)(void *), (*seek_frame)(void *);
        int (*pause)(void *, ThreeMMI32 *reason, ThreeMMI32 *delay);
        int (*background_present)(void *);
        ThreeMMI32 (*current_camera)(void *);
        int (*background)(void *, const ThreeMMTag *, ThreeMMTag *old);
        int (*camera)(void *, ThreeMMI32 camera, ThreeMMI32 *old);
        void (*pause_type)(void *, ThreeMMI32 reason), (*flush)(void *), (*clear)(void *);
    } ThreeMMSceneEditUndoServices;
    /* Persistent payloads borrow tags without retaining. Background undo switches
     * scenes without seeking; camera and pause undo seek the saved frame. Later
     * failures retain completed scene mutations and leave saved background/camera
     * state unchanged. Pause fields swap directly through core. Flush precedes clear
     * on failure; clear may destroy the undo object, so no state is accessed afterward. */
    int three_mm_scene_pause_undo(ThreeMMScenePauseUndo *, const ThreeMMSceneEditUndoServices *);
    int three_mm_scene_background_undo(ThreeMMSceneBackgroundUndo *, const ThreeMMSceneEditUndoServices *);
#ifdef __cplusplus
}
#endif
#endif

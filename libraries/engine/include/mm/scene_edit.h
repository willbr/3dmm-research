/* Scene pause/camera/background editing converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_EDIT_H
#define MM_ENGINE_SCENE_EDIT_H
#include "scene_sound_edit.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SCENE_UNDO_PAUSE,
        MM_SCENE_UNDO_BACKGROUND
    };
    enum
    {
        MM_SCENE_EDIT_NO_BACKGROUND_EVENT,
        MM_SCENE_EDIT_FIRST_BACKGROUND
    };
    typedef struct MMScenePauseUndo
    {
        MMI32 reason, delay;
        int add;
    } MMScenePauseUndo;
    typedef struct MMSceneBackgroundUndo
    {
        MMTag tag;
        MMI32 camera;
        int background;
    } MMSceneBackgroundUndo;
    typedef struct MMSceneEditServices
    {
        void *context;
        MMSceneSoundEditServices sound;
        void (*background_name)(void *), (*scene_change)(void *), (*loaded)(void *);
        int (*can_notify)(void *);
        MMI32 (*current_camera)(void *);
        void *(*new_undo)(void *, int kind);
        MMScenePauseUndo *(*pause_state)(void *, void *undo);
        MMSceneBackgroundUndo *(*background_state)(void *, void *undo);
        int (*commit_undo)(void *, void *undo);
        void (*release_undo)(void *, void *undo), (*clear_undo)(void *), (*not_undoable)(void *);
        int (*camera_tag)(void *, MMI32 camera, MMTag *), (*cache_tag)(void *, const MMTag *);
        void (*close_tag)(void *, MMTag *);
        void (*diagnostic)(void *, int issue);
        void *output_context;
        void (*publish_tag)(void *, const MMTag *, int first);
        void (*publish_camera)(void *, MMI32 camera);
    } MMSceneEditServices;
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
    int mm_scene_pause_edit(MMI32 *reason, MMI32 *delay, const MMSceneEditServices *);
    int mm_scene_camera_edit(MMI32 camera, MMI32 *old, const MMSceneEditServices *);
    /* A journal key for a cut that follows the first frame when prepending. */
#define MM_SCENE_OPENING_CAMERA_FRAME (-2147483647L - 1L)
    MMI32 mm_scene_camera_key(MMI32 first, MMI32 frame);
    MMI32 mm_scene_camera_frame(MMI32 first, MMI32 key);
    typedef struct MMSceneCameraEvents
    {
        void *context;
        MMI32 (*count)(void *);
        void (*event)(void *, MMI32 index, MMI32 *frame, int *kind);
        void (*erase)(void *, MMI32 index);
        int (*insert)(void *, MMI32 index, MMI32 frame, MMU32 camera);
    } MMSceneCameraEvents;
    /* Replace all cuts at the frame, preserving the order of other events. */
    int mm_scene_camera_replace(MMI32 frame, MMU32 camera, const MMSceneCameraEvents *);
    typedef struct MMSceneCameraStoreServices
    {
        MMSceneCameraEvents events;
        int (*open_events)(void *);
        int (*replace_events)(void *);
        int (*drop_thumbnail)(void *);
    } MMSceneCameraStoreServices;
    /* Runs against a private save copy. Services return 1/0 without unwinding. */
    int mm_scene_camera_store(MMI32 first, MMI32 key, MMU32 camera, const MMSceneCameraStoreServices *);
    typedef struct MMSceneCameraJournalServices
    {
        void *context;
        void (*position)(void *, MMI32 *first, MMI32 *frame, MMU32 *camera_count);
        int (*prepare)(void *, MMI32 key, MMU32 camera);
        int (*apply)(void *);
        void (*publish)(void *);
    } MMSceneCameraJournalServices;
    enum { MM_SCENE_CAMERA_INVALID = -1 };
    /* Reserve undo in prepare and publish only after applying succeeds. */
    int mm_scene_camera_journal(MMI32 scene, unsigned long camera, const MMSceneCameraJournalServices *);
    int mm_scene_background_edit(const MMTag *, MMTag *old, const MMSceneEditServices *);
    int mm_scene_is_empty(const MMSceneEditServices *);
    int mm_scene_pause_undo_edit(MMI32 reason, MMI32 delay, const MMSceneEditServices *);
    int mm_scene_camera_undo_edit(MMI32 camera, const MMSceneEditServices *);
    int mm_scene_background_undo_edit(MMTag *, const MMSceneEditServices *);
    typedef struct MMSceneEditUndoServices
    {
        void *context;
        int (*switch_scene)(void *), (*seek_frame)(void *);
        int (*pause)(void *, MMI32 *reason, MMI32 *delay);
        int (*background_present)(void *);
        MMI32 (*current_camera)(void *);
        int (*background)(void *, const MMTag *, MMTag *old);
        int (*camera)(void *, MMI32 camera, MMI32 *old);
        void (*pause_type)(void *, MMI32 reason), (*flush)(void *), (*clear)(void *);
    } MMSceneEditUndoServices;
    /* Persistent payloads borrow tags without retaining. Background undo switches
     * scenes without seeking; camera and pause undo seek the saved frame. Later
     * failures retain completed scene mutations and leave saved background/camera
     * state unchanged. Pause fields swap directly through core. Flush precedes clear
     * on failure; clear may destroy the undo object, so no state is accessed afterward. */
    int mm_scene_pause_undo(MMScenePauseUndo *, const MMSceneEditUndoServices *);
    int mm_scene_background_undo(MMSceneBackgroundUndo *, const MMSceneEditUndoServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Scene chopping and snapshot undo converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_CHOP_H
#define THREE_MM_ENGINE_SCENE_CHOP_H
#include "scene_events.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMSceneChopUndo
    {
        void *resource;
        ThreeMMU32 number;
    } ThreeMMSceneChopUndo;
    typedef struct ThreeMMSceneChopServices
    {
        void *context;
        ThreeMMSceneState *data;
        ThreeMMSceneEventServices events;
        int (*trim_actor)(void *, void *, int back);
        void (*remove_actor)(void *, void *);
        int (*remove_text)(void *, void *);
        int (*lifetime)(void *, void *, ThreeMMI32 *first, ThreeMMI32 *last);
        int (*show_text)(void *, void *);
        void (*start_text)(void *, void *, ThreeMMI32 frame);
        void (*erase_frame)(void *, ThreeMMI32), (*set_frame)(void *, ThreeMMI32 index, ThreeMMI32 frame);
        void (*dirty)(void *), (*discard_background_frame)(void *), (*bad_lifetime)(void *);
        void *(*new_undo)(void *);
        int (*save_undo)(void *, void *), (*commit_undo)(void *, void *);
        void (*release_undo)(void *, void *), (*clear_undo)(void *);
    } ThreeMMSceneChopServices;
    /* Valid initialized Scene/cursor and non-reentrant typed callbacks required.
     * Object lists borrow references. Removal may shrink the list. Forward removes
     * events from the cursor onward; backward keeps the newest past camera, moved
     * to the current frame, and all current-frame events. Sound tags close before
     * erase. Failed lifetime queries skip that caption; failed removals that leave
     * the object in place advance rather than stall. Other completed prefixes remain.
     * The background-frame discard is a host diagnostic poison (release may no-op).
     * Public transactions create/save/commit undo before editing, release their local
     * undo reference before core, and clear undo only on a subsequent core failure. */
    int three_mm_scene_chop(int back, const ThreeMMSceneChopServices *);
    int three_mm_scene_chop_edit(int back, const ThreeMMSceneChopServices *);
    typedef struct ThreeMMSceneSnapshotServices
    {
        void *context;
        void *(*new_file)(void *), *(*new_resource)(void *, void *file);
        void (*release)(void *, void *), (*begin)(void *), (*end)(void *);
        int (*write)(void *, void *scene, void *resource, ThreeMMU32 *number);
        void *(*read)(void *, void *resource, ThreeMMU32 number);
        int (*start)(void *, void *scene), (*switch_scene)(void *, ThreeMMI32 index);
        void *(*current)(void *);
        void (*retain)(void *, void *scene), (*close)(void *, void *scene);
        int (*insert)(void *, ThreeMMI32 index, void *scene), (*remove)(void *, ThreeMMI32 index);
        void (*delete_chunk)(void *, void *resource, ThreeMMU32 number), (*rewind)(void *, void *scene);
        void (*seek)(void *, ThreeMMI32 frame), (*invalidate)(void *), (*flush)(void *), (*clear)(void *);
    } ThreeMMSceneSnapshotServices;
    /* Fresh zero-initialized save records own their resource even on failed write.
     * Swap inserts the restored Scene before deleting/rewriting the saved snapshot.
     * Write/remove failures keep the original destructive prefixes. Failed final
     * switch is nonfatal; failed seek is ignored. Close and release are distinct.
     * Failure flushes then clears (which may destroy the record), then ends the
     * operation through independent, stable service context. No state access follows
     * clear. The same swap implements do and undo. Dispose releases before NULL. */
    int three_mm_scene_chop_save(ThreeMMSceneChopUndo *, void *scene, const ThreeMMSceneSnapshotServices *);
    int three_mm_scene_chop_swap(ThreeMMSceneChopUndo *, ThreeMMI32 index, ThreeMMI32 frame,
                                 const ThreeMMSceneSnapshotServices *);
    void three_mm_scene_chop_dispose(ThreeMMSceneChopUndo *, const ThreeMMSceneSnapshotServices *);
#ifdef __cplusplus
}
#endif
#endif

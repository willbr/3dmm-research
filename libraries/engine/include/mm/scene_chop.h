/* Scene chopping and snapshot undo converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_CHOP_H
#define MM_ENGINE_SCENE_CHOP_H
#include "movie_scene.h"
#include "scene_runtime.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMSceneChopServices
    {
        void *context;
        MMSceneEventServices events;
        int (*trim_actor)(void *, void *, int back);
        void (*remove_actor)(void *, void *);
        int (*remove_text)(void *, void *);
        int (*lifetime)(void *, void *, MMI32 *first, MMI32 *last);
        int (*show_text)(void *, void *);
        void (*start_text)(void *, void *, MMI32 frame);
        void (*dirty)(void *), (*discard_background_frame)(void *), (*bad_lifetime)(void *);
        void *(*new_undo)(void *);
        int (*save_undo)(void *, void *), (*commit_undo)(void *, void *);
        void (*release_undo)(void *, void *), (*clear_undo)(void *);
    } MMSceneChopServices;
    /* Valid initialized Scene/cursor and non-reentrant typed callbacks required.
     * Object lists borrow references. Removal may shrink the list. Forward removes
     * events from the cursor onward; backward keeps the newest past camera, moved
     * to the current frame, and all current-frame events. Sound tags close before
     * erase. Failed lifetime queries skip that caption; failed removals that leave
     * the object in place advance rather than stall. Other completed prefixes remain.
     * The background-frame discard is a host diagnostic poison (release may no-op).
     * Public transactions create/save/commit undo before editing, release their local
     * undo reference before core, and clear undo only on a subsequent core failure. */
    int mm_scene_chop(int back, const MMSceneChopServices *);
    int mm_scene_chop_edit(int back, const MMSceneChopServices *);
    typedef struct MMSceneChopSwapServices
    {
        MMSceneSnapshotServices snapshot;
        void (*delete_failed)(void *), (*rewind)(void *, void *scene);
        void (*seek)(void *scene, MMI32 frame);
    } MMSceneChopSwapServices;
    /* Swap inserts the restored Scene before deleting/rewriting the saved snapshot.
     * Write/remove failures keep the original destructive prefixes. Failed final
     * switch is nonfatal; failed seek is ignored. Close and release are distinct.
     * Failure flushes then clears (which may destroy the record), then ends the
     * operation through independent, stable service context. No state access follows
     * clear. The same swap implements do and undo. */
    int mm_scene_chop_swap(MMSceneSnapshot *, MMI32 index, MMI32 frame,
                          const MMSceneChopSwapServices *, const MMMovieSceneServices *);
#ifdef __cplusplus
}
#endif
#endif

/* Scene sound editing/undo converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_SOUND_EDIT_H
#define MM_ENGINE_SCENE_SOUND_EDIT_H
#include "scene_runtime.h"
#include "scene_sound.h"
#include "movie_sound.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum MMSceneSoundEditIssue
    {
        MM_SCENE_SOUND_STYLE_MISSING,
        MM_SCENE_SOUND_MISSING,
        MM_SCENE_SOUND_QUEUE_RESTORE,
        MM_SCENE_SOUND_OFFSET_OVERFLOW,
        MM_SCENE_SOUND_LIST_RESIZE
    };
    typedef struct MMSceneSoundValueServices
    {
        void *context;
        MMSceneSoundPacketServices packets;
        void (*release)(void *, MMSceneSound *);
    } MMSceneSoundValueServices;
    typedef struct MMSceneSoundUndo
    {
        MMSceneSound *sound;
        MMI32 style;
    } MMSceneSoundUndo;
    typedef struct MMSceneSoundEditServices
    {
        void *context;
        MMSceneEventServices events;
        MMSceneSoundValueServices values;
        void *(*allocate_records)(void *, MMI32 bytes);
        void (*free_raw)(void *, void *);
        MMMovieSoundServices (*movie_sound)(void *);
        MMU32 (*unused_child)(void *);
        void *(*fetch_input)(void *, const MMTag *);
        void *(*create_tag_list)(void *, MMI32 count);
        int (*resize_tag_list)(void *, void *list, MMI32 count);
        void (*put_list_tag)(void *, void *list, MMI32 index, const MMTag *);
        void (*release_list)(void *, void *list);
        void *query_context;
        void (*publish_query)(void *, void *list, int metadata, MMI32 volume, int loop);
        MMI32 (*actor_count)(void *);
        int (*actor_sound)(void *, MMI32 index);
        void (*dirty)(void *), (*sound_frame)(void *, int value);
        void *(*new_undo)(void *);
        MMSceneSoundUndo *(*undo_state)(void *, void *undo);
        int (*commit_undo)(void *, void *undo);
        void (*release_undo)(void *, void *undo), (*clear_undo)(void *);
        void (*diagnostic)(void *, int issue);
    } MMSceneSoundEditServices;
    typedef struct MMSceneSoundUndoServices
    {
        void *context;
        MMSceneSoundValueServices values;
        int (*switch_scene)(void *), (*seek_frame)(void *);
        void (*flush)(void *), (*play)(void *), (*clear)(void *);
        int (*get)(void *, MMI32 style, int *found, MMSceneSound **);
        void (*remove)(void *, MMI32 style);
        int (*restore)(void *, const MMSceneSound *);
        void (*diagnostic)(void *, int issue);
    } MMSceneSoundUndoServices;
    /* Valid initialized state/services and stable input arrays required. Callbacks
     * must not reenter editing/undo. Payloads may move when groups are mutated;
     * temporary packets own independent references. Successful put preserves old
     * group prefix references; failed put disposes the shared temporary prefix once.
     * Effects/prefix mutations and dirty notifications are not rolled back on later
     * failures. get leaves the packet output untouched when absent; query leaves
     * volume/loop untouched when absent/failing and returns borrowed list tags.
     * A clear-undo callback may destroy the undo object: no state is accessed after it.
     * Undo targets/common object lifetime remain supplied by the host services.
     * Sound add resolves document tags in the caller's mutable input array. */
    int mm_scene_edit_insert(MMI32 frame, int kind, MMI32 bytes, const void *,
                                   const MMSceneSoundEditServices *);
    int mm_scene_sound_add(int loop, int queue, MMI32 volume, MMI32 style, MMI32 count, MMTag *tags,
                                 const MMSceneSoundEditServices *);
    int mm_scene_sound_restore(int loop, int queue, MMI32 volume, MMI32 style, MMI32 count,
                                const MMSceneSoundTag *records, const MMSceneSoundEditServices *);
    int mm_scene_sound_get(MMI32 style, int *found, MMSceneSound **,
                                 const MMSceneSoundEditServices *);
    void mm_scene_sound_remove(MMI32 style, const MMSceneSoundEditServices *);
    int mm_scene_sound_query(MMI32 style, void **list, MMI32 *volume, int *loop,
                                   const MMSceneSoundEditServices *);
    void mm_scene_sound_volume(MMI32 style, MMI32 volume, const MMSceneSoundEditServices *);
    void mm_scene_sound_update_indicator(const MMSceneSoundEditServices *);
    MMU32 mm_scene_sound_background_offset(MMI32 frame, MMI32 first, int *overflow);
    void mm_scene_sound_background_play(const MMSceneSoundEditServices *);
    int mm_scene_sound_add_undo(int loop, int queue, MMI32 volume, MMI32 style, MMTag *tag,
                                      const MMSceneSoundEditServices *);
    int mm_scene_sound_remove_undo(MMI32 style, const MMSceneSoundEditServices *);
    void mm_scene_sound_undo_init(MMSceneSoundUndo *);
    void mm_scene_sound_undo_dispose(MMSceneSoundUndo *, const MMSceneSoundValueServices *);
    int mm_scene_sound_undo_set(MMSceneSoundUndo *, const MMSceneSound *,
                                      const MMSceneSoundValueServices *);
    int mm_scene_sound_undo_swap(MMSceneSoundUndo *, const MMSceneSoundUndoServices *);
#ifdef __cplusplus
}
#endif
#endif

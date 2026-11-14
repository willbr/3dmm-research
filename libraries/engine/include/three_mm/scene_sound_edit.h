/* Scene sound editing/undo converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_SOUND_EDIT_H
#define THREE_MM_ENGINE_SCENE_SOUND_EDIT_H
#include "scene_events.h"
#include "scene_sound.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum ThreeMMSceneSoundEditIssue
    {
        THREE_MM_SCENE_SOUND_STYLE_MISSING,
        THREE_MM_SCENE_SOUND_MISSING,
        THREE_MM_SCENE_SOUND_QUEUE_RESTORE,
        THREE_MM_SCENE_SOUND_OFFSET_OVERFLOW,
        THREE_MM_SCENE_SOUND_LIST_RESIZE
    };
    typedef struct ThreeMMSceneSoundValueServices
    {
        void *context;
        ThreeMMSceneSoundPacketServices packets;
        void (*release)(void *, ThreeMMSceneSound *);
    } ThreeMMSceneSoundValueServices;
    typedef struct ThreeMMSceneSoundUndo
    {
        ThreeMMSceneSound *sound;
        ThreeMMI32 style;
    } ThreeMMSceneSoundUndo;
    typedef struct ThreeMMSceneSoundEditServices
    {
        void *context;
        ThreeMMSceneEventServices events;
        ThreeMMSceneSoundValueServices values;
        int (*insert)(void *, ThreeMMI32 index, ThreeMMI32 frame, int kind, ThreeMMI32 bytes, const void *payload);
        int (*put)(void *, ThreeMMI32 index, ThreeMMI32 bytes, const void *payload);
        void (*erase)(void *, ThreeMMI32 index);
        void *(*allocate_records)(void *, ThreeMMI32 bytes);
        void (*free_raw)(void *, void *);
        void (*input_tag)(void *, void *tags, ThreeMMI32 index, ThreeMMTag *);
        int (*child)(void *, ThreeMMU32 number, ThreeMMU32 *child);
        ThreeMMU32 (*unused_child)(void *);
        int (*resolve_input)(void *, void *tags, ThreeMMI32 index, ThreeMMU32 child);
        void *(*fetch_input)(void *, void *tags, ThreeMMI32 index);
        void *(*create_tag_list)(void *, ThreeMMI32 count);
        int (*resize_tag_list)(void *, void *list, ThreeMMI32 count);
        void (*put_list_tag)(void *, void *list, ThreeMMI32 index, const ThreeMMTag *);
        void (*release_list)(void *, void *list);
        void *query_context;
        void (*publish_query)(void *, void *list, int metadata, ThreeMMI32 volume, int loop);
        ThreeMMI32 (*actor_count)(void *);
        int (*actor_sound)(void *, ThreeMMI32 index);
        void (*dirty)(void *), (*sound_frame)(void *, int value);
        void *(*new_undo)(void *);
        ThreeMMSceneSoundUndo *(*undo_state)(void *, void *undo);
        int (*commit_undo)(void *, void *undo);
        void (*release_undo)(void *, void *undo), (*clear_undo)(void *);
        void (*diagnostic)(void *, int issue);
    } ThreeMMSceneSoundEditServices;
    typedef struct ThreeMMSceneSoundUndoServices
    {
        void *context;
        ThreeMMSceneSoundValueServices values;
        int (*switch_scene)(void *), (*seek_frame)(void *);
        void (*flush)(void *), (*play)(void *), (*clear)(void *);
        int (*get)(void *, ThreeMMI32 style, int *found, ThreeMMSceneSound **);
        void (*remove)(void *, ThreeMMI32 style);
        int (*restore)(void *, const ThreeMMSceneSound *);
        void (*diagnostic)(void *, int issue);
    } ThreeMMSceneSoundUndoServices;
    /* Valid initialized state/services and stable input arrays required. Callbacks
     * must not reenter editing/undo. Payloads may move when groups are mutated;
     * temporary packets own independent references. Successful put preserves old
     * group prefix references; failed put disposes the shared temporary prefix once.
     * Effects/prefix mutations and dirty notifications are not rolled back on later
     * failures. get leaves the packet output untouched when absent; query leaves
     * volume/loop untouched when absent/failing and returns borrowed list tags.
     * A clear-undo callback may destroy the undo object: no state is accessed after it.
     * Undo targets/common object lifetime remain supplied by the host services. */
    int three_mm_scene_edit_insert(ThreeMMI32 frame, int kind, ThreeMMI32 bytes, const void *,
                                   const ThreeMMSceneSoundEditServices *);
    int three_mm_scene_sound_add(int loop, int queue, ThreeMMI32 volume, ThreeMMI32 style, ThreeMMI32 count, void *tags,
                                 const ThreeMMSceneSoundEditServices *);
    int three_mm_scene_sound_restore(int loop, int queue, ThreeMMI32 volume, ThreeMMI32 style, ThreeMMI32 count,
                                     const void *records, const ThreeMMSceneSoundEditServices *);
    int three_mm_scene_sound_get(ThreeMMI32 style, int *found, ThreeMMSceneSound **,
                                 const ThreeMMSceneSoundEditServices *);
    void three_mm_scene_sound_remove(ThreeMMI32 style, const ThreeMMSceneSoundEditServices *);
    int three_mm_scene_sound_query(ThreeMMI32 style, void **list, ThreeMMI32 *volume, int *loop,
                                   const ThreeMMSceneSoundEditServices *);
    void three_mm_scene_sound_volume(ThreeMMI32 style, ThreeMMI32 volume, const ThreeMMSceneSoundEditServices *);
    void three_mm_scene_sound_update_indicator(const ThreeMMSceneSoundEditServices *);
    ThreeMMU32 three_mm_scene_sound_background_offset(ThreeMMI32 frame, ThreeMMI32 first, int *overflow);
    void three_mm_scene_sound_background_play(const ThreeMMSceneSoundEditServices *);
    int three_mm_scene_sound_add_undo(int loop, int queue, ThreeMMI32 volume, ThreeMMI32 style, void *tag,
                                      const ThreeMMSceneSoundEditServices *);
    int three_mm_scene_sound_remove_undo(ThreeMMI32 style, const ThreeMMSceneSoundEditServices *);
    void three_mm_scene_sound_undo_init(ThreeMMSceneSoundUndo *);
    void three_mm_scene_sound_undo_dispose(ThreeMMSceneSoundUndo *, const ThreeMMSceneSoundValueServices *);
    int three_mm_scene_sound_undo_set(ThreeMMSceneSoundUndo *, const ThreeMMSceneSound *,
                                      const ThreeMMSceneSoundValueServices *);
    int three_mm_scene_sound_undo_swap(ThreeMMSceneSoundUndo *, const ThreeMMSceneSoundUndoServices *);
#ifdef __cplusplus
}
#endif
#endif

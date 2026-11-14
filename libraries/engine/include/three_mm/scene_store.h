/* Scene tag collection and saving converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_STORE_H
#define THREE_MM_ENGINE_SCENE_STORE_H
#include "scene_sound_edit.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMSceneStoreGroups
    {
        void *context;
        ThreeMMI32 (*count)(void *, void *group);
        void (*fixed)(void *, void *group, ThreeMMI32 index, unsigned char bytes[8]);
        void *(*data)(void *, void *group, ThreeMMI32 index, ThreeMMI32 *bytes);
        void (*release)(void *, void *object), (*free_raw)(void *, void *allocation);
    } ThreeMMSceneStoreGroups;
    typedef struct ThreeMMSceneCollectServices
    {
        void *context;
        ThreeMMSceneStoreGroups groups;
        ThreeMMSceneSoundPacketServices packets;
        ThreeMMU32 camera_type;
        void *(*read_group)(void *, ThreeMMU32 scene, int start, int *swap);
        int (*actor_child)(void *, ThreeMMU32 scene, ThreeMMU32 child, ThreeMMU32 *number);
        void *(*actor_tags)(void *, ThreeMMU32 number, int *error);
        void (*list_tag)(void *, void *list, ThreeMMI32 index, ThreeMMTag *);
        int (*insert_tag)(void *, const ThreeMMTag *);
        int (*background)(void *, ThreeMMTag *);
        int (*insert_child)(void *, const ThreeMMTag *, ThreeMMU32 child, ThreeMMU32 type);
        void (*diagnostic)(void *, int start, int missing_background);
    } ThreeMMSceneCollectServices;
    /* Collects starts before frames; background callbacks may update their tag, and
     * the last successful background is used for camera children. Caption starts and
     * pauses are ignored. A failing callback retains the previously inserted prefix.
     * Actor tag lists and decoded sound temporaries are released before their source
     * wire group. Collected tags borrow cache pointers; sound decode never opens them.
     * Inputs are private wire groups with eight-byte fixed entries and valid offsets.
     * Unexpected kinds diagnose/skip; wrong fixed-size payloads fail before copying.
     * Synchronous callbacks cannot reenter or change the groups being traversed. */
    int three_mm_scene_collect_tags(ThreeMMU32 scene, const ThreeMMSceneCollectServices *);
    typedef struct ThreeMMSceneStoreServices
    {
        void *context;
        ThreeMMSceneOwner *owner;
        ThreeMMSceneStoreGroups groups;
        ThreeMMSceneSoundValueServices values;
        int (*add_scene)(void *, ThreeMMU32 *number);
        void (*delete_scene)(void *, ThreeMMU32 number);
        void *(*new_group)(void *);
        int (*insert)(void *, void *group, ThreeMMI32 index, ThreeMMI32 frame, int kind, ThreeMMI32 bytes,
                      const void *);
        int (*sound_child)(void *, ThreeMMU32 scene, ThreeMMU32 child);
        int (*sound_exists)(void *, ThreeMMU32 number);
        int (*adopt_sound)(void *, ThreeMMU32 scene, ThreeMMU32 number, ThreeMMU32 child);
        int (*add_child)(void *, ThreeMMU32 scene, int kind, ThreeMMU32 child, ThreeMMU32 *number);
        void *(*object)(void *, void *group, ThreeMMI32 index);
        int (*write_object)(void *, void *object, int kind, ThreeMMU32 number, ThreeMMU32 scene);
        int (*save_background)(void *, void *group, ThreeMMI32 index, ThreeMMTag *);
        int (*write_events)(void *, ThreeMMU32 scene, void *frames, void *starts);
        void (*update_thumbnail)(void *);
        ThreeMMI32 (*thumbnail_size)(void *, void *thumbnail);
        int (*add_thumbnail)(void *, ThreeMMI32 bytes, ThreeMMU32 *number);
        int (*write_thumbnail)(void *, void *thumbnail);
        int (*adopt_thumbnail)(void *, ThreeMMU32 scene, ThreeMMU32 number);
        void (*delete_thumbnail)(void *, ThreeMMU32 number), (*clear_loner)(void *, ThreeMMU32 number);
        int (*write_metadata)(void *, ThreeMMU32 scene);
        void (*diagnostic)(void *, int start);
    } ThreeMMSceneStoreServices;
    /* Borrows valid native-endian runtime groups; callbacks cannot reenter/resize them.
     * Publishes nil before add_scene and retains its output even on failure. Sounds
     * are independently duplicated/retained, adopted only if absent by child and
     * present by number, encoded, inserted, then released. Actor/text child counters
     * are independent. Background saving may update the original tag, even if a later
     * step fails. Events precede thumbnail update/write, then header/name publication.
     * Failure deletes an unadopted thumbnail explicitly, then the partial scene, then
     * releases start/frame temporary groups. Success releases frame/start groups.
     * Chunk operations, actor/caption serialization and thumbnail generation are
     * supplied primitives; their own partial side effects follow their contracts. */
    int three_mm_scene_store(ThreeMMU32 *number, const ThreeMMSceneStoreServices *);
#ifdef __cplusplus
}
#endif
#endif

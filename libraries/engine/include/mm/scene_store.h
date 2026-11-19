/* Scene tag collection and saving converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_STORE_H
#define MM_ENGINE_SCENE_STORE_H
#include "scene_sound_edit.h"
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum { MM_SCENE_ACTOR_CHUNK = 0x41435452 };
    typedef struct MMSceneStoreGroups
    {
        void *context;
        MMI32 (*count)(void *, void *group);
        void (*fixed)(void *, void *group, MMI32 index, unsigned char bytes[8]);
        void *(*data)(void *, void *group, MMI32 index, MMI32 *bytes);
        void (*release)(void *, void *object), (*free_raw)(void *, void *allocation);
    } MMSceneStoreGroups;
    typedef struct MMSceneCollectServices
    {
        void *context;
        MMSceneStoreGroups groups;
        MMSceneSoundPacketServices packets;
        MMU32 camera_type;
        MMChunkAccess chunks;
        void *(*read_group)(void *, MMU32 type, MMU32 number, int *swap);
        void *(*actor_tags)(void *, MMU32 number, int *error);
        void (*list_tag)(void *, void *list, MMI32 index, MMTag *);
        int (*insert_tag)(void *, const MMTag *);
        int (*background)(void *, MMTag *);
        int (*insert_child)(void *, const MMTag *, MMU32 child, MMU32 type);
        void (*diagnostic)(void *, int start, int missing_background);
    } MMSceneCollectServices;
    /* Collects starts before frames; background callbacks may update their tag, and
     * the last successful background is used for camera children. Caption starts and
     * pauses are ignored. A failing callback retains the previously inserted prefix.
     * Actor tag lists and decoded sound temporaries are released before their source
     * wire group. Collected tags borrow cache pointers; sound decode never opens them.
     * Inputs are private wire groups with eight-byte fixed entries and valid offsets.
     * Unexpected kinds diagnose/skip; wrong fixed-size payloads fail before copying.
     * Synchronous callbacks cannot reenter or change the index or event groups. */
    int mm_scene_collect_tags(MMU32 scene, const MMSceneCollectServices *);
    typedef struct MMSceneStoreServices
    {
        void *context;
        MMSceneOwner *owner;
        MMSceneStoreGroups groups;
        MMSceneSoundValueServices values;
        int (*add_scene)(void *, MMU32 *number);
        void (*delete_scene)(void *, MMU32 number);
        void *(*new_group)(void *);
        int (*insert)(void *, void *group, MMI32 index, MMI32 frame, int kind, MMI32 bytes,
                      const void *);
        MMChunkAccess (*chunks)(void *);
        int (*adopt_sound)(void *, MMU32 scene, MMU32 number, MMU32 child);
        int (*add_child)(void *, MMU32 scene, int kind, MMU32 child, MMU32 *number);
        void *(*object)(void *, void *group, MMI32 index);
        int (*write_object)(void *, void *object, int kind, MMU32 number, MMU32 scene);
        int (*save_background)(void *, void *group, MMI32 index, MMTag *);
        int (*write_events)(void *, MMU32 scene, void *frames, void *starts);
        void (*update_thumbnail)(void *);
        MMI32 (*thumbnail_size)(void *, void *thumbnail);
        int (*add_thumbnail)(void *, MMI32 bytes, MMU32 *number);
        int (*write_thumbnail)(void *, void *thumbnail);
        int (*adopt_thumbnail)(void *, MMU32 scene, MMU32 number);
        void (*delete_thumbnail)(void *, MMU32 number), (*clear_loner)(void *, MMU32 number);
        int (*write_metadata)(void *, MMU32 scene);
        void (*diagnostic)(void *, int start);
    } MMSceneStoreServices;
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
    int mm_scene_store(MMU32 *number, const MMSceneStoreServices *);
#ifdef __cplusplus
}
#endif
#endif

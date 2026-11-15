/* Scene loading and sound resolution converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_LOAD_H
#define MM_ENGINE_SCENE_LOAD_H
#include "scene_owner.h"
#include "scene_sound.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMSceneLoadServices
    {
        void *context;
        MMSceneOwnerServices owners;
        MMSceneSoundPacketServices packets;
        int (*metadata)(void *, MMU32 number, MMSceneState *);
        void (*publish_metadata)(void *, void *scene, const MMSceneState *);
        int (*child)(void *, MMU32 scene, int kind, MMU32 child, MMU32 *number);
        void *(*thumbnail)(void *, MMU32 number);
        void *(*read_group)(void *, MMU32 number, int start, int *swap);
        void (*fixed)(void *, void *group, MMI32 index, unsigned char bytes[8]);
        void *(*object)(void *, MMU32 number, int kind, void *scene);
        int (*insert)(void *, void *group, MMI32 index, MMI32 frame, int kind, MMI32 bytes,
                      const void *payload);
        int (*insert_tag)(void *, void *group, MMI32 index, MMI32 frame, const MMTag *);
        int (*open_tag)(void *, MMSceneSound *, MMI32 index);
        void (*load_name)(void *, void *scene, MMU32 number);
    } MMSceneLoadServices;
    /* metadata returns validated Scene state; pointer_bytes is sizeof(void *).
     * Reads metadata before creating the scene; optional thumbnail failure is ignored.
     * read_group returns an owned validated wire group. Fixed headers and child/
     * background words are decoded into local values; pause/camera bytes pass through
     * unchanged, including opposite-order payloads. Sound decoding normalizes its
     * private wire payload before allocation, as in the packet codec. Insert copies
     * payload bytes and transfers embedded references only on success. Callbacks must
     * not reenter loading, mutate input groups or resize live temporary packets.
     * Failure closes the current sound's opened prefix in reverse order, releases
     * the wire group, then unwinds completed start/frame prefixes before destruction.
     * Native objects, lists, file lookup and codecs are supplied as host primitives. */
    void *mm_scene_load(void *movie, MMU32 number, unsigned text_unit, const MMSceneLoadServices *);
    typedef struct MMSceneResolveServices
    {
        void *context;
        MMSceneOwner *owner;
        MMI32 (*count)(void *, void *group), (*kind)(void *, void *group, MMI32 index);
        int (*actor)(void *, MMI32 index, MMU32 scene);
        void (*lock)(void *, void *group), (*unlock)(void *, void *group);
        MMSceneSound *(*sound)(void *, void *group, MMI32 index);
        MMSceneSoundPacketServices packets;
        int (*resolve)(void *, void *record, MMU32 scene);
    } MMSceneResolveServices;
    /* Captures actor/event counts once, visits actors before locking frame storage,
     * resolves sid-zero tags in place and preserves the completed prefix on failure.
     * Every acquired frame lock is released, including failed resolution. Callbacks
     * may update tag values but cannot resize/reenter or replace the scene's groups. */
    int mm_scene_resolve_sounds(MMU32 scene, const MMSceneResolveServices *);
#ifdef __cplusplus
}
#endif
#endif

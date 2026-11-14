/* Scene loading and sound resolution converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_LOAD_H
#define THREE_MM_ENGINE_SCENE_LOAD_H
#include "scene_owner.h"
#include "scene_sound.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMSceneLoadServices
    {
        void *context;
        ThreeMMSceneOwnerServices owners;
        ThreeMMSceneSoundPacketServices packets;
        int (*metadata)(void *, ThreeMMU32 number, ThreeMMSceneState *);
        void (*publish_metadata)(void *, void *scene, const ThreeMMSceneState *);
        int (*child)(void *, ThreeMMU32 scene, int kind, ThreeMMU32 child, ThreeMMU32 *number);
        void *(*thumbnail)(void *, ThreeMMU32 number);
        void *(*read_group)(void *, ThreeMMU32 number, int start, int *swap);
        void (*fixed)(void *, void *group, ThreeMMI32 index, unsigned char bytes[8]);
        void *(*object)(void *, ThreeMMU32 number, int kind, void *scene);
        int (*insert)(void *, void *group, ThreeMMI32 index, ThreeMMI32 frame, int kind, ThreeMMI32 bytes,
                      const void *payload);
        int (*insert_tag)(void *, void *group, ThreeMMI32 index, ThreeMMI32 frame, const ThreeMMTag *);
        int (*open_tag)(void *, ThreeMMSceneSound *, ThreeMMI32 index);
        void (*load_name)(void *, void *scene, ThreeMMU32 number);
    } ThreeMMSceneLoadServices;
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
    void *three_mm_scene_load(void *movie, ThreeMMU32 number, unsigned text_unit, const ThreeMMSceneLoadServices *);
    typedef struct ThreeMMSceneResolveServices
    {
        void *context;
        ThreeMMSceneOwner *owner;
        ThreeMMI32 (*count)(void *, void *group), (*kind)(void *, void *group, ThreeMMI32 index);
        int (*actor)(void *, ThreeMMI32 index, ThreeMMU32 scene);
        void (*lock)(void *, void *group), (*unlock)(void *, void *group);
        ThreeMMSceneSound *(*sound)(void *, void *group, ThreeMMI32 index);
        ThreeMMSceneSoundPacketServices packets;
        int (*resolve)(void *, void *record, ThreeMMU32 scene);
    } ThreeMMSceneResolveServices;
    /* Captures actor/event counts once, visits actors before locking frame storage,
     * resolves sid-zero tags in place and preserves the completed prefix on failure.
     * Every acquired frame lock is released, including failed resolution. Callbacks
     * may update tag values but cannot resize/reenter or replace the scene's groups. */
    int three_mm_scene_resolve_sounds(ThreeMMU32 scene, const ThreeMMSceneResolveServices *);
#ifdef __cplusplus
}
#endif
#endif

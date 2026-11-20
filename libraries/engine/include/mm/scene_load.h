/* Scene loading and sound resolution converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_LOAD_H
#define MM_ENGINE_SCENE_LOAD_H
#include "movie_session.h"
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
        MMMovieSceneReader read;
        void *title_file;
        MMChunkTitleIO titles;
        void *(*thumbnail)(void *, MMU32 number);
        void *(*object)(void *, MMU32 number, int kind, void *scene);
        void *(*retain_background_cache)(void *); /* Returns one reference to the document's live cache. */
        int (*open_tag)(void *, MMSceneSound *, MMI32 index);
    } MMSceneLoadServices;
    /* The reader supplies validated scene events.
     * Reads the header before creating the scene and fills the initialized owner
     * before loading its children. Optional thumbnail failure is ignored. Reads the
     * stored name after both tracks succeed using the owner's native text width.
     * The read group owns its wire bytes. Fixed headers and child/
     * background words are decoded into local values; pause/camera bytes pass through
     * unchanged, including opposite-order payloads. Document-owned background
     * MMTag payloads borrow a cache retained by the scene. Sound decoding normalizes its
     * private wire payload before allocation, as in the packet codec. Track insertion copies
     * payload bytes and transfers embedded references only on success. Callbacks must
     * not reenter loading, mutate input groups or resize live temporary packets.
     * Failure closes the current sound's opened prefix in reverse order, releases
     * the wire group, then unwinds completed start/frame prefixes before destruction.
     * The scene owns both C tracks. Native object factories and cache services
     * are supplied by the host. */
    void *mm_scene_load(void *movie, MMU32 number, const MMSceneLoadServices *);
    typedef struct MMSceneResolveServices
    {
        void *context;
        MMSceneOwner *owner;
        int (*actor)(void *, void *actor, MMU32 scene);
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

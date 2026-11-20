/* Scene tag collection and saving converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_STORE_H
#define MM_ENGINE_SCENE_STORE_H
#include "movie_session.h"
#include "movie_archive_edit.h"
#include "scene_sound_edit.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMSceneCollectServices
    {
        void *context;
        MMMovieSceneReader read;
        MMMovieSessionGraph graph;
        void (*actor_diagnostic)(void *, int issue);
        void (*diagnostic)(void *, int start, int missing_background);
    } MMSceneCollectServices;
    /* Collects starts before frames. The last successful background is used for
     * camera children. Caption starts and pauses are ignored. Failure retains the
     * previously inserted prefix. Actor dependencies and sound packets are validated
     * before their tags are published. Sound tags never open cache pointers.
     * Inputs are private wire groups with eight-byte fixed entries and valid offsets.
     * Unexpected kinds diagnose/skip; wrong fixed-size payloads fail before copying.
     * Synchronous callbacks cannot reenter or change the index or event groups. */
    int mm_scene_collect_tags(MMU32 scene, MMTagList *, const MMSceneCollectServices *);
    typedef struct MMSceneStoreServices
    {
        void *context;
        MMSceneOwner *owner;
        const MMSceneState *metadata;
        const MMMovieArchiveEditor *editor;
        MMSceneSoundValueServices values;
        MMMovieEmbeddedArchive assets;
        void (*free_sound_bytes)(void *, void *);
        int (*write_object)(void *, void *object, int kind, MMU32 number, MMU32 scene);
        void (*update_thumbnail)(void *);
        MMI32 (*thumbnail_size)(void *, void *thumbnail);
        int (*write_thumbnail)(void *, void *thumbnail, const MMStorageWriter *);
        void (*diagnostic)(void *, int start);
    } MMSceneStoreServices;
    /* Borrows the owner's native-endian event tracks and scene metadata. Callbacks
     * cannot reenter, resize or modify the live tracks. The editor must support discard.
     * Publishes nil before adding a scene and retains the assigned number on failure. Sounds
     * are independently duplicated/retained, adopted only if absent by child and
     * present by number, encoded, inserted, then released. Actor/text child counters
     * are independent. Document backgrounds are adopted by the new scene. Only
     * the wire tag uses the copied number. Events precede thumbnail update/write,
     * then header/name publication.
     * The transaction owns both temporary tracks, using their live counterparts'
     * allocators. Failure discards an unlinked child and the partial scene. The
     * editor's adoption operation clears the loner flag. */
    int mm_scene_store(MMU32 *number, const MMSceneStoreServices *);
#ifdef __cplusplus
}
#endif
#endif

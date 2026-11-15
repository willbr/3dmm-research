/* Scene thumbnails/dirty propagation converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_THUMBNAIL_H
#define MM_ENGINE_SCENE_THUMBNAIL_H
#include "scene_events.h"
#include "masked_bitmap.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SCENE_THUMBNAIL_WIDTH = 144,
        MM_SCENE_THUMBNAIL_HEIGHT = 81,
        MM_SCENE_THUMBNAIL_TRANSPARENT = 250
    };
    typedef struct MMSceneDirtyServices
    {
        void *context;
        void (*mark)(void *, void *movie);
    } MMSceneDirtyServices;
    void mm_scene_mark_dirty(const MMSceneOwner *, int dirty, const MMSceneDirtyServices *);
    typedef struct MMSceneThumbnailServices
    {
        void *context;
        MMSceneState *data;
        MMSceneRuntime *runtime;
        MMSceneOwner *owner;
        MMSceneOwnerServices owners;
        void *scene;
        void *(*selected)(void *, int text);
        MMI32 (*sound_time)(void *);
        void (*mute)(void *), (*resume)(void *, MMI32 time);
        void *(*view)(void *);
        int (*dirty)(void *);
        void (*dimensions)(void *, MMI32 *width, MMI32 *height);
        void *(*new_port)(void *, const MMMaskedRect *);
        void (*palette)(void *, void *port), (*release_port)(void *, void *port);
        int (*seek)(void *, MMI32 frame), (*text_mode)(void *, void *view);
        void (*select)(void *, int text, void *object);
        void (*draw)(void *, void *view, void *port, const MMMaskedRect *);
        void *copy_context;
        void (*copy)(void *, void *source, void *target, const MMMaskedRect *, const MMMaskedRect *);
        void (*end_copy)(void *);
        MMI32 (*row_bytes)(void *, void *port);
        void *(*lock)(void *, void *port);
        void *(*bitmap)(void *, const void *pixels, MMI32 row_bytes, const MMMaskedRect *, int transparent);
        void (*unlock)(void *, void *port);
    } MMSceneThumbnailServices;
    /* Synchronous, non-reentrant callbacks; valid Scene/Movie/current cursor required.
     * Owner slots are authoritative. Ports are indexed eight-bit surfaces. copy/end_copy
     * delimit graphics environments which retain/lock ports across local port release,
     * frame restoration and mask restoration. copy cannot fail as an operation; pixel
     * backend diagnostics/failure prefixes retain native behavior. selected/view pointers
     * are borrowed and remain valid throughout. Selection uses the existing policy;
     * primary-only restoration (including its extra-selection effects) is preserved.
     * Every exit resumes sound at the saved time. Failed initial seek keeps its partial
     * frame change and restores only masks/ports. Successful rendering drops the old
     * thumbnail before allocation and publishes even NULL before unlock. A saved frame
     * restores only inside (first,last]; restoration failure remains nonfatal. The
     * accessor updates before returning the borrowed owned-thumbnail pointer. */
    void mm_scene_update_thumbnail(const MMSceneThumbnailServices *);
    void *mm_scene_thumbnail(const MMSceneThumbnailServices *);
#ifdef __cplusplus
}
#endif
#endif

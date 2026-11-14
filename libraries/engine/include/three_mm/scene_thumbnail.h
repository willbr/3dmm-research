/* Scene thumbnails/dirty propagation converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_THUMBNAIL_H
#define THREE_MM_ENGINE_SCENE_THUMBNAIL_H
#include "scene_events.h"
#include "masked_bitmap.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_SCENE_THUMBNAIL_WIDTH = 144,
        THREE_MM_SCENE_THUMBNAIL_HEIGHT = 81,
        THREE_MM_SCENE_THUMBNAIL_TRANSPARENT = 250
    };
    typedef struct ThreeMMSceneDirtyServices
    {
        void *context;
        void (*mark)(void *, void *movie);
    } ThreeMMSceneDirtyServices;
    void three_mm_scene_mark_dirty(const ThreeMMSceneOwner *, int dirty, const ThreeMMSceneDirtyServices *);
    typedef struct ThreeMMSceneThumbnailServices
    {
        void *context;
        ThreeMMSceneState *data;
        ThreeMMSceneRuntime *runtime;
        ThreeMMSceneOwner *owner;
        ThreeMMSceneOwnerServices owners;
        void *scene;
        void *(*selected)(void *, int text);
        ThreeMMI32 (*sound_time)(void *);
        void (*mute)(void *), (*resume)(void *, ThreeMMI32 time);
        void *(*view)(void *);
        int (*dirty)(void *);
        void (*dimensions)(void *, ThreeMMI32 *width, ThreeMMI32 *height);
        void *(*new_port)(void *, const ThreeMMMaskedRect *);
        void (*palette)(void *, void *port), (*release_port)(void *, void *port);
        int (*seek)(void *, ThreeMMI32 frame), (*text_mode)(void *, void *view);
        void (*select)(void *, int text, void *object);
        void (*draw)(void *, void *view, void *port, const ThreeMMMaskedRect *);
        void *copy_context;
        void (*copy)(void *, void *source, void *target, const ThreeMMMaskedRect *, const ThreeMMMaskedRect *);
        void (*end_copy)(void *);
        ThreeMMI32 (*row_bytes)(void *, void *port);
        void *(*lock)(void *, void *port);
        void *(*bitmap)(void *, const void *pixels, ThreeMMI32 row_bytes, const ThreeMMMaskedRect *, int transparent);
        void (*unlock)(void *, void *port);
    } ThreeMMSceneThumbnailServices;
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
    void three_mm_scene_update_thumbnail(const ThreeMMSceneThumbnailServices *);
    void *three_mm_scene_thumbnail(const ThreeMMSceneThumbnailServices *);
#ifdef __cplusplus
}
#endif
#endif
